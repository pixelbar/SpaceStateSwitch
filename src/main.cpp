// SpaceStateSwitch: reads the physical space state switch at Pixelbar and publishes
// it to spacestate.pixelbar.nl, MQTT (Home Assistant) and a small local JSON API.

#include <Arduino.h>
#include <WiFi.h>
// WiFiManager pulls in the synchronous WebServer; it must come before
// ESPAsyncWebServer so the latter can reconcile the HTTP method enums.
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

#include <atomic>

#include "Config.h"
#include "DoubleResetDetector.h"
#include "MqttPublisher.h"
#include "OtaUpdater.h"
#include "Settings.h"
#include "SpaceStateSync.h"

// setup() runs the OTA update (TLS + signature verification); give it some headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

Settings settings;
DoubleResetDetector doubleReset(config::kDoubleResetWindowMs);
WiFiManager wifiManager;
MqttPublisher mqtt;
SpaceStateSync spaceStateSync;
AsyncWebServer webServer(80);
Adafruit_NeoPixel neopixels(config::kNumberOfPixels, config::kNeopixelPin, NEO_GRB + NEO_KHZ800);

// Shared between the main loop, the sync task, the MQTT task and the web server.
std::atomic<bool> spaceOpen{false};   // The effective state (switch, or online override)
std::atomic<bool> switchOpen{false};  // The physical switch position
std::atomic<bool> ledsDirty{true};
bool updating = false;

uint32_t lastSwitchPollAt = 0;
uint32_t lastMqttPublishAt = 0;

const char *stateName(bool open) {
  return open ? "open" : "closed";
}

// MARK: - LEDs

// Only call from the main task; other tasks set ledsDirty instead.
void renderLeds() {
  ledsDirty = false;
  const bool open = spaceOpen;
  digitalWrite(config::kBuiltinLedPin, open ? HIGH : LOW);

  uint32_t color;
  if (updating) {
    color = Adafruit_NeoPixel::Color(128, 0, 255);  // Purple: installing an update
  } else if (!WiFi.isConnected()) {
    color = Adafruit_NeoPixel::Color(0, 0, 255);  // Blue: no WiFi
  } else if (open) {
    color = Adafruit_NeoPixel::Color(0, 255, 0);  // Green: open
  } else {
    color = Adafruit_NeoPixel::Color(255, 0, 0);  // Red: closed
  }
  neopixels.fill(color);
  neopixels.show();
}

// MARK: - Space state

bool readSwitch() {
  return digitalRead(config::kSwitchPin) == LOW;
}

void setSpaceState(bool open, const char *reason) {
  const bool previous = spaceOpen.exchange(open);
  if (previous == open) {
    return;
  }
  Serial.printf("Space state changed to '%s' (%s)\n", stateName(open), reason);
  ledsDirty = true;
  mqtt.publishState(open);
}

void pollSwitch() {
  if (millis() - lastSwitchPollAt < config::kSwitchPollIntervalMs) {
    return;
  }
  lastSwitchPollAt = millis();

  const bool open = readSwitch();
  if (open == switchOpen) {
    return;
  }
  switchOpen = open;
  setSpaceState(open, "switch");
  spaceStateSync.submit(open);
}

// Called from the sync task when the website reports a different state.
void onOnlineState(bool open) {
  if (open != spaceOpen) {
    setSpaceState(open, "overridden online");
  }
}

// MARK: - WiFi

void setupWiFi(bool forcePortal) {
  // WiFiManagerParameter copies the default value. Secrets aren't pre-filled, so
  // they can't be read back from the portal; leaving them empty keeps the stored value.
  static WiFiManagerParameter mqttServer("mqtt_server", "MQTT server (leave blank to disable)", settings.mqttServer.c_str(), 64);
  static WiFiManagerParameter mqttPort("mqtt_port", "MQTT port", String(settings.mqttPort).c_str(), 5, " type='number' min='1' max='65535'");
  static WiFiManagerParameter mqttUsername("mqtt_username", "MQTT username (blank if unauthenticated)", settings.mqttUsername.c_str(), 64, " autocapitalize='none'");
  static WiFiManagerParameter mqttPassword("mqtt_password", "MQTT password (blank keeps current)", "", 64, " type='password'");
  static WiFiManagerParameter spaceStateUsername("ss_username", "Space state API user", settings.spaceStateUsername.c_str(), 64, " autocapitalize='none'");
  static WiFiManagerParameter spaceStateToken("ss_token", "Space state API token (blank keeps current)", "", 128, " type='password'");

  wifiManager.addParameter(&mqttServer);
  wifiManager.addParameter(&mqttPort);
  wifiManager.addParameter(&mqttUsername);
  wifiManager.addParameter(&mqttPassword);
  wifiManager.addParameter(&spaceStateUsername);
  wifiManager.addParameter(&spaceStateToken);

  wifiManager.setSaveParamsCallback([]() {
    settings.mqttServer = mqttServer.getValue();
    settings.mqttServer.trim();
    const long port = String(mqttPort.getValue()).toInt();
    settings.mqttPort = (port > 0 && port <= 65535) ? port : config::kMqttDefaultPort;
    settings.mqttUsername = mqttUsername.getValue();
    if (strlen(mqttPassword.getValue()) > 0 || settings.mqttUsername.isEmpty()) {
      settings.mqttPassword = mqttPassword.getValue();
    }
    settings.spaceStateUsername = spaceStateUsername.getValue();
    if (strlen(spaceStateToken.getValue()) > 0) {
      settings.spaceStateToken = spaceStateToken.getValue();
    }
    settings.save();
    Serial.println("Settings saved");
  });
  // Restart after saving, so everything starts with the new configuration.
  wifiManager.setSaveConfigCallback([]() { wifiManager.reboot(); });
  wifiManager.setConfigPortalTimeoutCallback([]() { wifiManager.reboot(); });
  wifiManager.setAPCallback([](WiFiManager *) {
    doubleReset.stop();  // A reset while in the portal shouldn't count as a double reset.
    Serial.printf("Config portal started: connect to '%s' with password '%s'\n", config::kConfigApSsid, config::kConfigApPassword);
  });

  wifiManager.setHostname(config::kHostname);
  wifiManager.setConfigPortalTimeout(config::kConfigPortalTimeoutS);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setDebugOutput(false);

  if (forcePortal) {
    Serial.println("Double reset detected, starting config portal");
    wifiManager.startConfigPortal(config::kConfigApSsid, config::kConfigApPassword);
  }

  // Blocks until connected; opens the portal (and eventually reboots) if that fails.
  if (!wifiManager.autoConnect(config::kConfigApSsid, config::kConfigApPassword)) {
    ESP.restart();
  }
  Serial.printf("Connected: IP %s, MAC %s\n", WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
}

void setupMdns() {
  if (!MDNS.begin(config::kHostname)) {
    Serial.println("Failed to start mDNS");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "id", WiFi.macAddress().c_str());
  MDNS.addServiceTxt("http", "tcp", "ma", WiFi.macAddress().c_str());
  MDNS.addServiceTxt("http", "tcp", "hw", "esp32");
  MDNS.addServiceTxt("http", "tcp", "fw", FIRMWARE_VERSION);
}

// MARK: - Web server

void sendJson(AsyncWebServerRequest *request, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void setupWebServer() {
  webServer.onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == AsyncWebRequestMethod::HTTP_OPTIONS) {
      request->send(200);
    } else {
      request->send(404, "text/html", "<html><head><title>Page Not Found</title><body>Page Not Found</body></html>");
    }
  });

  webServer.on("/", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const bool open = spaceOpen;
    const bool physical = switchOpen;
    doc["is_open"] = open;
    doc["state"] = stateName(open);
    doc["is_overwritten"] = open != physical;
    doc["physical_state"] = physical;
    JsonArray endpoints = doc["endpoints"].to<JsonArray>();
    endpoints.add("GET /heap");
    endpoints.add("GET /info");
    sendJson(request, doc);
  });

  webServer.on("/heap", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["uptime"] = millis();
    doc["heap"] = ESP.getFreeHeap();
    doc["min_heap"] = ESP.getMinFreeHeap();
    sendJson(request, doc);
  });

  webServer.on("/info", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["chip_id"] = WiFi.macAddress();
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["uptime"] = millis();
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["change_wifi_instructions"] = "Double press the reset button to enter config mode.";
    if (settings.hasMqtt()) {
      doc["mqtt_server"] = settings.mqttServer;
      doc["mqtt_connected"] = mqtt.isConnected();
    } else {
      doc["mqtt_server"] = nullptr;
    }
    JsonObject about = doc["about"].to<JsonObject>();
    about["original_by"] = "meepe";
    about["modified_by"] = "renssies";
    about["hardware"] = "Arduino on a ESP32 Wemos D1 Mini Alternative";
    about["repository"] = config::kRepositoryUrl;
    sendJson(request, doc);
  });

  webServer.begin();
}

// MARK: - OTA

void checkForUpdate() {
  updating = true;
  ota::checkAndUpdate([](size_t written, size_t total) {
    static int lastPercent = -1;
    const int percent = written * 100 / total;
    if (percent / 10 != lastPercent / 10) {
      Serial.printf("[OTA] %d%%\n", percent);
      // Blink while downloading.
      neopixels.setBrightness(percent / 10 % 2 ? config::kNeopixelBrightness / 4 : config::kNeopixelBrightness);
      renderLeds();
    }
    lastPercent = percent;
  });
  // Only returns if no update was installed.
  updating = false;
  neopixels.setBrightness(config::kNeopixelBrightness);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\nSpaceStateSwitch %s\n", FIRMWARE_VERSION);

  pinMode(config::kSwitchPin, INPUT_PULLUP);
  pinMode(config::kBuiltinLedPin, OUTPUT);
  neopixels.begin();
  neopixels.setBrightness(config::kNeopixelBrightness);

  // Show the switch position straight away, even before WiFi is up.
  switchOpen = readSwitch();
  spaceOpen = switchOpen.load();
  Serial.printf("Initial switch state: %s\n", stateName(switchOpen));
  renderLeds();

  settings.load();
  setupWiFi(doubleReset.detect());
  renderLeds();

  checkForUpdate();

  // We got this far (connected to WiFi), so a freshly installed update works.
  ota::markFirmwareValid();

  setupMdns();
  setupWebServer();
  mqtt.begin(settings, []() -> bool { return spaceOpen; });
  spaceStateSync.begin(settings.spaceStateUsername, settings.spaceStateToken, onOnlineState);
}

void loop() {
  doubleReset.loop();
  pollSwitch();

  static bool wasConnected = true;
  if (WiFi.isConnected() != wasConnected) {
    wasConnected = !wasConnected;
    ledsDirty = true;
  }
  if (ledsDirty) {
    renderLeds();
  }

  // Re-publish periodically so Home Assistant's expire_after doesn't kick in.
  if (millis() - lastMqttPublishAt > config::kMqttRepublishIntervalMs) {
    lastMqttPublishAt = millis();
    mqtt.publishState(spaceOpen);
  }

  delay(10);
}
