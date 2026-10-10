// WEEK 7 - 2 JALUR LED BIASA - SENDER (ESP32 #1)
// Logic sama dengan Week6/tes_2jalur_sender. Tanpa input = kedua jalur aman (LED receiver nyala).
// Jalur terblokir api (mask bit = 1) -> receiver mematikan LED jalur tsb.
//
// Sumber input (digabung dengan OR):
//   1) Simulasi lewat Serial Monitor (default, tidak butuh sensor):
//        1 / 2        -> toggle jalur 1 / 2 (simulasi ada api di jalur itu)
//        block 1      -> blokir jalur 1        (juga: b 1)
//        clear 1      -> buka jalur 1 lagi     (juga: c 1)
//        reset        -> kedua jalur aman      (juga: 0)
//        list         -> status sekarang
//        help
//   2) Sensor api asli (opsional): set USE_FLAME_SENSOR 1. Output sensor = 1 -> jalur terblokir.
//      (Modul flame sensor umumnya aktif LOW; ubah FLAME_ACTIVE_LEVEL ke LOW bila modulmu begitu.)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL    1
#define SEND_INTERVAL_MS  200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

#define USE_FLAME_SENSOR  0     // 0 = simulasi Serial saja, 1 = baca sensor api di pin bawah
#define FLAME_PIN_1       32    // DO sensor api jalur 1
#define FLAME_PIN_2       33    // DO sensor api jalur 2
#define FLAME_ACTIVE_LEVEL HIGH // level output sensor saat mendeteksi api ("1" = HIGH)

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast: tidak perlu MAC receiver

typedef struct __attribute__((packed)) {
  uint8_t blockedMask;  // bit0 = jalur 1 terblokir, bit1 = jalur 2 terblokir
} PathPacket;

uint8_t simMask = 0;       // dari Serial
uint8_t sensorMask = 0;    // dari sensor api
uint8_t lastSentMask = 0;
unsigned long lastSendMs = 0;

uint8_t effectiveMask() { return (simMask | sensorMask) & 0x03; }

void readSensors() {
#if USE_FLAME_SENSOR
  sensorMask = 0;
  if (digitalRead(FLAME_PIN_1) == FLAME_ACTIVE_LEVEL) sensorMask |= 1;
  if (digitalRead(FLAME_PIN_2) == FLAME_ACTIVE_LEVEL) sensorMask |= 2;
#endif
}

void sendMask() {
  lastSentMask = effectiveMask();
  PathPacket pkt = { lastSentMask };
  esp_err_t res = esp_now_send(broadcastMac, (uint8_t *)&pkt, sizeof(pkt));
  if (res != ESP_OK) Serial.println("SEND ERROR");
}

void printState() {
  uint8_t m = effectiveMask();
  Serial.print("Jalur 1: ");
  Serial.print((m & 1) ? "TERBLOKIR" : "aman");
  Serial.print(" | Jalur 2: ");
  Serial.print((m & 2) ? "TERBLOKIR" : "aman");
  Serial.print("  (mask=0b");
  Serial.print(m, BIN);
  Serial.println(")");
}

void printHelp() {
  Serial.println("Perintah: 1 | 2 (toggle) | block <1|2> | clear <1|2> | reset | list | help");
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();

  if (line == "1" || line == "2") {
    simMask ^= (line == "1") ? 1 : 2;
  } else if (cmd == "reset" || line == "0") {
    simMask = 0;
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    if (arg != "1" && arg != "2") {
      Serial.println("Pakai: block 1 | block 2 | clear 1 | clear 2");
      return;
    }
    uint8_t bit = (arg == "1") ? 1 : 2;
    if (cmd == "block" || cmd == "b") simMask |= bit;
    else                              simMask &= ~bit;
  } else if (cmd == "list" || cmd == "l") {
    printState();
    return;
  } else if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  } else {
    Serial.println("Perintah tidak dikenal. Ketik 'help'.");
    return;
  }

  printState();
  sendMask();
}

void setup() {
  Serial.begin(115200);

#if USE_FLAME_SENSOR
  pinMode(FLAME_PIN_1, INPUT);
  pinMode(FLAME_PIN_2, INPUT);
#endif

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

  Serial.print("SENDER 2 jalur (LED biasa) siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  printState();
}

void loop() {
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  readSensors();
  if (effectiveMask() != lastSentMask) {   // perubahan dari sensor: kirim & lapor segera
    printState();
    sendMask();
  }

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendMask();
  }
}
