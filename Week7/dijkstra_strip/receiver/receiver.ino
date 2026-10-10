// WEEK 7 - NAVIGASI DIJKSTRA (LED STRIP) - RECEIVER (ESP32 #2)
// Terima rute evakuasi dari sender (urutan jalur dari ruangan ke exit + arahnya) dan menyalakan
// LED strip WS2812B: jalur rute HIJAU dengan kepala terang yang berjalan searah rute (nyala sekuensial).
// Jalur lain mati. Tidak ada paket 2 detik = link putus (titik biru redup di tiap jalur).
//
// Susunan strip: 20 jalur disambung dalam 1 rantai di LED_DATA_PIN, urutan = nomor jalur 1..20
// (lihat 'edges' di sender). Tiap jalur panjangnya EDGE_LEN[i] LED (sementara semua 40).
// Arah rantai tiap jalur = dari node pertama ke node kedua nama jalur (mis. jalur #1 "j1-j2" mengalir
// dari j1 ke j2). Kalau strip jalur itu terpasang kebalikannya, set EDGE_REVERSED[i] = true.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_EDGES         20
#define MAX_ROUTE_EDGES   15
#define LED_DATA_PIN      5      // pin data strip (lewat resistor 330 ohm)
#define LED_BRIGHTNESS    60     // 0-255, batasi arus strip
#define ESPNOW_CHANNEL    1      // harus sama dengan sender
#define LINK_TIMEOUT_MS   2000

#define ANIMATE           1      // 1 = kepala terang berjalan searah rute, 0 = hijau diam
#define CHASE_LEN         8      // panjang kepala terang (LED)
#define CHASE_STEP_MS     12     // makin kecil makin cepat
#define CHASE_GAP         20     // jeda LED sebelum animasi mengulang
#define SHOW_BLOCKED_RED  0      // 1 = jalur kena api merah, 0 = mati

// Panjang tiap jalur beda-beda; sementara diasumsikan semua 40 LED. Ubah per jalur bila perlu.
const uint16_t EDGE_LEN[NUM_EDGES] = {
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40,
  40, 40, 40, 40, 40
};
// true = strip jalur itu terpasang dari node kedua ke node pertama
const bool EDGE_REVERSED[NUM_EDGES] = {
  false, false, false, false, false,
  false, false, false, false, false,
  false, false, false, false, false,
  false, false, false, false, false
};

typedef struct __attribute__((packed)) {
  uint8_t  count;
  uint8_t  seq[MAX_ROUTE_EDGES];   // bit0-6 = indeks jalur, bit7 = dilewati dari node kedua ke pertama
  uint32_t blockedMask;
  uint8_t  flags;                  // bit0 = tidak ada rute aman
} RoutePacket;

uint16_t edgeStart[NUM_EDGES];
uint16_t totalLeds = 0;
Adafruit_NeoPixel *strip = nullptr;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
RoutePacket shared;                      // diisi callback, dibaca loop (dilindungi mux)
volatile unsigned long lastPacketMs = 0;

// Signature callback berbeda antara Arduino-ESP32 core 3.x dan 2.x
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(RoutePacket)) return;
  portENTER_CRITICAL(&mux);
  memcpy(&shared, data, sizeof(shared));
  lastPacketMs = millis();
  portEXIT_CRITICAL(&mux);
}

// ---------- tampilan ----------
void showLinkLost() {
  strip->clear();
  uint32_t blue = strip->Color(0, 0, 60);
  for (int i = 0; i < NUM_EDGES; i++)
    for (uint16_t k = 0; k < EDGE_LEN[i]; k += 10) strip->setPixelColor(edgeStart[i] + k, blue);
  strip->show();
}

// Pixel ke-k (0..len-1) dari jalur `idx` dalam urutan perjalanan; rev = dilewati dari node kedua ke pertama
inline uint16_t travelPixel(uint8_t idx, bool rev, uint16_t k) {
  bool backwardInChain = (rev != EDGE_REVERSED[idx]);
  return edgeStart[idx] + (backwardInChain ? (EDGE_LEN[idx] - 1 - k) : k);
}

void render(const RoutePacket &pkt, uint32_t frame) {
  strip->clear();

#if SHOW_BLOCKED_RED
  for (int i = 0; i < NUM_EDGES; i++)
    if ((pkt.blockedMask >> i) & 1)
      for (uint16_t k = 0; k < EDGE_LEN[i]; k++) strip->setPixelColor(edgeStart[i] + k, strip->Color(255, 0, 0));
#endif

  uint32_t baseGreen = strip->Color(0, ANIMATE ? 70 : 255, 0);
  uint32_t headColor = strip->Color(120, 255, 120);

  uint32_t total = 0;
  for (int e = 0; e < pkt.count && e < MAX_ROUTE_EDGES; e++) total += EDGE_LEN[pkt.seq[e] & 0x7F];
  int32_t head = total ? (int32_t)(frame % (total + CHASE_GAP)) : 0;

  uint32_t q = 0;  // posisi di sepanjang rute
  for (int e = 0; e < pkt.count && e < MAX_ROUTE_EDGES; e++) {
    uint8_t idx = pkt.seq[e] & 0x7F;
    bool rev = pkt.seq[e] & 0x80;
    if (idx >= NUM_EDGES) continue;
    for (uint16_t k = 0; k < EDGE_LEN[idx]; k++, q++) {
      uint32_t color = baseGreen;
#if ANIMATE
      int32_t d = head - (int32_t)q;
      if (d >= 0 && d < CHASE_LEN) color = headColor;
#endif
      strip->setPixelColor(travelPixel(idx, rev, k), color);
    }
  }
  strip->show();
}

void printRoute(const RoutePacket &pkt) {
  if (pkt.flags & 1) { Serial.println("[BAHAYA] Tidak ada rute aman, semua jalur mati"); return; }
  Serial.print("Rute (jalur # berurutan): ");
  for (int e = 0; e < pkt.count && e < MAX_ROUTE_EDGES; e++) {
    Serial.print((pkt.seq[e] & 0x7F) + 1);
    Serial.print((pkt.seq[e] & 0x80) ? "< " : "> ");   // > searah rantai, < berlawanan
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_EDGES; i++) { edgeStart[i] = totalLeds; totalLeds += EDGE_LEN[i]; }
  strip = new Adafruit_NeoPixel(totalLeds, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->setBrightness(LED_BRIGHTNESS);
  strip->clear();
  strip->show();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER strip siap, total LED: ");
  Serial.print(totalLeds);
  Serial.print(". MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  static bool lastLink = true;
  static RoutePacket lastPkt;
  static bool havePkt = false;
  static unsigned long lastFrameMs = 0;
  static uint32_t frame = 0;

  RoutePacket pkt;
  unsigned long lastRx;
  portENTER_CRITICAL(&mux);
  memcpy(&pkt, &shared, sizeof(pkt));
  lastRx = lastPacketMs;
  portEXIT_CRITICAL(&mux);

  bool linkUp = lastRx != 0 && (millis() - lastRx <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) { Serial.println("[LINK PUTUS] Tidak ada data dari sender"); showLinkLost(); }
    lastLink = false;
    delay(50);
    return;
  }

  bool changed = !havePkt || memcmp(&pkt, &lastPkt, sizeof(pkt)) != 0;
  if (!lastLink) Serial.println("[LINK TERSAMBUNG]");
  if (changed || !lastLink) {
    lastPkt = pkt;
    havePkt = true;
    frame = 0;
    printRoute(pkt);
  }

  // Render saat ada perubahan, atau tiap CHASE_STEP_MS bila animasi aktif
  bool animate = ANIMATE && pkt.count > 0;
  if (changed || !lastLink || (animate && millis() - lastFrameMs >= CHASE_STEP_MS)) {
    lastFrameMs = millis();
    render(pkt, frame++);
  }
  lastLink = true;
  delay(2);
}
