#include "SpaceStateSync.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "Config.h"

namespace {

const char *stateName(bool open) {
  return open ? "open" : "closed";
}

}  // namespace

void SpaceStateSync::begin(const String &user, const String &apiKey, OverrideCallback onOverride) {
  user_ = user.isEmpty() ? String(config::kHostname) : user;
  apiKey_ = apiKey;
  onOverride_ = onOverride;
  if (apiKey_.isEmpty()) {
    Serial.println("[SpaceState] No API key configured, changes won't be submitted");
  }
  xTaskCreate(&SpaceStateSync::taskEntry, "SpaceStateSync", 8192, this, 1, &task_);
}

void SpaceStateSync::submit(bool open) {
  pending_ = open ? 1 : 0;
  if (task_ != nullptr) {
    xTaskNotifyGive(task_);
  }
}

void SpaceStateSync::taskEntry(void *self) {
  static_cast<SpaceStateSync *>(self)->run();
}

void SpaceStateSync::run() {
  // Signed difference so the comparison survives millis() wrapping around.
  auto msUntil = [](uint32_t deadline) { return static_cast<int32_t>(deadline - millis()); };
  uint32_t nextPollAt = millis();  // Fetch the online state straight away on boot.

  for (;;) {
    uint32_t waitMs = config::kSpaceStatePollIntervalMs;

    if (!WiFi.isConnected()) {
      waitMs = 1000;
    } else {
      int8_t pending = pending_.load();
      if (pending != kNothingPending) {
        if (submitState(pending == 1)) {
          // Only clear if no newer state was queued while submitting.
          pending_.compare_exchange_strong(pending, kNothingPending);
        } else {
          waitMs = config::kSpaceStateRetryIntervalMs;
        }
      }

      if (pending_.load() == kNothingPending && msUntil(nextPollAt) <= 0) {
        bool online;
        if (fetchState(online)) {
          nextPollAt = millis() + config::kSpaceStatePollIntervalMs;
          // A submit may have been queued while fetching; the switch wins in that case.
          if (pending_.load() == kNothingPending && onOverride_) {
            onOverride_(online);
          }
        } else {
          nextPollAt = millis() + config::kSpaceStateRetryIntervalMs;
        }
      }

      if (pending_.load() == kNothingPending) {
        waitMs = std::min<uint32_t>(waitMs, std::max<int32_t>(0, msUntil(nextPollAt)));
      }
    }

    // Sleep until the next poll/retry, or until submit() wakes us up.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
  }
}

bool SpaceStateSync::submitState(bool open) {
  if (apiKey_.isEmpty()) {
    return true;  // Nothing we can do; don't retry forever.
  }

  JsonDocument body;
  body["state"] = stateName(open);
  body["user"] = user_;
  body["method"] = config::kSpaceStateMethod;
  String payload;
  serializeJson(body, payload);

  NetworkClientSecure client;
  client.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(config::kHttpTimeoutMs);
  http.setTimeout(config::kHttpTimeoutMs);
  if (!http.begin(client, config::kSpaceStateApiUrl)) {
    Serial.println("[SpaceState] Invalid state URL");
    return false;
  }
  http.addHeader("Authorization", "Bearer " + apiKey_);
  http.addHeader("Content-Type", "application/json");

  Serial.printf("[SpaceState] Submitting '%s'\n", stateName(open));
  const int status = http.POST(payload);
  const String response = status > 0 ? http.getString() : String();
  http.end();

  switch (status) {
    case HTTP_CODE_OK:
      Serial.println("[SpaceState] State changed");
      return true;
    case HTTP_CODE_CONFLICT:  // The space was already in this state; nothing to do.
      Serial.println("[SpaceState] Already in that state");
      return true;
    case HTTP_CODE_UNAUTHORIZED:
      // Retrying won't help until the key is changed in the config portal.
      Serial.println("[SpaceState] Submit rejected: API key missing, invalid or not 'write'");
      return true;
    case HTTP_CODE_BAD_REQUEST:
      Serial.printf("[SpaceState] Submit rejected as invalid: %s\n", response.c_str());
      return true;
    default:
      if (status <= 0) {
        Serial.printf("[SpaceState] Submit failed: %s\n", HTTPClient::errorToString(status).c_str());
      } else {
        Serial.printf("[SpaceState] Submit failed: %d %s\n", status, response.c_str());
      }
      return false;  // Network or server error: retry.
  }
}

bool SpaceStateSync::fetchState(bool &open) {
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(config::kHttpTimeoutMs);
  http.setTimeout(config::kHttpTimeoutMs);
  if (!http.begin(client, config::kSpaceStateApiUrl)) {
    Serial.println("[SpaceState] Invalid state URL");
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[SpaceState] Fetch failed: %d %s\n", status, status < 0 ? HTTPClient::errorToString(status).c_str() : "");
    http.end();
    return false;
  }

  JsonDocument doc;
  // getString() decodes chunked transfer encoding; the response is tiny.
  const DeserializationError error = deserializeJson(doc, http.getString());
  http.end();

  const char *state = doc["state"];
  if (error || state == nullptr) {
    Serial.println("[SpaceState] Unsupported response from space state API");
    return false;
  }
  open = strcmp(state, "open") == 0;
  return true;
}
