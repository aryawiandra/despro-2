// WEEK 7 - NAVIGASI DIJKSTRA - SENDER (ESP32 #1)
// Graf 15 node / 20 jalur (lihat graph.h). Tiap jalur punya 1 sensor api; di sini sensor
// disimulasikan lewat teks di Serial Monitor. Tiap ada perubahan, sender menjalankan Dijkstra
// dari ruangan asal ke exit terdekat (jalur kena api dibuang), lalu mengirim jalur terpilih
// ke receiver lewat ESP-NOW. Receiver menyalakan jalur tsb hijau.
//
// Perintah (Serial Monitor 115200, line ending "Newline"):
//   room <0-7>        -> pilih ruangan asal (0,1=j1  2,3=j5  4,5=j8  6,7=j11)
//   start <node>      -> atau langsung pilih node asal, mis. start j5
//   block <jalur>     -> api di jalur (sensor = 1), mis. block j1-j2   atau   block 1
//   clear <jalur>     -> api padam, mis. clear j1-j2
//   reset             -> semua jalur aman
//   edges             -> daftar 20 jalur (nomor, nama, bobot, status)
//   list              -> status + rute sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "graph.h"

#define ESPNOW_CHANNEL    1
#define SEND_INTERVAL_MS  200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast: tidak perlu MAC receiver

typedef struct __attribute__((packed)) {
  uint32_t pathMask;     // bit i = jalur i dilewati rute evakuasi (hijau)
  uint32_t blockedMask;  // bit i = jalur i kena api
  uint8_t  flags;        // bit0 = tidak ada rute aman
} RoutePacket;

int startRoom = 0;
int startNode = ROOM_TO_JUNCTION[0];
uint32_t blockedMask = 0;
RouteResult route;
unsigned long lastSendMs = 0;

void sendRoute() {
  RoutePacket pkt;
  pkt.pathMask = route.found ? route.pathMask : 0;
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
    if ((blockedMask >> i) & 1) Serial.print("  API");
    if (route.found && ((route.pathMask >> i) & 1)) Serial.print("  RUTE");
    Serial.println();
  }
}

void printHelp() {
  Serial.println("Perintah: room <0-7> | start <node> | block <jalur> | clear <jalur> | reset | edges | list | help");
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

void recompute() {
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
    if (cmd == "block" || cmd == "b") blockedMask |= (1UL << e);
    else                              blockedMask &= ~(1UL << e);
  } else if (cmd == "reset" || cmd == "0") {
    blockedMask = 0;
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

  Serial.print("SENDER Dijkstra siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  route = computeRoute(startNode, blockedMask);
  printRoute();
}

void loop() {
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendRoute();
  }
}
