// WEEK 7 - 2 JALUR LED BIASA - RECEIVER (ESP32 #2)
// Pengganti LED strip (gagal) dengan LED biasa. Logic sama dengan Week6/tes_2jalur_receiver:
//   - Tidak ada input apa pun di sender  -> kedua LED jalur NYALA
//   - Sender mengirim jalur terblokir api -> LED jalur tsb MATI, jalur lain tetap nyala
//   - Tidak ada paket 2 detik (link putus) -> kedua LED berkedip bersamaan
// Tiap jalur = 1 LED biasa (anoda -> resistor -> pin GPIO, katoda -> GND).
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define LED_PIN_1        5      // LED jalur 1 (GPIO5 -> R 220 ohm -> anoda LED -> katoda ke GND)
#define LED_PIN_2        18     // LED jalur 2 (GPIO18 -> R 220 ohm -> anoda LED -> katoda ke GND)
#define NUM_PATHS        2
#define ESPNOW_CHANNEL   1      // harus sama dengan sender
#define LINK_TIMEOUT_MS  2000
#define BLINK_MS         500    // periode kedip saat link putus

typedef struct __attribute__((packed)) {
  uint8_t blockedMask;  // bit0 = jalur 1 terblokir, bit1 = jalur 2 terblokir
} PathPacket;

const uint8_t ledPins[NUM_PATHS] = { LED_PIN_1, LED_PIN_2 };

volatile uint8_t blockedMask = 0;
volatile unsigned long lastPacketMs = 0;

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

// Link putus: kedua LED berkedip bersamaan
void showLinkLost() {
  bool on = (millis() / BLINK_MS) % 2 == 0;
  for (int i = 0; i < NUM_PATHS; i++) digitalWrite(ledPins[i], on ? HIGH : LOW);
}

// LED nyala = jalur aman, LED mati = jalur terblokir
void showPaths(uint8_t mask) {
  for (int i = 0; i < NUM_PATHS; i++) {
    bool blocked = (mask >> i) & 1;
    digitalWrite(ledPins[i], blocked ? LOW : HIGH);
  }

  Serial.print("Jalur 1: ");
  Serial.print((mask & 1) ? "TERBLOKIR (LED mati)" : "aman (LED nyala)");
  Serial.print(" | Jalur 2: ");
  Serial.println((mask & 2) ? "TERBLOKIR (LED mati)" : "aman (LED nyala)");
  if ((mask & 3) == 3) Serial.println("[BAHAYA] Kedua jalur terblokir");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_PATHS; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER 2 jalur (LED biasa) siap. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  static bool lastLink = true;
  static uint8_t lastMask = 0xFF;  // nilai mustahil, memaksa tampilan pertama

  bool linkUp = lastPacketMs != 0 && (millis() - lastPacketMs <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) Serial.println("[LINK PUTUS] Tidak ada data dari sender");
    lastLink = false;
    showLinkLost();
    delay(20);
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
