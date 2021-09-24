#include <GxEPD2_32_BW.h>
#include "HelveticaNeue_CondensedBlack72pt7b.h"
#include "HelveticaNeue_CondensedBlack19pt7b.h"
#include "DollieScript_PersonalUse28pt7b.h"
#include <HX711_ADC.h>
#include <WiFi.h>
#include <AsyncMqttClient.h> // https://github.com/marvinroger/async-mqtt-client

// ---- Config ---
#define MQTT_HOST      IPAddress(10, 10, 1, 100)
#define MQTT_PORT      1883
#define MQTT_CLIENTID  "scales"
#define WIFI_SSID      "ssid"
#define WIFI_PASSWORD  "pass"
#define MQTT_USER      "user"
#define MQTT_PASS      "pass"
#define TouchThreshold 36
#define HX711DATA 22
#define HX711CLOCK 21
#define Person1MQTT "devices/scales/person1/weight"
#define Person2MQTT "devices/scales/person2/weight"
#define Person1Min 50000
#define Person1Max 60000
#define Person2Min 75000
#define Person2Max 85000
const float calibrationValue = -20.0;
// ---- end Config ---


// --- Init ---
HX711_ADC LoadCell(HX711DATA, HX711CLOCK);
WiFiClient wifiClient;
AsyncMqttClient mqttClient;
GxEPD2_32_BW display(GxEPD2::GDEH029A1, 5, 17, 16, 4); // MOSI 23, SCK 18

float grams = 0.0;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;
// --- end init ---


// --- Setup ---
void setup() {
  btStop();
  touchAttachInterrupt(T2, touchCallback, TouchThreshold);
  touchAttachInterrupt(T7, touchCallback, TouchThreshold);
  esp_sleep_enable_touchpad_wakeup();
  if (touchRead(T2) < TouchThreshold || touchRead(T7) < TouchThreshold) {
    // Try to prevent unit from waking up while you're still standing on touch pads
    delay(3000);
    esp_deep_sleep_start();
  }
  
  Serial.begin(115200);
  display.init();
  display.setRotation(3);
  display.setTextColor(GxEPD_BLACK);
  
  LoadCell.begin();
  LoadCell.powerUp();
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
  WiFi.onEvent(WiFiEvent);
  WiFi.setHostname("scales");
  mqttClient.onConnect(onMqttConnect);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setClientId(MQTT_CLIENTID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
  mqttClient.onPublish(onMqttPublish);

  connectToWifi();
  scales();
}
// --- end Setup ---


void loop() {
  //  touchpadDiag();
  //  should not be here, try to sleep
  esp_deep_sleep_start();
  delay (5000);
}


// --- Program ---
void scales() {
  calibrate();
  startMessage();
  measure();
  if (grams > 500) { // only try to publish the data if there's a reasonable amount of weight detected
    postResults();
  } else {
    goToSleep();
  }
}


void calibrate() {
  Serial.println("[info      ] Startup");
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&HelveticaNeue_CondensedBlack19pt7b);
    display.setCursor(50, 100);
    display.println("Calibrating...");
  }
  while (display.nextPage());
  display.setPartialWindow(0, 0, display.width(), display.height()); // activate partial screen updates
  
  Serial.println("[info      ] Calibrating");
  LoadCell.start(1000, true);
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println("[error     ] Timeout, error connecting to HX711");
    goToSleep();
  } else {
    LoadCell.setCalFactor(calibrationValue);
    Serial.println("[info      ] Startup is complete");
  }  
}


void startMessage() {
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&HelveticaNeue_CondensedBlack19pt7b);
    display.setCursor(40, 30);  display.println("Place feet on");
    display.setCursor(30, 75);  display.println("scale and wait");
    display.setCursor(30, 120); display.println("a few seconds.");
  }
  while (display.nextPage());
}


void measure() {
  unsigned long t1 = millis();
  unsigned long t2 = millis();
  static float history[4];
  static float liveWeight;
  static bool stable = false;

  do {
    if (millis() - t2 > 70) {
      t2 = millis();
      LoadCell.update();
      liveWeight = abs(LoadCell.getData());
      history[3] = history[2];
      history[2] = history[1];
      history[1] = history[0];
      history[0] = liveWeight;
      grams = (history[0] + history[1] + history[2] + history[3]) / 4;
      if ((abs(grams - liveWeight) < 1) && (grams > 10)) {
        stable = true;
      }
    }

    if (millis() - t1 > 5000 && grams < 50) {
      Serial.println(millis());
      Serial.println(t1);
      // no weighing activity for 5 seconds, turn off.
      Serial.println ("[info      ] No activity, going to sleep");
      goToSleep();
    }
  } while (stable == false);
  
  Serial.printf ("[info      ] Measured weight %.1f g \n", grams);
}


void showMeasurement() {
  char weightString[5];
  String units;

  if (grams < 70000) {
    dtostrf(grams / 1000, 3, 1, weightString);
    units = "kg";
  } else {
    dtostrf(grams * 0.00220462262185, 3, 0, weightString);
    units = "lb";
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&HelveticaNeue_CondensedBlack72pt7b);
    display.setCursor(0, 120);
    display.printf(weightString);
    display.setFont(&HelveticaNeue_CondensedBlack19pt7b);
    display.setCursor(255, 30);
    display.println(units);
  }
  while (display.nextPage());
}


void postResults() {
  showMeasurement();
  char weightString[5];
  String units;

  if (grams > Person1Min  && grams < Person1Max) {  // post in kilo's
    dtostrf(grams / 1000, 3, 1, weightString);
    mqttClient.publish(Person1MQTT, 1, true, weightString);
  }
  if (grams > Person2Min  && grams < Person2Max) { // post in pounds
    dtostrf(grams * 0.00220462262185, 3, 0, weightString);
    mqttClient.publish(Person2MQTT, 1, true, weightString);
  }

  Serial.print ("[info      ] publish data to MQTT (if applicable)");
}


void goToSleep() {
  if (grams < 500) {
    // if no real weight was measured, show a "logo" instead of the measured value
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setFont(&DollieScript_PersonalUse28pt7b);
      display.setCursor(10, 60);
      display.println("Bathroom Scales");
      display.setFont(&HelveticaNeue_CondensedBlack19pt7b);
      display.setCursor(225, 120);
      display.println("v1.0");
    }
    while (display.nextPage());
  }
  
  LoadCell.powerDown();
  delay (50);
  esp_deep_sleep_start();
}
// --- end Program ---


// --- Wifi ---
void connectToWifi() {
  Serial.println("[WIFI      ] Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.print("[WIFI      ] WiFi connected:  ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("[WIFI      ] WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}
// --- end Wifi ---


// --- MQTT ---
void connectToMqtt() {
  Serial.println("[MQTT      ] Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("[MQTT      ] Connected to MQTT.");
}

void onMqttPublish(uint16_t packetId) {
  Serial.print("[MQTT      ] Publish acknowledged.     ");
  goToSleep();
}
// --- end MQTT ---




void touchpadDiag() {
  Serial.print("TL: " + String(touchRead(T9)) + "   ");
  Serial.print("TR: " + String(touchRead(T8)) + "   ");
  Serial.print("BL: " + String(touchRead(T7)) + "   ");
  Serial.println("BR: " + String(touchRead(T2)));
  delay(100);
}
void touchCallback() {}
