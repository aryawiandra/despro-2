// TES KOMUNIKASI ESP-NOW (hanya Serial, tanpa sensor/LED) - ESP32 #1 (SENDER)
// Pakai alamat broadcast, jadi TIDAK perlu tahu MAC receiver.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL 1

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t counter = 0;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.print("SENDER siap. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  counter++;
  esp_err_t res = esp_now_send(broadcastMac, (uint8_t *)&counter, sizeof(counter));
  Serial.print("Kirim #");
  Serial.print(counter);
  Serial.println(res == ESP_OK ? " -> dikirim" : " -> ERROR");
  delay(1000);
}
