// NODE 1 (SENSOR): baca 20 sensor via 2x CD74HC4067, kirim bitmask ke Node 2 via ESP-NOW
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_EDGES        20
#define ESPNOW_CHANNEL   1
#define SEND_INTERVAL_MS 200

// 1 = mode tes komunikasi (tanpa sensor): kirim pola mask yang berganti tiap TEST_STEP_MS.
//     LED merah di receiver akan "berjalan" dari jalur 0 sampai 19, lalu semua hijau/mati (mask=0).
// 0 = mode normal: baca sensor sungguhan lewat mux.
#define TEST_MODE        1
#define TEST_STEP_MS     1000

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

// Pola tes: langkah 0 = tidak ada yang terblokir, langkah 1..20 = jalur (langkah-1) terblokir
uint32_t testMask() {
  uint32_t step = (millis() / TEST_STEP_MS) % (NUM_EDGES + 1);
  return step == 0 ? 0 : (1UL << (step - 1));
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
  Serial.print("MAC sender ini: ");
  Serial.println(WiFi.macAddress());
  if (receiverMac[0] == 0xAA && receiverMac[1] == 0xBB) {
    Serial.println("PERINGATAN: receiverMac masih placeholder, ganti dengan MAC receiver!");
  }
  Serial.println(TEST_MODE ? "Node sensor siap (MODE TES, tanpa sensor)." : "Node sensor siap.");
}

void loop() {
  #if TEST_MODE
  SensorPacket pkt = { testMask() };
#else
  SensorPacket pkt = { readBlockedMask() };
#endif
  esp_err_t res = esp_now_send(receiverMac, (uint8_t *)&pkt, sizeof(pkt));

  Serial.print("mask=0b");
  Serial.print(pkt.blockedMask, BIN);
  Serial.println(res == ESP_OK ? " sent" : " SEND ERROR");

  delay(SEND_INTERVAL_MS);
}
