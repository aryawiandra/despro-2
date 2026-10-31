// SENDER ESP32 - DIJKSTRA 20 JALUR
// 10 sensor fisik untuk edge #1 sampai #10.
// Edge #11 sampai #20 diatur manual melalui Serial Monitor.
// Semua rute terpendek dari 8 ruangan ke exit terdekat digabung.
//
// Sensor DO aktif LOW dianggap mendeteksi api.
// Satukan GND sensor dengan GND ESP32.
// Input ESP32 maksimal 3.3 V; jangan masukkan output sensor 5 V.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#define ESPNOW_CHANNEL       1
#define NUM_NODES            15
#define NUM_EDGES            20
#define NUM_ROOMS            8
#define NUM_SENSORS          10
#define FIRST_SERIAL_EDGE    11
#define LAST_SERIAL_EDGE     20

#define SEND_REPEATS         3
#define SEND_REPEAT_MS       100
#define HEARTBEAT_MS         0
#define REQUEST_MAGIC        0xA5
#define SENSOR_SCAN_MS       50
#define SENSOR_CONFIRM       3
#define SENSOR_FIRE_LEVEL    LOW

const char *NODE_NAMES[NUM_NODES] = {
  "j1", "j2", "j3", "j4", "j5", "j6",
  "j7", "j8", "j9", "j10", "j11", "j12",
  "e1", "e2", "e3"
};

struct Edge {
  uint8_t u;
  uint8_t v;
  uint16_t weight;
};

// Nomor edge harus sama dengan nomor edge di receiver.
const Edge EDGES[NUM_EDGES] = {
  {  0,  1, 49 }, // #1  j1-j2
  {  0,  2, 23 }, // #2  j1-j3
  {  1,  2, 12 }, // #3  j2-j3
  {  1, 11, 13 }, // #4  j2-j12
  {  1, 12,  5 }, // #5  j2-e1
  {  2,  3, 13 }, // #6  j3-j4
  {  3,  4, 23 }, // #7  j4-j5
  {  3,  5, 12 }, // #8  j4-j6
  {  4,  5, 49 }, // #9  j5-j6
  {  5,  6, 13 }, // #10 j6-j7
  {  6,  8, 12 }, // #11 j7-j9
  {  6,  7, 23 }, // #12 j7-j8
  {  7,  8, 49 }, // #13 j8-j9
  {  8,  9, 13 }, // #14 j9-j10
  {  9, 11, 12 }, // #15 j10-j12
  {  9, 10, 49 }, // #16 j10-j11
  { 10, 11, 23 }, // #17 j11-j12
  {  3, 13,  6 }, // #18 j4-e2
  {  5, 13,  6 }, // #19 j6-e2
  { 11, 14,  5 }  // #20 j12-e3
};

// Ruangan 0-1 mulai dari j1, 2-3 dari j5,
// 4-5 dari j8, dan 6-7 dari j11.
// Bobot dari ruangan ke junction awal tidak dihitung.
const uint8_t ROOM_TO_JUNCTION[NUM_ROOMS] = {
  0, 0, 4, 4, 7, 7, 10, 10
};

// Sensor fisik 1-10 masing-masing mengawasi edge #1-#10.
const uint8_t SENSOR_EDGE[NUM_SENSORS] = {
   1,  // Sensor 1  -> edge #1  j1-j2
   2,  // Sensor 2  -> edge #2  j1-j3
   3,  // Sensor 3  -> edge #3  j2-j3
   4,  // Sensor 4  -> edge #4  j2-j12
   5,  // Sensor 5  -> edge #5  j2-e1
   6,  // Sensor 6  -> edge #6  j3-j4
   7,  // Sensor 7  -> edge #7  j4-j5
   8,  // Sensor 8  -> edge #8  j4-j6
   9,  // Sensor 9  -> edge #9  j5-j6
  10   // Sensor 10 -> edge #10 j6-j7
};

// Sambungkan pin DO sensor ke GPIO berikut.
// GPIO1 dan GPIO3 tidak digunakan sensor agar Serial Monitor USB berfungsi.
const uint8_t SENSOR_PIN[NUM_SENSORS] = {
   4,  // Sensor 1  j1-j2  -> GPIO4
  13,  // Sensor 2  j1-j3  -> GPIO13
  14,  // Sensor 3  j2-j3  -> GPIO14
  18,  // Sensor 4  j2-j12 -> GPIO18
  19,  // Sensor 5  j2-e1  -> GPIO19
  21,  // Sensor 6  j3-j4  -> GPIO21
  22,  // Sensor 7  j4-j5  -> GPIO22
  23,  // Sensor 8  j4-j6  -> GPIO23
  25,  // Sensor 9  j5-j6  -> GPIO25
  26   // Sensor 10 j6-j7  -> GPIO26
};

// Edge #11-#20 tidak memakai sensor fisik.
const char *SERIAL_EDGE_NAMES[10] = {
  "j7-j9", "j7-j8", "j8-j9", "j9-j10", "j10-j12",
  "j10-j11", "j11-j12", "j4-e2", "j6-e2", "j12-e3"
};

struct PathResult {
  bool found;
  int8_t exitNode;
  uint16_t totalWeight;
  uint8_t nodeCount;
  uint8_t nodes[NUM_NODES];
  uint8_t edgeCount;
  uint8_t edges[NUM_NODES];
};

// Struktur paket harus sama dengan RoutePacket di receiver.
typedef struct __attribute__((packed)) {
  uint32_t greenMask;
  uint32_t blockedMask;
  uint32_t revMask;
  uint8_t depth[10];
  uint8_t flags;
} RoutePacket;

const uint8_t broadcastMac[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

uint32_t sensorBlockMask = 0;
uint32_t serialBlockMask = 0;
uint32_t blockedMask = 0;
uint32_t greenMask = 0;
uint32_t revMask = 0;

PathResult roomPaths[NUM_ROOMS];

unsigned long lastSensorScanMs = 0;
unsigned long lastSendMs = 0;
unsigned long nextSendMs = 0;

uint8_t pendingSends = 0;
volatile bool requestFlag = false;
bool radioReady = false;

char serialInputBuffer[32];
uint8_t serialInputLength = 0;

// Prototipe fungsi.
PathResult dijkstraToNearestExit(int startNode, uint32_t blocked);
void recomputeRoutes();
void requestSend();
void printCurrentState();
void printSerialHelp();
void processSerialCommand(String line);

// Baca 10 sensor fisik dengan debounce.
bool scanSensors() {
  static uint8_t confirmCount[NUM_SENSORS] = {0};
  bool changed = false;

  for (int k = 0; k < NUM_SENSORS; k++) {
    int edgeIndex = SENSOR_EDGE[k] - 1;

    bool wasBlocked =
      ((sensorBlockMask >> edgeIndex) & 1UL) != 0;

    bool readsFire =
      (digitalRead(SENSOR_PIN[k]) == SENSOR_FIRE_LEVEL);

    if (readsFire == wasBlocked) {
      confirmCount[k] = 0;
      continue;
    }

    confirmCount[k]++;

    if (confirmCount[k] >= SENSOR_CONFIRM) {
      if (readsFire) {
        sensorBlockMask |= (1UL << edgeIndex);
      } else {
        sensorBlockMask &= ~(1UL << edgeIndex);
      }

      confirmCount[k] = 0;
      changed = true;
    }
  }

  return changed;
}

// Cetak status semua jalur dan sumber blokirnya.
void printCurrentState() {
  Serial.println("\n=== STATUS SEMUA JALUR ===");

  for (int e = 0; e < NUM_EDGES; e++) {
    uint32_t bit = 1UL << e;

    bool sensorBlocked = (sensorBlockMask & bit) != 0;
    bool manualBlocked = (serialBlockMask & bit) != 0;
    bool isGreen = (greenMask & bit) != 0;

    Serial.printf(
      "#%d %s-%s : ",
      e + 1,
      NODE_NAMES[EDGES[e].u],
      NODE_NAMES[EDGES[e].v]
    );

    if (sensorBlocked && manualBlocked) {
      Serial.println("MERAH - diblokir sensor dan Serial Monitor");
    } else if (sensorBlocked) {
      Serial.println("MERAH - sensor mendeteksi api");
    } else if (manualBlocked) {
      Serial.println("MERAH - diblokir manual lewat Serial Monitor");
    } else if (isGreen) {
      Serial.println("HIJAU - dipakai rute");
    } else {
      Serial.println("MERAH - tidak dipakai rute");
    }
  }

  Serial.println("===========================\n");
}

void printSerialHelp() {
  Serial.println("\nSerial Monitor: 115200 baud.");
  Serial.println("Akhiran baris: Newline, Carriage return, atau Both.");
  Serial.println("Format: <nomor edge> ON atau <nomor edge> OFF");
  Serial.println("ON = blokir jalur; OFF = buka jalur.");
  Serial.println("Contoh: 11 ON atau 11 OFF");
  Serial.println("Edge yang bisa diatur manual:");

  for (int i = 0; i < 10; i++) {
    Serial.printf(
      "#%d %s\n",
      FIRST_SERIAL_EDGE + i,
      SERIAL_EDGE_NAMES[i]
    );
  }

  Serial.println("Ketik HELP untuk menampilkan petunjuk ini lagi.\n");
}

// Proses satu perintah setelah tombol Enter ditekan.
void processSerialCommand(String line) {
  line.trim();
  line.toUpperCase();

  if (line.length() == 0) {
    return;
  }

  if (line == "HELP" || line == "?") {
    printSerialHelp();
    return;
  }

  int separator = line.indexOf(' ');

  if (separator < 1) {
    Serial.println("Format salah. Contoh: 11 ON atau 11 OFF.");
    return;
  }

  int edgeNumber = line.substring(0, separator).toInt();

  String action = line.substring(separator + 1);
  action.trim();

  if (edgeNumber < FIRST_SERIAL_EDGE ||
      edgeNumber > LAST_SERIAL_EDGE) {
    Serial.println(
      "Nomor edge harus 11-20. Edge #1-#10 memakai sensor fisik."
    );
    return;
  }

  if (action != "ON" && action != "OFF") {
    Serial.println("Status harus ON atau OFF.");
    return;
  }

  uint32_t bit = 1UL << (edgeNumber - 1);
  bool wasBlocked = (serialBlockMask & bit) != 0;
  bool shouldBlock = (action == "ON");

  if (wasBlocked == shouldBlock) {
    Serial.printf(
      "Edge #%d (%s) memang sudah %s; status tidak berubah.\n",
      edgeNumber,
      SERIAL_EDGE_NAMES[edgeNumber - FIRST_SERIAL_EDGE],
      action.c_str()
    );

    printCurrentState();
    return;
  }

  if (shouldBlock) {
    serialBlockMask |= bit;
  } else {
    serialBlockMask &= ~bit;
  }

  recomputeRoutes();
  requestSend();

  Serial.printf(
    "Edge #%d (%s) berubah menjadi %s.\n",
    edgeNumber,
    SERIAL_EDGE_NAMES[edgeNumber - FIRST_SERIAL_EDGE],
    action.c_str()
  );

  printCurrentState();
}

// Tampung karakter sampai Enter, lalu proses sebagai satu perintah.
// Mendukung line ending CR, LF, maupun CRLF.
void handleSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r' || c == '\n') {
      if (serialInputLength == 0) {
        continue;
      }

      serialInputBuffer[serialInputLength] = '\0';
      Serial.println();

      processSerialCommand(String(serialInputBuffer));
      serialInputLength = 0;
      continue;
    }

    if (c == 8 || c == 127) {
      if (serialInputLength > 0) {
        serialInputLength--;
        Serial.print("\b \b");
      }
      continue;
    }

    if (serialInputLength < sizeof(serialInputBuffer) - 1) {
      serialInputBuffer[serialInputLength++] = c;
      Serial.write(c);
    } else {
      Serial.println("\nInput terlalu panjang; baris dibatalkan.");
      serialInputLength = 0;
    }
  }
}

// Dijkstra dari junction awal ke exit terdekat.
PathResult dijkstraToNearestExit(int startNode, uint32_t blocked) {
  const uint32_t INF = 0x3FFFFFFF;

  uint32_t dist[NUM_NODES];
  int8_t parentNode[NUM_NODES];
  bool visited[NUM_NODES] = {false};

  for (int i = 0; i < NUM_NODES; i++) {
    dist[i] = INF;
    parentNode[i] = -1;
  }

  dist[startNode] = 0;

  for (int step = 0; step < NUM_NODES; step++) {
    int u = -1;
    uint32_t best = INF;

    for (int n = 0; n < NUM_NODES; n++) {
      if (!visited[n] && dist[n] < best) {
        best = dist[n];
        u = n;
      }
    }

    if (u < 0) {
      break;
    }

    visited[u] = true;

    for (int e = 0; e < NUM_EDGES; e++) {
      if ((blocked >> e) & 1UL) {
        continue;
      }

      int v = -1;

      if (EDGES[e].u == u) {
        v = EDGES[e].v;
      } else if (EDGES[e].v == u) {
        v = EDGES[e].u;
      } else {
        continue;
      }

      uint32_t candidate = dist[u] + EDGES[e].weight;

      if (candidate < dist[v]) {
        dist[v] = candidate;
        parentNode[v] = u;
      }
    }
  }

  // Node 12, 13, 14 adalah exit e1, e2, dan e3.
  int exitNode = -1;
  uint32_t bestExitDist = INF;

  for (int n = 12; n <= 14; n++) {
    if (dist[n] < bestExitDist) {
      bestExitDist = dist[n];
      exitNode = n;
    }
  }

  PathResult result = {};
  result.found = false;
  result.exitNode = -1;

  if (exitNode < 0 || bestExitDist == INF) {
    return result;
  }

  // Telusuri node dari exit kembali ke junction awal.
  int reverseNodes[NUM_NODES];
  int count = 0;
  int current = exitNode;

  while (current >= 0 && count < NUM_NODES) {
    reverseNodes[count++] = current;

    if (current == startNode) {
      break;
    }

    current = parentNode[current];
  }

  if (count == 0 || reverseNodes[count - 1] != startNode) {
    return result;
  }

  result.found = true;
  result.exitNode = exitNode;
  result.totalWeight = (uint16_t)bestExitDist;
  result.nodeCount = count;

  // Susun urutan node dari junction menuju exit.
  for (int i = 0; i < count; i++) {
    result.nodes[i] = reverseNodes[count - 1 - i];
  }

  result.edgeCount = count - 1;

  // Tentukan edge untuk setiap pasangan node dalam rute.
  for (int i = 0; i < result.edgeCount; i++) {
    int edgeIndex = -1;
    int a = result.nodes[i];
    int b = result.nodes[i + 1];

    for (int e = 0; e < NUM_EDGES; e++) {
      if ((EDGES[e].u == a && EDGES[e].v == b) ||
          (EDGES[e].u == b && EDGES[e].v == a)) {
        edgeIndex = e;
        break;
      }
    }

    if (edgeIndex < 0) {
      result.found = false;
      return result;
    }

    result.edges[i] = edgeIndex;
  }

  return result;
}

// Hitung rute semua ruangan dan gabungkan edge yang terpilih.
void recomputeRoutes() {
  blockedMask = sensorBlockMask | serialBlockMask;
  greenMask = 0;
  revMask = 0;

  for (int r = 0; r < NUM_ROOMS; r++) {
    roomPaths[r] =
      dijkstraToNearestExit(ROOM_TO_JUNCTION[r], blockedMask);

    const PathResult &path = roomPaths[r];

    if (!path.found) {
      continue;
    }

    for (int p = 0; p < path.edgeCount; p++) {
      int edgeIndex = path.edges[p];
      uint32_t bit = 1UL << edgeIndex;

      // Edge yang dipakai oleh setidaknya satu ruangan menjadi hijau.
      greenMask |= bit;

      int from = path.nodes[p];
      int to = path.nodes[p + 1];

      // revMask menandai arah perjalanan dari node v ke node u.
      if (EDGES[edgeIndex].u == to &&
          EDGES[edgeIndex].v == from) {
        revMask |= bit;
      } else {
        revMask &= ~bit;
      }
    }
  }
}

void sendRoute() {
  RoutePacket packet = {};

  packet.greenMask = greenMask;
  packet.blockedMask = blockedMask;
  packet.revMask = revMask;
  packet.flags = (greenMask == 0) ? 1 : 0;

  esp_now_send(
    broadcastMac,
    reinterpret_cast<const uint8_t *>(&packet),
    sizeof(packet)
  );
}

void requestSend() {
  pendingSends = SEND_REPEATS;
  nextSendMs = 0;
}

// Receiver dapat meminta sender mengirim paket ulang.
void onDataRecv(const void *infoOrMac, const uint8_t *data, int len) {
  if (len == 1 && data[0] == REQUEST_MAGIC) {
    requestFlag = true;
  }
}

void setup() {
  Serial.begin(115200);

  // Sensor DO aktif LOW.
  for (int k = 0; k < NUM_SENSORS; k++) {
    pinMode(SENSOR_PIN[k], INPUT_PULLUP);
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW gagal dimulai.");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Gagal menambahkan peer ESP-NOW.");
    return;
  }

  esp_now_register_recv_cb(
    reinterpret_cast<esp_now_recv_cb_t>(onDataRecv)
  );

  radioReady = true;

  // Baca status sensor saat sender mulai.
  for (int k = 0; k < NUM_SENSORS; k++) {
    int edgeIndex = SENSOR_EDGE[k] - 1;

    if (digitalRead(SENSOR_PIN[k]) == SENSOR_FIRE_LEVEL) {
      sensorBlockMask |= (1UL << edgeIndex);
    }
  }

  recomputeRoutes();
  requestSend();

  printSerialHelp();
  printCurrentState();
}

void loop() {
  handleSerialInput();

  if (!radioReady) {
    delay(1000);
    return;
  }

  unsigned long now = millis();

  // Periksa sensor berkala.
  if (now - lastSensorScanMs >= SENSOR_SCAN_MS) {
    lastSensorScanMs = now;

    if (scanSensors()) {
      recomputeRoutes();
      requestSend();
      printCurrentState();
    }
  }

  // Kirim ulang jika receiver meminta paket.
  if (requestFlag) {
    requestFlag = false;
    requestSend();
  }

  // Kirim paket beberapa kali saat ada perubahan.
  if (pendingSends > 0 && millis() >= nextSendMs) {
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