#include "DoubleResetDetector.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

namespace {
constexpr const char *kNamespace = "drd";
constexpr const char *kKeyArmed = "armed";
}  // namespace

bool DoubleResetDetector::detect() {
  const esp_reset_reason_t reason = esp_reset_reason();
  // The EN/reset button on the ESP32 shows up as a power-on reset.
  const bool userReset = reason == ESP_RST_POWERON || reason == ESP_RST_EXT;

  Preferences prefs;
  prefs.begin(kNamespace, true);
  const bool wasArmed = prefs.getBool(kKeyArmed, false);
  prefs.end();

  if (userReset && wasArmed) {
    setArmed(false);
    return true;
  }

  setArmed(true);
  armedAtMs_ = millis();
  return false;
}

void DoubleResetDetector::loop() {
  if (armed_ && millis() - armedAtMs_ > windowMs_) {
    setArmed(false);
  }
}

void DoubleResetDetector::stop() {
  if (armed_) {
    setArmed(false);
  }
}

void DoubleResetDetector::setArmed(bool armed) {
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBool(kKeyArmed, armed);
    prefs.end();
  }
  armed_ = armed;
}
