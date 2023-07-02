#ifndef PB_CONTANTS
#define PB_CONTANTS 1

// The pin neopixel header (330Ohm resistor)
#define NEOPIXEL_PIN 27 // IO27

// The pin for the switch header (INPUT_PULLUP)
#define SWITCH_PIN 26 // IO26/D0

#define BUILTIN_LED_PIN 2

// The number of Neopixels supported on the header
#define NUMBER_OF_PIXELS 5

// Needed for "DoubleResetDetector", telling it to use SPIFFS.
#define ESP_DRD_USE_LITTLEFS    false
#define ESP_DRD_USE_SPIFFS      true
#define ESP_DRD_USE_EEPROM      false

// Number of seconds after reset during which a 
// subsequent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use. Required, but not used on the ESP32
#define DRD_ADDRESS 0

// The minimum interval between MQTT connection attempts
#define MQTT_CONNECT_INTERVAL 10000 // miliseconds

// The network SSID for the config portal.
#define CONFIG_AP_SSID "space-state-switch"

// The network password for the config portal.
#define CONFIG_AP_PASSWORD "1337hacker"

// The hostname for WiFi and mDNS.
#define HOSTNAME_PREFIX "space-state-switch"

// The namespace used for the ESP32 preferences.
#define PREFERENCES_NAMESPACE "space-state"

// The key used for the MQTT server in ESP32 Preferences and WiFiManager Parameter.
#define PREFERENCE_MQTT_SERVER_KEY "mqtt_server"

// The key used for the MQTT port in ESP32 Preferences and WiFiManager Parameter.
#define PREFERENCE_MQTT_PORT_KEY "mqtt_port"

// The key used for the MQTT username in ESP32 Preferences and WiFiManager Parameter.
#define PREFERENCE_MQTT_USERNAME_KEY "mqtt_username"

// The key used for the MQTT password in ESP32 Preferences and WiFiManager Parameter.
#define PREFERENCE_MQTT_PASSWORD_KEY "mqtt_password"

#endif 