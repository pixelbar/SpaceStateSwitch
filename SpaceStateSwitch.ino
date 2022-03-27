#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#ifndef STASSID
#define STASSID "SSID" // SSID of Pixelbar Services
#define STAPSK  "PASS" // Password of Services SSID
#endif

const char* ssid     = STASSID;
const char* password = STAPSK;

const int butPin = 5; // Pin D1 | 0 = D3
const int ledPin = 2; // Pin D4 and Onbard LED
bool stateOld = false;
bool stateNew = false;
unsigned long lastTime = 0;
unsigned long delTime = 250;

void setup() {
  pinMode(butPin, INPUT);
  pinMode(ledPin, OUTPUT);
  
  Serial.begin(115200);
  
  delay(1000); // Wait till booted

  Serial.print("[BOOTED]\n");
  
  connectWifi();
}

void connectWifi(){
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    cnt ++;
    if (cnt > 24){
      cnt = 0;
      Serial.println("");
      Serial.print(millis());
      Serial.println(" | Not connected, I will try again.");
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void turnOn(){
  Serial.print("[SWITCH] Turning state on\n");
  WiFiClient client;
  HTTPClient http;
  Serial.print("[HTTP] begin...\n");

  const char *URL = "http://spacestate.pixelbar.nl:8080/togglestate.php?user=user&token=token&state=open"; // Replace user and token with real info
  
  if (http.begin(client, URL)) {  // HTTP
    Serial.println("[HTTP] Execute GET command...");
    int httpCode = http.GET();    // start connection and send HTTP header
    if (httpCode > 0) {           // httpCode will be negative on error
      Serial.printf("[HTTP] GET... code: %d\n", httpCode); // HTTP header has been send and Server response header has been handled
      
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
      
    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
}

void turnOff(){
  Serial.print("[SWITCH] Turning state off\n");
  WiFiClient client;
  HTTPClient http;
  Serial.print("[HTTP] begin...\n");

  const char *URL = "http://spacestate.pixelbar.nl:8080/togglestate.php?user=user&token=token&state=closed";  // Replace user and token with real info
  
  if (http.begin(client, URL)) {  // HTTP
    Serial.println("[HTTP] Execute GET command...");
    int httpCode = http.GET();    // start connection and send HTTP header
    if (httpCode > 0) {           // httpCode will be negative on error
      Serial.printf("[HTTP] GET... code: %d\n", httpCode); // HTTP header has been send and Server response header has been handled
      
      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
      
    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
}

void loop() {
  stateNew = digitalRead(butPin);

  if (WiFi.status() == WL_CONNECTED){
    if(stateNew != stateOld){
      if((millis() - lastTime) > delTime){
        lastTime = millis();
        if(stateNew != stateOld){
          Serial.println("");
          Serial.print(millis());
          Serial.print(" State = ");
          Serial.println(stateNew);
          stateOld = stateNew;
          digitalWrite(ledPin, stateOld);
          if(stateNew){
            turnOff();
          } else {
            turnOn();
          }
        }
      }
    }
  } else {
    Serial.println("Not connected to WiFi...");
    connectWifi();
  }
}
