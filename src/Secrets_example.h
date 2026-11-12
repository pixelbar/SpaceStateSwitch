#ifndef PB_SECRETS
#define PB_SECRETS 1

// OPTIONAL. Copy to Secrets.h (git-ignored) to seed the space state API key
// on a device that doesn't have one stored yet. On first boot it is copied
// into NVS, so later OTA updates (which never contain Secrets.h) keep working.
//
// Both can also be set in the WiFi config portal instead.
// Never add Secrets.h to a release build: release binaries are public.

// API key with the `write` role for https://spacestate.pixelbar.nl/api/v1/state.
#define SPACESTATE_API_KEY "key"

// Optional: recorded as the `user` of state changes (defaults to the hostname).
#define SPACESTATE_USER "space-state-switch"

#endif
