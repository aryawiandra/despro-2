// NODE 1 (SENSOR): baca 20 sensor via 2x CD74HC4067, kirim bitmask ke Node 2 via ESP-NOW
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_EDGES        20
#define ESPNOW_CHANNEL   1
#define SEND_INTERVAL_MS 200

#define PIN_S0           18
#define PIN_S1           19
#define PIN_S2           21
#define PIN_S3           22
#define PIN_SIG_MUX1     34   // input-only, TIDAK punya pull-up internal -> wajib pull-up eksternal 10k ke 3V3
#define PIN_SIG_MUX2     35   // idem

// GANTI dengan MAC Node 2 (hasil sketch cek_mac)
uint8_t receiverMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

typedef struct __attribute__((packed)) {
  uint32_t blockedMask;  // bit i = 1 -> jalur i terblokir api/asap
} SensorPacket;

uint32_t readBlockedMask() {
  uint32_t mask = 0;
  for (byte ch = 0; ch < 16; ch++) {
    digitalWrite(PIN_S0, bitRead(ch, 0));
    digitalWrite(PIN_S1, bitRead(ch, 1));
    digitalWrite(PIN_S2, bitRead(ch, 2));
    digitalWrite(PIN_S3, bitRead(ch, 3));
    delayMicroseconds(30);

    if (digitalRead(PIN_SIG_MUX1) == LOW) mask |= (1UL << ch);
    if (ch < 4 && digitalRead(PIN_SIG_MUX2) == LOW) mask |= (1UL << (16 + ch));
  }
  return mask;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_SIG_MUX1, INPUT);
  pinMode(PIN_SIG_MUX2, INPUT);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Gagal add peer");
  }
  Serial.println("Node sensor siap.");
}

void loop() {
  SensorPacket pkt = { readBlockedMask() };
  esp_err_t res = esp_now_send(receiverMac, (uint8_t *)&pkt, sizeof(pkt));

  Serial.print("mask=0b");
  Serial.print(pkt.blockedMask, BIN);
  Serial.println(res == ESP_OK ? " sent" : " SEND ERROR");

  delay(SEND_INTERVAL_MS);
}
