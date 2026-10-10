// WEEK 7 - NAVIGASI DIJKSTRA 5 JALUR (LED STRIP) - RECEIVER (ESP32 #2)
// 5 strip WS2812B, masing-masing 1 pin data sendiri (STRIPS[]). Terima hasil Dijkstra seluruh peta dari
// sender dan hanya menampilkan jalur yang punya strip di sini:
//   - jalur kena api (blockedMask)        -> MERAH BERKEDIP
//   - jalur aman menuju exit (greenMask)  -> HIJAU, nyala sekuensial searah exit (kepala terang berjalan)
//   - jalur lain                          -> mati
// Tidak ada paket 2 detik = link putus (titik biru redup di tiap strip).
//
// Nomor jalur di STRIPS[] adalah nomor jalur di graf 20 jalur (lihat 'edges' di sender). Strip boleh berbeda panjang.
// Arah data strip = dari node pertama ke node kedua nama jalurnya (mis. #3 "j2-j3" mengalir dari j2 ke j3).
// Kalau strip dipasang kebalikannya, set reversed = true.
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_STRIPS        5
#define LED_BRIGHTNESS    60     // 0-255, batasi arus strip
#define ESPNOW_CHANNEL    1      // harus sama dengan sender
#define LINK_TIMEOUT_MS   0      // 0 = tanpa deteksi link putus (sender tidak mengirim berkala). Jika >0, isi lebih
                                 // besar dari HEARTBEAT_MS di sender (mis. HEARTBEAT_MS 1000, LINK_TIMEOUT_MS 3500)
#define REQUEST_MAGIC     0xA5   // paket 1 byte ke sender: "kirim status sekarang"
#define REQUEST_MS        1500   // minta data tiap N ms, hanya selama belum ada data dari sender

#define ANIMATE           1      // 1 = kepala terang berjalan searah rute, 0 = hijau diam
#define CHASE_LEN         3      // panjang kepala terang (LED); strip pendek, jadi kecil
#define CHASE_STEP_MS     70     // makin kecil makin cepat
#define CHASE_GAP         4      // jeda LED sebelum animasi mengulang
#define CHASE_UNIT        8      // panjang 1 jalur nominal (LED) untuk menyelaraskan fase animasi antar jalur
#define BLINK_MS          400    // lama merah menyala / padam saat berkedip

// Satu baris = satu strip: nomor jalur, pin data (lewat resistor 330 ohm ke DIN), jumlah LED, arah terbalik?
// Pin aman untuk output: 4, 5, 13, 14, 16-19, 21-23, 25-27, 32, 33.
struct StripCfg { uint8_t edge; int8_t pin; uint16_t len; bool reversed; };
const StripCfg STRIPS[NUM_STRIPS] = {
  {  3,  4, 7, false },   // #3  j2-j3  -> GPIO 4,  7 LED
  { 11,  5, 8, false },   // #11 j7-j9  -> GPIO 5,  8 LED
  { 15, 13, 8, false },   // #15 j10-j12 -> GPIO 13, 8 LED
  { 18, 14, 6, false },   // #18 j4-e2  -> GPIO 14, 6 LED
  { 19, 16, 5, false }    // #19 j6-e2  -> GPIO 16, 5 LED
};

typedef struct __attribute__((packed)) {
  uint32_t greenMask;     // bit i = jalur i+1 hijau
  uint32_t blockedMask;   // bit i = jalur i+1 merah (api)
  uint32_t revMask;       // bit i = arah menuju exit dari node kedua ke pertama
  uint8_t  depth[10];     // 2 jalur per byte (4 bit): jumlah jalur dari ujung awal jalur ini sampai exit
  uint8_t  flags;         // bit0 = tidak ada rute aman
} RoutePacket;

Adafruit_NeoPixel *strips[NUM_STRIPS];
bool configOk = false;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
RoutePacket shared;                      // diisi callback, dibaca loop (dilindungi mux)
volatile unsigned long lastPacketMs = 0;
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Callback ESP-NOW. Argumen pertama berbeda antara Arduino-ESP32 core 2.x (const uint8_t *mac) dan 3.x
// (const esp_now_recv_info_t *), dan isinya tidak dipakai di sini. Jadi diterima sebagai const void* lalu
// didaftarkan dengan cast. Sengaja TANPA #if di sekitar fungsi: PlatformIO membuat deklarasi untuk kedua
// versi dan tipe milik core 3.x tidak ada di core 2.x.
void onDataRecv(const void *infoOrMac, const uint8_t *data, int len) {
  if (len != sizeof(RoutePacket)) return;
  portENTER_CRITICAL(&mux);
  memcpy(&shared, data, sizeof(shared));
  lastPacketMs = millis();
  portEXIT_CRITICAL(&mux);
}

// ---------- helper ----------
inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return Adafruit_NeoPixel::Color(r, g, b); }
inline uint8_t depthOf(const RoutePacket &pkt, int edgeIdx) {
  return (pkt.depth[edgeIdx / 2] >> ((edgeIdx & 1) ? 4 : 0)) & 0x0F;
}
void clearAll() { for (int s = 0; s < NUM_STRIPS; s++) strips[s]->clear(); }
void showAll()  { for (int s = 0; s < NUM_STRIPS; s++) strips[s]->show(); }

// Pixel ke-k (0..len-1) dalam urutan perjalanan; rev = dilewati dari node kedua ke pertama
inline uint16_t travelPixel(const StripCfg &c, bool rev, uint16_t k) {
  bool backward = (rev != c.reversed);
  return backward ? (c.len - 1 - k) : k;
}

bool checkConfig() {
  bool ok = true;
  for (int s = 0; s < NUM_STRIPS; s++) {
    const StripCfg &c = STRIPS[s];
    if (c.edge < 1 || c.edge > 20) { Serial.print("STRIPS salah: nomor jalur "); Serial.println(c.edge); ok = false; }
    if (c.pin < 0 || c.pin > 33 || c.pin == 1 || c.pin == 3 || (c.pin >= 6 && c.pin <= 11)) { Serial.print("STRIPS salah: pin tidak valid untuk jalur #"); Serial.println(c.edge); ok = false; }
    if (c.len == 0) { Serial.print("STRIPS salah: len 0 untuk jalur #"); Serial.println(c.edge); ok = false; }
    for (int t = 0; t < s; t++) {
      if (STRIPS[t].edge == c.edge) { Serial.print("STRIPS salah: jalur #"); Serial.print(c.edge); Serial.println(" ganda"); ok = false; }
      if (STRIPS[t].pin == c.pin) { Serial.print("STRIPS salah: pin "); Serial.print(c.pin); Serial.println(" dipakai dua strip"); ok = false; }
    }
  }
  return ok;
}

// ---------- tampilan ----------
void showLinkLost() {
  clearAll();
  uint32_t blue = rgb(0, 0, 60);
  for (int s = 0; s < NUM_STRIPS; s++)
    for (uint16_t k = 0; k < STRIPS[s].len; k += 10) strips[s]->setPixelColor(k, blue);
  showAll();
}

void render(const RoutePacket &pkt, uint32_t frame, bool blinkOn) {
  clearAll();

  // Merah berkedip: nyala / padam bergantian tiap BLINK_MS
  if (blinkOn) {
    for (int s = 0; s < NUM_STRIPS; s++) {
      int e = STRIPS[s].edge - 1;
      if ((pkt.blockedMask >> e) & 1)
        for (uint16_t k = 0; k < STRIPS[s].len; k++) strips[s]->setPixelColor(k, rgb(255, 0, 0));
    }
  }

  uint32_t baseGreen = rgb(0, ANIMATE ? 70 : 255, 0);
  uint32_t headColor = rgb(120, 255, 120);

  // Fase animasi: jalur yang lebih jauh dari exit (depth besar) mulai lebih dulu, sehingga kepala terang
  // mengalir menyusuri jalur berurutan sampai exit. Posisi global q = (depthMax - depth) * CHASE_UNIT + k.
  uint8_t depthMax = 1;
  for (int s = 0; s < NUM_STRIPS; s++) {
    int e = STRIPS[s].edge - 1;
    if (((pkt.greenMask >> e) & 1) && depthOf(pkt, e) > depthMax) depthMax = depthOf(pkt, e);
  }
  uint32_t period = (uint32_t)depthMax * CHASE_UNIT + CHASE_GAP;
  int32_t head = (int32_t)(frame % period);

  for (int s = 0; s < NUM_STRIPS; s++) {
    const StripCfg &c = STRIPS[s];
    int e = c.edge - 1;
    if (!((pkt.greenMask >> e) & 1)) continue;
    bool rev = (pkt.revMask >> e) & 1;
    uint32_t q0 = (uint32_t)(depthMax - depthOf(pkt, e)) * CHASE_UNIT;
    for (uint16_t k = 0; k < c.len; k++) {
      uint32_t color = baseGreen;
#if ANIMATE
      int32_t d = head - (int32_t)(q0 + k);
      if (d >= 0 && d < CHASE_LEN) color = headColor;
#endif
      strips[s]->setPixelColor(travelPixel(c, rev, k), color);
    }
  }
  showAll();
}

void printRoute(const RoutePacket &pkt) {
  Serial.print("MERAH berkedip, jalur #: ");
  for (int s = 0; s < NUM_STRIPS; s++) if ((pkt.blockedMask >> (STRIPS[s].edge - 1)) & 1) { Serial.print(STRIPS[s].edge); Serial.print(" "); }
  Serial.println();
  if (pkt.flags & 1) { Serial.println("[BAHAYA] Tidak ada rute aman, tidak ada jalur hijau"); return; }
  Serial.print("HIJAU jalur #: ");
  for (int s = 0; s < NUM_STRIPS; s++) {
    int e = STRIPS[s].edge - 1;
    if (!((pkt.greenMask >> e) & 1)) continue;
    Serial.print(STRIPS[s].edge);
    Serial.print((pkt.revMask >> e) & 1 ? "< " : "> ");   // > searah data strip, < berlawanan
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  configOk = checkConfig();
  if (!configOk) { Serial.println("Perbaiki STRIPS[] di receiver.ino lalu upload ulang."); return; }

  for (int s = 0; s < NUM_STRIPS; s++) {
    strips[s] = new Adafruit_NeoPixel(STRIPS[s].len, STRIPS[s].pin, NEO_GRB + NEO_KHZ800);
    strips[s]->begin();
    strips[s]->setBrightness(LED_BRIGHTNESS);
    strips[s]->clear();
    strips[s]->show();
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(onDataRecv));

  esp_now_peer_info_t peer = {};                  // peer broadcast, hanya untuk mengirim permintaan data
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) Serial.println("Gagal add peer");

  Serial.print("RECEIVER 5 jalur siap. MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  if (!configOk) { delay(1000); return; }   // STRIPS[] salah, lihat pesan di Serial
  static bool lastLink = true;
  static RoutePacket lastPkt;
  static bool havePkt = false;
  static unsigned long lastFrameMs = 0;
  static uint32_t frame = 0;
  static bool lastBlink = true;

  RoutePacket pkt;
  unsigned long lastRx;
  portENTER_CRITICAL(&mux);
  memcpy(&pkt, &shared, sizeof(pkt));
  lastRx = lastPacketMs;
  portEXIT_CRITICAL(&mux);

  static unsigned long lastRequestMs = 0;
  static bool waitingPrinted = false;
  bool linkUp = lastRx != 0 && (LINK_TIMEOUT_MS == 0 || millis() - lastRx <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) {
      Serial.println(lastRx == 0 ? "Menunggu data dari sender..." : "[LINK PUTUS] Tidak ada data dari sender");
      showLinkLost();
    }
    lastLink = false;
    if (lastRx == 0 && (!waitingPrinted || millis() - lastRequestMs >= REQUEST_MS)) {   // minta status, bukan menunggu pasif
      waitingPrinted = true;
      lastRequestMs = millis();
      uint8_t req = REQUEST_MAGIC;
      esp_now_send(broadcastMac, &req, 1);
    }
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

  // Render saat ada perubahan, saat kedip berganti, atau tiap CHASE_STEP_MS bila animasi aktif
  bool animate = ANIMATE && pkt.greenMask != 0;
  bool blinkOn = ((millis() / BLINK_MS) % 2) == 0;
  bool blinkChanged = pkt.blockedMask != 0 && blinkOn != lastBlink;
  lastBlink = blinkOn;
  if (changed || !lastLink || blinkChanged || (animate && millis() - lastFrameMs >= CHASE_STEP_MS)) {
    lastFrameMs = millis();
    render(pkt, frame++, blinkOn);
  }
  lastLink = true;
  delay(2);
}
