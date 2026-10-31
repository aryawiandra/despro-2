// WEEK 7 - NAVIGASI DIJKSTRA, UJI 5 STRIP (LED STRIP) - SENDER (ESP32 #1)
// Graf dan Dijkstra tetap peta LENGKAP 20 jalur (graph.h). Strip LED fisik hanya ada di 5 jalur
// (lihat receiver): #3 j2-j3, #11 j7-j9, #15 j10-j12, #18 j4-e2, #19 j6-e2. Sensor api asli juga hanya
// di 5 jalur itu, dibaca dari pin AO analog (SENSOR_PIN[]). INPUT API HANYA DARI SENSOR: simulasi lewat teks
// (block / clear / reset) dimatikan (ALLOW_TEXT_SIM 0); jalur tanpa sensor tidak pernah dianggap kena api.
// Jalur yang kena api dibuang dari graf, Dijkstra dihitung dari
// SETIAP ruangan ke exit terdekat, lalu hasil seluruh peta dikirim ke receiver lewat ESP-NOW:
//   - jalur kena api            -> MERAH BERKEDIP
//   - jalur aman menuju exit    -> HIJAU, dengan nyala sekuensial searah exit
//   - jalur lain                -> mati
// Receiver hanya menyalakan 5 strip yang dimilikinya; jalur lain tetap dihitung tapi tidak ada lampunya.
// Mode default "room all": rute tercepat dari SETIAP ruangan (0-7). "room N" / "start j5" hanya satu ruangan/node.
//
// Perintah (Serial Monitor 115200, line ending "Newline"):
//   room all          -> rute tercepat dari semua ruangan ke exit (default)
//   room <0-7>        -> hanya rute satu ruangan (0,1=j1  2,3=j5  4,5=j8  6,7=j11)
//   start <node>      -> atau langsung pilih node asal, mis. start j5
//   (hanya jika ALLOW_TEXT_SIM 1, untuk uji tanpa sensor:)
//   block <jalur>     -> simulasi api di jalur mana pun, mis. block j6-j7   atau   block 9
//   clear <jalur>     -> padamkan simulasi, mis. clear j6-j7
//   reset             -> hapus semua simulasi teks (sensor asli tidak terpengaruh)
//   edges             -> daftar 20 jalur (nomor, nama, bobot, sensor, status)
//   sensors           -> nilai ADC (AO) 5 sensor, untuk kalibrasi ambang
//   list              -> status + rute sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "graph.h"

#define ESPNOW_CHANNEL    1
// Pengiriman berbasis kejadian: sender TIDAK mengirim terus-menerus. Paket hanya dikirim saat ada perubahan
// (sensor / perintah teks), saat boot, dan saat receiver meminta data (receiver baru menyala).
// ESP-NOW broadcast tidak ada ACK, jadi tiap pengiriman diulang SEND_REPEATS kali berselang SEND_REPEAT_MS.
#define SEND_REPEATS      3
#define SEND_REPEAT_MS    100
#define HEARTBEAT_MS      0     // 0 = tidak ada kirim berkala. >0 = kirim ulang tiap N ms (hanya jika receiver
                                // juga diberi LINK_TIMEOUT_MS > HEARTBEAT_MS untuk deteksi link putus)
#define REQUEST_MAGIC     0xA5  // paket 1 byte dari receiver: "kirim status sekarang"
#define SEND_ERROR_PRINT_MS 5000 // pesan SEND ERROR dibatasi 1x per 5 detik

// ---- Sensor api: hanya 5 jalur yang punya sensor asli. Dibaca lewat pin AO (analog) tiap sensor ----
#define USE_SENSORS         1
#define ALLOW_TEXT_SIM      0     // 0 = api HANYA dari sensor (default). 1 = aktifkan perintah block / clear / reset
#define SENSOR_FIRE_BELOW   1500  // nilai ADC (0-4095) DI BAWAH ini = ada api (nilai dari Week4/kodeUpdated.cpp)
#define SENSOR_CLEAR_ABOVE  1800  // jalur dianggap aman lagi bila nilai naik DI ATAS ini (histeresis anti-kedip)
#define SENSOR_SAMPLES      8     // rata-rata N pembacaan ADC tiap scan (meredam noise)
#define SENSOR_SCAN_MS      50    // periode scan sensor
#define SENSOR_CONFIRM      3     // butuh N scan berturut-turut untuk mengubah status (anti-noise, ~150 ms)

// Pin AO sensor tiap jalur. Indeks = nomor jalur - 1 (lihat 'edges'). -1 = jalur itu tidak punya sensor
// (api di jalur itu hanya bisa disimulasikan lewat teks).
// WAJIB pin ADC1: GPIO 32, 33, 34, 35, 36 (VP), 39 (VN). Pin ADC2 (4, 12-15, 25-27) TIDAK bisa dibaca analog
// saat WiFi/ESP-NOW aktif. Semua pin ini hanya-input atau bebas; tidak perlu pull-up.
const int8_t SENSOR_PIN[NUM_EDGES] = {
  -1,  // #1  j1-j2
  -1,  // #2  j1-j3
  32,  // #3  j2-j3   <- sensor AO, strip 7 LED
  -1,  // #4  j2-j12
  -1,  // #5  j3-j4
  -1,  // #6  j4-j5
  -1,  // #7  j4-j6
  -1,  // #8  j5-j6
  -1,  // #9  j6-j7
  -1,  // #10 j7-j8
  33,  // #11 j7-j9   <- sensor AO, strip 8 LED
  -1,  // #12 j8-j9
  -1,  // #13 j9-j10
  -1,  // #14 j10-j11
  34,  // #15 j10-j12 <- sensor AO, strip 8 LED
  -1,  // #16 j11-j12
  -1,  // #17 j2-e1
  35,  // #18 j4-e2   <- sensor AO, strip 6 LED
  36,  // #19 j6-e2   <- sensor AO (label VP / SVP), strip 5 LED
  -1   // #20 j12-e3
};

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast: tidak perlu MAC receiver

typedef struct __attribute__((packed)) {
  uint32_t greenMask;     // bit i = jalur i hijau (aman, menuju exit)
  uint32_t blockedMask;   // bit i = jalur i kena api (merah)
  uint32_t revMask;       // bit i = arah menuju exit melewati jalur i dari node v ke u
  uint8_t  depth[10];     // 2 jalur per byte (4 bit): jumlah jalur dari ujung awal jalur ini sampai exit
  uint8_t  flags;         // bit0 = tidak ada rute aman sama sekali
} RoutePacket;

#define DEFAULT_SHOW_ALL    1    // 1 = rute tercepat dari semua ruangan hijau; 0 = hanya rute DEFAULT_ROOM
#define DEFAULT_ROOM        0    // ruangan asal bila DEFAULT_SHOW_ALL = 0 (atau setelah 'room N')

bool showAll = DEFAULT_SHOW_ALL;
int startRoom = DEFAULT_ROOM;
int startNode = ROOM_TO_JUNCTION[DEFAULT_ROOM];
uint32_t simMask = 0;      // api dari teks Serial (simulasi)
uint32_t sensorMask = 0;   // api dari sensor asli (sudah di-debounce)
uint32_t blockedMask = 0;  // gabungan keduanya, dipakai Dijkstra
RouteResult route;         // rute satu ruangan (mode 'room N')
SafeMap safe;              // hasil akhir yang dikirim ke receiver
unsigned long lastSendMs = 0;
uint8_t       pendingSends = 0;      // sisa pengiriman ulang
unsigned long nextSendMs = 0;
volatile bool requestFlag = false;   // diisi callback saat receiver meminta data
unsigned long lastScanMs = 0;

// ---------- sensor ----------
static inline bool isAdc1Pin(int pin) { return pin == 32 || pin == 33 || pin == 34 || pin == 35 || pin == 36 || pin == 39; }

void sensorsInit() {
#if USE_SENSORS
  analogReadResolution(12);              // 0..4095
  analogSetAttenuation(ADC_11db);        // rentang 0..~3,3 V
  for (int i = 0; i < NUM_EDGES; i++) {
    int pin = SENSOR_PIN[i];
    if (pin < 0) continue;
    if (!isAdc1Pin(pin)) {
      Serial.print("PERINGATAN: sensor jalur #"); Serial.print(i + 1); Serial.print(" di GPIO "); Serial.print(pin);
      Serial.println(" bukan pin ADC1; pembacaan analog tidak akan jalan saat WiFi aktif. Pakai 32/33/34/35/36/39.");
    }
  }
#endif
}

// Rata-rata beberapa pembacaan ADC (0..4095) pada pin AO
int readAdc(int pin) {
  long sum = 0;
  for (int n = 0; n < SENSOR_SAMPLES; n++) sum += analogRead(pin);
  return (int)(sum / SENSOR_SAMPLES);
}

// Baca sensor, kembalikan bitmask mentah (bit i = sensor jalur i+1 melihat api).
// Histeresis: api terdeteksi bila nilai < SENSOR_FIRE_BELOW; baru dianggap padam bila nilai > SENSOR_CLEAR_ABOVE.
uint32_t readRawSensors() {
  uint32_t raw = 0;
#if USE_SENSORS
  for (int i = 0; i < NUM_EDGES; i++) {
    int pin = SENSOR_PIN[i];
    if (pin < 0) continue;
    int v = readAdc(pin);
    bool wasFire = (sensorMask >> i) & 1;
    bool fire = wasFire ? (v < SENSOR_CLEAR_ABOVE) : (v < SENSOR_FIRE_BELOW);
    if (fire) raw |= (1UL << i);
  }
#endif
  return raw;
}

// Scan + debounce. Return true jika sensorMask berubah.
bool scanSensors() {
  static uint8_t cnt[NUM_EDGES];
  uint32_t raw = readRawSensors();
  bool changed = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    bool now = (raw >> i) & 1, cur = (sensorMask >> i) & 1;
    if (now == cur) { cnt[i] = 0; continue; }
    if (++cnt[i] >= SENSOR_CONFIRM) {
      sensorMask ^= (1UL << i);
      cnt[i] = 0;
      changed = true;
    }
  }
  return changed;
}

void sendRoute() {
  RoutePacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.greenMask = safe.greenMask;
  pkt.blockedMask = blockedMask;
  pkt.revMask = safe.revMask;
  for (int i = 0; i < NUM_EDGES; i++) pkt.depth[i / 2] |= (safe.depth[i] & 0x0F) << ((i & 1) ? 4 : 0);
  pkt.flags = (safe.numGreen == 0) ? 1 : 0;
  esp_err_t res = esp_now_send(broadcastMac, (uint8_t *)&pkt, sizeof(pkt));
  static unsigned long lastErrMs = 0;
  if (res != ESP_OK && (lastErrMs == 0 || millis() - lastErrMs >= SEND_ERROR_PRINT_MS)) {
    lastErrMs = millis();
    Serial.print("SEND ERROR (kode "); Serial.print((int)res); Serial.println(")");
  }
}

// Jadwalkan pengiriman (diulang SEND_REPEATS kali). Dipanggil saat ada perubahan, bukan terus-menerus.
void requestSend() {
  pendingSends = SEND_REPEATS;
  nextSendMs = 0;
}

// Callback ESP-NOW. Argumen pertama berbeda antara Arduino-ESP32 core 2.x (const uint8_t *mac) dan 3.x
// (const esp_now_recv_info_t *), dan isinya tidak dipakai di sini. Jadi diterima sebagai const void* lalu
// didaftarkan dengan cast. Sengaja TANPA #if di sekitar fungsi: PlatformIO membuat deklarasi untuk kedua
// versi dan tipe milik core 3.x tidak ada di core 2.x.
void onDataRecv(const void *infoOrMac, const uint8_t *data, int len) {
  if (len == 1 && data[0] == REQUEST_MAGIC) requestFlag = true;
}

void printEdgeName(int i) {
  Serial.print(NODE_NAMES[EDGES[i].u]);
  Serial.print("-");
  Serial.print(NODE_NAMES[EDGES[i].v]);
}

void printNodeList(uint16_t mask) {
  bool any = false;
  for (int n = 0; n < 12; n++)
    if ((mask >> n) & 1) { if (any) Serial.print(", "); Serial.print(NODE_NAMES[n]); any = true; }
  if (!any) Serial.print("-");
}

// Rute tercepat tiap ruangan ke exit terdekatnya (berdasarkan bobot jarak), termasuk bobot penghubung ruangan
void printRooms() {
  for (int r = 0; r < 8; r++) {
    Serial.print("  Ruang "); Serial.print(r); Serial.print(": ");
    int start = ROOM_TO_JUNCTION[r];
    if (safe.roomExit[r] < 0) { Serial.println("TERPUTUS dari semua exit"); continue; }
    for (int n = start; ; n = safe.next[n]) {
      Serial.print(NODE_NAMES[n]);
      if (safe.next[n] < 0) break;
      Serial.print(" > ");
    }
    Serial.print("  (bobot "); Serial.print(safe.roomWeight[r]); Serial.println(")");
  }
}

void printRoute() {
  Serial.print("Mode: ");
  if (showAll) Serial.print("semua ruangan");
  else {
    Serial.print("rute ");
    if (startRoom >= 0) { Serial.print("ruang "); Serial.print(startRoom); Serial.print(" ("); }
    Serial.print(NODE_NAMES[startNode]);
    if (startRoom >= 0) Serial.print(")");
  }
  Serial.print(" | MERAH (api): ");
  bool any = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((blockedMask >> i) & 1) { if (any) Serial.print(", "); printEdgeName(i); any = true; }
  }
  if (!any) Serial.print("-");
  Serial.println();

  if (safe.numGreen == 0) {
    Serial.println("[BAHAYA] Tidak ada rute aman ke exit");
    return;
  }
  Serial.print("HIJAU (menuju exit): ");
  any = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    if (!((safe.greenMask >> i) & 1)) continue;
    bool rev = (safe.revMask >> i) & 1;
    if (any) Serial.print(", ");
    Serial.print(NODE_NAMES[rev ? EDGES[i].v : EDGES[i].u]);
    Serial.print(">");
    Serial.print(NODE_NAMES[rev ? EDGES[i].u : EDGES[i].v]);
    any = true;
  }
  Serial.println();
  if (showAll) printRooms();
  if (showAll && safe.isolatedMask) {
    Serial.print("Ruangan di junction ini TERPUTUS dari exit: ");
    printNodeList(safe.isolatedMask);
    Serial.println();
  }
  if (!showAll && route.found) {
    Serial.print("Rute -> "); Serial.print(NODE_NAMES[route.exitNode]);
    Serial.print(" (bobot "); Serial.print(route.totalWeight); Serial.println(")");
  }
}

void printEdges() {
  for (int i = 0; i < NUM_EDGES; i++) {
    Serial.print("#"); Serial.print(i + 1); Serial.print("  ");
    printEdgeName(i);
    Serial.print("  bobot "); Serial.print(EDGES[i].weight);
    if (SENSOR_PIN[i] >= 0) { Serial.print("  [sensor GPIO "); Serial.print(SENSOR_PIN[i]); Serial.print("]"); }
    if ((sensorMask >> i) & 1) Serial.print("  API(sensor)");
    if ((simMask >> i) & 1) Serial.print("  API(teks)");
    if ((safe.greenMask >> i) & 1) Serial.print("  HIJAU");
    Serial.println();
  }
}

void printHelp() {
#if ALLOW_TEXT_SIM
  Serial.println("Perintah: room all | room <0-7> | start <node> | block <jalur> | clear <jalur> | reset | edges | sensors | list | help");
  Serial.println("Jalur: nama 'j1-j2' atau nomor 1-20 (lihat 'edges')");
#else
  Serial.println("Perintah: room all | room <0-7> | start <node> | edges | sensors | list | help");
  Serial.println("Input api hanya dari sensor AO (simulasi teks dimatikan, ALLOW_TEXT_SIM 0)");
#endif
}

// "j1-j2" (urutan bebas) atau "1".."20" -> indeks jalur, -1 jika tidak valid
int parseEdge(String s) {
  s.trim();
  int dash = s.indexOf('-');
  if (dash > 0) {
    int a = nodeIndex(s.substring(0, dash).c_str());
    int b = nodeIndex(s.substring(dash + 1).c_str());
    if (a >= 0 && b >= 0) return findEdgeIndex(a, b);
    return -1;
  }
  int n = s.toInt();
  if (n >= 1 && n <= NUM_EDGES) return n - 1;
  return -1;
}

void printSensors() {
#if USE_SENSORS
  for (int i = 0; i < NUM_EDGES; i++) {
    if (SENSOR_PIN[i] < 0) continue;
    int v = readAdc(SENSOR_PIN[i]);
    Serial.print("#"); Serial.print(i + 1); Serial.print(" ");
    printEdgeName(i);
    Serial.print(" AO GPIO "); Serial.print(SENSOR_PIN[i]);
    Serial.print(" nilai="); Serial.print(v);
    Serial.print(" (api bila < "); Serial.print(SENSOR_FIRE_BELOW);
    Serial.print(") terkonfirmasi="); Serial.println((sensorMask >> i) & 1 ? "API" : "aman");
  }
#else
  Serial.println("USE_SENSORS = 0, sensor tidak dibaca");
#endif
}

void compute() {
  blockedMask = simMask | sensorMask;
  if (showAll) {
    safe = computeSafeForest(blockedMask);
  } else {
    route = computeRoute(startNode, blockedMask);
    safe = routeToSafeMap(route);
  }
}

void recompute() {
  compute();
  printRoute();
  requestSend();
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();

  if (cmd == "room" || cmd == "r") {
    if (arg == "all") {
      showAll = true;
    } else {
      int n = arg.toInt();
      if (arg.length() == 0 || n < 0 || n > 7) { Serial.println("Pakai: room all | room 0-7"); return; }
      showAll = false;
      startRoom = n;
      startNode = ROOM_TO_JUNCTION[n];
    }
  } else if (cmd == "start" || cmd == "s") {
    int n = nodeIndex(arg.c_str());
    if (n < 0 || n >= 12) { Serial.println("Node asal harus j1..j12"); return; }
    showAll = false;
    startRoom = -1;
    startNode = n;
#if !ALLOW_TEXT_SIM
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c" || cmd == "reset" || cmd == "0") {
    Serial.println("Simulasi api lewat teks dimatikan: api hanya dari sensor AO. (Set ALLOW_TEXT_SIM 1 di sender.ino untuk mengaktifkan.)");
    return;
#else
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    int e = parseEdge(arg);
    if (e < 0) { Serial.println("Jalur tidak dikenal. Contoh: block j1-j2  atau  block 1  (lihat 'edges')"); return; }
    if (cmd == "block" || cmd == "b") simMask |= (1UL << e);
    else                              simMask &= ~(1UL << e);
  } else if (cmd == "reset" || cmd == "0") {
    simMask = 0;
#endif
  } else if (cmd == "sensors") {
    printSensors();
    return;
  } else if (cmd == "edges" || cmd == "e") {
    printEdges();
    return;
  } else if (cmd == "list" || cmd == "l") {
    printRoute();
    return;
  } else if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  } else {
    Serial.println("Perintah tidak dikenal. Ketik 'help'.");
    return;
  }

  recompute();
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Gagal add peer");
  }

  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(onDataRecv));

  sensorsInit();

  Serial.print("SENDER Dijkstra siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  compute();
  printRoute();
  requestSend();            // kirim status awal (diulang beberapa kali), setelah itu diam sampai ada perubahan
}

void loop() {
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  if (millis() - lastScanMs >= SENSOR_SCAN_MS) {
    lastScanMs = millis();
    if (scanSensors()) {
      Serial.print("[SENSOR] api di: ");
      bool any = false;
      for (int i = 0; i < NUM_EDGES; i++)
        if ((sensorMask >> i) & 1) { if (any) Serial.print(", "); printEdgeName(i); any = true; }
      if (!any) Serial.print("-");
      Serial.println();
      recompute();
    }
  }

  if (requestFlag) { requestFlag = false; requestSend(); }

  if (pendingSends > 0 && millis() >= nextSendMs) {   // pengiriman ulang bergantian, bukan loop terus-menerus
    sendRoute();
    pendingSends--;
    nextSendMs = millis() + SEND_REPEAT_MS;
  }

#if HEARTBEAT_MS > 0
  if (millis() - lastSendMs >= HEARTBEAT_MS) {
    lastSendMs = millis();
    sendRoute();
  }
#endif
}
