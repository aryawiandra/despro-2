// WEEK 7 - NAVIGASI DIJKSTRA (LED STRIP) - RECEIVER (ESP32 #2)
// Terima status jalur dari sender dan menyalakan LED strip WS2812B:
//   - jalur kena api (blockedMask)        -> MERAH
//   - jalur aman menuju exit (greenMask)  -> HIJAU, dengan kepala terang berjalan searah exit (nyala sekuensial)
//   - jalur lain                          -> mati
// Beberapa jalur bisa hijau bersamaan (semua jalan aman yang menuju exit).
// Tidak ada paket 2 detik = link putus (titik biru redup di tiap jalur).
//
// Susunan strip: tiap jalur = 1 strip terpisah (EDGE_LEN[i] LED, sementara semua 40). Data WS2812B hanya
// mengalir lewat DIN -> DOUT, jadi strip yang tidak disambung butuh pin sendiri. Karena pin ESP32 terbatas,
// beberapa strip digabung dalam 1 "rantai" (DOUT strip pertama disambung kabel ke DIN strip kedua, dst),
// lalu tiap rantai diberi 1 pin data. Atur di CHAINS[] di bawah. Warna tiap LED ditentukan dari posisinya
// di rantai, bukan dari pin.
// Arah data tiap strip = dari node pertama ke node kedua nama jalurnya (mis. jalur #1 "j1-j2" mengalir
// dari j1 ke j2). Kalau strip jalur itu dipasang kebalikannya, set EDGE_REVERSED[i] = true.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_EDGES         20
#define LED_BRIGHTNESS    60     // 0-255, batasi arus strip
#define ESPNOW_CHANNEL    1      // harus sama dengan sender
#define LINK_TIMEOUT_MS   2000

#define ANIMATE           1      // 1 = kepala terang berjalan searah rute, 0 = hijau diam
#define CHASE_LEN         8      // panjang kepala terang (LED)
#define CHASE_STEP_MS     12     // makin kecil makin cepat
#define CHASE_GAP         20     // jeda LED sebelum animasi mengulang
#define CHASE_UNIT        40     // panjang 1 jalur nominal (LED) untuk menyelaraskan fase animasi antar jalur
#define SHOW_BLOCKED_RED  1      // 1 = jalur kena api merah, 0 = mati

// ---- Rantai strip ----
// Tiap baris = 1 pin data ESP32 (lewat resistor 330 ohm) + urutan strip di rantai itu.
// Isi dengan NOMOR JALUR 1..20 (lihat 'edges' di sender). Urutan = urutan fisik: pin -> DIN strip pertama,
// DOUT strip pertama -> DIN strip kedua, dst. Setiap jalur harus muncul tepat 1 kali.
// Maksimal 8 rantai (ESP32 punya 8 kanal RMT untuk WS2812). Pin aman: 4, 5, 13, 14, 16-19, 21-23, 25-27, 32, 33.
#define NUM_CHAINS        5
#define MAX_CHAIN_EDGES   8
struct Chain { int8_t pin; uint8_t count; uint8_t edges[MAX_CHAIN_EDGES]; };
const Chain CHAINS[NUM_CHAINS] = {
  {  4, 4, {  1,  2,  3, 17 } },   // j1-j2, j1-j3, j2-j3, j2-e1
  { 13, 4, {  4, 15, 16, 20 } },   // j2-j12, j10-j12, j11-j12, j12-e3
  { 14, 4, {  5,  6,  7, 18 } },   // j3-j4, j4-j5, j4-j6, j4-e2
  { 16, 4, {  8,  9, 10, 19 } },   // j5-j6, j6-j7, j7-j8, j6-e2
  { 17, 4, { 11, 12, 13, 14 } }    // j7-j9, j8-j9, j9-j10, j10-j11
};

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

Adafruit_NeoPixel *chains[NUM_CHAINS];
int8_t   edgeChain[NUM_EDGES];     // jalur i ada di rantai ke berapa
uint16_t edgeStart[NUM_EDGES];     // LED pertama jalur i di dalam rantainya
bool     configOk = false;

inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return Adafruit_NeoPixel::Color(r, g, b); }
inline void setPx(int edge, uint16_t k, uint32_t c) { chains[edgeChain[edge]]->setPixelColor(edgeStart[edge] + k, c); }
void clearAll() { for (int c = 0; c < NUM_CHAINS; c++) chains[c]->clear(); }
void showAll()  { for (int c = 0; c < NUM_CHAINS; c++) chains[c]->show(); }

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
  clearAll();
  uint32_t blue = rgb(0, 0, 60);
  for (int i = 0; i < NUM_EDGES; i++)
    for (uint16_t k = 0; k < EDGE_LEN[i]; k += 10) setPx(i, k, blue);
  showAll();
}

inline uint8_t depthOf(const RoutePacket &pkt, int i) {
  return (pkt.depth[i / 2] >> ((i & 1) ? 4 : 0)) & 0x0F;
}

// Pixel ke-k (0..len-1) dari jalur `idx` dalam urutan perjalanan; rev = dilewati dari node kedua ke pertama.
// Hasil: indeks LED di dalam jalur itu (0 = LED paling dekat DIN strip).
inline uint16_t travelPixel(uint8_t idx, bool rev, uint16_t k) {
  bool backwardInChain = (rev != EDGE_REVERSED[idx]);
  return backwardInChain ? (EDGE_LEN[idx] - 1 - k) : k;
}

void render(const RoutePacket &pkt, uint32_t frame) {
  clearAll();

#if SHOW_BLOCKED_RED
  uint32_t red = rgb(255, 0, 0);
  for (int i = 0; i < NUM_EDGES; i++)
    if ((pkt.blockedMask >> i) & 1)
      for (uint16_t k = 0; k < EDGE_LEN[i]; k++) setPx(i, k, red);
#endif

  uint32_t baseGreen = rgb(0, ANIMATE ? 70 : 255, 0);
  uint32_t headColor = rgb(120, 255, 120);

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
      setPx(i, travelPixel(i, rev, k), color);
    }
  }
  showAll();
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

  // Bangun rantai dari CHAINS[] dan periksa tiap jalur muncul tepat 1 kali
  int seen[NUM_EDGES] = {0};
  configOk = true;
  for (int c = 0; c < NUM_CHAINS; c++) {
    uint16_t len = 0;
    for (int n = 0; n < CHAINS[c].count; n++) {
      int e = CHAINS[c].edges[n] - 1;
      if (e < 0 || e >= NUM_EDGES) { Serial.print("CHAINS salah: nomor jalur tidak valid di rantai "); Serial.println(c); configOk = false; continue; }
      seen[e]++;
      edgeChain[e] = c;
      edgeStart[e] = len;
      len += EDGE_LEN[e];
    }
    chains[c] = new Adafruit_NeoPixel(len, CHAINS[c].pin, NEO_GRB + NEO_KHZ800);
    chains[c]->begin();
    chains[c]->setBrightness(LED_BRIGHTNESS);
    chains[c]->clear();
    chains[c]->show();
  }
  for (int e = 0; e < NUM_EDGES; e++)
    if (seen[e] != 1) { Serial.print("CHAINS salah: jalur #"); Serial.print(e + 1); Serial.println(seen[e] ? " muncul lebih dari 1 kali" : " belum dipasang di rantai mana pun"); configOk = false; }
  if (!configOk) { Serial.println("Perbaiki CHAINS[] di receiver.ino lalu upload ulang."); return; }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER strip siap, ");
  Serial.print(NUM_CHAINS);
  Serial.print(" rantai. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  if (!configOk) { delay(1000); return; }   // CHAINS[] salah, lihat pesan di Serial
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
