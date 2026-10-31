// RECEIVER ESP32 - 20 JALUR LED WS2812B
// Menerima RoutePacket dari sender Dijkstra dengan nomor edge yang sama.
//
// Warna jalur:
//   Hijau = jalur masuk rute Dijkstra dan tidak diblokir.
//   Merah = jalur tidak dipakai atau jalur diblokir.
//
// Sebelum paket pertama diterima, semua jalur merah.
// LINK_TIMEOUT_MS = 0 mempertahankan status paket terakhir.
//
// Setiap strip memakai catu 5 V eksternal yang sesuai.
// Satukan GND PSU LED dengan GND ESP32.
// Jangan menyambungkan daya LED dari pin 5 V/USB ESP32.

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#define NUM_STRIPS       20
#define LED_BRIGHTNESS   60
#define ESPNOW_CHANNEL   1
#define LINK_TIMEOUT_MS  0
#define REQUEST_MAGIC    0xA5
#define REQUEST_MS       1500

// Nomor edge harus sama dengan edges[] pada sender:
// #1 j1-j2,   #2 j1-j3,   #3 j2-j3,   #4 j2-j12,  #5 j2-e1
// #6 j3-j4,   #7 j4-j5,   #8 j4-j6,    #9 j5-j6,  #10 j6-j7
// #11 j7-j9,  #12 j7-j8,  #13 j8-j9,  #14 j9-j10, #15 j10-j12
// #16 j10-j11, #17 j11-j12, #18 j4-e2, #19 j6-e2, #20 j12-e3
//
// GPIO1 dan GPIO3 adalah UART USB dan dipakai sebagai output LED.
// Lepaskan kabel data strip dari GPIO1/GPIO3 saat mengunggah kode,
// lalu sambungkan lagi setelah upload selesai.
//
// GPIO0, GPIO2, GPIO5, GPIO12, dan GPIO15 adalah pin strapping saat boot.
// GPIO12 khususnya dapat mengganggu proses boot jika tertarik HIGH saat reset.

struct StripCfg {
  uint8_t edge;
  int8_t pin;
  uint16_t len;
  bool reversed;
};

const StripCfg STRIPS[NUM_STRIPS] = {
  {  9,  1, 35, false }, // j5-j6   : GPIO1 (TX0), 35 LED
  {  1,  3, 35, false }, // j1-j2   : GPIO3 (RX0), 35 LED
  { 16, 21, 35, false }, // j10-j11 : GPIO21, 35 LED
  { 13, 22, 35, false }, // j8-j9   : GPIO22, 35 LED
  {  7, 23, 11, false }, // j4-j5   : GPIO23, 11 LED
  {  2, 25, 12, false }, // j1-j3   : GPIO25, 12 LED
  {  6, 26, 11, false }, // j3-j4   : GPIO26, 11 LED
  { 18, 13,  6, false }, // j4-e2   : GPIO13, 6 LED
  { 19, 14,  5, false }, // j6-e2   : GPIO14, 5 LED
  { 10, 27, 10, false }, // j6-j7   : GPIO27, 10 LED
  {  3,  5,  7, false }, // j2-j3   : GPIO5, 7 LED
  {  4, 32, 11, false }, // j2-j12  : GPIO32, 11 LED
  { 14, 33, 10, false }, // j9-j10  : GPIO33, 10 LED
  { 15, 18,  8, false }, // j10-j12 : GPIO18, 8 LED
  { 11,  4,  8, false }, // j7-j9   : GPIO4, 8 LED
  { 12, 19, 11, false }, // j7-j8   : GPIO19, 11 LED
  { 17,  2, 10, false }, // j11-j12 : GPIO2, 10 LED
  {  8, 16, 10, false }, // j4-j6   : RX2/GPIO16, 10 LED
  { 20, 12,  4, false }, // j12-e3  : GPIO12, 4 LED
  {  5, 15,  4, false }  // j2-e1   : GPIO15, 4 LED
};

// Harus sama persis dengan RoutePacket di sender.
typedef struct __attribute__((packed)) {
  uint32_t greenMask;
  uint32_t blockedMask;
  uint32_t revMask;
  uint8_t depth[10];
  uint8_t flags;
} RoutePacket;

Adafruit_NeoPixel *strips[NUM_STRIPS];

bool configOk = false;
bool havePacket = false;
RoutePacket sharedPacket = {};

portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t lastPacketMs = 0;

const uint8_t broadcastMac[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

uint32_t colorGreen() {
  return Adafruit_NeoPixel::Color(0, 255, 0);
}

uint32_t colorRed() {
  return Adafruit_NeoPixel::Color(255, 0, 0);
}

// Callback saat paket dari sender diterima.
void onDataRecv(const void *infoOrMac, const uint8_t *data, int len) {
  if (len != sizeof(RoutePacket)) {
    return;
  }

  portENTER_CRITICAL(&packetMux);
  memcpy(&sharedPacket, data, sizeof(RoutePacket));
  lastPacketMs = millis();
  havePacket = true;
  portEXIT_CRITICAL(&packetMux);
}

// Memeriksa nomor edge, jumlah LED, dan pin yang dipakai.
bool checkConfig() {
  bool ok = true;

  for (int i = 0; i < NUM_STRIPS; i++) {
    const StripCfg &s = STRIPS[i];

    if (s.edge < 1 || s.edge > 20 ||
        s.len == 0 ||
        s.pin < 0 || s.pin > 33) {
      ok = false;
    }

    // GPIO6 sampai GPIO11 terhubung ke flash pada ESP32 klasik.
    if (s.pin >= 6 && s.pin <= 11) {
      ok = false;
    }

    for (int j = 0; j < i; j++) {
      // Tidak boleh ada edge yang didaftarkan dua kali.
      if (STRIPS[j].edge == s.edge) {
        ok = false;
      }

      // Setiap strip harus memiliki pin data sendiri.
      if (STRIPS[j].pin == s.pin) {
        ok = false;
      }
    }
  }

  return ok;
}

// Menyalakan semua jalur merah.
void showAllRed() {
  for (int s = 0; s < NUM_STRIPS; s++) {
    for (uint16_t p = 0; p < STRIPS[s].len; p++) {
      strips[s]->setPixelColor(p, colorRed());
    }
  }

  for (int s = 0; s < NUM_STRIPS; s++) {
    strips[s]->show();
  }
}

// Hijau jika edge dipakai rute dan tidak diblokir.
// Selain itu merah.
void render(const RoutePacket &pkt) {
  for (int s = 0; s < NUM_STRIPS; s++) {
    const StripCfg &cfg = STRIPS[s];
    uint32_t bit = 1UL << (cfg.edge - 1);

    bool isBlocked = (pkt.blockedMask & bit) != 0;
    bool isOnRoute = (pkt.greenMask & bit) != 0;

    uint32_t color =
      (isOnRoute && !isBlocked) ? colorGreen() : colorRed();

    for (uint16_t p = 0; p < cfg.len; p++) {
      strips[s]->setPixelColor(p, color);
    }
  }

  for (int s = 0; s < NUM_STRIPS; s++) {
    strips[s]->show();
  }
}

void setup() {
  delay(300);

  configOk = checkConfig();

  if (!configOk) {
    return;
  }

  // Siapkan setiap strip LED.
  for (int s = 0; s < NUM_STRIPS; s++) {
    strips[s] = new Adafruit_NeoPixel(
      STRIPS[s].len,
      STRIPS[s].pin,
      NEO_GRB + NEO_KHZ800
    );

    strips[s]->begin();
    strips[s]->setBrightness(LED_BRIGHTNESS);
    strips[s]->clear();
  }

  // Kondisi awal: semua jalur merah sampai menerima data sender.
  showAllRed();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    return;
  }

  esp_now_register_recv_cb(
    reinterpret_cast<esp_now_recv_cb_t>(onDataRecv)
  );

  // Tambahkan alamat broadcast agar receiver dapat meminta paket ulang.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;

  esp_now_add_peer(&peer);
}

void loop() {
  if (!configOk) {
    delay(1000);
    return;
  }

  static uint32_t lastRenderedPacketMs = 0;
  static uint32_t lastRequestMs = 0;

  RoutePacket pkt;
  uint32_t rxMs;
  bool packetAvailable;

  // Salin data yang diterima dengan aman dari callback.
  portENTER_CRITICAL(&packetMux);
  pkt = sharedPacket;
  rxMs = lastPacketMs;
  packetAvailable = havePacket;
  portEXIT_CRITICAL(&packetMux);

  uint32_t now = millis();

  bool linkUp =
    packetAvailable &&
    (LINK_TIMEOUT_MS == 0 ||
     (uint32_t)(now - rxMs) <= LINK_TIMEOUT_MS);

  if (!linkUp) {
    showAllRed();

    // Minta sender mengirim ulang paket.
    if (now - lastRequestMs >= REQUEST_MS) {
      lastRequestMs = now;
      uint8_t request = REQUEST_MAGIC;
      esp_now_send(broadcastMac, &request, 1);
    }

    delay(50);
    return;
  }

  // Perbarui LED hanya saat menerima paket baru.
  if (rxMs != lastRenderedPacketMs) {
    lastRenderedPacketMs = rxMs;
    render(pkt);
  }

  delay(5);
}