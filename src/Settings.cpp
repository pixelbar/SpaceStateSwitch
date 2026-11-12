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
// The user is shared with the old legacy-API firmware. The API key gets its own key:
// the legacy token stored under "ss_token" is not a valid API key.
constexpr const char *kKeySpaceStateUser = "ss_username";
constexpr const char *kKeySpaceStateApiKey = "ss_api_key";
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
  spaceStateUser = prefs.getString(kKeySpaceStateUser, "");
  spaceStateApiKey = prefs.getString(kKeySpaceStateApiKey, "");
  prefs.end();

#ifdef SPACESTATE_API_KEY
  if (!hasSpaceStateApiKey()) {
    Serial.println("[Settings] Migrating space state API key from Secrets.h to NVS");
    spaceStateApiKey = SPACESTATE_API_KEY;
#ifdef SPACESTATE_USER
    if (spaceStateUser.isEmpty()) {
      spaceStateUser = SPACESTATE_USER;
    }
#endif
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
  prefs.putString(kKeySpaceStateUser, spaceStateUser);
  prefs.putString(kKeySpaceStateApiKey, spaceStateApiKey);
  prefs.end();
}
