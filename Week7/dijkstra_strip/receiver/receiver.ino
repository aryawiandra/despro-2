// WEEK 7 - NAVIGASI DIJKSTRA (LED STRIP) - RECEIVER (ESP32 #2)
// Terima status jalur dari sender dan menyalakan LED strip WS2812B:
//   - jalur kena api (blockedMask)        -> MERAH
//   - jalur aman menuju exit (greenMask)  -> HIJAU, dengan kepala terang berjalan searah exit (nyala sekuensial)
//   - jalur lain                          -> mati
// Beberapa jalur bisa hijau bersamaan (rute tercepat dari tiap ruangan ke exit).
// Tidak ada paket 2 detik = link putus (titik biru redup di tiap jalur).
//
// Tiap jalur = 1 strip terpisah (EDGE_LEN[i] LED, sementara 40). Dua mode output (OUTPUT_MODE):
//   1 = SATU PIN PER JALUR (default): 20 jalur -> 20 pin data (EDGE_PIN[]). Strip tidak perlu disambung satu sama lain.
//       Dikirim serentak ke semua pin dengan driver bit-bang paralel di file ini (library Adafruit hanya kuat 8 pin).
//   0 = RANTAI: beberapa strip disambung DOUT -> DIN, tiap rantai 1 pin (CHAINS[]), memakai Adafruit_NeoPixel (maks 8 rantai).
// Arah data tiap strip = dari node pertama ke node kedua nama jalurnya (mis. jalur #1 "j1-j2" mengalir dari j1
// ke j2). Kalau strip jalur itu dipasang kebalikannya, set EDGE_REVERSED[i] = true.
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define OUTPUT_MODE       1      // 1 = satu pin per jalur (20 pin), 0 = rantai strip (CHAINS[])

#if OUTPUT_MODE == 1
#include <soc/soc.h>
#include <soc/gpio_reg.h>
#else
#include <Adafruit_NeoPixel.h>
#endif

#define NUM_EDGES         20
#define MAX_EDGE_LEN      64     // batas LED per jalur (EDGE_LEN tidak boleh melebihi ini)
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

// ---- Mode 1: satu pin data per jalur (lewat resistor 330 ohm ke DIN strip jalur itu) ----
// Indeks = nomor jalur - 1 (lihat 'edges' di sender). Tiap pin harus unik dan pin output yang valid.
// 16 pin pertama bersih. GPIO 15, 2, 12, 0 adalah pin boot (strapping): aman bila DIN strip dibiarkan
// tidak ditarik tinggi saat reset; kalau upload kode gagal, cabut kabel data dari GPIO 0 dan 2 saat upload.
// Jangan pakai GPIO 1 dan 3 (USB serial), 6-11 (flash), 34-39 (hanya input).
const int8_t EDGE_PIN[NUM_EDGES] = {
   4,   // #1  j1-j2
   5,   // #2  j1-j3
  13,   // #3  j2-j3
  14,   // #4  j2-j12
  16,   // #5  j3-j4
  17,   // #6  j4-j5
  18,   // #7  j4-j6
  19,   // #8  j5-j6
  21,   // #9  j6-j7
  22,   // #10 j7-j8
  23,   // #11 j7-j9
  25,   // #12 j8-j9
  26,   // #13 j9-j10
  27,   // #14 j10-j11
  32,   // #15 j10-j12
  33,   // #16 j11-j12
  15,   // #17 j2-e1   (pin boot)
   2,   // #18 j4-e2   (pin boot, LED onboard)
  12,   // #19 j6-e2   (pin boot)
   0    // #20 j12-e3  (pin boot, tombol BOOT)
};

// ---- Mode 0: rantai. Tiap baris = 1 pin + urutan strip (nomor jalur 1..20, tiap jalur tepat 1 kali) ----
#define NUM_CHAINS        5
#define MAX_CHAIN_EDGES   8
struct Chain { int8_t pin; uint8_t count; uint8_t edges[MAX_CHAIN_EDGES]; };
const Chain CHAINS[NUM_CHAINS] = {
  {  4, 4, {  1,  2,  3, 17 } },
  { 13, 4, {  4, 15, 16, 20 } },
  { 14, 4, {  5,  6,  7, 18 } },
  { 16, 4, {  8,  9, 10, 19 } },
  { 17, 4, { 11, 12, 13, 14 } }
};

typedef struct __attribute__((packed)) {
  uint32_t greenMask;     // bit i = jalur i hijau
  uint32_t blockedMask;   // bit i = jalur i merah (api)
  uint32_t revMask;       // bit i = arah menuju exit dari node kedua ke pertama
  uint8_t  depth[10];     // 2 jalur per byte (4 bit): jumlah jalur dari ujung awal jalur ini sampai exit
  uint8_t  flags;         // bit0 = tidak ada rute aman
} RoutePacket;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
RoutePacket shared;                      // diisi callback, dibaca loop (dilindungi mux)
volatile unsigned long lastPacketMs = 0;
bool configOk = false;

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

// ---------- framebuffer: warna 0x00RRGGBB tiap LED tiap jalur ----------
uint32_t fb[NUM_EDGES][MAX_EDGE_LEN];

inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b; }
inline void setPx(int edge, uint16_t k, uint32_t c) { fb[edge][k] = c; }
void clearAll() { memset(fb, 0, sizeof(fb)); }

// ---------- output ----------
#if OUTPUT_MODE == 1
// Driver bit-bang paralel WS2812B: semua pin dikirim bersamaan, bit demi bit (800 kHz, 1,25 us per bit).
//   t=0      : naikkan semua pin yang sedang mengirim data
//   t=400 ns : turunkan pin yang bit-nya 0
//   t=800 ns : turunkan semua pin (bit 1 berlogika tinggi 800 ns)
// Total 40 LED x 24 bit x 1,25 us = 1,2 ms untuk SEMUA jalur sekaligus.
#define MAX_SLOTS (MAX_EDGE_LEN * 24)
uint32_t slotAll0[MAX_SLOTS], slotZero0[MAX_SLOTS];   // pin GPIO 0..31: semua aktif / yang bit-nya 0
uint32_t slotAll1[MAX_SLOTS], slotZero1[MAX_SLOTS];   // pin GPIO 32..39
uint32_t pinMask0[NUM_EDGES], pinMask1[NUM_EDGES];
uint16_t maxLen = 0;

static inline uint32_t ccount() {
#if defined(__XTENSA__)
  uint32_t c;
  __asm__ __volatile__("rsr %0, ccount" : "=a"(c));
  return c;
#else
  static uint32_t fake = 0;
  return fake += 7;                       // hanya untuk tes di PC
#endif
}

void IRAM_ATTR sendParallel(uint32_t nSlots, uint32_t c400, uint32_t c800, uint32_t c1250) {
  noInterrupts();
  uint32_t t = ccount();
  for (uint32_t i = 0; i < nSlots; i++) {
    uint32_t a0 = slotAll0[i], a1 = slotAll1[i], z0 = slotZero0[i], z1 = slotZero1[i];
    while ((int32_t)(ccount() - t) < 0) {}
    REG_WRITE(GPIO_OUT_W1TS_REG, a0);
    REG_WRITE(GPIO_OUT1_W1TS_REG, a1);
    while ((int32_t)(ccount() - (t + c400)) < 0) {}
    REG_WRITE(GPIO_OUT_W1TC_REG, z0);
    REG_WRITE(GPIO_OUT1_W1TC_REG, z1);
    while ((int32_t)(ccount() - (t + c800)) < 0) {}
    REG_WRITE(GPIO_OUT_W1TC_REG, a0);
    REG_WRITE(GPIO_OUT1_W1TC_REG, a1);
    t += c1250;
  }
  interrupts();
}

void outputInit() {
  for (int e = 0; e < NUM_EDGES; e++) {
    pinMode(EDGE_PIN[e], OUTPUT);
    digitalWrite(EDGE_PIN[e], LOW);
    pinMask0[e] = EDGE_PIN[e] < 32 ? (1UL << EDGE_PIN[e]) : 0;
    pinMask1[e] = EDGE_PIN[e] >= 32 ? (1UL << (EDGE_PIN[e] - 32)) : 0;
    if (EDGE_LEN[e] > maxLen) maxLen = EDGE_LEN[e];
  }
}

void outputShow() {
  // Susun bit-slice: slot = (LED ke-n, byte g/r/b, bit 7..0). Urutan warna WS2812B = GRB.
  uint32_t n = 0;
  for (uint16_t k = 0; k < maxLen; k++) {
    for (int comp = 0; comp < 3; comp++) {                 // 0 = G, 1 = R, 2 = B
      int shift = (comp == 0) ? 8 : (comp == 1) ? 16 : 0;
      for (int bit = 7; bit >= 0; bit--, n++) {
        uint32_t a0 = 0, a1 = 0, o0 = 0, o1 = 0;
        for (int e = 0; e < NUM_EDGES; e++) {
          if (k >= EDGE_LEN[e]) continue;                  // strip lebih pendek: pin ini diam
          uint32_t c = fb[e][k];
          uint32_t v = (c >> shift) & 0xFF;
          v = (v * LED_BRIGHTNESS) / 255;
          a0 |= pinMask0[e]; a1 |= pinMask1[e];
          if ((v >> bit) & 1) { o0 |= pinMask0[e]; o1 |= pinMask1[e]; }
        }
        slotAll0[n] = a0; slotAll1[n] = a1;
        slotZero0[n] = a0 & ~o0; slotZero1[n] = a1 & ~o1;
      }
    }
  }
  uint32_t mhz = getCpuFrequencyMhz();
  sendParallel(n, 400UL * mhz / 1000, 800UL * mhz / 1000, 1250UL * mhz / 1000);
  delayMicroseconds(80);                                  // reset/latch WS2812B (> 50 us)
}
#else
Adafruit_NeoPixel *chains[NUM_CHAINS];
int8_t   edgeChain[NUM_EDGES];
uint16_t edgeStart[NUM_EDGES];

void outputInit() {
  int seen[NUM_EDGES] = {0};
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
    if (seen[e] != 1) { Serial.print("CHAINS salah: jalur #"); Serial.print(e + 1); Serial.println(seen[e] ? " muncul lebih dari 1 kali" : " belum ada di rantai mana pun"); configOk = false; }
}

void outputShow() {
  for (int e = 0; e < NUM_EDGES; e++)
    for (uint16_t k = 0; k < EDGE_LEN[e]; k++) chains[edgeChain[e]]->setPixelColor(edgeStart[e] + k, fb[e][k]);
  for (int c = 0; c < NUM_CHAINS; c++) chains[c]->show();
}
#endif

bool checkConfig() {
  bool ok = true;
  for (int e = 0; e < NUM_EDGES; e++) {
    if (EDGE_LEN[e] > MAX_EDGE_LEN || EDGE_LEN[e] == 0) { Serial.print("EDGE_LEN jalur #"); Serial.print(e + 1); Serial.println(" harus 1..MAX_EDGE_LEN"); ok = false; }
#if OUTPUT_MODE == 1
    int p = EDGE_PIN[e];
    if (p < 0 || p > 33 || p == 1 || p == 3 || (p >= 6 && p <= 11)) { Serial.print("EDGE_PIN jalur #"); Serial.print(e + 1); Serial.println(" bukan pin output yang valid"); ok = false; }
    for (int f = 0; f < e; f++)
      if (EDGE_PIN[f] == p) { Serial.print("EDGE_PIN ganda: jalur #"); Serial.print(f + 1); Serial.print(" dan #"); Serial.println(e + 1); ok = false; }
#endif
  }
  return ok;
}

// ---------- tampilan ----------
void showLinkLost() {
  clearAll();
  uint32_t blue = rgb(0, 0, 60);
  for (int i = 0; i < NUM_EDGES; i++)
    for (uint16_t k = 0; k < EDGE_LEN[i]; k += 10) setPx(i, k, blue);
  outputShow();
}

inline uint8_t depthOf(const RoutePacket &pkt, int i) {
  return (pkt.depth[i / 2] >> ((i & 1) ? 4 : 0)) & 0x0F;
}

// Pixel ke-k (0..len-1) dari jalur `idx` dalam urutan perjalanan; rev = dilewati dari node kedua ke pertama.
// Hasil: indeks LED di dalam strip jalur itu (0 = LED paling dekat DIN strip).
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
  outputShow();
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
    Serial.print((pkt.revMask >> i) & 1 ? "< " : "> ");   // > searah data strip, < berlawanan
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  configOk = checkConfig();
  if (configOk) {
    outputInit();            // mode 0 juga memeriksa CHAINS[] dan bisa mengubah configOk
    if (configOk) { clearAll(); outputShow(); }
  }
  if (!configOk) { Serial.println("Perbaiki konfigurasi di receiver.ino lalu upload ulang."); return; }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("RECEIVER strip siap, mode ");
  Serial.print(OUTPUT_MODE == 1 ? "1 pin per jalur (20 pin)" : "rantai");
  Serial.print(". MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  if (!configOk) { delay(1000); return; }   // konfigurasi salah, lihat pesan di Serial
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
