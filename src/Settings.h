#pragma once

#include <Arduino.h>

// Runtime configuration, persisted in NVS and editable through the WiFi config portal.
//
// Credentials deliberately live here rather than being compiled in: release
// firmware is published publicly on GitHub, so anything compiled into it can be
// extracted by anyone.
struct Settings {
  String mqttServer;
  uint16_t mqttPort = 0;
  String mqttUsername;
  String mqttPassword;

  String spaceStateUsername;
  String spaceStateToken;

  // Loads the settings from NVS. If no space state credentials are stored yet and
  // an (optional, git-ignored) src/Secrets.h is present, those are used and persisted
  // so that later OTA builds, which never contain Secrets.h, keep working.
  void load();
  void save() const;

  bool hasMqtt() const {
    return !mqttServer.isEmpty();
  }
  bool hasSpaceStateCredentials() const {
    return !spaceStateUsername.isEmpty() && !spaceStateToken.isEmpty();
  }
};
