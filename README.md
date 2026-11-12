# SpaceStateSwitch

Firmware for the space state switch at Pixelbar. It runs on an ESP32 (Wemos D1 Mini32). It reads the physical switch and publishes the state to [spacestate.pixelbar.nl](https://spacestate.pixelbar.nl), to MQTT (with Home Assistant discovery) and to a small local JSON API. On every boot it checks GitHub for a newer release and installs it over the air.

Code created by DiverAlpha, referencing the old code of royvandongen.

## Behaviour

| LED colour | Meaning |
| --- | --- |
| Green | Space is open |
| Red | Space is closed |
| Blue | Not connected to WiFi |
| Purple (blinking) | Installing a firmware update |

- If the switch changes, the new state is sent to the [space state API](https://pixelbar.nl/spacestate/) (`POST /api/v1/state`). Network and server errors are retried every 10 seconds; a rejected API key is logged and not retried.
- Every 10 minutes the online state is fetched. If someone changed the state on the website, that override is shown until the switch is flipped again.
- **Double press reset** to open the config portal. It runs as WiFi network `space-state-switch` with password `1337hacker` and closes after 2 minutes. In the portal you set the WiFi network, the MQTT broker and the space state API key (an API key with the `write` role) and user. Password and API key fields start empty; leave them empty to keep the stored value.

### HTTP API

`http://space-state-switch.local/`

- `GET /`: current state (`is_open`, `state`, `is_overwritten`, `physical_state`)
- `GET /info`: device, network and firmware info
- `GET /heap`: memory usage

### MQTT

- `space-state-switch/state`: `1` (open) or `0` (closed), retained and re-published every 5 minutes
- `space-state-switch/LWT`: `online` / `offline`
- Home Assistant discovery on `homeassistant/binary_sensor/space-state-switch/config`

## Building

This uses [PlatformIO](https://platformio.org) with the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform (Arduino-ESP32 3.x / ESP-IDF 5.x).

```sh
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial logs
```

Local builds report version `dev` and **never update themselves**, so a build you flashed by hand won't be replaced by the latest release. To put a device back on automatic updates, flash a release build (see below), for example by building it locally with `FIRMWARE_VERSION=v1.2.3 pio run -t upload`.

### Migrating from the old firmware

The partition table changed to make room for two OTA slots, and partition tables can't be changed over the air. Flash the first version of this firmware **over USB** with `pio run -t upload`. WiFi and MQTT settings are kept.

Space state credentials are no longer compiled in. Enter them in the config portal, or put them in `src/Secrets.h` (see `src/Secrets_example.h`) for that first USB flash. They are then stored on the device.

The switch uses the new space state API with a Bearer API key, not the legacy `togglestate.php` token. A device that already has a legacy token stored still needs an API key with the `write` role: ask a spacestate maintainer for one and enter it in the config portal. The stored user is kept.

## OTA updates

On boot, after connecting to WiFi, the device:

1. Fetches `https://api.github.com/repos/pixelbar/SpaceStateSwitch/releases/latest`.
2. Stops if the release tag (`vX.Y.Z`) isn't newer than the running version.
3. Streams the release's `firmware.bin` into the inactive OTA partition.
4. Checks the image's ECDSA signature and checks that the signed image contains the version the tag claims. If either check fails, the update is thrown away and the current firmware keeps running.
5. Reboots into the new firmware. If the new firmware doesn't get as far as connecting to WiFi (it crashes, or the config portal times out), the bootloader rolls back to the previous version.

### Security model

- **Transport:** every connection uses TLS. Certificates are checked against the Mozilla CA bundle that ships with ESP-IDF.
- **Authenticity:** TLS only proves the bytes came from GitHub. Devices also require a signature from the project's private key. Someone who gets into the GitHub repository or the release assets still can't install their own firmware without that key.
- **No downgrades:** only strictly newer versions are installed. The version is checked against a marker inside the signed image, so an older signed image can't be re-uploaded under a newer tag.
- **No secrets in firmware:** release binaries are public, so credentials live in the device's NVS, not in the binary.

### One-time setup

1. Generate the signing key pair:

   ```sh
   scripts/generate-ota-signing-key.sh
   ```

   This writes `keys/ota_signing_key.pub.pem` (commit it) and `ota_signing_key.pem` (private, git-ignored).

2. Store the private key as a GitHub Actions secret, keep a copy in the space's password manager, then delete the local file:

   ```sh
   gh secret set OTA_SIGNING_KEY < ota_signing_key.pem
   rm ota_signing_key.pem
   ```

3. Commit the public key. Flash a release build to the device over USB once, because the device needs firmware that contains the public key.

Until a public key is committed, OTA is disabled and the release workflow fails rather than publishing unsigned firmware.

### Releasing

```sh
git tag v1.2.3
git push origin v1.2.3
```

The [release workflow](.github/workflows/release.yml) builds with that version, signs the binary, checks the signature against the committed public key, and publishes a GitHub release with `firmware.bin`. Devices pick it up the next time they boot.

The workflow runs in a `release` GitHub environment. Add required reviewers to that environment if you want someone to approve each signing.

### Rotating the signing key

A device only accepts updates signed with the key built into the firmware it's running. To rotate the key:

1. Run `FORCE=1 scripts/generate-ota-signing-key.sh new_key.pem` and commit the new public key.
2. Release a version while `OTA_SIGNING_KEY` still holds the **old** private key. Devices accept it because it's signed with the key they trust, and it contains the new public key.
3. Once devices have updated, set `OTA_SIGNING_KEY` to the new private key (`gh secret set OTA_SIGNING_KEY < new_key.pem`).

The release workflow checks each release against the public key of the *previous* release, which is the key devices actually trust, so it catches a key that was swapped too early. Devices still on firmware older than the rotation release will reject new releases and have to be updated over USB.

If the private key is lost, devices have to be re-flashed over USB.
