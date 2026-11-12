#include "MqttPublisher.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <mqtt_client.h>

#include "Config.h"
#include "Settings.h"

namespace {
constexpr const char *kPayloadOnline = "online";
constexpr const char *kPayloadOffline = "offline";
constexpr const char *kPayloadOpen = "1";
constexpr const char *kPayloadClosed = "0";
}  // namespace

void MqttPublisher::begin(const Settings &settings, StateProvider currentState) {
  if (!settings.hasMqtt()) {
    Serial.println("[MQTT] No server configured");
    return;
  }
  currentState_ = currentState;

  // The client copies all strings, so temporaries are fine here.
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.hostname = settings.mqttServer.c_str();
  cfg.broker.address.port = settings.mqttPort;
  cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  cfg.credentials.client_id = config::kHostname;
  if (!settings.mqttUsername.isEmpty()) {
    cfg.credentials.username = settings.mqttUsername.c_str();
    cfg.credentials.authentication.password = settings.mqttPassword.c_str();
  }
  cfg.session.last_will.topic = config::kMqttAvailabilityTopic;
  cfg.session.last_will.msg = kPayloadOffline;
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = config::kMqttReconnectIntervalMs;

  client_ = esp_mqtt_client_init(&cfg);
  if (client_ == nullptr) {
    Serial.println("[MQTT] Failed to create client");
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttPublisher::onEvent, this);
  if (esp_mqtt_client_start(client_) != ESP_OK) {
    Serial.println("[MQTT] Failed to start client");
  }
}

void MqttPublisher::publishState(bool open) {
  if (!connected_) {
    return;
  }
  // Non-blocking: queued in the client's outbox and sent by the MQTT task.
  esp_mqtt_client_enqueue(client_, config::kMqttStateTopic, open ? kPayloadOpen : kPayloadClosed, 0, 0, 1, true);
}

void MqttPublisher::onEvent(void *arg, const char *base, int32_t eventId, void *eventData) {
  auto *self = static_cast<MqttPublisher *>(arg);
  switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED:
      Serial.println("[MQTT] Connected");
      self->connected_ = true;
      esp_mqtt_client_publish(self->client_, config::kMqttAvailabilityTopic, kPayloadOnline, 0, 1, 1);
      self->publishDiscovery();
      if (self->currentState_) {
        self->publishState(self->currentState_());
      }
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (self->connected_) {
        Serial.println("[MQTT] Disconnected");
      }
      self->connected_ = false;
      break;
    case MQTT_EVENT_ERROR:
      Serial.println("[MQTT] Connection error");
      break;
    default:
      break;
  }
}

void MqttPublisher::publishDiscovery() {
  // Home Assistant MQTT discovery, using abbreviated keys:
  // https://www.home-assistant.io/integrations/mqtt/#discovery-messages
  JsonDocument payload;
  payload["dev_cla"] = "opening";
  payload["stat_t"] = config::kMqttStateTopic;
  payload["pl_on"] = kPayloadOpen;
  payload["pl_off"] = kPayloadClosed;
  payload["uniq_id"] = config::kHostname;
  payload["name"] = "Space state";
  payload["avty_t"] = config::kMqttAvailabilityTopic;
  payload["exp_aft"] = config::kMqttExpireAfterS;

  JsonObject device = payload["dev"].to<JsonObject>();
  device["name"] = "Space state switch";
  device["cu"] = String("http://") + WiFi.localIP().toString() + "/";
  device["mdl"] = "ESP32";
  device["mf"] = "renssies";
  device["sw"] = FIRMWARE_VERSION;
  device["ids"].to<JsonArray>().add(WiFi.macAddress());

  String json;
  serializeJson(payload, json);
  if (esp_mqtt_client_publish(client_, config::kMqttDiscoveryTopic, json.c_str(), json.length(), 1, 1) < 0) {
    Serial.println("[MQTT] Failed to publish Home Assistant discovery");
  }
}
