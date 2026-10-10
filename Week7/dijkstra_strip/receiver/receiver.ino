// WEEK 7 - NAVIGASI DIJKSTRA (LED STRIP) - RECEIVER (ESP32 #2)
// Terima status jalur dari sender dan menyalakan LED strip WS2812B:
//   - jalur kena api (blockedMask)        -> MERAH
//   - jalur aman menuju exit (greenMask)  -> HIJAU, dengan kepala terang berjalan searah exit (nyala sekuensial)
//   - jalur lain                          -> mati
// Beberapa jalur bisa hijau bersamaan (semua jalan aman yang menuju exit).
// Tidak ada paket 2 detik = link putus (titik biru redup di tiap jalur).
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
#define LED_DATA_PIN      5      // pin data strip (lewat resistor 330 ohm)
#define LED_BRIGHTNESS    60     // 0-255, batasi arus strip
#define ESPNOW_CHANNEL    1      // harus sama dengan sender
#define LINK_TIMEOUT_MS   2000

#define ANIMATE           1      // 1 = kepala terang berjalan searah rute, 0 = hijau diam
#define CHASE_LEN         8      // panjang kepala terang (LED)
#define CHASE_STEP_MS     12     // makin kecil makin cepat
#define CHASE_GAP         20     // jeda LED sebelum animasi mengulang
#define CHASE_UNIT        40     // panjang 1 jalur nominal (LED) untuk menyelaraskan fase animasi antar jalur
#define SHOW_BLOCKED_RED  1      // 1 = jalur kena api merah, 0 = mati

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
  uint32_t greenMask;     // bit i = jalur i hijau
  uint32_t blockedMask;   // bit i = jalur i merah (api)
  uint32_t revMask;       // bit i = arah menuju exit dari node kedua ke pertama
  uint8_t  depth[10];     // 2 jalur per byte (4 bit): jumlah jalur dari ujung awal jalur ini sampai exit
  uint8_t  flags;         // bit0 = tidak ada rute aman
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

inline uint8_t depthOf(const RoutePacket &pkt, int i) {
  return (pkt.depth[i / 2] >> ((i & 1) ? 4 : 0)) & 0x0F;
}

// Pixel ke-k (0..len-1) dari jalur `idx` dalam urutan perjalanan; rev = dilewati dari node kedua ke pertama
inline uint16_t travelPixel(uint8_t idx, bool rev, uint16_t k) {
  bool backwardInChain = (rev != EDGE_REVERSED[idx]);
  return edgeStart[idx] + (backwardInChain ? (EDGE_LEN[idx] - 1 - k) : k);
}

void render(const RoutePacket &pkt, uint32_t frame) {
  strip->clear();

#if SHOW_BLOCKED_RED
  uint32_t red = strip->Color(255, 0, 0);
  for (int i = 0; i < NUM_EDGES; i++)
    if ((pkt.blockedMask >> i) & 1)
      for (uint16_t k = 0; k < EDGE_LEN[i]; k++) strip->setPixelColor(edgeStart[i] + k, red);
#endif

  uint32_t baseGreen = strip->Color(0, ANIMATE ? 70 : 255, 0);
  uint32_t headColor = strip->Color(120, 255, 120);

  // Fase animasi: jalur yang lebih jauh dari exit (depth besar) mulai lebih dulu, sehingga kepala terang
  // mengalir menyusuri jalur berurutan sampai exit. Posisi global q = (depthMax - depth) * CHASE_UNIT + k.
  uint8_t depthMax = 1;
  for (int i = 0; i < NUM_EDGES; i++)
    if (((pkt.greenMask >> i) & 1) && depthOf(pkt, i) > depthMax) depthMax = depthOf(pkt, i);
  uint32_t period = (uint32_t)depthMax * CHASE_UNIT + CHASE_GAP;
  int32_t head = (int32_t)(frame % period);

  for (int i = 0; i < NUM_EDGES; i++) {
    if (!((pkt.greenMask >> i) & 1)) continue;
    bool rev = (pkt.revMask >> i) & 1;
    uint32_t q0 = (uint32_t)(depthMax - depthOf(pkt, i)) * CHASE_UNIT;
    for (uint16_t k = 0; k < EDGE_LEN[i]; k++) {
      uint32_t color = baseGreen;
#if ANIMATE
      int32_t d = head - (int32_t)(q0 + k);
      if (d >= 0 && d < CHASE_LEN) color = headColor;
#endif
      strip->setPixelColor(travelPixel(i, rev, k), color);
    }
  }
  strip->show();
}

void printRoute(const RoutePacket &pkt) {
  Serial.print("MERAH (api) jalur #: ");
  for (int i = 0; i < NUM_EDGES; i++) if ((pkt.blockedMask >> i) & 1) { Serial.print(i + 1); Serial.print(" "); }
  Serial.println();
  if (pkt.flags & 1) { Serial.println("[BAHAYA] Tidak ada rute aman, tidak ada jalur hijau"); return; }
  Serial.print("HIJAU jalur #: ");
  for (int i = 0; i < NUM_EDGES; i++) {
    if (!((pkt.greenMask >> i) & 1)) continue;
    Serial.print(i + 1);
    Serial.print((pkt.revMask >> i) & 1 ? "< " : "> ");   // > searah rantai, < berlawanan
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
  bool animate = ANIMATE && pkt.greenMask != 0;
  if (changed || !lastLink || (animate && millis() - lastFrameMs >= CHASE_STEP_MS)) {
    lastFrameMs = millis();
    render(pkt, frame++);
  }
  lastLink = true;
  delay(2);
}
