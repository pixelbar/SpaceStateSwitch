#pragma once

#include <Arduino.h>

#include <atomic>

// Keeps spacestate.pixelbar.nl in sync with the switch.
//
// A single background task submits switch changes and periodically fetches the
// online state, so the two never race. If the online state differs from ours
// (someone overrode it on the website) the override callback is invoked.
class SpaceStateSync {
 public:
  using OverrideCallback = void (*)(bool open);

  void begin(const String &username, const String &token, OverrideCallback onOverride);

  // Queues the given state for submission. Thread-safe; only the latest state is sent.
  void submit(bool open);

 private:
  static void taskEntry(void *self);
  void run();
  bool submitState(bool open);
  bool fetchState(bool &open);

  static constexpr int8_t kNothingPending = -1;

  String username_;
  String token_;
  OverrideCallback onOverride_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<int8_t> pending_{kNothingPending};
};
