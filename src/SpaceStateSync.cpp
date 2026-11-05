#include "SpaceStateSync.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "Config.h"

namespace {

String urlEncode(const String &value) {
  static const char *kHex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += kHex[(c >> 4) & 0xF];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

const char *stateName(bool open) {
  return open ? "open" : "closed";
}

}  // namespace

void SpaceStateSync::begin(const String &username, const String &token, OverrideCallback onOverride) {
  username_ = username;
  token_ = token;
  onOverride_ = onOverride;
  if (username_.isEmpty() || token_.isEmpty()) {
    Serial.println("[SpaceState] No credentials configured, changes won't be submitted");
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
  if (username_.isEmpty() || token_.isEmpty()) {
    return true;  // Nothing we can do; don't retry forever.
  }

  NetworkClientSecure client;
  client.useBuiltinCACertBundle();

  const String url = String(config::kToggleStateUrl) + "?user=" + urlEncode(username_) + "&token=" + urlEncode(token_) +
                     "&state=" + stateName(open);

  HTTPClient http;
  http.setConnectTimeout(config::kHttpTimeoutMs);
  http.setTimeout(config::kHttpTimeoutMs);
  if (!http.begin(client, url)) {
    Serial.println("[SpaceState] Invalid submit URL");
    return false;
  }
  // Don't log the URL, it contains the token.
  Serial.printf("[SpaceState] Submitting '%s'\n", stateName(open));
  const int status = http.GET();
  http.end();

  if (status <= 0) {
    Serial.printf("[SpaceState] Submit failed: %s\n", HTTPClient::errorToString(status).c_str());
    return false;
  }
  Serial.printf("[SpaceState] Submit response: %d\n", status);
  return status >= 200 && status < 400;
}

bool SpaceStateSync::fetchState(bool &open) {
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(config::kHttpTimeoutMs);
  http.setTimeout(config::kHttpTimeoutMs);
  http.useHTTP10(true);  // No chunked encoding, so we can parse straight from the stream
  if (!http.begin(client, config::kSpaceStateUrl)) {
    Serial.println("[SpaceState] Invalid state URL");
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK && status != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.printf("[SpaceState] Fetch failed: %d %s\n", status, status < 0 ? HTTPClient::errorToString(status).c_str() : "");
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  const char *state = doc["state"];
  if (error || state == nullptr) {
    Serial.println("[SpaceState] Unsupported response from space state API");
    return false;
  }
  open = strcmp(state, "open") == 0;
  return true;
}
