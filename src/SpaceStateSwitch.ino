#include "Constants.h"
#include "Secrets.h"

#include <Arduino.h>
#include <WiFi.h> //  Built in with ESP32
#include <WiFiClient.h> //  Built in with ESP32
#include <Preferences.h> //  Built in with ESP32
#include <ESPmDNS.h>  //  Built in with ESP32
#include <HTTPClient.h> // Built in with ESP32

#include <ESP_DoubleResetDetector.h> 
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h> 
#include <PubSubClient.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "SPIFFS.h"

char mqttServer[40];
char mqttPort[6] = "1883";
char mqttUsername[34];
char mqttPassword[34];

WiFiManager wifiManager;
WiFiManagerParameter paramMQTTServer(PREFERENCE_MQTT_SERVER_KEY, "MQTT Server", mqttServer, 40);
WiFiManagerParameter paramMQTTPort(PREFERENCE_MQTT_PORT_KEY, "MQTT Port", mqttPort, 6);
WiFiManagerParameter paramMQTTUsername(PREFERENCE_MQTT_USERNAME_KEY, "MQTT Auth Username (leave blank if unauthenticated)", mqttUsername, 40, " type='username' autocapitalize='none'");
WiFiManagerParameter paramMQTTPassword(PREFERENCE_MQTT_PASSWORD_KEY, "MQTT Auth Password (leave blank if unauthenticated)", mqttPassword, 40, " type='password'");

Preferences preferences;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

Adafruit_NeoPixel neopixels(NUMBER_OF_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

String hostname;
String mqttStateTopic;
String mqttLastWillTopic;

bool hasDeterminedInitalState = false;
bool currentSwitchState = false; // false == off/closed, true == on/open

unsigned long lastSwitchStateCheck = 0;
unsigned long switchStateCheckInterval = 250;
unsigned long lastMQTTConnectTime = 0;

DoubleResetDetector* drd;

AsyncWebServer webServer(80);

void setup() {
  SPIFFS.begin(true); // Format spiffs if not formatted.

  Serial.begin(115200);

  neopixels.begin();
  updateLEDs();

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_LED_PIN, OUTPUT);

  hostname =             "space-state-switch";
  mqttStateTopic =       "space-state-switch/state";
  mqttLastWillTopic =    "space-state-switch/LWT";

  readPreferences();

  setupWiFiManager();
  
  setupHTTPServer();
}

void setupWiFiManager() {
  
  wifiManager.addParameter(&paramMQTTServer);
  wifiManager.addParameter(&paramMQTTPort);
  wifiManager.addParameter(&paramMQTTUsername);
  wifiManager.addParameter(&paramMQTTPassword);

  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setHostname(hostname);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setConfigPortalTimeoutCallback(configModeTimeoutCallback);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(false);

  if (drd->detectDoubleReset()) {
    drd->stop();
    Serial.print("Double reset detected, starting config portal with password: ");
    Serial.println(CONFIG_AP_PASSWORD);
    wifiManager.startConfigPortal(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
  } else {
    Serial.print("If the switch fails to connect to the known network it with the following password: ");
    Serial.println(CONFIG_AP_PASSWORD);
  } 

  if(wifiManager.autoConnect(CONFIG_AP_SSID, CONFIG_AP_PASSWORD)) {
    Serial.println("Connected...yaay :)");
    Serial.print("IP Address: ");
    Serial.print(WiFi.localIP());
    Serial.print(" MAC Address: ");
    Serial.println(WiFi.macAddress());
    if (!MDNS.begin(hostname.c_str())) {
      Serial.println("Failed to start mDNS");
    }
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "id", WiFi.macAddress().c_str());
    MDNS.addServiceTxt("http", "tcp", "ma", WiFi.macAddress().c_str());
    MDNS.addServiceTxt("http", "tcp", "hw", "esp32");
  }
}

void setupHTTPServer() {
  webServer.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      request->send(200);
    } else {
      request->send(404, "text/html", "<html><head><title>Page Not Found</title><body>Page Not Found</body></html>");
    }
  });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse * response = new AsyncJsonResponse();
    response->addHeader("X-Server","ESP Async Web Server");

    JsonObject root = response->getRoot();
    root["is_open"] = currentSwitchState;
    root["state"] = currentSwitchState;
    root["is_overwritten"] = false;
    root["physical_state"] = currentSwitchState;
    JsonArray array = root.createNestedArray("endpoints");
    array.add("GET /heap");
    array.add("GET /info");
    response->setLength();

    request->send(response);
  });

  webServer.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    response->addHeader("X-Server","ESP Async Web Server");

    JsonObject root = response->getRoot();
    root["uptime"] = millis();
    root["heap"] = ESP.getFreeHeap();
    response->setLength();
    request->send(response);
  });

  webServer.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    response->addHeader("X-Server","ESP Async Web Server");

    JsonObject root = response->getRoot();
    root["chip_id"] = WiFi.macAddress();
    root["ip_address"] = WiFi.localIP();
    root["mac_address"] = WiFi.macAddress();
    root["uptime"] = millis();
    root["change_wifi_instructions"] = "Double press the reset button to enter config mode.";
    if (strlen(mqttServer) > 1) {
      root["mqtt_server"] = mqttServer;  
    } else {
      root["mqtt_server"] = nullptr;
    }
    JsonObject aboutObject = root.createNestedObject("about");
    aboutObject["original_by"] = "meepe";
    aboutObject["modified_by"] = "renssies";
    aboutObject["hardware"] = "Arduino on a ESP32 Wemos D1 Mini Alternative";
    aboutObject["repository"] = "https://github.com/pixelbar/SpaceStateSwitch";
    response->setLength();
    
    request->send(response);
  });

  webServer.begin();
}

void readPreferences() {
  Serial.println("Reading preferences");

  if(!preferences.begin(PREFERENCES_NAMESPACE, true)) {
    Serial.println("Failed to open read-only preferences");
    if(!preferences.begin(PREFERENCES_NAMESPACE, false)) {
      Serial.println("Failed to open write preferences");
    }
  }

  preferences.getString(PREFERENCE_MQTT_SERVER_KEY, mqttServer, 40);
  preferences.getString(PREFERENCE_MQTT_PORT_KEY, mqttPort, 6);
  preferences.getString(PREFERENCE_MQTT_USERNAME_KEY, mqttUsername, 34);
  preferences.getString(PREFERENCE_MQTT_PASSWORD_KEY, mqttPassword, 34);

  Serial.print("MQTT Server: ");
  Serial.println(mqttServer);

  if (atoi(mqttPort) <= 0) {
    strcpy(mqttPort, "1883");
  }

  paramMQTTServer.setValue(mqttServer, 40);
  paramMQTTPort.setValue(mqttPort, 6);
  paramMQTTUsername.setValue(mqttUsername, 34);
  paramMQTTPassword.setValue(mqttPassword, 34);

  preferences.end();

}

void turnOn(){
  Serial.print("[SWITCH] Turning state on\n");
  WiFiClient client;
  HTTPClient http;
  Serial.print("[HTTP] begin...\n");

  String url = "http://spacestate.pixelbar.nl:8080/togglestate.php?user=user&token=token&state=open"; // Replace user and token with real info
  
  if (http.begin(client, url)) {  // HTTP
    Serial.println("[HTTP] Execute GET command...");
    int httpCode = http.GET();    // start connection and send HTTP header
    if (httpCode > 0) {           // httpCode will be negative on error
      Serial.printf("[HTTP] GET... code: %d\n", httpCode); // HTTP header has been send and Server response header has been handled
      
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
      
    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
}

void turnOff(){
  Serial.print("[SWITCH] Turning state off\n");
  WiFiClient client;
  HTTPClient http;
  Serial.print("[HTTP] begin...\n");

  String url = "http://spacestate.pixelbar.nl:8080/togglestate.php?user=user&token=token&state=closed";  // Replace user and token with real info
  
  if (http.begin(client, url)) {  // HTTP
    Serial.println("[HTTP] Execute GET command...");
    int httpCode = http.GET();    // start connection and send HTTP header
    if (httpCode > 0) {           // httpCode will be negative on error
      Serial.printf("[HTTP] GET... code: %d\n", httpCode); // HTTP header has been send and Server response header has been handled
      
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
      
    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
}

void loop() {
  drd->loop();
  mqttClient.loop();
  connectMQTT();

  checkSwitchState();
}

void checkSwitchState() {
  unsigned long now = millis();
  if ((now - lastSwitchStateCheck) <= switchStateCheckInterval) {
    return;
  }
  bool newSwitchState = digitalRead(SWITCH_PIN) == LOW;
  lastSwitchStateCheck = now;
  if (!hasDeterminedInitalState) {
    currentSwitchState = newSwitchState;
    hasDeterminedInitalState = true;
    Serial.print("Determined original state: ");
    if (newSwitchState) {
      Serial.println("Open!");
    } else {
      Serial.println("Closed!");
    }
    updateLEDs();
    publishStateTopic(newSwitchState);
    return;
  }
  if (currentSwitchState == newSwitchState) {
    return;
  }
  currentSwitchState = newSwitchState;

  Serial.print("State has changed, new: ");
  if (newSwitchState) {
    Serial.println("Open!");
  } else {
    Serial.println("Closed!");
  }
  updateLEDs();
  publishStateTopic(newSwitchState);
  if (newSwitchState) {
    turnOn();
  } else {
    turnOff();
  }
}

void updateLEDs() {
  int ledState = currentSwitchState ? HIGH : LOW;
  digitalWrite(BUILTIN_LED_PIN, ledState);

  uint32_t neopixelColor = neopixels.Color(255, 0, 0); // Red

  if (currentSwitchState) {
    neopixelColor = neopixels.Color(0, 255, 0); // Green
  }

  neopixels.fill(neopixelColor);
  neopixels.show();
}

//
// MARK: - MQTT
//

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (mqttClient.connected()) {
    return;
  }
  String mqttServerStr = String(mqttServer);
  mqttServerStr.trim();
  if (mqttServerStr.isEmpty()) {
    return;
  }
  long now = millis();
  if ((now - lastMQTTConnectTime) < MQTT_CONNECT_INTERVAL && lastMQTTConnectTime != 0) {
    return;
  }
  int mqttPortInt = atoi(mqttPort);
  if (mqttPortInt <= 0) {
    mqttPortInt = 1883;
  }
  mqttClient.setServer(mqttServerStr.c_str(), mqttPortInt);
  if (mqttClient.connect(hostname.c_str(), mqttUsername, mqttPassword, mqttLastWillTopic.c_str(), 0, true, "offline")) {
    Serial.println("[MQTT]: Connected");
    publishHADiscoveryTopic();
    publishStateTopic(currentSwitchState);
    mqttClient.publish(mqttLastWillTopic.c_str(), "online", true);
  } else {
    Serial.println("[MQTT]: Failed to connect");
  }
  lastMQTTConnectTime = now;
  
}

void publishHADiscoveryTopic() {
  String topic = "homeassistant/binary_sensor/" + hostname + "/config";

  DynamicJsonDocument payload(512);
  payload["dev_cla"] = "opening";
  payload["stat_t"] = mqttStateTopic;
  payload["pl_on"] = "1";
  payload["pl_off"] = "0";
  payload["uniq_id"] = hostname;
  payload["name"] = "Space state";
  payload["avty_t"] = mqttLastWillTopic;
  payload["exp_aft"] = 900; // Seconds after which the sensor is deemed unresponsive.

  JsonObject deviceObject = payload.createNestedObject("dev");
  deviceObject["name"] = "Space state switch";
  deviceObject["cu"] = String("http://") + WiFi.localIP().toString() + "/";
  deviceObject["mdl"] = "ESP32";
  deviceObject["mf"] = "renssies";

  JsonArray ids = deviceObject.createNestedArray("ids");
  ids.add(WiFi.macAddress());

  char payloadBuffer[512];
  serializeJson(payload, payloadBuffer);

  mqttClient.setBufferSize(512); // The discovery payload is rather big so we temporarily increase the max size.
  if(!mqttClient.publish(topic.c_str(), payloadBuffer, true)) {
    Serial.println("[MQTT]: Failed to publish HA discovery");
  } else {
    Serial.println("[MQTT]: Published HA discovery");
  }
  mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
}

void publishStateTopic(bool state) {
  if (!mqttClient.connected()) {
    return;
  }
  if (!mqttClient.publish(mqttStateTopic.c_str(), String(state).c_str())) {
    Serial.println("[MQTT]: Failed to publish state");
  }
}

// 
// MARK: - Callbacks
// 

void saveConfigCallback() {
  Serial.println("Should save config");

  strcpy(mqttServer, paramMQTTServer.getValue());
  strcpy(mqttPort, paramMQTTPort.getValue());
  strcpy(mqttUsername, paramMQTTUsername.getValue());
  strcpy(mqttPassword, paramMQTTPassword.getValue());

  preferences.begin(PREFERENCES_NAMESPACE, false);
  
  preferences.putString(PREFERENCE_MQTT_SERVER_KEY, mqttServer);
  preferences.putString(PREFERENCE_MQTT_PORT_KEY, mqttPort);
  preferences.putString(PREFERENCE_MQTT_USERNAME_KEY, mqttUsername);
  preferences.putString(PREFERENCE_MQTT_PASSWORD_KEY, mqttPassword);

  preferences.end();

  mqttClient.disconnect();
  MDNS.end();

  wifiManager.reboot();
}

void configModeTimeoutCallback() {
  wifiManager.reboot();
}
