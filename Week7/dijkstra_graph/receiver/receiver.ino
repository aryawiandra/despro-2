// WEEK 7 - NAVIGASI DIJKSTRA - RECEIVER (ESP32 #2)
// Terima rute evakuasi dari sender (bitmask 20 jalur) dan menyalakan jalur rute itu HIJAU.
// Jalur lain mati. Tidak ada paket 2 detik = link putus (semua jalur kedip / biru).
//
// Dua mode output (pilih USE_ADDRESSABLE):
//   0 = LED biasa  : 1 pin GPIO per jalur (tabel EDGE_PIN). Satu pin menyalakan 1 grup LED hijau
//                    (asumsi 40 LED per jalur) lewat transistor/MOSFET. Jalur dengan pin -1 diabaikan,
//                    jadi bisa dites dengan beberapa LED saja di breadboard.
//   1 = Strip addressable (WS2812B) : semua jalur disambung 1 rantai di LED_DATA_PIN,
//                    tiap jalur = EDGE_LEN[i] LED (default 40). Urutan rantai = nomor jalur 1..20.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define USE_ADDRESSABLE  0
#define NUM_EDGES        20
#define ESPNOW_CHANNEL   1      // harus sama dengan sender
#define LINK_TIMEOUT_MS  2000
#define BLINK_MS         500    // periode kedip saat link putus

// ---- Mode 0: LED biasa ----
// Indeks = nomor jalur - 1 (lihat 'edges' di sender). -1 = tidak ada LED untuk jalur itu.
// Default: hanya jalur #1 (j1-j2) di GPIO5 dan jalur #2 (j1-j3) di GPIO18, cukup untuk demo
// 2 LED: "room 0" menyalakan #2; setelah "block j1-j3" rute pindah ke #1.
const int8_t EDGE_PIN[NUM_EDGES] = {
   5, 18, -1, -1, -1,
  -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1
};

// ---- Mode 1: strip addressable ----
#define LED_DATA_PIN     5
#define LED_BRIGHTNESS   60
#define SHOW_BLOCKED_RED 0      // 1 = jalur kena api merah, 0 = mati
// Panjang tiap jalur beda-beda; sementara diasumsikan semua 40 LED. Ubah per jalur bila perlu.
const uint16_t EDGE_LEN[NUM_EDGES] = {
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40
};

#if USE_ADDRESSABLE
#include <Adafruit_NeoPixel.h>
uint16_t edgeStart[NUM_EDGES];
uint16_t totalLeds = 0;
Adafruit_NeoPixel *strip = nullptr;
#endif

typedef struct __attribute__((packed)) {
  uint32_t pathMask;
  uint32_t blockedMask;
  uint8_t  flags;        // bit0 = tidak ada rute aman
} RoutePacket;

volatile uint32_t pathMask = 0;
volatile uint32_t blockedMask = 0;
volatile uint8_t routeFlags = 0;
volatile unsigned long lastPacketMs = 0;

// Signature callback berbeda antara Arduino-ESP32 core 3.x dan 2.x
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(RoutePacket)) return;
  RoutePacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  pathMask = pkt.pathMask;
  blockedMask = pkt.blockedMask;
  routeFlags = pkt.flags;
  lastPacketMs = millis();
}

// ---------- output ----------
void outputInit() {
#if USE_ADDRESSABLE
  for (int i = 0; i < NUM_EDGES; i++) { edgeStart[i] = totalLeds; totalLeds += EDGE_LEN[i]; }
  strip = new Adafruit_NeoPixel(totalLeds, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->setBrightness(LED_BRIGHTNESS);
  strip->clear();
  strip->show();
#else
  for (int i = 0; i < NUM_EDGES; i++) {
    if (EDGE_PIN[i] >= 0) { pinMode(EDGE_PIN[i], OUTPUT); digitalWrite(EDGE_PIN[i], LOW); }
  }
#endif
}

void showLinkLost() {
#if USE_ADDRESSABLE
  strip->fill(strip->Color(0, 0, 40));
  strip->show();
#else
  bool on = (millis() / BLINK_MS) % 2 == 0;
  for (int i = 0; i < NUM_EDGES; i++)
    if (EDGE_PIN[i] >= 0) digitalWrite(EDGE_PIN[i], on ? HIGH : LOW);
#endif
}

void showRoute(uint32_t path, uint32_t blocked) {
#if USE_ADDRESSABLE
  strip->clear();
  for (int i = 0; i < NUM_EDGES; i++) {
    uint32_t color = 0;
    if ((path >> i) & 1) color = strip->Color(0, 255, 0);
#if SHOW_BLOCKED_RED
    else if ((blocked >> i) & 1) color = strip->Color(255, 0, 0);
#endif
    if (color) for (uint16_t k = 0; k < EDGE_LEN[i]; k++) strip->setPixelColor(edgeStart[i] + k, color);
  }
  strip->show();
#else
  for (int i = 0; i < NUM_EDGES; i++)
    if (EDGE_PIN[i] >= 0) digitalWrite(EDGE_PIN[i], ((path >> i) & 1) ? HIGH : LOW);
#endif
}

void printRoute(uint32_t path, uint8_t flags) {
  if (flags & 1) { Serial.println("[BAHAYA] Tidak ada rute aman, semua jalur mati"); return; }
  Serial.print("Jalur hijau #: ");
  for (int i = 0; i < NUM_EDGES; i++) if ((path >> i) & 1) { Serial.print(i + 1); Serial.print(" "); }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  outputInit();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER Dijkstra siap (mode ");
  Serial.print(USE_ADDRESSABLE ? "strip addressable" : "LED biasa");
  Serial.print("). MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  static bool lastLink = true;
  static uint32_t lastPath = 0xFFFFFFFF;  // nilai mustahil (bit 20..31 tak pernah dipakai), memaksa tampilan pertama
  static uint32_t lastBlocked = 0xFFFFFFFF;

  bool linkUp = lastPacketMs != 0 && (millis() - lastPacketMs <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) Serial.println("[LINK PUTUS] Tidak ada data dari sender");
    lastLink = false;
    showLinkLost();
    delay(20);
    return;
  }

  uint32_t path = pathMask, blocked = blockedMask;
  uint8_t flags = routeFlags;
  if (!lastLink || path != lastPath || blocked != lastBlocked) {
    if (!lastLink) Serial.println("[LINK TERSAMBUNG]");
    lastLink = true;
    lastPath = path;
    lastBlocked = blocked;
    showRoute(path, blocked);
    printRoute(path, flags);
  }
  delay(20);
}
