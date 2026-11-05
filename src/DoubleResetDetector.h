#pragma once

#include <cstdint>

// Detects two resets in quick succession (e.g. double pressing the reset button).
//
// A flag is armed in NVS on boot and cleared once the device has been running
// for longer than the window. If the device boots while the flag is still armed,
// that's a double reset. Software restarts (OTA, config saved) and crashes are
// ignored so they never trigger the config portal.
class DoubleResetDetector {
 public:
  explicit DoubleResetDetector(uint32_t windowMs) : windowMs_(windowMs) {}

  // Call once, early in setup(). Returns true if a double reset was detected.
  bool detect();

  // Call from loop(); disarms the detector once the window has passed.
  void loop();

  // Disarms immediately, e.g. before blocking in the config portal.
  void stop();

 private:
  void setArmed(bool armed);

  uint32_t windowMs_;
  uint32_t armedAtMs_ = 0;
  bool armed_ = false;
};
