# PlatformIO pre-build script: injects FIRMWARE_VERSION into the build.
#
# CI sets the FIRMWARE_VERSION environment variable from the release tag
# (e.g. "1.2.3"). Local builds default to "dev", which disables OTA updates
# so a development build flashed over USB is not immediately replaced by the
# latest release.
import os

Import("env")  # noqa: F821 - provided by PlatformIO/SCons

version = os.environ.get("FIRMWARE_VERSION", "").strip() or "dev"
if version.startswith("v"):
    version = version[1:]

print(f"Firmware version: {version}")
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])  # noqa: F821

# Development-only features (e.g. serial commands that simulate the switch).
if version == "dev":
    env.Append(CPPDEFINES=["DEV_BUILD"])  # noqa: F821
