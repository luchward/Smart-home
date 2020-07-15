#include <SPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <OneWire.h>
#include <HTU21D.h>
#include <DallasTemperature.h>
#define TCAADDR 0x70
#define LED_PIN  13
boolean isDeviceOn[5] = {false, false, false, false, false};
boolean isDeviceOnPrevious[2] = {false, false};
boolean tenStateChange[2];
float specificPowerConsumption = 940.00;
float tenConsumption[2];
byte boilerState;
unsigned long tenTimeOn[2];
unsigned long tenTimeOff[2];
byte humidity[4];
boolean isHumidityCorrect[4];
byte humidityUpperLimit = 15;
byte humidityLowLimit = 10;
float temperature[6];
boolean isTemperatureCorrect[6];
float targetTemperature[4] = {46, 54, 0, 23};
float hysteresis = 1;
byte boilerSwitchOnTime = 8;
byte boilerToggleTime = 21;
byte boilerSwitchOffTime = 22;
boolean isValveOpen[5];
boolean isLightOn[2];
unsigned long timeout[5] = {1, 5, 60, 240, 300};
unsigned long previousTime[7];
boolean jaguarLag = false;
boolean isAnyValveOpen = false;
unsigned long lightOnTime;
unsigned long lightOffTime;
boolean toiletFanNeedsOff = false;
boolean lightLag = false;
byte ssrOnLevel = 66;
float tempValue;
boolean isWatchDogActive;
String temperaturePlace[6] = {"kitchen", "bathroom", "", "", "boiler", "outdoor"}, humidityPlace[2] = {"kitchen", "bathroom"};
String deviceName[5] = {"TEN1", "TEN2", "toilet fan", "bathroom fan", "Jaguar"}, valvePlace[5] = {"childroom", "kitchen", "bedroom", "toilet", "bathroom"};
unsigned long epoch, epochGotTime, epochNow;
boolean megaNeedsEpoch = true;
OneWire oneWire(22);
DallasTemperature tempSensors(&oneWire);
DeviceAddress ds18b20Boiler = {0x28, 0xAA, 0x5E, 0xC7, 0x4B, 0x14, 0x01, 0x7C};
DeviceAddress ds18b20Out = {0x28, 0xFF, 0xB2, 0x0B, 0x60, 0x17, 0x03, 0xD5};
RTC_DS1307 rtc;
HTU21D HTU21(HTU21D_RES_RH8_TEMP12);
int hour, minute, second;
String localTime, inString, dataToDebag, dataToESP;

void getTime() {
  epochNow = (millis() - epochGotTime) / 1000 + epoch;
  hour = (epochNow  % 86400L) / 3600;
  minute = (epochNow  % 3600) / 60;
  second = epochNow % 60;
  localTime = String(hour) + ":";
  localTime += minute < 10 ? ("0" + String(minute) + ":") : (String(minute) + ":");
  localTime += second < 10 ? ("0" + String(second)) : String(second);
}

void tcaSelect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();

}

void coilSelect(float targetTemperature, boolean oddOrEvenHour) {
  if (0 < temperature[4] && temperature[4] < targetTemperature - 2 * hysteresis) {
    isDeviceOn[0] = true;
    isDeviceOn[1] = true;
    digitalWrite(LED_PIN, HIGH);
  }
  if (targetTemperature - 2 * hysteresis <= temperature[4] && temperature[4] < targetTemperature - hysteresis) {
    if (oddOrEvenHour % 2 > 0) {
      isDeviceOn[0] = true;
      isDeviceOn[1] = false;
    } else {
      isDeviceOn[0] = false;
      isDeviceOn[1] = true;
    }
    digitalWrite(LED_PIN, HIGH);
  }
  if (targetTemperature + hysteresis <= temperature[4]) {
    isDeviceOn[0] = false;
    isDeviceOn[1] = false;
    digitalWrite(LED_PIN, LOW);
  }
}

void(* resetFunc) (void) = 0;

void hardResetESP() {
  Serial.println(F ("ESP didn't respond on time. Hard reset."));
  digitalWrite(46, HIGH);
  delay(300);
  digitalWrite(46, LOW);
}

void serialEvent3() {
  while (Serial3.available()) {
    char inChar = Serial3.read();
    Serial.write(inChar);
    inString += inChar;
    if (inChar == ']') {
      tcaSelect(7);
      if (inString.indexOf("[boilerOn]") >= 0) {
        boilerState = 3;
        rtc.writenvram(55, 3);
      } else if (inString.indexOf("[boilerOff]") >= 0) {
        boilerState = 1;
        rtc.writenvram(55, 1);
      } else if (inString.indexOf("[boilerTempDependent]") >= 0) {
        boilerState = 2;
        rtc.writenvram(55, 2);
      } else if (inString.indexOf("[softResetMega]") >= 0) {
        resetFunc();
      } else if (inString.indexOf("[dataSent]") >= 0) {
        previousTime[5] = millis();
      } else if (inString.indexOf("[hardResetESP]") >= 0) {
        hardResetESP();
      } else if (inString.indexOf("[epoch]") >= 0) {
        epoch = Serial3.parseInt();
        epochGotTime = millis();
        getTime();
        megaNeedsEpoch = false;
      } else if (inString.indexOf("[setDayTemperature]") >= 0) {
        targetTemperature[0] = Serial3.parseInt();
        rtc.writenvram(4, targetTemperature[0]);
      } else if (inString.indexOf("[setEveningTemperature]") >= 0) {
        targetTemperature[1] = Serial3.parseInt();
        rtc.writenvram(5, targetTemperature[1]);
      } else if (inString.indexOf("[boilerSwitchOnTime]") >= 0) {
        boilerSwitchOnTime = Serial3.parseInt();
        rtc.writenvram(6, boilerSwitchOnTime);
        Serial.println("boilerSwitchOnTime = " + String(boilerSwitchOnTime));
      } else if (inString.indexOf("[boilerToggleTime]") >= 0) {
        boilerToggleTime = Serial3.parseInt();
        rtc.writenvram(7, boilerToggleTime);
        Serial.println("boilerToggleTime = " + String(boilerToggleTime));
      } else if (inString.indexOf("[boilerSwitchOffTime]") >= 0) {
        boilerSwitchOffTime = Serial3.parseInt();
        rtc.writenvram(8, boilerSwitchOffTime);
        Serial.println("boilerSwitchOffTime = " + String(boilerSwitchOffTime));
      }
      inString = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial3.begin(115200);
  SPI.begin();
  tempSensors.begin();
  Wire.begin();
  tempSensors.setResolution(ds18b20Boiler, 10);
  tempSensors.setResolution(ds18b20Out, 10);
  tcaSelect(7);
  rtc.begin();
  rtc.isrunning() ? Serial.println(String F("RTC is running!")) : Serial.println(String F("RTC is not running!"));
  for (byte i = 0; i < 2; i++) {
    if (i > 26) {
      Serial.println("Address can't be over 26");
      break;
    }
    float intPart = rtc.readnvram(i * 2);
    float decPart = rtc.readnvram(i * 2 + 1);
    tenConsumption[i] = intPart + decPart / 100;
  }
  for (byte i = 4; i < 6; i++) {
    if (i > 26) {
      Serial.println("Address can't be over 26");
      break;
    }
    if (rtc.readnvram(i)) targetTemperature[i - 4] = rtc.readnvram(i);
  }
  boilerSwitchOnTime = rtc.readnvram(6);
  boilerToggleTime = rtc.readnvram(7);
  boilerSwitchOffTime = rtc.readnvram(8);
  switch(rtc.readnvram(55)) {
    case 1: boilerState = 1;
    break;
    case 3: boilerState = 3;
    break;
    default: boilerState = 2;
  }
  for (byte i = 0; i < 2; i++) {
    tcaSelect(i);
    HTU21.begin();
  }
  HTU21.begin() ? Serial.println(String F("HTU21D sensor is active")) : Serial.println(String F("HTU21D sensor is failed or not connected"));
  pinMode(5, OUTPUT);
  pinMode(28, OUTPUT);
  pinMode(29, OUTPUT);
  pinMode(30, OUTPUT);
  pinMode(31, OUTPUT);
  pinMode(32, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(34, OUTPUT);
  pinMode(35, INPUT);
  pinMode(36, INPUT);
  pinMode(37, INPUT);
  pinMode(38, INPUT);
  pinMode(39, INPUT);
  pinMode(40, INPUT);
  pinMode(41, INPUT);
  pinMode(42, INPUT);
  pinMode(43, INPUT);
  pinMode(44, INPUT);
  pinMode(45, INPUT);
  pinMode(46, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(28, HIGH);
  digitalWrite(29, LOW);
  digitalWrite(30, LOW);
  digitalWrite(31, LOW);
  digitalWrite(32, LOW);
  digitalWrite(33, LOW);
  digitalWrite(34, HIGH);
  digitalWrite(35, HIGH);
  digitalWrite(36, HIGH);
  digitalWrite(37, HIGH);
  digitalWrite(38, HIGH);
  digitalWrite(39, HIGH);
  digitalWrite(40, HIGH);
  digitalWrite(41, HIGH);
  digitalWrite(42, HIGH);
  digitalWrite(43, HIGH);
  digitalWrite(44, HIGH);
  digitalWrite(45, HIGH);
  digitalWrite(46, LOW);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  if (millis() >= previousTime[0] + 1000 * timeout[0]) {
    previousTime[0] = millis();
    for (byte i = 0; i < 4; i++) {
      if (digitalRead(i + 35) == LOW) {
        digitalWrite(34, LOW);
        Serial.println(String F("Flood occurs!"));
      }
    }
  }
  if (millis() >= previousTime[1] + 1000 * timeout[1]) {
    previousTime[1] = millis();
    getTime();
    isAnyValveOpen = false;
    for (byte i = 0; i < 5; i++) {
      isValveOpen[i] = digitalRead(i + 39) == LOW;
      isAnyValveOpen = isAnyValveOpen || isValveOpen[i];
    }
    if (isAnyValveOpen) {
      if (!isDeviceOn[4]) {
        if (!jaguarLag) {
          previousTime[3] = millis();
          jaguarLag = true;
        }
      }
    } else {
      if (isDeviceOn[4]) {
        Serial.println(localTime + ". Jaguar has been switched off!");
        isDeviceOn[4] = false;
      }
    }
    for (byte i = 0; i < 2; i++) {
      isLightOn[i] = digitalRead(i + 44) == LOW;
    }
    if (isLightOn[0]) {
      if (!isDeviceOn[2]) {
        lightOnTime = millis();
        isDeviceOn[2] = true;
        Serial.println(localTime + ". Light on! Fan on");
      }
    } else {
      if (isDeviceOn[2]) {
        if (!lightLag) {
          lightOffTime = millis();
          lightLag = true;
          if (lightOffTime - lightOnTime >= ((7 <= hour && hour < 10) ? 1 : 2) * 60000) {
            previousTime[4] = lightOffTime;
            toiletFanNeedsOff = true;
            Serial.println(localTime + ". Light > 2 min!");
          } else {
            isDeviceOn[2] = false;
            lightLag = false;
            Serial.println(localTime + ". Light off! Fan off");
          }
        }
      }
    }
    for (byte i = 0; i < 5; i++) {
      isDeviceOn[i] ? digitalWrite(29 + i, HIGH) : digitalWrite(29 + i, LOW);
    }
    for (byte i = 0; i < 2; i++) {
      if (isDeviceOn[i] > isDeviceOnPrevious[i]) {
        tenTimeOn[i] = millis();
        isDeviceOnPrevious[i] = isDeviceOn[i];
        Serial.println(String F("tenTimeOn = ") + tenTimeOn[i]);
        tenStateChange[i] = true;
      }
      if (isDeviceOn[i] < isDeviceOnPrevious[i]) {
        tenTimeOff[i] = millis();
        isDeviceOnPrevious[i] = isDeviceOn[i];
        float workTime = (tenTimeOff[i] - tenTimeOn[i]) / 1000.00 / 3600.00;
        tenConsumption[i] +=  workTime * specificPowerConsumption / 1000.00;
        Serial.println(String("New TEN consumption ") + i + " = " + tenConsumption[i]);
        if (i > 26) {
          Serial.println("Address can't be over 26");
          break;
        }
        byte intPart = trunc(tenConsumption[i]);
        byte decPart = tenConsumption[i] * 100 - intPart * 100;
        tcaSelect(7);
        rtc.writenvram(i * 2, intPart);
        rtc.writenvram(i * 2 + 1, decPart);
        tenStateChange[i] = true;
      }
    }
  }
  
  if (millis() >= previousTime[2] + 1000 * timeout[2]) {
    previousTime[2] = millis();
    getTime();
    if (epoch != 0 && hour == 0 && minute == 0) {
      for (byte i = 0; i < 2; i++) {
        rtc.writenvram(i * 2, 0);
        rtc.writenvram(i * 2 + 1, 0);
        tenConsumption[i] = 0;
        tenStateChange[i] = true;
      }
    }
    Serial.print(String F("\n") + localTime + ". ");
    for (byte i = 0; i < 2; i++) {
      tcaSelect(i);
      tempValue = HTU21.readTemperature();
      isTemperatureCorrect[i] = (tempValue != 255.00);
      if (isTemperatureCorrect[i]) temperature[i] = round(tempValue * 10.0) / 10.0;
      tempValue = HTU21.readCompensatedHumidity();
      isHumidityCorrect[i] = (tempValue != 255.00);
      if (isHumidityCorrect[i]) humidity[i] = round(tempValue * 10.0) / 10.0;
    }
    tempSensors.requestTemperatures();
    tempValue = tempSensors.getTempC(ds18b20Boiler);
    isTemperatureCorrect[4] = (tempValue != -127.00);
    if (isTemperatureCorrect[4]) temperature[4] = round(tempValue * 10.0) / 10.0;
    tempValue = tempSensors.getTempC(ds18b20Out);
    isTemperatureCorrect[5] = (tempValue != -127.00);
    if (isTemperatureCorrect[5]) temperature[5] = round(tempValue * 10.0) / 10.0;
    if (humidity[1] > humidity[0] + humidityUpperLimit) {
      isDeviceOn[3] = true;
    }
    if (humidity[1] < humidity[0] + humidityLowLimit) {
      isDeviceOn[3] = false;
    }

    switch(boilerState) {
      case 1:
        isDeviceOn[0] = false;
        isDeviceOn[1] = false;
        digitalWrite(LED_PIN, LOW);
        break;
      
      case 3:
        isDeviceOn[0] = true;
        isDeviceOn[1] = true;
        digitalWrite(LED_PIN, HIGH);
        break;
      
      default:
        if (boilerSwitchOnTime <= hour && hour < boilerToggleTime) {
          Serial.println(String F("It's day. Boiler has been switched on!"));
          coilSelect(targetTemperature[0], hour);
        } else if (boilerToggleTime <= hour && hour < boilerSwitchOffTime) {
          Serial.println(String F("It's evening. Boiler has been switched on for high temperature!"));
          coilSelect(targetTemperature[1], hour);
        }  else {
          Serial.println(String F("It's night. Boiler has been switched off!"));
          isDeviceOn[0] = false;
          isDeviceOn[1] = false;
        }
    }

    dataToESP = "";
    byte j[4] = {0, 1, 4, 5};
    dataToDebag = String F("\tTemperatures: ");
    for (byte i = 0; i < 4; i++) {
      dataToDebag += temperaturePlace[j[i]] + "-" + temperature[j[i]];
      dataToDebag += (isTemperatureCorrect[j[i]] ? "; " : "(not OK); ");
      dataToESP += String (temperature[j[i]]) + ",";
    }
    Serial.println(dataToDebag);
    dataToDebag = String F("\tHumidities: ");
    for (byte i = 0; i < 2; i++) {
      dataToDebag += humidityPlace[i] + "-" + humidity[i];
      dataToDebag += (isHumidityCorrect[i] ? "; " : "(not OK); ");
      dataToESP += String (humidity[i]) + ",";
    }
    Serial.println(dataToDebag);
    dataToDebag = String F("\tDevices: ");
    for (byte i = 0; i < 5; i++) {
      dataToDebag += deviceName[i] + "-";
      dataToDebag += (isDeviceOn[i] ? "ON; " : "OFF; ");
      dataToESP += String (isDeviceOn[i]) + ",";
    }
    Serial.println(dataToDebag);
    dataToDebag = String F("\tThermostats: ");
    for (byte i = 0; i < 5; i++) {
      dataToDebag += valvePlace[i] + "-";
      dataToDebag += (isValveOpen[i] ? "ON; " : "OFF; ");
      dataToESP += String (isValveOpen[i]) + ",";
    }
    dataToESP += String (tenStateChange[0]) + "," + tenStateChange[1] + ",";
    dataToESP += String (tenConsumption[0]) + "," + tenConsumption[1] + ",";
    dataToESP += String (targetTemperature[0]) + "," + targetTemperature[1] + ",";
    dataToESP += String (boilerState) + "," + megaNeedsEpoch + ",";
    dataToESP += String (boilerSwitchOnTime) + "," + boilerToggleTime + "," + boilerSwitchOffTime;
    Serial.println(dataToDebag);
    Serial.println(dataToESP);
    Serial3.print(dataToESP);
    for (byte i = 0; i < 2; i++) {
      if (tenStateChange[i]) tenStateChange[i] = false;
    }
  }

  if (jaguarLag && millis() >= previousTime[3] + 1000 * timeout[3]) {
    if (isAnyValveOpen) {
      isDeviceOn[4] = true;
      Serial.println(localTime + ". Jaguar has been switched on!");
      jaguarLag = false;
    } else {
      jaguarLag = false;
    }
  }

  if (toiletFanNeedsOff && millis() >= previousTime[4] + 1000 * timeout[4]) {
    toiletFanNeedsOff = false;
    lightLag = false;
    isDeviceOn[2] = false;
    getTime();
    Serial.println(localTime + ". Fan off");
  }

  if (millis() <= previousTime[5] + 5 * 1000 * timeout[2]) {
    isWatchDogActive = false;
  } else if (!isWatchDogActive) {
    isWatchDogActive = true;
    previousTime[6] = millis();
    Serial.println(F("ESP didn't respond on time. WatchDog is active"));
  }
  if (isWatchDogActive) {
    if (millis() > previousTime[6] + 5 * 1000 * timeout[2]) {
      previousTime[5] = millis();
      isWatchDogActive = false;
      hardResetESP();
    }
  }
}
