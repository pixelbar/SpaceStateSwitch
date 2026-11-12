#pragma once

#include <cstdint>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"  // Normally injected by scripts/firmware_version.py
#endif

namespace config {

// MARK: - Hardware

constexpr uint8_t kNeopixelPin = 27;    // IO27, Neopixel header (330 Ohm resistor)
constexpr uint8_t kSwitchPin = 26;      // IO26/D0, switch header (INPUT_PULLUP)
constexpr uint8_t kBuiltinLedPin = 2;
constexpr uint16_t kNumberOfPixels = 20;
constexpr uint8_t kNeopixelBrightness = 80;

// MARK: - Timing

// A second reset within this window after boot opens the WiFi config portal.
constexpr uint32_t kDoubleResetWindowMs = 5 * 1000;
// How often the physical switch is sampled.
constexpr uint32_t kSwitchPollIntervalMs = 250;
// How often the online space state is fetched, to detect overrides.
constexpr uint32_t kSpaceStatePollIntervalMs = 10 * 60 * 1000;
// Delay before retrying a failed space state submission or fetch.
constexpr uint32_t kSpaceStateRetryIntervalMs = 10 * 1000;
// Delay between MQTT reconnection attempts.
constexpr uint32_t kMqttReconnectIntervalMs = 10 * 1000;
// How often the state is re-published to MQTT. Must be below kMqttExpireAfterS.
constexpr uint32_t kMqttRepublishIntervalMs = 5 * 60 * 1000;
// Home Assistant marks the sensor unavailable after this many seconds without a state message.
constexpr uint32_t kMqttExpireAfterS = 15 * 60;
// How long the config portal stays open before rebooting.
constexpr uint32_t kConfigPortalTimeoutS = 120;
// Timeout for individual HTTP(S) operations.
constexpr uint32_t kHttpTimeoutMs = 15 * 1000;

// MARK: - Network

constexpr const char *kHostname = "space-state-switch";
constexpr const char *kConfigApSsid = "space-state-switch";
constexpr const char *kConfigApPassword = "1337hacker";

// NVS namespace for Settings (unchanged from the original firmware, so settings carry over).
constexpr const char *kPreferencesNamespace = "space-state";

// MARK: - Space state API

// GET returns the current state; POST (with a `write` API key) changes it.
// See https://pixelbar.nl/spacestate/
constexpr const char *kSpaceStateApiUrl = "https://spacestate.pixelbar.nl/api/v1/state";
// Reported to the API as the `method` of a state change.
constexpr const char *kSpaceStateMethod = "button";

// MARK: - MQTT

constexpr uint16_t kMqttDefaultPort = 1883;
constexpr const char *kMqttStateTopic = "space-state-switch/state";
constexpr const char *kMqttAvailabilityTopic = "space-state-switch/LWT";
constexpr const char *kMqttDiscoveryTopic = "homeassistant/binary_sensor/space-state-switch/config";

// MARK: - OTA updates

// GitHub repository ("owner/name") whose latest release is installed on boot.
constexpr const char *kGithubRepository = "pixelbar/SpaceStateSwitch";
// Name of the signed firmware asset attached to each release.
constexpr const char *kFirmwareAssetName = "firmware.bin";

constexpr const char *kRepositoryUrl = "https://github.com/pixelbar/SpaceStateSwitch";

}  // namespace config
