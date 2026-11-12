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

  // Recorded as the `user` of state changes. Defaults to the hostname when empty.
  String spaceStateUser;
  // Bearer key with the `write` role for the space state API.
  String spaceStateApiKey;

  // Loads the settings from NVS. If no space state API key is stored yet and an
  // (optional, git-ignored) src/Secrets.h is present, that is used and persisted
  // so that later OTA builds, which never contain Secrets.h, keep working.
  void load();
  void save() const;

  bool hasMqtt() const {
    return !mqttServer.isEmpty();
  }
  bool hasSpaceStateApiKey() const {
    return !spaceStateApiKey.isEmpty();
  }
};
