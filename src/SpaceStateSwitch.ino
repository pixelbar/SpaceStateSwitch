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

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

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

bool currentSpaceState = false;  // false == off/closed, true == on/open
bool hasDeterminedInitalState = false;
bool currentSwitchState = false; // false == off/closed, true == on/open
bool performingRequest = false;
bool shouldSubmitCurrentSwitchState = false;

unsigned long lastSwitchStateCheck = 0;
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

  xTaskCreate(checkSpaceStateTask, "CheckSpaceState", 6000, NULL, 5, NULL);
  xTaskCreate(submitSpaceStateTask, "SubmitSpaceState", 6000, NULL, 5, NULL);
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
    updateLEDs();
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
    root["is_open"] = currentSpaceState;
    root["state"] = stringForState(currentSpaceState);
    root["is_overwritten"] = currentSpaceState != currentSwitchState;
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

void loop() {
  drd->loop();
  mqttClient.loop();
  connectMQTT();

  checkSwitchState();
}

void checkSwitchState() {
  unsigned long now = millis();
  if ((now - lastSwitchStateCheck) <= SWITCH_STATE_INTERVAL) {
    return;
  }
  bool newSwitchState = digitalRead(SWITCH_PIN) == LOW;
  lastSwitchStateCheck = now;
  if (!hasDeterminedInitalState) {
    currentSpaceState = newSwitchState;
    currentSwitchState = newSwitchState;
    hasDeterminedInitalState = true;
    
    Serial.print("Determined original state: ");
    Serial.println(stringForState(newSwitchState));
    spaceStateDidChange(newSwitchState, !newSwitchState);
    return;
  }
  if (currentSwitchState == newSwitchState) {
    return;
  }
  currentSwitchState = newSwitchState;
  currentSpaceState = newSwitchState;

  spaceStateDidChange(newSwitchState, !newSwitchState);
  shouldSubmitCurrentSwitchState = true;
}

void submitSpaceStateTask(void *param) {
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setCACert(digicert_global_root_g2_ca_pem);
  while(true) {
    vTaskDelay(portTICK_PERIOD_MS * 200);
    if (!shouldSubmitCurrentSwitchState) {
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      continue;
    }
    if (!client) {
      Serial.println("Failed to create secure client");
      client->stop();
      continue;
    }

    String url = "https://spacestate.pixelbar.nl/togglestate.php";
    url += "?user=";
    url += SPACESTATE_REQUEST_USERNAME;
    url += "&token=";
    url += SPACESTATE_REQUEST_TOKEN;
    url += "&state=";
    url += stringForState(currentSwitchState);

    HTTPClient httpsClient;
    httpsClient.useHTTP10(true);
    if (!httpsClient.begin(*client, url)) {  // HTTPS
      Serial.println("Malformed request");
      httpsClient.end();
      client->stop();
      continue;
    }
    Serial.print("GETting URL: ");
    Serial.println(url);
    performingRequest = true;
    updateLEDs();
    int httpCode = httpsClient.GET();
    if (httpCode <= 0) {
      httpsClient.end();
      client->stop();
      Serial.printf("Failed to perform submit GET request, error: %s\n", httpsClient.errorToString(httpCode).c_str());
      performingRequest = false;
      continue;
    }
    Serial.printf("Submit space state response code: %d\n", httpCode);
    if (httpCode >= 200 && httpCode < 400) {
      shouldSubmitCurrentSwitchState = false;
    }
    performingRequest = false;
    httpsClient.end();
    client->stop();
    
  }
}

void checkSpaceStateTask(void *param) {
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setCACert(digicert_global_root_g2_ca_pem);
  bool hasPerformedFirstCheck = false;
  while(true) {
    if (hasPerformedFirstCheck) {
      vTaskDelay(portTICK_PERIOD_MS * SPACE_STATE_CHECK_INTERVAL);
    }
    if (performingRequest) {
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      continue;
    }
    if (!client) {
      Serial.println("Failed to create secure client");
      client->stop();
      continue;
    }
    hasPerformedFirstCheck = true;

    const char url[46] = "https://spacestate.pixelbar.nl/spacestate.php";

    HTTPClient httpsClient;
    httpsClient.useHTTP10(true);
    if (!httpsClient.begin(*client, url)) {  // HTTPS
      Serial.println("Malformed request");
      httpsClient.end();
      client->stop();
      continue;
    }
    Serial.print("GETting URL: ");
    Serial.println(url);
    updateLEDs();
    int httpCode = httpsClient.GET();
    if (httpCode <= 0) {
      httpsClient.end();
      client->stop();
      Serial.printf("Failed to perform GET request, error: %s\n", httpsClient.errorToString(httpCode).c_str());
      continue;
    }
    Serial.printf("Response code: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
      DynamicJsonDocument doc(128);
      DeserializationError error = deserializeJson(doc, httpsClient.getStream());
      if (error.code() == error.Ok && doc.containsKey("state") && doc["state"].is<String>()) {
        String stateString = doc["state"].as<String>();
        bool onlineSpaceState = stateString.equals("open");
        if (onlineSpaceState != currentSpaceState) {
          Serial.println("An space state overwrite was detected");
          currentSpaceState = onlineSpaceState;
          spaceStateDidChange(onlineSpaceState, !onlineSpaceState);
        }
      } else {
        Serial.print("Unsupported response from space state");
        Serial.println(httpsClient.getString());
      }
      doc.clear();
    }
    httpsClient.end();
    client->stop();
  }
}

void spaceStateDidChange(bool newState, bool oldState) {
  currentSpaceState = newState;
  Serial.print("Space state has changed to: '");
  Serial.print(stringForState(newState));
  Serial.print("', previous: '");
  Serial.print(stringForState(oldState));
  Serial.println("'");
  updateLEDs();
  publishStateTopic(newState);
}

String stringForState(bool state) {
  if (state) {
    return "open";
  } else {
    return "closed";
  }
}

void updateLEDs() {
  int ledState = currentSpaceState ? HIGH : LOW;
  digitalWrite(BUILTIN_LED_PIN, ledState);

  uint32_t neopixelColor = neopixels.Color(255, 0, 0); // Red

  if (currentSpaceState) {
    neopixelColor = neopixels.Color(0, 255, 0); // Green
  }
  if (WiFi.status() != WL_CONNECTED) {
    neopixelColor = neopixels.Color(0, 0, 255); // Blue
  }

  neopixels.fill(neopixelColor);
  neopixels.setBrightness(80);
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
    mqttClient.publish(mqttLastWillTopic.c_str(), "online", true);
    publishHADiscoveryTopic();
    publishStateTopic(currentSpaceState);
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
