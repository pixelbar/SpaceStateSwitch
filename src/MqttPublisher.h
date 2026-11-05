#pragma once

#include <Arduino.h>

#include <atomic>

struct Settings;
struct esp_mqtt_client;

// Publishes the space state to MQTT, including Home Assistant discovery.
//
// Uses the ESP-IDF MQTT client, which runs in its own task and handles
// (re)connecting in the background, so the main loop never blocks on the broker.
class MqttPublisher {
 public:
  using StateProvider = bool (*)();

  // Starts the client if an MQTT server is configured. `currentState` is called
  // (from the MQTT task) to publish the state after (re)connecting.
  void begin(const Settings &settings, StateProvider currentState);

  // Thread-safe. No-op when not connected; the state is re-sent on (re)connect.
  void publishState(bool open);

  bool isConnected() const {
    return connected_;
  }

 private:
  static void onEvent(void *arg, const char *base, int32_t eventId, void *eventData);
  void publishDiscovery();

  esp_mqtt_client *client_ = nullptr;
  StateProvider currentState_ = nullptr;
  std::atomic<bool> connected_{false};
};
