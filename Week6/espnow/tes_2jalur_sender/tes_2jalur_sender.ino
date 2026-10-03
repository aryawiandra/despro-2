// TES 2 JALUR - SENDER (ESP32 #1)
// Simulasi 2 jalur evakuasi. Tanpa input = kedua jalur aman. Ketik perintah di Serial Monitor
// untuk memblokir jalur (api/asap); receiver menyalakan LED hanya pada jalur yang AMAN.
//
// Perintah (115200, line ending "Newline"):
//   1            -> toggle jalur 1 (aman <-> terblokir)
//   2            -> toggle jalur 2
//   block 1      -> blokir jalur 1        (juga: b 1)
//   clear 1      -> buka jalur 1 lagi     (juga: c 1)
//   reset        -> kedua jalur aman      (juga: 0)
//   list         -> status sekarang
//   help
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL   1
#define SEND_INTERVAL_MS 200   // heartbeat; receiver menganggap link putus jika 2 detik tanpa paket

uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // broadcast: tidak perlu MAC receiver

typedef struct __attribute__((packed)) {
  uint8_t blockedMask;  // bit0 = jalur 1 terblokir, bit1 = jalur 2 terblokir
} PathPacket;

uint8_t blockedMask = 0;
unsigned long lastSendMs = 0;

void sendMask() {
  PathPacket pkt = { blockedMask };
  esp_err_t res = esp_now_send(broadcastMac, (uint8_t *)&pkt, sizeof(pkt));
  if (res != ESP_OK) Serial.println("SEND ERROR");
}

void printState() {
  Serial.print("Jalur 1: ");
  Serial.print((blockedMask & 1) ? "TERBLOKIR" : "aman");
  Serial.print(" | Jalur 2: ");
  Serial.print((blockedMask & 2) ? "TERBLOKIR" : "aman");
  Serial.print("  (mask=0b");
  Serial.print(blockedMask, BIN);
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
    blockedMask ^= (line == "1") ? 1 : 2;
  } else if (cmd == "reset" || line == "0") {
    blockedMask = 0;
  } else if (cmd == "block" || cmd == "b" || cmd == "clear" || cmd == "c") {
    if (arg != "1" && arg != "2") {
      Serial.println("Pakai: block 1 | block 2 | clear 1 | clear 2");
      return;
    }
    uint8_t bit = (arg == "1") ? 1 : 2;
    if (cmd == "block" || cmd == "b") blockedMask |= bit;
    else                              blockedMask &= ~bit;
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

  Serial.print("SENDER 2 jalur siap. MAC: ");
  Serial.println(WiFi.macAddress());
  printHelp();
  printState();
}

void loop() {
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    sendMask();
  }
}
