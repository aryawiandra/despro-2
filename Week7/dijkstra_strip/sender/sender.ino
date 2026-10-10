// WEEK 7 - NAVIGASI DIJKSTRA (LED STRIP) - SENDER (ESP32 #1)
// Graf 15 node / 20 jalur (lihat graph.h). Tiap jalur punya 1 sensor api (20 sensor, dibaca lewat
// 2 multiplexer CD74HC4067, sama seperti Week5). Sensor juga bisa disimulasikan lewat teks di
// Serial Monitor; hasil keduanya digabung (OR). Tiap ada perubahan, sender menjalankan Dijkstra
// dari ruangan asal ke exit terdekat (jalur kena api dibuang), lalu mengirim jalur terpilih
// ke receiver lewat ESP-NOW, berurutan dari ruangan ke exit lengkap dengan arahnya, supaya
// receiver bisa menyalakan LED strip hijau dan menjalankan nyala sekuensial searah rute.
//
// Perintah (Serial Monitor 115200, line ending "Newline"):
//   room <0-7>        -> pilih ruangan asal (0,1=j1  2,3=j5  4,5=j8  6,7=j11)
//   start <node>      -> atau langsung pilih node asal, mis. start j5
//   block <jalur>     -> simulasi api di jalur, mis. block j1-j2   atau   block 1
//   clear <jalur>     -> padamkan simulasi, mis. clear j1-j2
//   reset             -> hapus semua simulasi teks (sensor asli tidak terpengaruh)
//   edges             -> daftar 20 jalur (nomor, nama, bobot, status, sumber api)
//   sensors           -> bacaan mentah 20 sensor
//   list              -> status + rute sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "graph.h"

#define ESPNOW_CHANNEL    1
#define SEND_INTERVAL_MS  200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

// ---- Sensor api: 20 sensor lewat 2x CD74HC4067 (rancangan Week5) ----
#define USE_SENSORS         1
#define PIN_S0              18   // pin selektor, paralel ke kedua multiplexer
#define PIN_S1              19
#define PIN_S2              21
#define PIN_S3              22
#define PIN_SIG_MUX1        34   // common (SIG) mux 1 -> jalur #1..#16 (C0..C15). Input-only, butuh pull-up eksternal 10k ke 3V3
#define PIN_SIG_MUX2        35   // common (SIG) mux 2 -> jalur #17..#20 (C0..C3). Idem
#define SENSOR_ACTIVE_LEVEL LOW  // level output sensor saat ada api (modul flame sensor umumnya LOW)
#define SENSOR_ENABLED_MASK 0x000FFFFFUL  // bit i = sensor jalur i+1 dipasang; matikan bit untuk sensor yang belum ada
#define SENSOR_SCAN_MS      50   // periode scan 20 sensor
#define SENSOR_CONFIRM      3    // butuh N scan berturut-turut untuk mengubah status (anti-noise, ~150 ms)

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast: tidak perlu MAC receiver

#define MAX_ROUTE_EDGES 15

typedef struct __attribute__((packed)) {
  uint8_t  count;                    // jumlah jalur di rute (0 = tidak ada)
  uint8_t  seq[MAX_ROUTE_EDGES];     // urut dari start ke exit: bit0-6 = indeks jalur, bit7 = 1 jika dilewati v -> u
  uint32_t blockedMask;              // bit i = jalur i kena api
  uint8_t  flags;                    // bit0 = tidak ada rute aman
} RoutePacket;

#define DEFAULT_ROOM        0    // ruangan asal saat menyala (0-7); bisa diganti kapan saja dengan 'room N'

int startRoom = DEFAULT_ROOM;
int startNode = ROOM_TO_JUNCTION[DEFAULT_ROOM];
uint32_t simMask = 0;      // api dari teks Serial (simulasi)
uint32_t sensorMask = 0;   // api dari sensor asli (sudah di-debounce)
uint32_t blockedMask = 0;  // gabungan keduanya, dipakai Dijkstra
RouteResult route;
unsigned long lastSendMs = 0;
unsigned long lastScanMs = 0;

// ---------- sensor ----------
void sensorsInit() {
#if USE_SENSORS
  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_SIG_MUX1, INPUT);   // GPIO34/35 tidak punya pull-up internal -> pull-up eksternal 10k
  pinMode(PIN_SIG_MUX2, INPUT);
#endif
}

// Baca 20 sensor, kembalikan bitmask mentah (bit i = sensor jalur i melihat api)
uint32_t readRawSensors() {
  uint32_t raw = 0;
#if USE_SENSORS
  for (byte ch = 0; ch < 16; ch++) {
    digitalWrite(PIN_S0, bitRead(ch, 0));
    digitalWrite(PIN_S1, bitRead(ch, 1));
    digitalWrite(PIN_S2, bitRead(ch, 2));
    digitalWrite(PIN_S3, bitRead(ch, 3));
    delayMicroseconds(30);   // tunggu mux settle
    if (digitalRead(PIN_SIG_MUX1) == SENSOR_ACTIVE_LEVEL) raw |= (1UL << ch);
    if (ch < 4 && digitalRead(PIN_SIG_MUX2) == SENSOR_ACTIVE_LEVEL) raw |= (1UL << (16 + ch));
  }
  raw &= SENSOR_ENABLED_MASK;
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
  if (route.found) {
    pkt.count = route.numEdges;
    for (int i = 0; i < route.numEdges; i++)
      pkt.seq[i] = (uint8_t)route.edgeSeq[i] | (route.edgeRev[i] ? 0x80 : 0);
  }
  pkt.blockedMask = blockedMask;
  pkt.flags = route.found ? 0 : 1;
  esp_err_t res = esp_now_send(broadcastMac, (uint8_t *)&pkt, sizeof(pkt));
  if (res != ESP_OK) Serial.println("SEND ERROR");
}

void printEdgeName(int i) {
  Serial.print(NODE_NAMES[EDGES[i].u]);
  Serial.print("-");
  Serial.print(NODE_NAMES[EDGES[i].v]);
}

void printRoute() {
  Serial.print("Asal: ");
  if (startRoom >= 0) { Serial.print("ruang "); Serial.print(startRoom); Serial.print(" ("); }
  Serial.print(NODE_NAMES[startNode]);
  if (startRoom >= 0) Serial.print(")");
  Serial.print(" | jalur terblokir: ");
  bool any = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((blockedMask >> i) & 1) { if (any) Serial.print(", "); printEdgeName(i); any = true; }
  }
  if (!any) Serial.print("-");
  Serial.println();

  if (!route.found) {
    Serial.println("[BAHAYA] Tidak ada rute aman ke exit");
    return;
  }
  Serial.print("Rute -> ");
  Serial.print(NODE_NAMES[route.exitNode]);
  Serial.print(" (bobot ");
  Serial.print(route.totalWeight);
  Serial.print("): ");
  for (int i = 0; i < route.numNodes; i++) {
    if (i) Serial.print(" > ");
    Serial.print(NODE_NAMES[route.nodes[i]]);
  }
  Serial.print("  | LED hijau jalur #: ");
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((route.pathMask >> i) & 1) { Serial.print(i + 1); Serial.print(" "); }
  }
  Serial.println();
}

void printEdges() {
  for (int i = 0; i < NUM_EDGES; i++) {
    Serial.print("#"); Serial.print(i + 1); Serial.print("  ");
    printEdgeName(i);
    Serial.print("  bobot "); Serial.print(EDGES[i].weight);
    if ((sensorMask >> i) & 1) Serial.print("  API(sensor)");
    if ((simMask >> i) & 1) Serial.print("  API(teks)");
    if (route.found && ((route.pathMask >> i) & 1)) Serial.print("  RUTE");
    Serial.println();
  }
}

void printHelp() {
  Serial.println("Perintah: room <0-7> | start <node> | block <jalur> | clear <jalur> | reset | edges | sensors | list | help");
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
    Serial.print("#"); Serial.print(i + 1); Serial.print(" ");
    printEdgeName(i);
    Serial.print(" mentah="); Serial.print((raw >> i) & 1 ? "API" : "aman");
    Serial.print(" terkonfirmasi="); Serial.println((sensorMask >> i) & 1 ? "API" : "aman");
  }
#else
  Serial.println("USE_SENSORS = 0, sensor tidak dibaca");
#endif
}

void recompute() {
  blockedMask = simMask | sensorMask;
  route = computeRoute(startNode, blockedMask);
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
    int n = arg.toInt();
    if (arg.length() == 0 || n < 0 || n > 7) { Serial.println("Pakai: room 0-7"); return; }
    startRoom = n;
    startNode = ROOM_TO_JUNCTION[n];
  } else if (cmd == "start" || cmd == "s") {
    int n = nodeIndex(arg.c_str());
    if (n < 0 || n >= 12) { Serial.println("Node asal harus j1..j12"); return; }
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
  blockedMask = simMask | sensorMask;
  route = computeRoute(startNode, blockedMask);
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
