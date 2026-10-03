// HUB (NODE 2): terima status jalur dari node sensor via ESP-NOW, gabungkan, jalankan Dijkstra,
// nyalakan LED strip. Tujuan evakuasi = pintu keluar (e1/e2/e3) terdekat; yang diblokir adalah JALUR, bukan simpul.
//
// Perintah Serial Monitor (115200, line ending "Newline"):
//   room 4      -> tampilkan rute evakuasi dari ruangan 4 (0-7)
//   room all    -> tampilkan gabungan rute dari semua ruangan
//   status      -> info link & paket

#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define LED_PIN          5       // Pin data LED Strip WS2812B
#define NUM_EDGES        20      // Tepat 20 jalur aktif
#define NUM_NODES        15      // 12 Junction + 3 Exit
#define INF              999999
#define LED_BRIGHTNESS   60      // 0-255, batasi arus strip
#define ESPNOW_CHANNEL   1       // harus sama dengan sender
#define LINK_TIMEOUT_MS  2000    // tanpa paket selama ini = link putus

volatile uint32_t blockedMask = 0;
volatile unsigned long lastPacketMs = 0;
volatile uint32_t packetCount = 0;

typedef struct __attribute__((packed)) {
  uint8_t  nodeId;
  uint32_t validMask;    // bit jalur yang dimiliki node pengirim
  uint32_t blockedMask;
} SensorPacket;

int selectedRoom = 4;    // 0-7, atau -1 = semua ruangan

Adafruit_NeoPixel strip(NUM_EDGES, LED_PIN, NEO_GRB + NEO_KHZ800);

// Indeks Simpul:
// j1=0, j2=1, j3=2, j4=3, j5=4, j6=5, j7=6, j8=7, j9=8, j10=9, j11=10, j12=11
// e1=12, e2=13, e3=14
const char* nodeNames[NUM_NODES] = {
  "j1", "j2", "j3", "j4", "j5", "j6", 
  "j7", "j8", "j9", "j10", "j11", "j12", 
  "e1", "e2", "e3"
};

// Pemetaan Ruangan (0 - 7) ke Junction Pertama
const int roomToJunction[8] = {
  0,  // Ruang 0 -> j1
  0,  // Ruang 1 -> j1
  4,  // Ruang 2 -> j5
  4,  // Ruang 3 -> j5
  7,  // Ruang 4 -> j8
  7,  // Ruang 5 -> j8
  10, // Ruang 6 -> j11
  10  // Ruang 7 -> j11
};

const int exitNodes[] = {12, 13, 14}; // e1, e2, e3
const int numExits = 3;

struct Edge {
  int u;
  int v;
  int weight;
  bool isBlocked;
};

// 20 Jalur Tunggal Tanpa Duplikasi
Edge edges[NUM_EDGES] = {
  {0, 1, 49, false},   // [0]  j1 - j2
  {0, 2, 23, false},   // [1]  j1 - j3
  {1, 2, 12, false},   // [2]  j2 - j3
  {1, 11, 13, false},  // [3]  j2 - j12
  {2, 3, 13, false},   // [4]  j3 - j4
  {3, 4, 23, false},   // [5]  j4 - j5
  {3, 5, 12, false},   // [6]  j4 - j6
  {4, 5, 49, false},   // [7]  j5 - j6
  {5, 6, 13, false},   // [8]  j6 - j7
  {6, 7, 23, false},   // [9]  j7 - j8
  {6, 8, 12, false},   // [10] j7 - j9
  {7, 8, 49, false},   // [11] j8 - j9
  {8, 9, 13, false},   // [12] j9 - j10
  {9, 10, 49, false},  // [13] j10 - j11
  {9, 11, 12, false},  // [14] j10 - j12
  {10, 11, 23, false}, // [15] j11 - j12
  {1, 12, 5, false},   // [16] j2 - e1
  {3, 13, 6, false},   // [17] j4 - e2
  {5, 13, 6, false},   // [18] j6 - e2
  {11, 14, 5, false}   // [19] j12 - e3
};

int adjMatrix[NUM_NODES][NUM_NODES];

// Callback ESP-NOW. Signature berbeda antara Arduino-ESP32 core 3.x dan 2.x.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
  if (len != sizeof(SensorPacket)) return;
  SensorPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  // Tiap node hanya mengubah bit miliknya (validMask), bit node lain tetap
  blockedMask = (blockedMask & ~pkt.validMask) | (pkt.blockedMask & pkt.validMask);
  lastPacketMs = millis();
  packetCount++;
}

// Terapkan bitmask dari Node 1 ke status jalur
void applySensorMask(uint32_t mask) {
  for (int i = 0; i < NUM_EDGES; i++) {
    edges[i].isBlocked = (mask >> i) & 1;
  }
}

// Semua LED biru = link ke Node 1 putus (bukan "aman")
void showLinkLost() {
  for (int i = 0; i < NUM_EDGES; i++) strip.setPixelColor(i, strip.Color(0, 0, 40));
  strip.show();
  Serial.println("[LINK PUTUS] Tidak ada data dari node sensor");
}

// Rekonstruksi Matriks Berdasarkan Status Sensor
void buildAdjacencyMatrix() {
  for (int i = 0; i < NUM_NODES; i++) {
    for (int j = 0; j < NUM_NODES; j++) {
      adjMatrix[i][j] = (i == j) ? 0 : INF;
    }
  }

  for (int i = 0; i < NUM_EDGES; i++) {
    if (!edges[i].isBlocked) {
      adjMatrix[edges[i].u][edges[i].v] = edges[i].weight;
      adjMatrix[edges[i].v][edges[i].u] = edges[i].weight;
    }
  }
}

int findEdgeIndex(int u, int v) {
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((edges[i].u == u && edges[i].v == v) || (edges[i].u == v && edges[i].v == u)) {
      return i;
    }
  }
  return -1;
}

// Dijkstra dari junction ruangan ke exit terdekat. Isi pathEdges (tandai jalur rute) dan pathNodes.
// Return true jika ada rute.
bool routeFromRoom(int room, bool pathEdges[], int pathNodes[], int &pathLen, int &exitNode, int &totalDist) {
  int startNode = roomToJunction[room];

  int dist[NUM_NODES];
  bool visited[NUM_NODES];
  int parent[NUM_NODES];

  for (int i = 0; i < NUM_NODES; i++) {
    dist[i] = INF;
    visited[i] = false;
    parent[i] = -1;
  }
  dist[startNode] = 0;

  for (int count = 0; count < NUM_NODES - 1; count++) {
    int minDist = INF;
    int u = -1;
    for (int i = 0; i < NUM_NODES; i++) {
      if (!visited[i] && dist[i] < minDist) {
        minDist = dist[i];
        u = i;
      }
    }
    if (u == -1 || minDist == INF) break;
    visited[u] = true;

    for (int v = 0; v < NUM_NODES; v++) {
      if (!visited[v] && adjMatrix[u][v] != INF && dist[u] + adjMatrix[u][v] < dist[v]) {
        dist[v] = dist[u] + adjMatrix[u][v];
        parent[v] = u;
      }
    }
  }

  exitNode = -1;
  totalDist = INF;
  for (int i = 0; i < numExits; i++) {
    int e = exitNodes[i];
    if (dist[e] < totalDist) {
      totalDist = dist[e];
      exitNode = e;
    }
  }
  if (exitNode == -1 || totalDist == INF) return false;

  // Telusuri balik exit -> start; pathNodes disusun start -> exit
  int rev[NUM_NODES];
  int n = 0;
  for (int curr = exitNode; curr != -1; curr = parent[curr]) rev[n++] = curr;
  pathLen = n;
  for (int i = 0; i < n; i++) pathNodes[i] = rev[n - 1 - i];

  for (int i = 0; i + 1 < n; i++) {
    int idx = findEdgeIndex(pathNodes[i], pathNodes[i + 1]);
    if (idx != -1) pathEdges[idx] = true;
  }
  return true;
}

void printRoute(int room, bool ok, const int pathNodes[], int pathLen, int totalDist) {
  Serial.print("Ruang ");
  Serial.print(room);
  Serial.print(" (");
  Serial.print(nodeNames[roomToJunction[room]]);
  Serial.print("): ");
  if (!ok) {
    Serial.println("[BAHAYA] tidak ada rute keluar yang aman");
    return;
  }
  for (int i = 0; i < pathLen; i++) {
    if (i) Serial.print(" > ");
    Serial.print(nodeNames[pathNodes[i]]);
  }
  Serial.print(" | bobot ");
  Serial.println(totalDist);
}

// Hitung rute (1 ruangan atau semua), log ke Serial, dan nyalakan LED:
// merah = jalur terblokir, hijau = rute evakuasi, mati = jalur terbuka bukan rute
void updateDisplay() {
  bool pathEdges[NUM_EDGES] = {false};

  int firstRoom = selectedRoom >= 0 ? selectedRoom : 0;
  int lastRoom  = selectedRoom >= 0 ? selectedRoom : 7;
  for (int room = firstRoom; room <= lastRoom; room++) {
    int pathNodes[NUM_NODES], pathLen = 0, exitNode = -1, totalDist = INF;
    bool ok = routeFromRoom(room, pathEdges, pathNodes, pathLen, exitNode, totalDist);
    printRoute(room, ok, pathNodes, pathLen, totalDist);
  }

  strip.clear();
  for (int i = 0; i < NUM_EDGES; i++) {
    if (edges[i].isBlocked)  strip.setPixelColor(i, strip.Color(255, 0, 0));
    else if (pathEdges[i])   strip.setPixelColor(i, strip.Color(0, 255, 0));
  }
  strip.show();
}

void printBlocked(uint32_t mask) {
  Serial.print("[RX] paket #");
  Serial.print(packetCount);
  Serial.print(" terblokir: ");
  bool any = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((mask >> i) & 1) {
      if (any) Serial.print(", ");
      Serial.print(nodeNames[edges[i].u]);
      Serial.print("-");
      Serial.print(nodeNames[edges[i].v]);
      any = true;
    }
  }
  if (!any) Serial.print("(tidak ada)");
  Serial.print("  mask=0b");
  Serial.println(mask, BIN);
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();

  if (line.startsWith("room")) {
    String arg = line.substring(4);
    arg.trim();
    if (arg == "all") {
      selectedRoom = -1;
    } else if (arg.length() == 1 && isDigit(arg[0]) && arg[0] <= '7') {
      selectedRoom = arg.toInt();
    } else {
      Serial.println("Pakai: room 0-7 atau room all");
    }
  } else if (line == "status") {
    Serial.print("Paket diterima: ");
    Serial.print(packetCount);
    Serial.print(" | paket terakhir ");
    Serial.print(millis() - lastPacketMs);
    Serial.println(" ms lalu");
  } else if (line.length() > 0) {
    Serial.println("Perintah: room 0-7 | room all | status");
  }
}

void setup() {
  Serial.begin(115200);

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.clear();
  strip.show();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.print("HUB navigasi evakuasi aktif. MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Perintah: room 0-7 | room all | status");
}

void loop() {
  static bool lastLink = true;
  static uint32_t lastMask = 0xFFFFFFFF;
  static int lastRoom = -2;

  handleSerial();

  bool linkUp = lastPacketMs != 0 && (millis() - lastPacketMs <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (lastLink) showLinkLost();
    lastLink = false;
    delay(50);
    return;
  }

  uint32_t mask = blockedMask;
  if (!lastLink || mask != lastMask || selectedRoom != lastRoom) {
    if (!lastLink) Serial.println("[LINK TERSAMBUNG]");
    if (mask != lastMask) printBlocked(mask);
    lastLink = true;
    lastMask = mask;
    lastRoom = selectedRoom;

    applySensorMask(mask);
    buildAdjacencyMatrix();
    updateDisplay();
  }
  delay(20);
}
