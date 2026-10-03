// Upload ke ESP32 yang jadi RECEIVER (Node 2), buka Serial Monitor 115200, salin MAC-nya
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
