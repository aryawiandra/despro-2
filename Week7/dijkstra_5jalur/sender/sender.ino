// WEEK 7 - NAVIGASI DIJKSTRA 5 JALUR (LED STRIP) - SENDER (ESP32 #1)
// Versi kecil dari dijkstra_strip: hanya 5 jalur dari graf 20 jalur yang dipakai (ACTIVE[] di bawah),
// masing-masing punya 1 sensor api langsung ke 1 pin GPIO. Jalur lain di graf dianggap tidak ada.
// Sensor juga bisa disimulasikan lewat teks di Serial Monitor; hasil keduanya digabung (OR). Jalur yang
// kena api dibuang dari graf, lalu Dijkstra dihitung ke exit terdekat dan hasilnya dikirim ke receiver:
//   - jalur kena api            -> MERAH BERKEDIP
//   - jalur aman menuju exit    -> HIJAU, dengan nyala sekuensial searah exit
//   - jalur lain                -> mati
// Mode default "room all": rute tercepat dari SETIAP ruangan yang ada di 5 jalur ini. "room N" hanya satu ruangan.
//
// Perintah (Serial Monitor 115200, line ending "Newline"):
//   room all          -> rute tercepat dari semua ruangan aktif (default)
//   room <N>          -> hanya rute ruangan N (hanya ruangan di junction aktif)
//   block <jalur>     -> simulasi api di jalur aktif, mis. block j4-e2   atau   block 18
//   clear <jalur>     -> padamkan simulasi
//   reset             -> hapus semua simulasi teks (sensor asli tidak terpengaruh)
//   edges             -> daftar jalur aktif (nomor, nama, bobot, pin sensor, status)
//   sensors           -> bacaan mentah sensor
//   list              -> status + rute sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "graph.h"

#define ESPNOW_CHANNEL    1
#define SEND_INTERVAL_MS  200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

// ---- 5 jalur aktif: nomor jalur (1..20, lihat graph.h / 'edges') + pin DO sensor api jalur itu ----
// Default: sudut exit e2 dengan 3 rute masuk dari ruangan 2 dan 3 (junction j5).
//   #6 j4-j5, #7 j4-j6, #8 j5-j6, #18 j4-e2, #19 j6-e2
// Ganti isinya dengan 5 jalur lain yang kamu pasang (nomor jalur harus tepat 5 dan tidak ganda).
// Pin sensor: pakai pin dengan pull-up internal (4, 5, 13, 14, 16-19, 21-23, 25-27, 32, 33).
#define NUM_ACTIVE 5
struct ActiveEdge { uint8_t edge; int8_t sensorPin; };
const ActiveEdge ACTIVE[NUM_ACTIVE] = {
  {  6, 32 },   // j4-j5  -> sensor di GPIO 32
  {  7, 33 },   // j4-j6  -> GPIO 33
  {  8, 25 },   // j5-j6  -> GPIO 25
  { 18, 26 },   // j4-e2  -> GPIO 26
  { 19, 27 }    // j6-e2  -> GPIO 27
};

#define USE_SENSORS         1
#define SENSOR_ACTIVE_LEVEL LOW  // level output sensor saat ada api (modul flame sensor umumnya LOW)
#define SENSOR_SCAN_MS      50   // periode scan sensor
#define SENSOR_CONFIRM      3    // butuh N scan berturut-turut untuk mengubah status (anti-noise, ~150 ms)

uint32_t activeMask = 0;   // bit i = jalur i+1 aktif (dihitung dari ACTIVE[])
uint8_t  roomMask = 0;     // bit r = ruangan r punya junction yang tersentuh jalur aktif
uint16_t activeNodeMask = 0; // bit n = node n tersentuh jalur aktif
bool     configOk = false;

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
  for (int n = 0; n < NUM_ACTIVE; n++) pinMode(ACTIVE[n].sensorPin, INPUT_PULLUP);
#endif
}

// Baca sensor, kembalikan bitmask mentah (bit i = sensor jalur i+1 melihat api)
uint32_t readRawSensors() {
  uint32_t raw = 0;
#if USE_SENSORS
  for (int n = 0; n < NUM_ACTIVE; n++)
    if (digitalRead(ACTIVE[n].sensorPin) == SENSOR_ACTIVE_LEVEL) raw |= (1UL << (ACTIVE[n].edge - 1));
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
    if (!((roomMask >> r) & 1)) continue;
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
  for (int n = 0; n < NUM_ACTIVE; n++) {
    int i = ACTIVE[n].edge - 1;
    Serial.print("#"); Serial.print(i + 1); Serial.print("  ");
    printEdgeName(i);
    Serial.print("  bobot "); Serial.print(EDGES[i].weight);
    Serial.print("  sensor GPIO "); Serial.print(ACTIVE[n].sensorPin);
    if ((sensorMask >> i) & 1) Serial.print("  API(sensor)");
    if ((simMask >> i) & 1) Serial.print("  API(teks)");
    if ((safe.greenMask >> i) & 1) Serial.print("  HIJAU");
    Serial.println();
  }
}

void printHelp() {
  Serial.println("Perintah: room all | room <N> | start <node> | block <jalur> | clear <jalur> | reset | edges | sensors | list | help");
  Serial.println("Jalur: nama 'j4-j5' atau nomor jalur (hanya 5 jalur aktif, lihat 'edges')");
}

// "j1-j2" (urutan bebas) atau "1".."20" -> indeks jalur, -1 jika tidak valid
int parseEdge(String s) {
  s.trim();
  int dash = s.indexOf('-');
  if (dash > 0) {
    int a = nodeIndex(s.substring(0, dash).c_str());
    int b = nodeIndex(s.substring(dash + 1).c_str());
    if (a >= 0 && b >= 0) { int e = findEdgeIndex(a, b); return (e >= 0 && ((activeMask >> e) & 1)) ? e : -1; }
    return -1;
  }
  int n = s.toInt();
  if (n >= 1 && n <= NUM_EDGES && ((activeMask >> (n - 1)) & 1)) return n - 1;
  return -1;
}

void printSensors() {
#if USE_SENSORS
  uint32_t raw = readRawSensors();
  for (int n = 0; n < NUM_ACTIVE; n++) {
    int i = ACTIVE[n].edge - 1;
    Serial.print("#"); Serial.print(i + 1); Serial.print(" ");
    printEdgeName(i);
    Serial.print(" GPIO "); Serial.print(ACTIVE[n].sensorPin);
    Serial.print(" mentah="); Serial.print((raw >> i) & 1 ? "API" : "aman");
    Serial.print(" terkonfirmasi="); Serial.println((sensorMask >> i) & 1 ? "API" : "aman");
  }
#else
  Serial.println("USE_SENSORS = 0, sensor tidak dibaca");
#endif
}

void compute() {
  blockedMask = simMask | sensorMask;
  uint32_t graphBlocked = blockedMask | (~activeMask & 0x000FFFFFUL);   // jalur non-aktif dianggap tidak ada
  if (showAll) {
    safe = computeSafeForest(graphBlocked, roomMask);
  } else {
    route = computeRoute(startNode, graphBlocked);
    safe = routeToSafeMap(route);
  }
}

// Hitung activeMask / roomMask dari ACTIVE[] dan periksa konfigurasinya
bool initActive() {
  bool ok = true;
  activeMask = 0; activeNodeMask = 0; roomMask = 0;
  for (int n = 0; n < NUM_ACTIVE; n++) {
    int e = ACTIVE[n].edge;
    if (e < 1 || e > NUM_EDGES) { Serial.print("ACTIVE salah: nomor jalur "); Serial.println(e); ok = false; continue; }
    if ((activeMask >> (e - 1)) & 1) { Serial.print("ACTIVE salah: jalur #"); Serial.print(e); Serial.println(" ganda"); ok = false; }
    activeMask |= (1UL << (e - 1));
    activeNodeMask |= (1U << EDGES[e - 1].u) | (1U << EDGES[e - 1].v);
  }
  for (int r = 0; r < 8; r++)
    if ((activeNodeMask >> ROOM_TO_JUNCTION[r]) & 1) roomMask |= (1 << r);
  if (roomMask == 0) { Serial.println("ACTIVE salah: tidak ada junction ruangan (j1, j5, j8, j11) di jalur aktif"); ok = false; }
  return ok;
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
      if (arg.length() == 0 || n < 0 || n > 7 || !((roomMask >> n) & 1)) {
        Serial.print("Pakai: room all | room N, ruangan aktif:");
        for (int r = 0; r < 8; r++) if ((roomMask >> r) & 1) { Serial.print(" "); Serial.print(r); }
        Serial.println();
        return;
      }
      showAll = false;
      startRoom = n;
      startNode = ROOM_TO_JUNCTION[n];
    }
  } else if (cmd == "start" || cmd == "s") {
    int n = nodeIndex(arg.c_str());
    if (n < 0 || n >= 12 || !((activeNodeMask >> n) & 1)) { Serial.println("Node asal harus junction (j1..j12) yang tersentuh jalur aktif"); return; }
    showAll = false;
    startRoom = -1;
    startNode = n;
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    int e = parseEdge(arg);
    if (e < 0) { Serial.println("Jalur tidak aktif / tidak dikenal. Lihat 'edges' untuk 5 jalur aktif."); return; }
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

  configOk = initActive();
  if (!configOk) { Serial.println("Perbaiki ACTIVE[] di sender.ino lalu upload ulang."); return; }
  for (int r = 0; r < 8; r++)                       // ruangan awal untuk mode 'room N' = ruangan aktif pertama
    if ((roomMask >> r) & 1) { startRoom = r; startNode = ROOM_TO_JUNCTION[r]; break; }

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

  Serial.print("SENDER Dijkstra 5 jalur siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  compute();
  printRoute();
}

void loop() {
  if (!configOk) { delay(1000); return; }   // ACTIVE[] salah, lihat pesan di Serial
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
