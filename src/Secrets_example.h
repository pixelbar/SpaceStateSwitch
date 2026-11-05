#ifndef PB_SECRETS
#define PB_SECRETS 1

// OPTIONAL. Copy to Secrets.h (git-ignored) to seed the space state credentials
// on a device that doesn't have them stored yet. On first boot they are copied
// into NVS, so later OTA updates (which never contain Secrets.h) keep working.
//
// The credentials can also be set in the WiFi config portal instead.
// Never add Secrets.h to a release build: release binaries are public.

// The username used for the request of setting the current space state.
#define SPACESTATE_REQUEST_USERNAME "user"

// The authentication token used for the request of setting the current space state.
#define SPACESTATE_REQUEST_TOKEN "token"

#endif
