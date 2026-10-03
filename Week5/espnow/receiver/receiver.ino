// NODE 2 (RECEIVER + LED): terima status sensor via ESP-NOW, jalankan Dijkstra, nyalakan LED strip
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

typedef struct __attribute__((packed)) {
  uint32_t blockedMask;
} SensorPacket;

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

// Callback ESP-NOW. Signature ini untuk Arduino-ESP32 core 3.x.
// Jika memakai core 2.x, ganti parameter pertama menjadi: const uint8_t *mac
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(SensorPacket)) return;
  SensorPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  blockedMask = pkt.blockedMask;
  lastPacketMs = millis();
}

// Terapkan bitmask dari Node 1 ke status jalur
void applySensorMask() {
  uint32_t mask = blockedMask;
  for (int i = 0; i < NUM_EDGES; i++) {
    edges[i].isBlocked = (mask >> i) & 1;
  }
}

// Semua LED biru = link ke Node 1 putus (bukan "aman")
void showLinkLost() {
  for (int i = 0; i < NUM_EDGES; i++) strip.setPixelColor(i, strip.Color(0, 0, 40));
  strip.show();
  Serial.println("[LINK PUTUS] Tidak ada data dari Node sensor");
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

// Menjalankan Algoritma Dijkstra dan Kontrol Warna LED
void runDijkstraEvacuation(int selectedRoom) {
  int startNode = roomToJunction[selectedRoom]; // Titik j awal ruangan

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

  // Cari pintu keluar terdekat
  int bestExit = -1;
  int minExitDist = INF;
  for (int i = 0; i < numExits; i++) {
    int e = exitNodes[i];
    if (dist[e] < minExitDist) {
      minExitDist = dist[e];
      bestExit = e;
    }
  }

  // Identifikasi segmen jalur evakuasi
  bool isPathEdge[NUM_EDGES] = {false};
  if (bestExit != -1 && minExitDist != INF) {
    int curr = bestExit;
    while (parent[curr] != -1) {
      int prev = parent[curr];
      int edgeIdx = findEdgeIndex(prev, curr);
      if (edgeIdx != -1) {
        isPathEdge[edgeIdx] = true;
      }
      curr = prev;
    }
  }

  // Tampilkan Status LED Strip (20 Titik Jalur)
  strip.clear();
  for (int i = 0; i < NUM_EDGES; i++) {
    if (edges[i].isBlocked) {
      // Jalur Tertutup Api / Asap -> MERAH
      strip.setPixelColor(i, strip.Color(255, 0, 0));
    } else if (isPathEdge[i]) {
      // Jalur Rute Evakuasi Tercepat -> HIJAU
      strip.setPixelColor(i, strip.Color(0, 255, 0));
    } else {
      // Jalur Terbuka tetapi Bukan Rute Terpendek -> MATI
      strip.setPixelColor(i, strip.Color(0, 0, 0));
    }
  }
  strip.show();

  // Log Hasil ke Serial Monitor
  if (bestExit == -1 || minExitDist == INF) {
    Serial.print("[BAHAYA] Tidak ada rute keluar yang aman dari Ruangan ");
    Serial.println(selectedRoom);
  } else {
    Serial.print("Ruangan: ");
    Serial.print(selectedRoom);
    Serial.print(" (via ");
    Serial.print(nodeNames[startNode]);
    Serial.print(") -> Exit: ");
    Serial.print(nodeNames[bestExit]);
    Serial.print(" | Bobot Rute: ");
    Serial.println(minExitDist);
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

  Serial.println("Sistem Navigasi Evakuasi 20-Jalur Aktif (ESP-NOW receiver).");
}

void loop() {
  if (millis() - lastPacketMs > LINK_TIMEOUT_MS) {
    showLinkLost();
    delay(500);
    return;
  }

  applySensorMask();
  buildAdjacencyMatrix();

  // Ganti parameter dengan ruangan yang ingin dipandu (0 - 7)
  int ruanganSaatIni = 4;
  runDijkstraEvacuation(ruanganSaatIni);

  delay(500);
}
