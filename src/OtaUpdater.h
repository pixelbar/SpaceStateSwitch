#pragma once

#include <cstddef>

// Over-the-air updates from GitHub releases.
//
// On boot the device asks the GitHub API for the latest release of
// config::kGithubRepository. If its tag (e.g. "v1.2.3") is newer than the running
// FIRMWARE_VERSION, the release's signed firmware asset is streamed into the
// inactive OTA partition and the device reboots into it.
//
// Security:
//  - All traffic uses TLS, verified against the ESP-IDF Mozilla CA bundle.
//  - The image must carry a valid ECDSA P-256 signature from the key whose public
//    half is keys/ota_signing_key.pub.pem. TLS only proves the bytes came from
//    GitHub; the signature proves they came from us, so a compromised GitHub
//    account or release cannot push arbitrary firmware.
//  - The signed image must contain a version marker matching the release tag,
//    so an older (validly signed) image can't be re-published under a newer tag
//    to downgrade devices.
//  - Only strictly newer versions are installed; "dev" builds never update.
//  - If the new firmware fails to boot and connect, the bootloader rolls back to
//    the previous image (see markFirmwareValid()).
namespace ota {

enum class Result {
  Disabled,  // Dev build or no signing key compiled in.
  UpToDate,
  Failed,    // Network/verification error; the running firmware is kept.
  // On success the device reboots, so there is no "Updated" result.
};

using ProgressCallback = void (*)(size_t written, size_t total);

// Checks GitHub for a newer release and installs it. Requires WiFi to be connected.
Result checkAndUpdate(ProgressCallback onProgress = nullptr);

// Confirms the running image is healthy, cancelling a pending rollback.
// Call once the firmware has proven it works (e.g. connected to WiFi).
void markFirmwareValid();

}  // namespace ota
