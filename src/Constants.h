#ifndef PB_CONTANTS
#define PB_CONTANTS 1

// The pin neopixel header (330Ohm resistor)
#define NEOPIXEL_PIN 27 // IO27

// The pin for the switch header (INPUT_PULLUP)
#define SWITCH_PIN 26 // IO26/D0

#define BUILTIN_LED_PIN 2

// The number of Neopixels supported on the header
#define NUMBER_OF_PIXELS 20

// Needed for "DoubleResetDetector", telling it to use SPIFFS.
#define ESP_DRD_USE_LITTLEFS    false
#define ESP_DRD_USE_SPIFFS      true
#define ESP_DRD_USE_EEPROM      false

// Number of seconds after reset during which a 
// subsequent reset will be considered a double reset.
#define DRD_TIMEOUT 5

// RTC Memory Address for the DoubleResetDetector to use. Required, but not used on the ESP32
#define DRD_ADDRESS 0

// The minimum interval between MQTT connection attempts
#define MQTT_CONNECT_INTERVAL 10000 // miliseconds

// The interval between checks for the space state if it has been overwritten.
#define SPACE_STATE_CHECK_INTERVAL 10 * 60 * 1000 // 10 minutes in miliseconds.

// The interval between checks for switch state changes.
#define SWITCH_STATE_INTERVAL 250 // milliseconds

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


const char* digicert_global_root_g2_ca_pem = \
     "-----BEGIN CERTIFICATE-----\n" \
     "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
     "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
     "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
     "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
     "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
     "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
     "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
     "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
     "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
     "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
     "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
     "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
     "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
     "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
     "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
     "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
     "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
     "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
     "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
     "MrY=\n" \
     "-----END CERTIFICATE-----\n";


#endif 