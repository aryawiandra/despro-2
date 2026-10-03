// TES 2 JALUR - RECEIVER (ESP32 #2)
// Terima status 2 jalur dari sender. Strip hijau menyala pada jalur yang AMAN, mati pada jalur terblokir.
// Tanpa input di sender = kedua strip menyala. Tidak ada paket 2 detik = kedua strip biru (link putus).
// Tiap jalur = 1 strip LED dengan pin data sendiri.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define LED_PIN_1        5      // pin data strip jalur 1
#define LED_PIN_2        18     // pin data strip jalur 2
#define LEDS_PER_STRIP   10     // ISI sesuai jumlah LED sebenarnya di tiap strip
#define NUM_PATHS        2
#define LED_BRIGHTNESS   60
#define ESPNOW_CHANNEL   1      // harus sama dengan sender
#define LINK_TIMEOUT_MS  2000

typedef struct __attribute__((packed)) {
  uint8_t blockedMask;  // bit0 = jalur 1 terblokir, bit1 = jalur 2 terblokir
} PathPacket;

volatile uint8_t blockedMask = 0;
volatile unsigned long lastPacketMs = 0;

Adafruit_NeoPixel strip1(LEDS_PER_STRIP, LED_PIN_1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(LEDS_PER_STRIP, LED_PIN_2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel *strips[NUM_PATHS] = { &strip1, &strip2 };  // strips[0] = jalur 1, strips[1] = jalur 2

// Signature callback berbeda antara Arduino-ESP32 core 3.x dan 2.x
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(PathPacket)) return;
  PathPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  blockedMask = pkt.blockedMask & 0x03;
  lastPacketMs = millis();
}

void showLinkLost() {
  for (int i = 0; i < NUM_PATHS; i++) {
    strips[i]->fill(strips[i]->Color(0, 0, 40));
    strips[i]->show();
  }
  Serial.println("[LINK PUTUS] Tidak ada data dari sender");
}

// Hijau = jalur aman, mati = jalur terblokir
void showPaths(uint8_t mask) {
  for (int i = 0; i < NUM_PATHS; i++) {
    bool blocked = (mask >> i) & 1;
    strips[i]->clear();
    if (!blocked) strips[i]->fill(strips[i]->Color(0, 255, 0));
    strips[i]->show();
  }

  Serial.print("Jalur 1: ");
  Serial.print((mask & 1) ? "TERBLOKIR (strip mati)" : "aman (strip nyala)");
  Serial.print(" | Jalur 2: ");
  Serial.println((mask & 2) ? "TERBLOKIR (strip mati)" : "aman (strip nyala)");
  if ((mask & 3) == 3) Serial.println("[BAHAYA] Kedua jalur terblokir");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_PATHS; i++) {
    strips[i]->begin();
    strips[i]->setBrightness(LED_BRIGHTNESS);
    strips[i]->clear();
    strips[i]->show();
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER 2 jalur siap. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  static bool lastLink = true;
  static uint8_t lastMask = 0xFF;  // nilai mustahil, memaksa tampilan pertama

  bool linkUp = lastPacketMs != 0 && (millis() - lastPacketMs <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) showLinkLost();
    lastLink = false;
    delay(50);
    return;
  }

  uint8_t mask = blockedMask;
  if (!lastLink || mask != lastMask) {
    if (!lastLink) Serial.println("[LINK TERSAMBUNG]");
    lastLink = true;
    lastMask = mask;
    showPaths(mask);
  }
  delay(20);
}
