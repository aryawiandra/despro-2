// TES KOMUNIKASI ESP-NOW (hanya Serial, tanpa sensor/LED) - ESP32 #2 (RECEIVER)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL 1

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t *mac = info->src_addr;
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  uint32_t counter = 0;
  if (len == sizeof(counter)) memcpy(&counter, data, sizeof(counter));
  Serial.printf("TERIMA #%lu dari %02X:%02X:%02X:%02X:%02X:%02X\n",
                (unsigned long)counter, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER siap. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}
