// NODE SENSOR (SPOKE): kirim status jalur terblokir ke HUB via ESP-NOW.
//  SIM_MODE 1 -> tanpa sensor. Jalur "terbakar" diatur lewat perintah di Serial Monitor.
//  SIM_MODE 0 -> baca 20 sensor sungguhan via 2x CD74HC4067.
//
// Arsitektur hub & spoke: tiap node sensor hanya mengirim bit jalur miliknya (OWNED_MASK);
// hub (receiver) menggabungkan semua node, menjalankan Dijkstra, lalu mengatur LED.
//
// Perintah Serial Monitor (115200, line ending "Newline"):
//   block j2-j3 j4-j5   atau   block 2 5     -> jalur terblokir (api)
//   clear j2-j3                              -> jalur dibuka lagi
//   reset                                    -> semua jalur dibuka
//   list                                     -> tabel jalur + status
//   demo                                     -> jalankan skenario otomatis (stop = hentikan)
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define NUM_EDGES        20
#define ESPNOW_CHANNEL   1
#define SEND_INTERVAL_MS 200   // juga berfungsi sebagai heartbeat ke hub

#define SIM_MODE         1

#define NODE_ID          1
// Bit jalur yang dimiliki node ini. Satu node memegang semua 20 jalur (bit 0..19).
// Jika nanti dibagi beberapa node, tiap node diberi bit berbeda, mis. node 1 = 0x000FF, node 2 = 0xFFF00.
const uint32_t OWNED_MASK = (1UL << NUM_EDGES) - 1;

#define PIN_S0           18
#define PIN_S1           19
#define PIN_S2           21
#define PIN_S3           22
#define PIN_SIG_MUX1     34   // input-only, TIDAK punya pull-up internal -> wajib pull-up eksternal 10k ke 3V3
#define PIN_SIG_MUX2     35   // idem

// Default broadcast: tidak perlu tahu MAC hub. Isi MAC hub jika ingin unicast.
uint8_t hubMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct __attribute__((packed)) {
  uint8_t  nodeId;
  uint32_t validMask;    // bit jalur yang dimiliki node pengirim
  uint32_t blockedMask;  // bit i = 1 -> jalur i terblokir api/asap (hanya bit di validMask yang dipakai hub)
} SensorPacket;

// Nama jalur = urutan indeks sama dengan edges[] di receiver
const char* edgeNames[NUM_EDGES] = {
  "j1-j2", "j1-j3", "j2-j3", "j2-j12", "j3-j4", "j4-j5", "j4-j6", "j5-j6", "j6-j7", "j7-j8",
  "j7-j9", "j8-j9", "j9-j10", "j10-j11", "j10-j12", "j11-j12", "j2-e1", "j4-e2", "j6-e2", "j12-e3"
};

uint32_t simMask = 0;
unsigned long lastSendMs = 0;

// Skenario demo: tiap DEMO_STEP_MS jalankan satu perintah (diuji dengan room 4 di hub)
#define DEMO_STEP_MS 4000
const char* demoSteps[] = {
  "reset",             // semua terbuka: rute normal
  "block j6-j7",       // jalur ke e2 putus -> hub cari rute lain
  "block j10-j12",     // rute lewat j12 makin jauh
  "block j11-j12",     // semua jalur ke exit tertutup -> BAHAYA
  "reset"              // kembali normal
};
const int numDemoSteps = sizeof(demoSteps) / sizeof(demoSteps[0]);
int demoIdx = -1;      // -1 = demo tidak berjalan
unsigned long demoNextMs = 0;

uint32_t readBlockedMask() {
  uint32_t mask = 0;
  for (byte ch = 0; ch < 16; ch++) {
    digitalWrite(PIN_S0, bitRead(ch, 0));
    digitalWrite(PIN_S1, bitRead(ch, 1));
    digitalWrite(PIN_S2, bitRead(ch, 2));
    digitalWrite(PIN_S3, bitRead(ch, 3));
    delayMicroseconds(30);

    if (digitalRead(PIN_SIG_MUX1) == LOW) mask |= (1UL << ch);
    if (ch < 4 && digitalRead(PIN_SIG_MUX2) == LOW) mask |= (1UL << (16 + ch));
  }
  return mask;
}

void sendMask() {
  SensorPacket pkt;
  pkt.nodeId = NODE_ID;
  pkt.validMask = OWNED_MASK;
#if SIM_MODE
  pkt.blockedMask = simMask;
#else
  pkt.blockedMask = readBlockedMask();
#endif
  esp_err_t res = esp_now_send(hubMac, (uint8_t *)&pkt, sizeof(pkt));
  if (res != ESP_OK) Serial.println("SEND ERROR");
}

// ---------- Input simulasi ----------

// Terima indeks (0-19) atau nama jalur ("j2-j3", urutan simpul bebas). Return -1 jika tidak dikenal.
int parseEdge(String t) {
  t.trim();
  t.toLowerCase();
  if (t.length() == 0) return -1;

  bool digits = true;
  for (unsigned i = 0; i < t.length(); i++) {
    if (!isDigit(t[i])) digits = false;
  }
  if (digits) {
    int n = t.toInt();
    return n < NUM_EDGES ? n : -1;
  }

  int dash = t.indexOf('-');
  if (dash < 0) return -1;
  String rev = t.substring(dash + 1) + "-" + t.substring(0, dash);
  for (int i = 0; i < NUM_EDGES; i++) {
    if (t == edgeNames[i] || rev == edgeNames[i]) return i;
  }
  return -1;
}

void printState() {
  Serial.print("Terblokir: ");
  bool any = false;
  for (int i = 0; i < NUM_EDGES; i++) {
    if ((simMask >> i) & 1) {
      if (any) Serial.print(", ");
      Serial.print(edgeNames[i]);
      any = true;
    }
  }
  if (!any) Serial.print("(tidak ada)");
  Serial.print("  mask=0b");
  Serial.println(simMask, BIN);
}

void printEdges() {
  for (int i = 0; i < NUM_EDGES; i++) {
    Serial.print(i);
    Serial.print("\t");
    Serial.print(edgeNames[i]);
    Serial.println(((simMask >> i) & 1) ? "\tTERBLOKIR" : "\tbuka");
  }
}

void printHelp() {
  Serial.println("Perintah: block <jalur..> | clear <jalur..> | reset | list | demo | stop | help");
  Serial.println("Jalur = indeks 0-19 atau nama, mis. j2-j3 (lihat 'list'). Contoh: block j4-j5 j6-e2");
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? "" : line.substring(sp + 1);
  rest.trim();

  if (cmd == "list" || cmd == "l") {
    printEdges();
  } else if (cmd == "help" || cmd == "?") {
    printHelp();
  } else if (cmd == "demo") {
    demoIdx = 0;
    demoNextMs = millis();
    Serial.println("[DEMO] dimulai (ketik 'stop' untuk menghentikan)");
  } else if (cmd == "stop") {
    demoIdx = -1;
    Serial.println("[DEMO] dihentikan");
  } else if (cmd == "reset" || ((cmd == "clear" || cmd == "c") && rest.length() == 0)) {
    simMask = 0;
    printState();
    sendMask();
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    bool set = (cmd == "block" || cmd == "b");
    while (rest.length() > 0) {
      int s2 = rest.indexOf(' ');
      String tok = s2 < 0 ? rest : rest.substring(0, s2);
      rest = s2 < 0 ? "" : rest.substring(s2 + 1);
      rest.trim();

      int e = parseEdge(tok);
      if (e < 0) {
        Serial.print("Jalur tidak dikenal: ");
        Serial.println(tok);
        continue;
      }
      if (set) simMask |= (1UL << e);
      else     simMask &= ~(1UL << e);
    }
    printState();
    sendMask();
  } else {
    Serial.println("Perintah tidak dikenal. Ketik 'help'.");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_SIG_MUX1, INPUT);
  pinMode(PIN_SIG_MUX2, INPUT);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, hubMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Gagal add peer");
  }

  Serial.print("Node sensor ");
  Serial.print(NODE_ID);
  Serial.print(SIM_MODE ? " siap (MODE SIMULASI). MAC: " : " siap. MAC: ");
  Serial.println(WiFi.macAddress());
  if (SIM_MODE) printHelp();
}

void loop() {
#if SIM_MODE
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  if (demoIdx >= 0 && millis() >= demoNextMs) {
    Serial.print("[DEMO] > ");
    Serial.println(demoSteps[demoIdx]);
    handleCommand(demoSteps[demoIdx]);
    demoIdx++;
    demoNextMs = millis() + DEMO_STEP_MS;
    if (demoIdx >= numDemoSteps) {
      demoIdx = -1;
      Serial.println("[DEMO] selesai");
    }
  }
#endif

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendMask();
  }
}
