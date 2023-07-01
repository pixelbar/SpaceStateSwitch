#include <Arduino.h>
#include <WiFi.h> //  Built in with ESP32
#include <WiFiClient.h> //  Built in with ESP32
#include <Preferences.h> //  Built in with ESP32
#include <ESPmDNS.h>  //  Built in with ESP32
#include <HTTPClient.h> // Built in with ESP32

#include <ESP_DoubleResetDetector.h> // From Library Manager, version 1.3.2
#include <Adafruit_NeoPixel.h> // From Library Manager, version 1.11.0
#include <WiFiManager.h> // From Library Manager, version 2.0.15-rc.1
#include <PubSubClient.h> // https://pubsubclient.knolleary.net, version 2.8.0

#include <AsyncTCP.h> // Included with ESPAsyncWebserver
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer/tree/f71e3d427b5be9791a8a2c93cf8079792c3a9a26
#include <AsyncJson.h> // Included with ArduinoJson
#include <ArduinoJson.h> // From Library Manager, version 6.21.2

#include "Constants.h"

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

DoubleResetDetector* drd;



void setup() {
  Serial.begin(115200);

  neopixels.begin();
  updateLEDs();

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_LED_PIN, OUTPUT);

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  hostname =             "space-state-switch";
  mqttStateTopic =       "space-state-switch/state";
  mqttLastWillTopic =    "space-state-switch/LWT";

  readPreferences();

  setupWiFiManager();
}

void setupWiFiManager() {
  wifiManager.addParameter(&paramMQTTServer);
  wifiManager.addParameter(&paramMQTTPort);
  wifiManager.addParameter(&paramMQTTUsername);
  wifiManager.addParameter(&paramMQTTPassword);

  wifiManager.setConfigPortalBlocking(true); // Disable blocking so we can animate the display
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setHostname(hostname);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.setDebugOutput(true);

  if (drd->detectDoubleReset()) {
    Serial.println("Double reset detected");
    wifiManager.setConfigPortalTimeout(30);
    wifiManager.setConfigPortalTimeoutCallback(configModeTimeoutCallback);
    wifiManager.startConfigPortal(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
    Serial.print("Config portal started, network password: ");
    Serial.println(CONFIG_AP_PASSWORD);
  } else if(wifiManager.autoConnect(CONFIG_AP_SSID, CONFIG_AP_PASSWORD)) {
    Serial.println("Connected...yaay :)");
    Serial.print("IP Address: ");
    Serial.print(WiFi.localIP());
    Serial.print(" MAC Address: ");
    Serial.println(WiFi.macAddress());
    if (!MDNS.begin(hostname)) {
      Serial.println("Failed to start mDNS");
    }
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "id", WiFi.macAddress().c_str());
    MDNS.addServiceTxt("http", "tcp", "ma", WiFi.macAddress().c_str());
    MDNS.addServiceTxt("http", "tcp", "hw", "esp32");
  } else {
    Serial.print("Config portal started automatically, network password: ");
    Serial.println(CONFIG_AP_PASSWORD);
  }
}

void readPreferences() {
  Serial.println("Reading preferenes");

  preferences.begin(PREFERENCES_NAMESPACE, true);

  preferences.getString(PREFERENCE_MQTT_SERVER_KEY, mqttServer, 40);
  preferences.getString(PREFERENCE_MQTT_PORT_KEY, mqttPort, 6);
  preferences.getString(PREFERENCE_MQTT_USERNAME_KEY, mqttUsername, 34);
  preferences.getString(PREFERENCE_MQTT_PASSWORD_KEY, mqttPassword, 34);

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
    return;
  }
  if (currentSwitchState == newSwitchState) {
    return;
  }
  currentSwitchState = newSwitchState;

  Serial.println("State has changed, new: ");
  if (newSwitchState) {
    Serial.println("Open!");
  } else {
    Serial.println("Closed!");
  }
  updateLEDs();
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
