#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ThingSpeak.h>

#ifndef STASSID
#define STASSID "net SID"
#define STAPSK  "password"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

unsigned int localPort = 2390;
IPAddress timeServerIP;
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;
unsigned long highWord, lowWord, secsSince1900, epoch, epochGotTime;
unsigned long timeZoneShift = 10800;
const unsigned long seventyYears = 2208988800UL;
unsigned long epochNow;
int hour, minute, second;
String localTime, timeSynchronized = "";

WiFiServer server(8081);
MDNSResponder mdns;

#define watchDogPin 0
String temperaturePlace[4] = {"kitchen", "bathroom", "electric boiler", "outdoor"}, humidityPlace[2] = {"kitchen", "bathroom"};
String deviceName[5] = {"heaterOne", "heaterTwo", "toiletFan", "bathroomFan", "gasFiredBoiler"}, valvePlace[5] = {"childroom", "kitchen", "bedroom", "toilet", "bathroom"};
String targetTemperatureName[2] = {"day", "evening"};
String inString, dataResponse, espUrl;
unsigned long timeout[3] = {1, 86400, 60}, previousTime[5];
float temperature[4], previousTemperature[4], tempInsensibility = 0.2;
byte targetTemperature[2] = {33, 44};
byte humidity[2], previousHumidity[2], boilerState = 2, humInsensibility = 2;
byte boilerSwitchOnTime, boilerToggleTime, boilerSwitchOffTime;
boolean isDeviceOn[5], isValveOpen[5], isWatchDogActive = false, megaNeedsEpoch = false;
float powerConsumption;
float tenConsumption[2];
boolean isValueChanged = false, tenStateChange[2] = {false, false};

unsigned long temperatureChannel = 999999;
unsigned long humidityChannel = 999999;
unsigned long powerConsumptionChannel = 999999;
const char * temperatureChannelAPIKey = "KEY";
const char * humidityChannelAPIKey = "KEY";
const char * powerConsumptionChannelAPIKey = "KEY";

String htmlString, header, badgeColor, megaStatus = "Everything is OK. ESP reseted";

WiFiClient  client;

void sendNTPpacket(IPAddress& address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  udp.beginPacket(address, 123);
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void megaHardReset() {
  digitalWrite(watchDogPin, LOW);
  delay(300);
  digitalWrite(watchDogPin, HIGH);
}

void getTime() {
  epochNow = (millis() - epochGotTime) / 1000 + epoch;
  hour = (epochNow  % 86400L) / 3600;
  minute = (epochNow  % 3600) / 60;
  second = epochNow % 60;
  localTime = String(hour) + ":";
  localTime += ((minute < 10) ? ("0" + String(minute)) : String(minute));
}

void getEpoch() {
  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP);
  delay(1000);
  int cb = udp.parsePacket();
  if (cb) {
    udp.read(packetBuffer, NTP_PACKET_SIZE);
    highWord = word(packetBuffer[40], packetBuffer[41]);
    lowWord = word(packetBuffer[42], packetBuffer[43]);
    secsSince1900 = highWord << 16 | lowWord;
    epochGotTime = millis();
    epoch = secsSince1900 - seventyYears + timeZoneShift;
    Serial.println("[epoch]" + String(epoch));
    getTime();
    timeSynchronized = localTime;
    timeout[1] = 86400;
  } else {
    timeout[1] = 10;
  }
}

void setup() {
  pinMode(watchDogPin, OUTPUT);
  digitalWrite(watchDogPin, HIGH);
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.onStart([]() {
    String type;
    type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    switch (error) {
      case OTA_AUTH_ERROR : Serial.println("Auth Failed");
      break;
      case OTA_BEGIN_ERROR : Serial.println("Begin Failed");
      break;
      case OTA_CONNECT_ERROR : Serial.println("Connect Failed");
      break;
      case OTA_RECEIVE_ERROR : Serial.println("Receive Failed");
      break;
      case OTA_END_ERROR : Serial.println("End Failed");
      break;
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  while (!Serial) {
    ;
  }
  
  WiFi.begin(ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }
  
  udp.begin(localPort);
  
  Serial.println("[something]");
  getEpoch();
  
  server.begin();
  Serial.println("Web server started");

  ThingSpeak.begin(client);

  previousTime[2] = millis();
}

void loop() {
  ArduinoOTA.handle();
  WiFiClient client = server.available();
  if (client) {
    Serial.println("New client");
    boolean blank_line = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n' && blank_line) {
          Serial.print(header);
          if (header.indexOf("GET /password") >= 0) {
            if (header.indexOf("192.168.0.5") >= 0) {
              espUrl = "http://192.168.0.5";
            } else {
              espUrl = "http://real_ip";
            }
            client.println("HTTP/1.1 200 OK");
            if (header.indexOf("/request") >= 0) {
              client.println("Content-Type: application/json");
              client.println("Connection: close");
              client.println();
              if (header.indexOf("/getData") >= 0) {
                dataResponse = "{\"temperature\":{";
                for(byte i = 0; i < 4; i++) {
                  dataResponse += "\"" + String(temperaturePlace[i]) + "\":" + String(temperature[i]);
                  if (i != 3) dataResponse += ",";
                }
                dataResponse += "},\"humidity\":{";
                for(byte i = 0; i < 2; i++) {
                  dataResponse += "\"" + String(humidityPlace[i]) + "\":" + String(humidity[i]);
                  if (i != 1) dataResponse += ",";
                }
                dataResponse += "},\"isDeviceOn\":{";
                for(byte i = 0; i < 5; i++) {
                  dataResponse += "\"" + String(deviceName[i]) + "\":" + String(isDeviceOn[i]);
                  if (i != 4) dataResponse += ",";
                }
                dataResponse += "},\"isValveOpen\":{";
                for(byte i = 0; i < 5; i++) {
                  dataResponse += "\"" + String(valvePlace[i]) + "\":" + String(isValveOpen[i]);
                  if (i != 4) dataResponse += ",";
                }
                dataResponse += "},\"heaterPowerConsumption\":{";
                for(byte i = 0; i < 2; i++) {
                  dataResponse += "\"" + String(deviceName[i]) + "\":" + String(tenConsumption[i]);
                  if (i != 1) dataResponse += ",";
                }
                dataResponse += "},\"targetTemperature\":{";
                for(byte i = 0; i < 2; i++) {
                  dataResponse += "\"" + String(targetTemperatureName[i]) + "\":" + String(targetTemperature[i]);
                  if (i != 1) dataResponse += ",";
                }
                dataResponse += "},\"electricBoilerState\":\"" + String(boilerState == 1 ? "switched off" : boilerState == 2 ? "automatic" : "switched on") + "\",";
                dataResponse += "\"timeSynchronized\":\"" + timeSynchronized + "\",";
                dataResponse += "\"localTime\":\"" + localTime + "\",";
                dataResponse += "\"dataUpdatePeriod\":" + String(timeout[2]) + ",";
                dataResponse += "\"boilerWorkingPeriods\": [" + String(boilerSwitchOnTime) + ",";
                dataResponse += String(boilerToggleTime) + "," + String(boilerSwitchOffTime) + "]}";
                client.println(dataResponse);
                Serial.println("JSON data sent");
              } else if (header.indexOf("/switchedOn") >= 0) {
                Serial.println("[boilerOn]");
                boilerState = 3;
                client.println("{\"response\":\"Electric boiler switched on\"}");
              } else if (header.indexOf("/switchedOff") >= 0) {
                Serial.println("[boilerOff]");
                boilerState = 1;
                client.println("{\"response\":\"Electric boiler switched off\"}");
              } else if (header.indexOf("/automatic") >= 0) {
                Serial.println("[boilerTempDependent]");
                boilerState = 2;
                client.println("{\"response\":\"Electric boiler is in automatic mode\"}");
              } else if (header.indexOf("/SoftResetMega") >= 0) {
                Serial.println("[softResetMega]");
                client.println("{\"response\":\"Mega board is soft reseted\"}");
              } else if (header.indexOf("/SoftResetESP") >= 0) {
                ESP.restart();
                client.println("{\"response\":\"ESP board is soft reseted\"}");
              } else if (header.indexOf("/HardResetMega") >= 0) {
                megaHardReset();
                client.println("{\"response\":\"Mega board is hard reseted\"}");
              } else if (header.indexOf("/HardResetESP") >= 0) {
                Serial.println("[hardResetESP]");
                client.println("{\"response\":\"ESP board is hard reseted\"}");
              } else if (header.indexOf("/setDayTemperature") >= 0) {
                Serial.println("[setDayTemperature]" + header.substring(37, 39));
                client.println("{\"response\":\"Day target temperature is set\"}");
              } else if (header.indexOf("/setEveningTemperature") >= 0) {
                Serial.println("[setEveningTemperature]" + header.substring(41, 43));
                client.println("{\"response\":\"Evening target temperature is set\"}");
              } else if (header.indexOf("/setWorkingPeriods") >= 0) {
                String values = header.substring(37);
                byte dotPosition = values.indexOf(".");
                Serial.println("[boilerSwitchOnTime]" + values.substring(0, dotPosition));
                values = values.substring(dotPosition + 3);
                dotPosition = values.indexOf(".");
                Serial.println("[boilerToggleTime]" + values.substring(0, dotPosition));
                 values = values.substring(dotPosition + 3);
                dotPosition = values.indexOf(".");
                Serial.println("[boilerSwitchOffTime]" + values.substring(0, dotPosition));
                client.println("{\"response\":\"Electric boiler working periods are set\"}");
              }
            } else {
              Serial.println("Main Web Page sent");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              client.println("<!DOCTYPE html>");
              client.println("<html lang=\"en\">");
              client.println("<head>");
              client.println("<meta charset=\"UTF-8\">");
              client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\">");
              client.println("<title>Edward's smart home</title>");
              client.println("<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.5.0/css/bootstrap.min.css\"");
              client.println("integrity=\"sha384-9aIt2nRpC12Uk9gS9baDl411NQApFmC26EwAOH8WgZl5MYYxFfc+NcPb1dKGj7Sk\" crossorigin=\"anonymous\">");
              client.println("<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/animate.css/4.0.0/animate.min.css\">");
              client.println("<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/noUiSlider/14.5.0/nouislider.min.css\">");
              client.println("<style>");
              client.println(".container {");
              client.println("padding: 5px;");
              client.println("}");
              client.println(".row {");
              client.println("margin-right: 0;");
              client.println("}");
              client.println(".card-body {");
              client.println("padding: 0.5rem;");
              client.println("text-align: center;");
              client.println("}");
              client.println(".input-group {");
              client.println("text-align: center;");
              client.println("}");
              client.println(".navbar-light h3 {");
              client.println("margin-top: 0.5rem!important;");
              client.println("}");
              client.println(".navbar-light p {");
              client.println("margin-top: 0.5rem;");
              client.println("margin-bottom: 0.5rem;");
              client.println("}");
              client.println(".col, .col-1, .col-10, .col-11, .col-12, .col-2, .col-3, .col-4, .col-5, .col-6, .col-7, .col-8, .col-9, .col-auto, .col-lg, .col-lg-1, .col-lg-10, .col-lg-11, .col-lg-12, .col-lg-2, .col-lg-3, .col-lg-4, .col-lg-5, .col-lg-6, .col-lg-7, .col-lg-8, .col-lg-9, .col-lg-auto, .col-md, .col-md-1, .col-md-10, .col-md-11, .col-md-12, .col-md-2, .col-md-3, .col-md-4, .col-md-5, .col-md-6, .col-md-7, .col-md-8, .col-md-9, .col-md-auto, .col-sm, .col-sm-1, .col-sm-10, .col-sm-11, .col-sm-12, .col-sm-2, .col-sm-3, .col-sm-4, .col-sm-5, .col-sm-6, .col-sm-7, .col-sm-8, .col-sm-9, .col-sm-auto, .col-xl, .col-xl-1, .col-xl-10, .col-xl-11, .col-xl-12, .col-xl-2, .col-xl-3, .col-xl-4, .col-xl-5, .col-xl-6, .col-xl-7, .col-xl-8, .col-xl-9, .col-xl-auto {");
              client.println("padding-right: 0;");
              client.println("}");
              client.println(".footer {");
              client.println("position: relative;");
              client.println("width: 100%;");
              client.println("height: 40px;");
              client.println("line-height: 40px;");
              client.println("}");
              client.println("#temperature-block {");
              client.println("display: none;");
              client.println("}");
              client.println("#humidity-block {");
              client.println("display: none;");
              client.println("}");
              client.println("#service-block {");
              client.println("display: none;");
              client.println("}");
              client.println(".slider {");
              client.println("margin-top: 50px;");
              client.println("margin-bottom: 40px;");
              client.println("margin-left: 20px;");
              client.println("margin-right: 20px;");
              client.println("}");
              client.println(".container h6 {");
              client.println("text-align: center;");
              client.println("}");
              client.println("</style>");
              client.println("</head>");
              client.println("<body>");
              client.println("<header>");
              client.println("<nav class=\"navbar navbar-expand-lg navbar-dark bg-dark\">");
              client.println("<a class=\"navbar-brand\" href=\"index.html\">Edward's smart home</a>");
              client.println("<button class=\"navbar-toggler\" type=\"button\" data-toggle=\"collapse\" data-target=\"#navbarNavDropdown\"");
              client.println("aria-controls=\"navbarNavDropdown\" aria-expanded=\"false\" aria-label=\"Toggle navigation\">");
              client.println("<span class=\"navbar-toggler-icon\"></span>");
              client.println("</button>");
              client.println("<div class=\"collapse navbar-collapse animate__animated animate__fadeInLeft\" id=\"navbarNavDropdown\">");
              client.println("<div class=\"navbar-nav\">");
              client.println("<button type=\"button\" class=\"btn btn-dark\" id=\"basic-information-btn\"><i class=\"fas fa-tachometer-alt\"></i> Basic information</button>");
              client.println("<button type=\"button\" class=\"btn btn-dark\" id=\"temperature-btn\"><i class=\"fas fa-temperature-low\"></i> Temperatures</button>");
              client.println("<button type=\"button\" class=\"btn btn-dark\" id=\"humidity-btn\"><i class=\"fas fa-tint\"></i> Humidities</button>");
              client.println("<button type=\"button\" class=\"btn btn-dark\" id=\"service-btn\"><i class=\"fas fa-tools\"></i> Service section</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</nav>");
              client.println("</header>");
              client.println("<main>");
              client.println("<div class=\"animate__animated animate__fadeInUpBig\" id=\"basic-information-block\">");
              client.println("<nav class=\"navbar navbar-light bg-light\">");
              client.println("<h3 class=\"mt-4\">Basic information</h3>");
              client.println("<p>Data updated at <span id=\"localTime\"></span></p>");
              client.println("</nav>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-temperature-low\"></i>Temperatures</div>");
              client.println("<div class=\"card-body\"><canvas id=\"temperatureBar\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-tint\"></i>Humidities</div>");
              client.println("<div class=\"card-body\"><canvas id=\"humidityBar\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<nav class=\"navbar navbar-dark bg-dark\">");
              client.println("<span class=\"navbar-text text-white\" id=\"devicesState\">Devices' state</span>");
              client.println("</nav>");
              client.println("<div class=\"container\">");
              client.println("<div class=\"row row-cols-2 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"heaterOne\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">heater 1</h6>");
              client.println("<p class=\"card-text\" id=\"heaterOneConsumption\"></p>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"heaterTwo\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">heater 2</h6>");
              client.println("<p class=\"card-text\" id=\"heaterTwoConsumption\"></p>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"toiletFan\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">toilet fan</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"bathroomFan\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">bathroom fan</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"gasFiredBoiler\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">gas-fired boiler</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<nav class=\"navbar navbar-dark bg-dark\">");
              client.println("<span class=\"navbar-text text-white\" id=\"radiatorsState\">Radiators' state</span>");
              client.println("</nav>");
              client.println("<div class=\"container\">");
              client.println("<div class=\"row row-cols-2 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"childroomRadiator\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">childroom</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"kitchenRadiator\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">kitchen</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"bedroomRadiator\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">bedroom</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"toiletRadiator\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">toilet</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 text-white\" id=\"bathroomRadiator\">");
              client.println("<div class=\"card-body\">");
              client.println("<h6 class=\"card-title\">bathroom</h6>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"animate__animated animate__fadeInUpBig\" id=\"temperature-block\">");
              client.println("<nav class=\"navbar navbar-light bg-light\">");
              client.println("<h3 class=\"mt-4\">Temperatures</h3>");
              client.println("</nav>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"container\">");
              client.println("<h6>Temperature values range</h6>");
              client.println("<div class=\"slider\" id=\"temperatureRange\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"setTemperatureRange\">set</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"container\">");
              client.println("<h6>Shown period in range</h6>");
              client.println("<div class=\"slider\" id=\"temperaturePeriod\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"showTemperaturePeriod\">show</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Kitchen temperature</div>");
              client.println("<div class=\"card-body\"><canvas id=\"kitchenTemperature\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Bathroom temperature</div>");
              client.println("<div class=\"card-body\"><canvas id=\"bathroomTemperature\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Electric boiler temperature</div>");
              client.println("<div class=\"card-body\"><canvas id=\"electricBoilerTemperature\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Outdoor temperature</div>");
              client.println("<div class=\"card-body\"><canvas id=\"outdoorTemperature\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"animate__animated animate__fadeInUpBig\" id=\"humidity-block\">");
              client.println("<nav class=\"navbar navbar-light bg-light\">");
              client.println("<h3 class=\"mt-4\">Humidities</h3>");
              client.println("</nav>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"container\">");
              client.println("<h6>Humidity values range</h6>");
              client.println("<div class=\"slider\" id=\"humidityRange\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"setHumidityRange\">set</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"container\">");
              client.println("<h6>Shown period in range</h6>");
              client.println("<div class=\"slider\" id=\"humidityPeriod\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"showHumidityPeriod\">show</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Kitchen humidity</div>");
              client.println("<div class=\"card-body\"><canvas id=\"kitchenHumidity\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<div class=\"card mb-4\">");
              client.println("<div class=\"card-header bg-dark text-white\"><i class=\"fas fa-chart-area mr-1\"></i>Bathroom humidity</div>");
              client.println("<div class=\"card-body\"><canvas id=\"bathroomHumidity\" width=\"100%\" height=\"60\"></canvas></div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"animate__animated animate__fadeInUpBig\" id=\"service-block\">");
              client.println("<nav class=\"navbar navbar-light bg-light\">");
              client.println("<h3 class=\"mt-4\">Service section</h3>");
              client.println("<p>Time was synchronized at <span id=\"timeSynchronized\"></span></p>");
              client.println("</nav>");
              client.println("<div class=\"row\">");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<nav class=\"navbar navbar-dark bg-dark\">");
              client.println("<span class=\"navbar-text text-white\" id=\"boilerSetup\">Electric boiler setup</span>");
              client.println("</nav>");
              client.println("<div class=\"container\">");
              client.println("<h6>Mode</h6>");
              client.println("<div class=\"row row-cols-2 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-secondary text-white\">");
              client.println("<button type=\"button\" class=\"btn btn-secondary\" id=\"switchedOn\">switched on</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-secondary text-white\">");
              client.println("<button type=\"button\" class=\"btn btn-secondary\" id=\"automatic\">automatic</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-secondary text-white\">");
              client.println("<button type=\"button\" class=\"btn btn-secondary\" id=\"switchedOff\">switched off</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"container\">");
              client.println("<h6>Day target temperature</h6>");
              client.println("<div class=\"slider\" id=\"day-target-temperature\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"setDayTemperature\">set</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"container\">");
              client.println("<h6>Evening target temperature</h6>");
              client.println("<div class=\"slider\" id=\"evening-target-temperature\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"setEveningTemperature\">set</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"container\">");
              client.println("<h6>Working periods setup</h6>");
              client.println("<div class=\"slider\" id=\"boiler-working-periods\"></div>");
              client.println("<div class=\"row row-cols-3 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"setWorkingPeriods\">set</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"container\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"reset\">reset all sliders</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col-md-6 col-lg-6 col-xl-6\">");
              client.println("<nav class=\"navbar navbar-dark bg-dark\">");
              client.println("<span class=\"navbar-text text-white\" id=\"resetSection\">Controllers' reset</span>");
              client.println("</nav>");
              client.println("<div class=\"container\">");
              client.println("<div class=\"row row-cols-2 row-cols-sm-3 row-cols-md-3\">");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"softResetMega\">soft reset Mega</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"softResetESP\">soft reset ESP</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"hardResetMega\">hard reset Mega</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("<div class=\"col mb-2\">");
              client.println("<div class=\"card h-100 bg-warning\">");
              client.println("<button type=\"button\" class=\"btn btn-warning\" id=\"hardResetESP\">hard reset ESP</button>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</div>");
              client.println("</main>");
              client.println("<footer class=\"footer\">");
              client.println("<nav class=\"navbar navbar-dark bg-dark\">");
              client.println("<div class=\"text-muted\">Design and development by Eduard Luchuk &copy; 2020</div>");
              client.println("</nav>");
              client.println("</footer>");
              client.println("<script src=\"https://code.jquery.com/jquery-3.5.1.slim.min.js\"");
              client.println("integrity=\"sha384-DfXdz2htPH0lsSSs5nCTpuj/zy4C+OGpamoFVy38MVBnE+IbbVYUew+OrCXaRkfj\"");
              client.println("crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdn.jsdelivr.net/npm/popper.js@1.16.0/dist/umd/popper.min.js\"");
              client.println("integrity=\"sha384-Q6E9RHvbIyZFJoft+2mJbHaEWldlvI9IOYy5n3zV9zzTtmI3UksdQRVvoxMfooAo\"");
              client.println("crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://stackpath.bootstrapcdn.com/bootstrap/4.5.0/js/bootstrap.min.js\"");
              client.println("integrity=\"sha384-OgVRvuATP1z7JjHLkuOU7Xw704+h835Lr+6QL9UvYjZE3Ipu6Tp75j7Bh/kR0JKI\"");
              client.println("crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.11.2/js/all.min.js\"");
              client.println("crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.8.0/Chart.min.js\" crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdn.datatables.net/1.10.20/js/jquery.dataTables.min.js\" crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdn.datatables.net/1.10.20/js/dataTables.bootstrap4.min.js\" crossorigin=\"anonymous\"></script>");
              client.println("<script src=\"https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels\"></script>");
              client.println("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/noUiSlider/14.5.0/nouislider.min.js\"></script>");
              client.println("<script src=\"https://cdnjs.cloudflare.com/ajax/libs/wnumb/1.2.0/wNumb.min.js\"></script>");
              client.println("<script>");
              client.println("\"use strict\";");
              client.println("Chart.defaults.global.defaultFontFamily = '-apple-system,system-ui,BlinkMacSystemFont,\"Segoe UI\",Roboto,\"Helvetica Neue\",Arial,sans-serif';");
              client.println("Chart.defaults.global.defaultFontColor = \"#292b2c\";");
              client.println("Chart.plugins.unregister(ChartDataLabels);");
              client.println("const espUrl = \"" + espUrl + "\";");
              client.println("const espData = {};");
              client.println("const thingTemperatureData = {};");
              client.println("const thingHumidityData = {};");
              client.println("");
              client.println("const currentValuesChartData = {};");
              client.println("const tepmeratureChartData = {};");
              client.println("const humidityChartData = {};");
              client.println("");
              client.println("const currentValuesChart = {};");
              client.println("const temperatureChart = {};");
              client.println("const humidityChart = {};");
              client.println("");
              client.println("function fetchData(method, url, body = null) {");
              client.println("const headers = { \"Content-Type\": \"application/json\", };");
              client.println("const parameters = { method: method, headers: headers, };");
              client.println("if (method === \"POST\") parameters.body = JSON.stringify(body);");
              client.println("return fetch(url, parameters).then(response => response.json()).catch(err => console.error(err));");
              client.println("}");
              client.println("");
              client.println("function generatePipsPositions(min, max, step, multiplier) {");
              client.println("const pipsArray = [0, 100];");
              client.println("const rangeStep = Math.floor((max - min) / step / multiplier) * step * 100 / (max - min);");
              client.println("let pipsPosition = rangeStep;");
              client.println("while (pipsPosition < 100) {");
              client.println("pipsArray.push(pipsPosition);");
              client.println("pipsPosition += rangeStep;");
              client.println("}");
              client.println("return pipsArray;");
              client.println("}");
              client.println("");
              client.println("function SliderParameters(connect, tooltips, step, start, multiplier, min, max) {");
              client.println("this.connect = connect;");
              client.println("this.tooltips = tooltips;");
              client.println("this.step = step;");
              client.println("this.start = start;");
              client.println("this.pips = {");
              client.println("mode: \"positions\",");
              client.println("values: generatePipsPositions(min, max, step, multiplier),");
              client.println("density: 100,");
              client.println("format: wNumb({ decimals: 0 })");
              client.println("};");
              client.println("this.range = {");
              client.println("\"min\": min,");
              client.println("\"max\": max");
              client.println("};");
              client.println("}");
              client.println("");
              client.println("const dayTemperatureParameters = new SliderParameters(\"lower\", wNumb({ decimals: 0 }), 1, 30, 6, 30, 60);");
              client.println("const eveningTemperatureParameters = new SliderParameters(\"lower\", wNumb({ decimals: 0 }), 1, 30, 6, 30, 60);");
              client.println("const workingPeriodsParameters = new SliderParameters([false, true, true, false], [wNumb({ decimals: 0 }), wNumb({ decimals: 0 }), wNumb({ decimals: 0 })], 1, [0, 0, 0], 6, 0, 24);");
              client.println("const temperatureRangeParameters = new SliderParameters(\"lower\", wNumb({ decimals: 0 }), 10, 40, 4, 40, 300);");
              client.println("const temperaturePeriodParameters = new SliderParameters([false, true, false], [wNumb({ decimals: 0 }), wNumb({ decimals: 0 })], 10, [0, 40], 4, 0, 40);");
              client.println("const humidityRangeParameters = new SliderParameters(\"lower\", wNumb({ decimals: 0 }), 10, 40, 4, 40, 300);");
              client.println("const humidityPeriodParameters = new SliderParameters([false, true, false], [wNumb({ decimals: 0 }), wNumb({ decimals: 0 })], 10, [0, 40], 4, 0, 40);");
              client.println("");
              client.println("const dayTemperatureSlider = document.querySelector(\"#day-target-temperature\");");
              client.println("const eveningTemperatureSlider = document.querySelector(\"#evening-target-temperature\");");
              client.println("const workingPeriodsSlider = document.querySelector(\"#boiler-working-periods\");");
              client.println("const temperatureRangeSlider = document.querySelector(\"#temperatureRange\");");
              client.println("const temperaturePeriodSlider = document.querySelector(\"#temperaturePeriod\");");
              client.println("const humidityRangeSlider = document.querySelector(\"#humidityRange\");");
              client.println("const humidityPeriodSlider = document.querySelector(\"#humidityPeriod\");");
              client.println("");
              client.println("noUiSlider.create(dayTemperatureSlider, dayTemperatureParameters);");
              client.println("noUiSlider.create(eveningTemperatureSlider, eveningTemperatureParameters);");
              client.println("noUiSlider.create(workingPeriodsSlider, workingPeriodsParameters);");
              client.println("noUiSlider.create(temperatureRangeSlider, temperatureRangeParameters);");
              client.println("noUiSlider.create(temperaturePeriodSlider, temperaturePeriodParameters);");
              client.println("noUiSlider.create(humidityRangeSlider, humidityRangeParameters);");
              client.println("noUiSlider.create(humidityPeriodSlider, humidityPeriodParameters);");
              client.println("");
              client.println("const temperatureRange = {");
              client.println("start: temperaturePeriodParameters.start[0],");
              client.println("end: temperaturePeriodParameters.start[1]");
              client.println("};");
              client.println("");
              client.println("const humidityRange = {");
              client.println("start: humidityPeriodParameters.start[0],");
              client.println("end: humidityPeriodParameters.start[1]");
              client.println("};");
              client.println("");
              client.println("class ThingChannel {");
              client.println("constructor(channel, apiKey, valuesNumber) {");
              client.println("this.channel = channel;");
              client.println("this.apiKey = apiKey;");
              client.println("this.valuesNumber = valuesNumber");
              client.println("}");
              client.println("");
              client.println("get url() {");
              client.println("return `https://api.thingspeak.com/channels/${this.channel}/feeds.json?api_key=${this.apiKey}&results=${this.valuesNumber}`;");
              client.println("}");
              client.println("}");
              client.println("");
              client.println("const temperatureChannel = new ThingChannel(999999, \"KEY\", Number(temperatureRangeSlider.noUiSlider.get()));");
              client.println("const humidityChannel = new ThingChannel(999999, \"KEY\", Number(humidityRangeSlider.noUiSlider.get()));");
              client.println("");
              client.println("const timeShift = 3;");
              client.println("");
              client.println("class BarChart {");
              client.println("constructor(id, units, maxValue, dataObj) {");
              client.println("this.ctx = document.querySelector(`#${id}`);");
              client.println("this.units = units;");
              client.println("this.maxValue = maxValue;");
              client.println("this.dataObj = dataObj;");
              client.println("}");
              client.println("");
              client.println("buildBarChart() {");
              client.println("return new Chart(this.ctx, {");
              client.println("plugins: [ChartDataLabels],");
              client.println("type: \"bar\",");
              client.println("data: {");
              client.println("labels: espDataToChart(this.dataObj, \"labels\"),");
              client.println("datasets: [{ label: this.units, backgroundColor: \"rgba(2,117,216,1)\", borderColor: \"rgba(2,117,216,1)\", data: espDataToChart(this.dataObj, \"data\") }],");
              client.println("},");
              client.println("options: {");
              client.println("scales: {");
              client.println("xAxes: [{ time: { unit: \"spot\" }, gridLines: { display: false }, ticks: { maxTicksLimit: 6 } }],");
              client.println("yAxes: [{ ticks: { min: 0, max: this.maxValue, maxTicksLimit: 5 }, gridLines: { display: true } }],");
              client.println("},");
              client.println("legend: { display: false },");
              client.println("plugins: {");
              client.println("datalabels: { color: \"#fff\" }");
              client.println("}");
              client.println("},");
              client.println("});");
              client.println("}");
              client.println("}");
              client.println("");
              client.println("function espDataToChart(dataObj, dataType) {");
              client.println("const arr = [];");
              client.println("for (let key in dataObj) {");
              client.println("arr.push(dataType === \"labels\" ? key : dataType === \"data\" ? dataObj[key] : \"error\");");
              client.println("}");
              client.println("return arr;");
              client.println("}");
              client.println("");
              client.println("function updateBarCharts() {");
              client.println("for (let category in currentValuesChart) {");
              client.println("const labels = espDataToChart(espData[category], \"labels\");");
              client.println("const data = espDataToChart(espData[category], \"data\");");
              client.println("currentValuesChart[category].data.labels = labels;");
              client.println("currentValuesChart[category].data.datasets[0].data = data;");
              client.println("currentValuesChart[category].update();");
              client.println("};");
              client.println("}");
              client.println("");
              client.println("class LineChart {");
              client.println("constructor(id, field, maxValue, rangeType, dataArray) {");
              client.println("this.ctx = document.querySelector(`#${id}`);");
              client.println("this.field = field;");
              client.println("this.maxValue = maxValue;");
              client.println("this.dataArray = dataArray;");
              client.println("this.rangeType = rangeType;");
              client.println("}");
              client.println("");
              client.println("buildLineChart() {");
              client.println("return new Chart(this.ctx, {");
              client.println("type: \"line\",");
              client.println("data: {");
              client.println("labels: thingDataToChart(this.dataArray, this.rangeType, \"created_at\", timeShift),");
              client.println("datasets: [{");
              client.println("label: null,");
              client.println("lineTension: 0.3,");
              client.println("backgroundColor: \"rgba(2,117,216,0.2)\",");
              client.println("borderColor: \"rgba(2,117,216,1)\",");
              client.println("pointRadius: 5,");
              client.println("pointBackgroundColor: \"rgba(2,117,216,1)\",");
              client.println("pointBorderColor: \"rgba(255,255,255,0.8)\",");
              client.println("pointHoverRadius: 5,");
              client.println("pointHoverBackgroundColor: \"rgba(2,117,216,1)\",");
              client.println("pointHitRadius: 50,");
              client.println("pointBorderWidth: 2,");
              client.println("data: thingDataToChart(this.dataArray, this.rangeType, this.field),");
              client.println("}],");
              client.println("},");
              client.println("options: {");
              client.println("scales: {");
              client.println("xAxes: [{");
              client.println("time: { unit: \"time\" }, gridLines: { display: false }, ticks: { maxTicksLimit: 7 }");
              client.println("}],");
              client.println("yAxes: [{");
              client.println("ticks: { min: 0, max: this.maxValue, maxTicksLimit: 5 },");
              client.println("gridLines: { color: \"rgba(0, 0, 0, .125)\", }");
              client.println("}],");
              client.println("},");
              client.println("legend: { display: false }");
              client.println("}");
              client.println("});");
              client.println("}");
              client.println("}");
              client.println("");
              client.println("function thingDataToChart(dataType, rangeType, category, timeShift = null) {");
              client.println("let field;");
              client.println("switch (category) {");
              client.println("case \"kitchen\": field = \"field1\";");
              client.println("break;");
              client.println("case \"bathroom\": field = \"field2\";");
              client.println("break;");
              client.println("case \"electricBoiler\": field = \"field3\";");
              client.println("break;");
              client.println("case \"outdoor\": field = \"field4\";");
              client.println("break;");
              client.println("default: field = category;");
              client.println("}");
              client.println("const selectedTemperatureData = dataType.feeds.slice(rangeType.start, rangeType.end);");
              client.println("const dataToChart = [];");
              client.println("selectedTemperatureData.forEach(item => dataToChart.push(field === \"created_at\" ? item[field]");
              client.println(".slice(11, 16)");
              client.println(".split(\":\")");
              client.println(".map((item, index) => index === 0 ?");
              client.println("( +item + timeShift - 24) >= 0 ?");
              client.println("( +item + timeShift - 24) : ( +item + timeShift) :");
              client.println("item)");
              client.println(".join(\":\") : item[field]));");
              client.println("return dataToChart;");
              client.println("}");
              client.println("");
              client.println("function updateLineCharts(dataType, chartType, rangeType) {");
              client.println("const labels = thingDataToChart(dataType, rangeType, \"created_at\", timeShift);");
              client.println("for (let category in chartType) {");
              client.println("const data = thingDataToChart(dataType, rangeType, category);");
              client.println("chartType[category].data.labels = labels;");
              client.println("chartType[category].data.datasets[0].data = data;");
              client.println("chartType[category].update();");
              client.println("};");
              client.println("}");
              client.println("");
              client.println("function generateUrl(basis, request) {");
              client.println("return basis + \"/password/request/\" + request;");
              client.println("}");
              client.println("");
              client.println("function getData(generatedUrl, dataType) {");
              client.println("return fetchData(\"GET\", generatedUrl)");
              client.println(".then(data => Object.assign(dataType, data));");
              client.println("}");
              client.println("");
              client.println("document.addEventListener(\"DOMContentLoaded\", () => {");
              client.println("getData(generateUrl(espUrl, \"getData\"), espData).then(() => {");
              client.println("currentValuesChartData.temperature = new BarChart(\"temperatureBar\", \"degree\", 60, espData.temperature);");
              client.println("currentValuesChartData.humidity = new BarChart(\"humidityBar\", \"percent\", 100, espData.humidity);");
              client.println("currentValuesChart.temperature = currentValuesChartData.temperature.buildBarChart();");
              client.println("currentValuesChart.humidity = currentValuesChartData.humidity.buildBarChart();");
              client.println("toggleButton(espData.electricBoilerState);");
              client.println("setSliders();");
              client.println("insertCurrentValues(espData);");
              client.println("});");
              client.println("getData(temperatureChannel.url, thingTemperatureData).then(() => {");
              client.println("tepmeratureChartData.kitchen = new LineChart(\"kitchenTemperature\", \"kitchen\", 30, temperatureRange, thingTemperatureData);");
              client.println("tepmeratureChartData.bathroom = new LineChart(\"bathroomTemperature\", \"bathroom\", 30, temperatureRange, thingTemperatureData);");
              client.println("tepmeratureChartData.electricBoiler = new LineChart(\"electricBoilerTemperature\", \"electricBoiler\", 70, temperatureRange, thingTemperatureData);");
              client.println("tepmeratureChartData.outdoor = new LineChart(\"outdoorTemperature\", \"outdoor\", 40, temperatureRange, thingTemperatureData);");
              client.println("temperatureChart.kitchen = tepmeratureChartData.kitchen.buildLineChart();");
              client.println("temperatureChart.bathroom = tepmeratureChartData.bathroom.buildLineChart();");
              client.println("temperatureChart.electricBoiler = tepmeratureChartData.electricBoiler.buildLineChart();");
              client.println("temperatureChart.outdoor = tepmeratureChartData.outdoor.buildLineChart();");
              client.println("});");
              client.println("getData(humidityChannel.url, thingHumidityData).then(() => {");
              client.println("humidityChartData.kitchen = new LineChart(\"kitchenHumidity\", \"kitchen\", 100, humidityRange, thingHumidityData);");
              client.println("humidityChartData.bathroom = new LineChart(\"bathroomHumidity\", \"bathroom\", 100, humidityRange, thingHumidityData);");
              client.println("humidityChart.kitchen = humidityChartData.kitchen.buildLineChart();");
              client.println("humidityChart.bathroom = humidityChartData.bathroom.buildLineChart();");
              client.println("})");
              client.println("});");
              client.println("");
              client.println("document.querySelector(\"#setTemperatureRange\").addEventListener(\"click\", () => {");
              client.println("const rangeValue = Number(temperatureRangeSlider.noUiSlider.get());");
              client.println("temperaturePeriodSlider.noUiSlider.updateOptions({");
              client.println("range: {");
              client.println("\"min\": 0,");
              client.println("\"max\": rangeValue");
              client.println("},");
              client.println("start: [0, rangeValue],");
              client.println("pips: {");
              client.println("mode: \"positions\",");
              client.println("values: generatePipsPositions(0, rangeValue, 10, 4),");
              client.println("density: 100");
              client.println("}");
              client.println("});");
              client.println("temperatureChannel.valuesNumber = rangeValue;");
              client.println("getData(temperatureChannel.url, thingTemperatureData);");
              client.println("});");
              client.println("");
              client.println("document.querySelector(\"#showTemperaturePeriod\").addEventListener(\"click\", () => {");
              client.println("temperatureRange.start = Number(temperaturePeriodSlider.noUiSlider.get()[0]);");
              client.println("temperatureRange.end = Number(temperaturePeriodSlider.noUiSlider.get()[1]);");
              client.println("updateLineCharts(thingTemperatureData, temperatureChart, temperatureRange);");
              client.println("});");
              client.println("");
              client.println("document.querySelector(\"#setHumidityRange\").addEventListener(\"click\", () => {");
              client.println("const rangeValue = Number(humidityRangeSlider.noUiSlider.get());");
              client.println("humidityPeriodSlider.noUiSlider.updateOptions({");
              client.println("range: {");
              client.println("\"min\": 0,");
              client.println("\"max\": rangeValue");
              client.println("},");
              client.println("start: [0, rangeValue],");
              client.println("pips: {");
              client.println("mode: \"positions\",");
              client.println("values: generatePipsPositions(0, rangeValue, 10, 4),");
              client.println("density: 100");
              client.println("}");
              client.println("});");
              client.println("humidityChannel.valuesNumber = rangeValue;");
              client.println("getData(humidityChannel.url, thingHumidityData);");
              client.println("});");
              client.println("");
              client.println("document.querySelector(\"#showHumidityPeriod\").addEventListener(\"click\", () => {");
              client.println("humidityRange.start = Number(humidityPeriodSlider.noUiSlider.get()[0]);");
              client.println("humidityRange.end = Number(humidityPeriodSlider.noUiSlider.get()[1]);");
              client.println("updateLineCharts(thingHumidityData, humidityChart, humidityRange);");
              client.println("});");
              client.println("");
              client.println("function toggleButton(context) {");
              client.println("[\"switchedOn\", \"automatic\", \"switchedOff\"]");
              client.println(".forEach(id => document.querySelector(`#${id}`)");
              client.println(".setAttribute(\"class\", `btn btn-${id === context ? \"danger\" : \"secondary\"}`));");
              client.println("}");
              client.println("");
              client.println("function setSliders() {");
              client.println("dayTemperatureSlider.noUiSlider.set(espData.targetTemperature.day);");
              client.println("eveningTemperatureSlider.noUiSlider.set(espData.targetTemperature.evening);");
              client.println("workingPeriodsSlider.noUiSlider.set(espData.boilerWorkingPeriods);");
              client.println("}");
              client.println("");
              client.println("setInterval(() => getData(generateUrl(espUrl, \"getData\"), espData)");
              client.println(".then(() => {");
              client.println("updateBarCharts();");
              client.println("insertCurrentValues(espData);");
              client.println("}), 60000);");
              client.println("");
              client.println("setInterval(() => {");
              client.println("getData(temperatureChannel.url, thingTemperatureData)");
              client.println(".then(() => updateLineCharts(thingTemperatureData, temperatureChart, temperatureRange));");
              client.println("getData(humidityChannel.url, thingHumidityData)");
              client.println(".then(() => updateLineCharts(thingHumidityData, humidityChart, humidityRange));");
              client.println("}, 1200000);");
              client.println("");
              client.println("function setBadgeType(id, value) {");
              client.println("document.querySelector(`#${id}`).setAttribute(\"class\", `card h-100 bg-${value ? \"danger\" : \"secondary\"} text-white`);");
              client.println("}");
              client.println("");
              client.println("function insertCurrentValues(data) {");
              client.println("document.querySelector(\"#timeSynchronized\").innerHTML = data.timeSynchronized;");
              client.println("document.querySelector(\"#localTime\").innerHTML = data.localTime;");
              client.println("setBadgeType(\"heaterOne\", data.isDeviceOn.heaterOne);");
              client.println("setBadgeType(\"heaterTwo\", data.isDeviceOn.heaterTwo);");
              client.println("setBadgeType(\"toiletFan\", data.isDeviceOn.toiletFan);");
              client.println("setBadgeType(\"bathroomFan\", data.isDeviceOn.bathroomFan);");
              client.println("setBadgeType(\"gasFiredBoiler\", data.isDeviceOn.gasFiredBoiler);");
              client.println("setBadgeType(\"childroomRadiator\", data.isValveOpen.childroom);");
              client.println("setBadgeType(\"kitchenRadiator\", data.isValveOpen.kitchen);");
              client.println("setBadgeType(\"bedroomRadiator\", data.isValveOpen.bedroom);");
              client.println("setBadgeType(\"bathroomRadiator\", data.isValveOpen.bathroom);");
              client.println("setBadgeType(\"toiletRadiator\", data.isValveOpen.toilet);");
              client.println("document.querySelector(\"#heaterOneConsumption\").innerHTML = `${data.heaterPowerConsumption.heaterOne} kW`;");
              client.println("document.querySelector(\"#heaterTwoConsumption\").innerHTML = `${data.heaterPowerConsumption.heaterTwo} kW`;");
              client.println("}");
              client.println("");
              client.println("function sendRequest(phrase, request) {");
              client.println("if (confirm(phrase)) {");
              client.println("return fetchData(\"GET\", generateUrl(espUrl, request)).then(data => alert(data.response));");
              client.println("}");
              client.println("}");
              client.println("");
              client.println("document.querySelector(\"#softResetMega\").addEventListener(\"click\", () => sendRequest(\"You are going to soft reset Mega!\", \"SoftResetMega\"));");
              client.println("document.querySelector(\"#softResetESP\").addEventListener(\"click\", () => sendRequest(\"You are going to soft reset ESP!\", \"SoftResetESP\"));");
              client.println("document.querySelector(\"#hardResetMega\").addEventListener(\"click\", () => sendRequest(\"You are going to hard reset Mega!\", \"HardResetMega\"));");
              client.println("document.querySelector(\"#hardResetESP\").addEventListener(\"click\", () => sendRequest(\"You are going to hard reset ESP!\", \"HardResetESP\"));");
              client.println("");
              client.println("document.querySelector(\"#switchedOn\").addEventListener(\"click\", () => sendRequest(\"Are you sure you want to set the boiler mode?\", \"switchedOn\")");
              client.println(".then(() => toggleButton(\"switchedOn\")));");
              client.println("document.querySelector(\"#automatic\").addEventListener(\"click\", () => sendRequest(\"Are you sure you want to set the boiler mode?\", \"automatic\")");
              client.println(".then(() => toggleButton(\"automatic\")));");
              client.println("document.querySelector(\"#switchedOff\").addEventListener(\"click\", () => sendRequest(\"Are you sure you want to set the boiler mode?\", \"switchedOff\")");
              client.println(".then(() => toggleButton(\"switchedOff\")));");
              client.println("");
              client.println("function sendSliderValue(sliderName, requestPhrase) {");
              client.println("const requestString = requestPhrase + sliderName.noUiSlider.get();");
              client.println("sendRequest(\"Are you sure you want to set value?\", requestString);");
              client.println("}");
              client.println("");
              client.println("document.querySelector(\"#setDayTemperature\").addEventListener(\"click\", () => sendSliderValue(dayTemperatureSlider, \"setDayTemperature\"));");
              client.println("document.querySelector(\"#setEveningTemperature\").addEventListener(\"click\", () => sendSliderValue(eveningTemperatureSlider, \"setEveningTemperature\"));");
              client.println("document.querySelector(\"#setWorkingPeriods\").addEventListener(\"click\", () => sendSliderValue(workingPeriodsSlider, \"setWorkingPeriods\"));");
              client.println("document.querySelector(\"#reset\").addEventListener(\"click\", setSliders);");
              client.println("");
              client.println("function toggleBlockDisplay(context) {");
              client.println("[\"basic-information-block\", \"temperature-block\", \"humidity-block\", \"service-block\"]");
              client.println(".forEach(id => document.querySelector(`#${id}`).style.display = id === context ? \"block\" : \"none\");");
              client.println("}");
              client.println("");
              client.println("document.querySelector(\"#basic-information-btn\").addEventListener(\"click\", () => toggleBlockDisplay(\"basic-information-block\"));");
              client.println("document.querySelector(\"#temperature-btn\").addEventListener(\"click\", () => toggleBlockDisplay(\"temperature-block\"));");
              client.println("document.querySelector(\"#humidity-btn\").addEventListener(\"click\", () => toggleBlockDisplay(\"humidity-block\"));");
              client.println("document.querySelector(\"#service-btn\").addEventListener(\"click\", () => toggleBlockDisplay(\"service-block\"));");
              client.println("</script>");
              client.println("</body>");
              client.println("</html>");
              client.println(htmlString);
            }
          } else {
            client.println("HTTP/1.1 401 Unauthorized");
            client.println("Connection: close");
            client.println();
          }
          header = ("");
          break;
        }
        if (c == '\n') {
          blank_line = true;
        } else if (c != '\r') {
          blank_line = false;
        }
      }
    }
    delay(10);
    client.stop();
    Serial.println("Client disconnected.");
  }
  
  while (Serial.available()) {
    previousTime[2] = millis();
    getTime();
    for(byte i = 0; i < 4; i++) {
      temperature[i] = Serial.parseFloat();
      if(temperature[i] > (previousTemperature[i] + tempInsensibility) || temperature[i] < (previousTemperature[i] - tempInsensibility)) {
        previousTemperature[i] = temperature[i];
        isValueChanged = true;
      }
    }
    if (isValueChanged) {
      for(byte i = 0; i < 4; i++) {
        ThingSpeak.setField(i + 1, temperature[i]);
      }
      ThingSpeak.writeFields(temperatureChannel, temperatureChannelAPIKey);
      isValueChanged = false;
    }
    for(byte i = 0; i < 2; i++) {
      humidity[i] = Serial.parseInt();
      if(humidity[i] > (previousHumidity[i] + humInsensibility) || humidity[i] < (previousHumidity[i] - humInsensibility)){
        previousHumidity[i] = humidity[i];
        isValueChanged = true;
      }
    }
    if (isValueChanged) {
      for(byte i = 0; i < 2; i++) {
        ThingSpeak.setField(i + 1, humidity[i]);
      }
      ThingSpeak.writeFields(humidityChannel, humidityChannelAPIKey);
      isValueChanged = false;
    }
    for(byte i = 0; i < 5; i++) {
      isDeviceOn[i] = Serial.parseInt();
    }
    for(byte i = 0; i < 5; i++) {
      isValveOpen[i] = Serial.parseInt();
    }
    for(byte i = 0; i < 2; i++) {
      tenStateChange[i] = Serial.parseInt();
    }
    for(byte i = 0; i < 2; i++) {
      tenConsumption[i] = Serial.parseFloat();
    }
    if (tenStateChange[0] || tenStateChange[1]) {
      powerConsumption = tenConsumption[0] + tenConsumption[1];
      for(byte i = 0; i < 2; i++) {
        ThingSpeak.setField(i + 1, tenConsumption[i]);
      }
      ThingSpeak.setField(3, powerConsumption);
      ThingSpeak.writeFields(powerConsumptionChannel, powerConsumptionChannelAPIKey);
    }
    for(byte i = 0; i < 2; i++) {
      targetTemperature[i] = Serial.parseFloat();
    }
    boilerState = Serial.parseInt();
    megaNeedsEpoch = Serial.parseInt();
    if (megaNeedsEpoch) Serial.println("[epoch]" + String(epochNow));
    boilerSwitchOnTime = Serial.parseInt();
    boilerToggleTime = Serial.parseInt();
    boilerSwitchOffTime = Serial.parseInt();
  }

  if (millis() >= previousTime[1] + 1000*timeout[1]) {
    previousTime[1] = millis();
    getEpoch();
  }
  
  if(millis() <= previousTime[2] + 5 * 1000 * timeout[2]) {
    isWatchDogActive = false;
  } else if(!isWatchDogActive) {
    megaStatus = "Mega didn't send data on time. WatchDog is active. Soft reset.";
    ThingSpeak.setStatus(megaStatus);
    ThingSpeak.writeFields(temperatureChannel, temperatureChannelAPIKey);
    isWatchDogActive = true;
    previousTime[3] = millis();
    Serial.println("[resetMega]");
  }
  if(isWatchDogActive) {
    if(millis() > previousTime[3] + 5 * 1000 * timeout[2]) {
      megaStatus = "Mega didn't send data after soft reset. Hard reset.";
      ThingSpeak.setStatus(megaStatus);
      ThingSpeak.writeFields(temperatureChannel, temperatureChannelAPIKey);
      previousTime[2] = millis();
      isWatchDogActive = false;
      megaHardReset();
    }
  }

  if(millis() > previousTime[4] + 1000 * timeout[2]) {
    previousTime[4] = millis();
    Serial.println("[dataSent]");
  }
  delay(50);
}