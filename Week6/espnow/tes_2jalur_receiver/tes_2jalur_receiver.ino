// TES 2 JALUR - RECEIVER (ESP32 #2)
// Terima status 2 jalur dari sender. LED hijau menyala pada jalur yang AMAN, mati pada jalur terblokir.
// Tanpa input di sender = kedua LED menyala. Tidak ada paket 2 detik = kedua LED biru (link putus).
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define LED_PIN          5      // pin data LED (WS2812B), LED jalur 1 = pixel 0, jalur 2 = pixel 1
#define NUM_PATHS        2
#define LED_BRIGHTNESS   60
#define ESPNOW_CHANNEL   1      // harus sama dengan sender
#define LINK_TIMEOUT_MS  2000

typedef struct __attribute__((packed)) {
  uint8_t blockedMask;  // bit0 = jalur 1 terblokir, bit1 = jalur 2 terblokir
} PathPacket;

volatile uint8_t blockedMask = 0;
volatile unsigned long lastPacketMs = 0;

Adafruit_NeoPixel strip(NUM_PATHS, LED_PIN, NEO_GRB + NEO_KHZ800);

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
  for (int i = 0; i < NUM_PATHS; i++) strip.setPixelColor(i, strip.Color(0, 0, 40));
  strip.show();
  Serial.println("[LINK PUTUS] Tidak ada data dari sender");
}

// Hijau = jalur aman, mati = jalur terblokir
void showPaths(uint8_t mask) {
  strip.clear();
  for (int i = 0; i < NUM_PATHS; i++) {
    bool blocked = (mask >> i) & 1;
    if (!blocked) strip.setPixelColor(i, strip.Color(0, 255, 0));
  }
  strip.show();

  Serial.print("Jalur 1: ");
  Serial.print((mask & 1) ? "TERBLOKIR (LED mati)" : "aman (LED nyala)");
  Serial.print(" | Jalur 2: ");
  Serial.println((mask & 2) ? "TERBLOKIR (LED mati)" : "aman (LED nyala)");
  if ((mask & 3) == 3) Serial.println("[BAHAYA] Kedua jalur terblokir");
}

void setup() {
  Serial.begin(115200);

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.clear();
  strip.show();

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
