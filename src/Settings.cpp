#include "Settings.h"

#include <Preferences.h>

#include "Config.h"

#if __has_include("Secrets.h")
#include "Secrets.h"
#endif

namespace {
// NVS keys are limited to 15 characters. The MQTT keys match the original firmware
// so existing devices keep their configuration.
constexpr const char *kKeyMqttServer = "mqtt_server";
constexpr const char *kKeyMqttPort = "mqtt_port";
constexpr const char *kKeyMqttUsername = "mqtt_username";
constexpr const char *kKeyMqttPassword = "mqtt_password";
constexpr const char *kKeySpaceStateUsername = "ss_username";
constexpr const char *kKeySpaceStateToken = "ss_token";
}  // namespace

void Settings::load() {
  Preferences prefs;
  // Open read-write so the namespace is created on first boot.
  if (!prefs.begin(config::kPreferencesNamespace, false)) {
    log_e("Failed to open preferences");
    return;
  }

  mqttServer = prefs.getString(kKeyMqttServer, "");
  mqttServer.trim();
  // The original firmware stored the port as a string.
  mqttPort = prefs.getString(kKeyMqttPort, "").toInt();
  if (mqttPort == 0) {
    mqttPort = config::kMqttDefaultPort;
  }
  mqttUsername = prefs.getString(kKeyMqttUsername, "");
  mqttPassword = prefs.getString(kKeyMqttPassword, "");
  spaceStateUsername = prefs.getString(kKeySpaceStateUsername, "");
  spaceStateToken = prefs.getString(kKeySpaceStateToken, "");
  prefs.end();

#if defined(SPACESTATE_REQUEST_USERNAME) && defined(SPACESTATE_REQUEST_TOKEN)
  if (!hasSpaceStateCredentials()) {
    Serial.println("[Settings] Migrating space state credentials from Secrets.h to NVS");
    spaceStateUsername = SPACESTATE_REQUEST_USERNAME;
    spaceStateToken = SPACESTATE_REQUEST_TOKEN;
    save();
  }
#endif
}

void Settings::save() const {
  Preferences prefs;
  if (!prefs.begin(config::kPreferencesNamespace, false)) {
    log_e("Failed to open preferences for writing");
    return;
  }
  prefs.putString(kKeyMqttServer, mqttServer);
  prefs.putString(kKeyMqttPort, String(mqttPort));
  prefs.putString(kKeyMqttUsername, mqttUsername);
  prefs.putString(kKeyMqttPassword, mqttPassword);
  prefs.putString(kKeySpaceStateUsername, spaceStateUsername);
  prefs.putString(kKeySpaceStateToken, spaceStateToken);
  prefs.end();
}
