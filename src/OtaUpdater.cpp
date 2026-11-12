#include "OtaUpdater.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <memory>

#include "Config.h"

#ifndef UPDATE_SIGN
#error "OTA requires signature verification: build with -DUPDATE_SIGN (see platformio.ini)"
#endif

// The public key is embedded from keys/ota_signing_key.pub.pem (board_build.embed_txtfiles).
// An empty file disables OTA.
extern const char kOtaPublicKeyPem[] asm("_binary_keys_ota_signing_key_pub_pem_start");

namespace {

// Marker embedded in every image. After the signature has been verified, the
// downloaded image is searched for the marker of the version the release claims
// to be, binding the (unsigned) release tag to the (signed) image contents.
// The 0x1F separators can't appear in a version string, so a match is unambiguous.
#define VERSION_MARKER_PREFIX "\x1fSSS-FW-VERSION:"
#define VERSION_MARKER_SUFFIX "\x1f"
const char kVersionMarker[] = VERSION_MARKER_PREFIX FIRMWARE_VERSION VERSION_MARKER_SUFFIX;

// Arduino-ESP32's Update library appends a zero-padded signature of this size.
constexpr size_t kSignatureBlockSize = 512;

struct Version {
  long major = 0;
  long minor = 0;
  long patch = 0;

  // Parses "1.2.3" or "v1.2.3". Pre-release / build suffixes are rejected.
  static bool parse(const char *s, Version &out) {
    if (s == nullptr) {
      return false;
    }
    if (*s == 'v' || *s == 'V') {
      s++;
    }
    long *parts[] = {&out.major, &out.minor, &out.patch};
    for (int i = 0; i < 3; i++) {
      if (!isdigit(static_cast<unsigned char>(*s))) {
        return false;
      }
      char *end = nullptr;
      *parts[i] = strtol(s, &end, 10);
      s = end;
      if (i < 2) {
        if (*s != '.') {
          return false;
        }
        s++;
      }
    }
    return *s == '\0';
  }

  bool operator>(const Version &o) const {
    if (major != o.major) return major > o.major;
    if (minor != o.minor) return minor > o.minor;
    return patch > o.patch;
  }
};

struct Release {
  String version;  // Tag without the leading "v"
  String firmwareUrl;
};

std::unique_ptr<NetworkClientSecure> makeTlsClient() {
  auto client = std::make_unique<NetworkClientSecure>();
  // Verify servers against the Mozilla CA bundle shipped with ESP-IDF, so GitHub
  // rotating certificates or CDN hosts doesn't break updates.
  client->useBuiltinCACertBundle();
  client->setHandshakeTimeout(config::kHttpTimeoutMs / 1000);
  return client;
}

void addCommonHeaders(HTTPClient &http) {
  http.setUserAgent("SpaceStateSwitch/" FIRMWARE_VERSION);
  http.setConnectTimeout(config::kHttpTimeoutMs);
  http.setTimeout(config::kHttpTimeoutMs);
  // Avoid chunked transfer encoding so the body can be streamed directly.
  http.useHTTP10(true);
}

bool fetchLatestRelease(NetworkClientSecure &client, Release &release) {
  const String url = String("https://api.github.com/repos/") + config::kGithubRepository + "/releases/latest";

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("[OTA] Invalid release URL");
    return false;
  }
  addCommonHeaders(http);
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    // 404 simply means there are no releases yet.
    Serial.printf("[OTA] Release lookup failed: %d %s\n", status, status < 0 ? http.errorToString(status).c_str() : "");
    http.end();
    return false;
  }

  // The full response is large (release notes, uploader details, ...); only keep what we need.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    Serial.printf("[OTA] Failed to parse release: %s\n", error.c_str());
    return false;
  }

  String tag = doc["tag_name"] | "";
  if (tag.startsWith("v") || tag.startsWith("V")) {
    tag.remove(0, 1);
  }
  release.version = tag;

  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if (strcmp(asset["name"] | "", config::kFirmwareAssetName) == 0) {
      release.firmwareUrl = asset["browser_download_url"] | "";
    }
  }
  return true;
}

// Searches the given partition for `needle` in the first `length` bytes.
bool partitionContains(const esp_partition_t *partition, size_t length, const String &needle) {
  constexpr size_t kChunkSize = 4096;
  const size_t overlap = needle.length() - 1;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kChunkSize + overlap]);
  if (!buffer) {
    return false;
  }

  size_t carried = 0;
  for (size_t offset = 0; offset < length; offset += kChunkSize) {
    const size_t toRead = std::min(kChunkSize, length - offset);
    if (esp_partition_read(partition, offset, buffer.get() + carried, toRead) != ESP_OK) {
      return false;
    }
    const size_t available = carried + toRead;
    if (memmem(buffer.get(), available, needle.c_str(), needle.length()) != nullptr) {
      return true;
    }
    // Keep the tail so markers spanning two chunks are still found.
    carried = std::min(overlap, available);
    memmove(buffer.get(), buffer.get() + available - carried, carried);
  }
  return false;
}

bool downloadAndInstall(NetworkClientSecure &client, const Release &release, ota::ProgressCallback onProgress) {
  const size_t keyLength = strlen(kOtaPublicKeyPem) + 1;  // mbedtls needs the NUL for PEM keys
  UpdaterECDSAVerifier verifier(reinterpret_cast<const uint8_t *>(kOtaPublicKeyPem), keyLength, HASH_SHA256);
  if (!Update.installSignature(&verifier)) {
    Serial.println("[OTA] Failed to install signature verifier");
    return false;
  }

  HTTPClient http;
  if (!release.firmwareUrl.startsWith("https://") || !http.begin(client, release.firmwareUrl)) {
    Serial.println("[OTA] Invalid firmware URL");
    return false;
  }
  addCommonHeaders(http);
  // Release downloads redirect to a CDN. HTTPClient only follows https -> https.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[OTA] Firmware download failed: %d %s\n", status, status < 0 ? http.errorToString(status).c_str() : "");
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= static_cast<int>(kSignatureBlockSize)) {
    Serial.printf("[OTA] Unexpected firmware size: %d\n", contentLength);
    http.end();
    return false;
  }
  const size_t totalSize = static_cast<size_t>(contentLength);

  if (!Update.begin(totalSize)) {
    Serial.printf("[OTA] Cannot start update: %s\n", Update.errorString());
    http.end();
    return false;
  }

  NetworkClient *stream = http.getStreamPtr();
  constexpr size_t kBufferSize = 1024;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kBufferSize]);
  if (!buffer) {
    Update.abort();
    http.end();
    return false;
  }
  uint32_t lastDataAt = millis();
  while (Update.remaining() > 0) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!stream->connected() || millis() - lastDataAt > config::kHttpTimeoutMs) {
        Serial.println("[OTA] Download interrupted");
        Update.abort();
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    const size_t toRead = std::min({available, kBufferSize, Update.remaining()});
    const size_t read = stream->readBytes(buffer.get(), toRead);
    if (read == 0 || Update.write(buffer.get(), read) != read) {
      Serial.printf("[OTA] Write failed: %s\n", Update.errorString());
      Update.abort();
      http.end();
      return false;
    }
    lastDataAt = millis();
    if (onProgress) {
      onProgress(Update.progress(), totalSize);
    }
  }
  http.end();

  // Verifies the image and its signature, then sets it as the boot partition.
  if (!Update.end()) {
    Serial.printf("[OTA] Verification failed, keeping current firmware: %s\n", Update.errorString());
    return false;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *updated = esp_ota_get_boot_partition();
  const String expectedMarker = String(VERSION_MARKER_PREFIX) + release.version + VERSION_MARKER_SUFFIX;
  if (updated == nullptr || updated == running ||
      !partitionContains(updated, totalSize - kSignatureBlockSize, expectedMarker)) {
    Serial.printf("[OTA] Signed image is not version %s, refusing to boot it\n", release.version.c_str());
    esp_ota_set_boot_partition(running);
    return false;
  }

  return true;
}

}  // namespace

// Defer the rollback decision until ota::markFirmwareValid() is called, rather than
// accepting a freshly installed image as soon as it starts.
extern "C" bool verifyRollbackLater() {
  return true;
}

namespace ota {

Result checkAndUpdate(ProgressCallback onProgress) {
  // Reference the marker so the linker keeps it in the image.
  (void)*static_cast<const volatile char *>(kVersionMarker);

  Version current;
  if (!Version::parse(FIRMWARE_VERSION, current)) {
    Serial.println("[OTA] Development build (" FIRMWARE_VERSION "), automatic updates disabled");
    return Result::Disabled;
  }
  if (kOtaPublicKeyPem[0] == '\0') {
    Serial.println("[OTA] No signing key compiled in, automatic updates disabled");
    return Result::Disabled;
  }
  if (!WiFi.isConnected()) {
    return Result::Failed;
  }

  Serial.printf("[OTA] Running %s, checking %s for updates\n", FIRMWARE_VERSION, config::kGithubRepository);
  auto client = makeTlsClient();

  Release release;
  if (!fetchLatestRelease(*client, release)) {
    return Result::Failed;
  }

  Version latest;
  if (!Version::parse(release.version.c_str(), latest)) {
    Serial.printf("[OTA] Ignoring release with unsupported tag '%s'\n", release.version.c_str());
    return Result::Failed;
  }
  if (!(latest > current)) {
    Serial.printf("[OTA] Up to date (latest release: %s)\n", release.version.c_str());
    return Result::UpToDate;
  }
  if (release.firmwareUrl.isEmpty()) {
    Serial.printf("[OTA] Release %s has no %s asset\n", release.version.c_str(), config::kFirmwareAssetName);
    return Result::Failed;
  }

  Serial.printf("[OTA] Installing %s\n", release.version.c_str());
  if (!downloadAndInstall(*client, release, onProgress)) {
    return Result::Failed;
  }

  Serial.printf("[OTA] Update to %s installed, rebooting\n", release.version.c_str());
  Serial.flush();
  ESP.restart();
  return Result::Failed;  // Unreachable
}

void markFirmwareValid() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      Serial.println("[OTA] New firmware confirmed, rollback cancelled");
    }
  }
}

}  // namespace ota
