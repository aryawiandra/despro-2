// WEEK 7 - NAVIGASI DIJKSTRA, UJI 5 STRIP (LED STRIP) - SENDER (ESP32 #1)
// Graf dan Dijkstra tetap peta LENGKAP 20 jalur (graph.h). Strip LED fisik hanya ada di 5 jalur
// (lihat receiver): #3 j2-j3, #11 j7-j9, #15 j10-j12, #18 j4-e2, #19 j6-e2. Sensor api asli juga hanya
// di 5 jalur itu (SENSOR_PIN[]); api di jalur lain disimulasikan lewat teks di Serial Monitor.
// Hasil sensor dan teks digabung (OR). Jalur yang kena api dibuang dari graf, Dijkstra dihitung dari
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
//   block <jalur>     -> simulasi api di jalur mana pun, mis. block j6-j7   atau   block 9
//   clear <jalur>     -> padamkan simulasi, mis. clear j6-j7
//   reset             -> hapus semua simulasi teks (sensor asli tidak terpengaruh)
//   edges             -> daftar 20 jalur (nomor, nama, bobot, sensor, status)
//   sensors           -> bacaan mentah 5 sensor
//   list              -> status + rute sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "graph.h"

#define ESPNOW_CHANNEL    1
#define SEND_INTERVAL_MS  200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

// ---- Sensor api: hanya 5 jalur yang punya sensor asli, tiap sensor langsung ke 1 pin GPIO ----
#define USE_SENSORS         1
#define SENSOR_ACTIVE_LEVEL LOW  // level output sensor saat ada api (modul flame sensor umumnya LOW)
#define SENSOR_SCAN_MS      50   // periode scan sensor
#define SENSOR_CONFIRM      3    // butuh N scan berturut-turut untuk mengubah status (anti-noise, ~150 ms)

// Pin DO sensor tiap jalur. Indeks = nomor jalur - 1 (lihat 'edges'). -1 = jalur itu tidak punya sensor
// (api di jalur itu hanya bisa disimulasikan lewat teks). Pakai pin dengan pull-up internal
// (4, 5, 13, 14, 16-19, 21-23, 25-27, 32, 33).
const int8_t SENSOR_PIN[NUM_EDGES] = {
  -1,  // #1  j1-j2
  -1,  // #2  j1-j3
  32,  // #3  j2-j3   <- sensor, strip 7 LED
  -1,  // #4  j2-j12
  -1,  // #5  j3-j4
  -1,  // #6  j4-j5
  -1,  // #7  j4-j6
  -1,  // #8  j5-j6
  -1,  // #9  j6-j7
  -1,  // #10 j7-j8
  33,  // #11 j7-j9   <- sensor, strip 8 LED
  -1,  // #12 j8-j9
  -1,  // #13 j9-j10
  -1,  // #14 j10-j11
  25,  // #15 j10-j12 <- sensor, strip 8 LED
  -1,  // #16 j11-j12
  -1,  // #17 j2-e1
  26,  // #18 j4-e2   <- sensor, strip 6 LED
  27,  // #19 j6-e2   <- sensor, strip 5 LED
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
unsigned long lastScanMs = 0;

// ---------- sensor ----------
void sensorsInit() {
#if USE_SENSORS
  for (int i = 0; i < NUM_EDGES; i++) {
    int pin = SENSOR_PIN[i];
    if (pin < 0) continue;
    // 34..39 tidak punya pull-up internal; sisanya pakai pull-up internal sebagai pengaman bila sensor lepas
    pinMode(pin, (pin >= 34 && pin <= 39) ? INPUT : INPUT_PULLUP);
  }
#endif
}

// Baca 20 sensor, kembalikan bitmask mentah (bit i = sensor jalur i melihat api)
uint32_t readRawSensors() {
  uint32_t raw = 0;
#if USE_SENSORS
  for (int i = 0; i < NUM_EDGES; i++) {
    int pin = SENSOR_PIN[i];
    if (pin >= 0 && digitalRead(pin) == SENSOR_ACTIVE_LEVEL) raw |= (1UL << i);
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
  if (res != ESP_OK) Serial.println("SEND ERROR");
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
  Serial.println("Perintah: room all | room <0-7> | start <node> | block <jalur> | clear <jalur> | reset | edges | sensors | list | help");
  Serial.println("Jalur: nama 'j1-j2' atau nomor 1-20 (lihat 'edges')");
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
  uint32_t raw = readRawSensors();
  for (int i = 0; i < NUM_EDGES; i++) {
    if (SENSOR_PIN[i] < 0) continue;
    Serial.print("#"); Serial.print(i + 1); Serial.print(" ");
    printEdgeName(i);
    Serial.print(" GPIO "); Serial.print(SENSOR_PIN[i]);
    Serial.print(" mentah="); Serial.print((raw >> i) & 1 ? "API" : "aman");
    Serial.print(" terkonfirmasi="); Serial.println((sensorMask >> i) & 1 ? "API" : "aman");
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
  sendRoute();
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
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    int e = parseEdge(arg);
    if (e < 0) { Serial.println("Jalur tidak dikenal. Contoh: block j1-j2  atau  block 1  (lihat 'edges')"); return; }
    if (cmd == "block" || cmd == "b") simMask |= (1UL << e);
    else                              simMask &= ~(1UL << e);
  } else if (cmd == "reset" || cmd == "0") {
    simMask = 0;
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

  sensorsInit();

  Serial.print("SENDER Dijkstra siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  compute();
  printRoute();
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

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendRoute();
  }
}
