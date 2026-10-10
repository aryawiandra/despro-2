# Week 7 — Navigasi 2 Jalur dengan LED Biasa

LED strip gagal, jadi diganti **LED biasa**. Logic tidak berubah dari
`Week6/espnow/tes_2jalur_*` (ESP-NOW, sender → receiver):

| Kondisi | LED Jalur 1 | LED Jalur 2 |
|---|---|---|
| Tidak ada input / sensor tidak mendeteksi apa-apa | NYALA | NYALA |
| Sensor api jalur 1 kirim `1` (ada api di jalur 1) | **MATI** | nyala |
| Sensor api jalur 2 kirim `1` (ada api di jalur 2) | nyala | **MATI** |
| Kedua jalur terkena api | MATI | MATI (Serial: `[BAHAYA]`) |
| Link ESP-NOW putus > 2 detik | kedip | kedip |

Kode: `led_biasa_2jalur/sender/sender.ino` dan `led_biasa_2jalur/receiver/receiver.ino`
(masing-masing punya `platformio.ini`, buka foldernya di VS Code + PlatformIO).

## Konfigurasi pin

### Receiver (ESP32 #2) — LED jalur

| Fungsi | GPIO ESP32 | Komponen |
|---|---|---|
| LED Jalur 1 | **GPIO 5** | resistor 220 Ω → anoda (+) LED |
| LED Jalur 2 | **GPIO 18** | resistor 220 Ω → anoda (+) LED |
| Ground | **GND** | katoda (−) kedua LED |

```
GPIO5  ──[220Ω]──►|── GND      (LED jalur 1, hijau)
GPIO18 ──[220Ω]──►|── GND      (LED jalur 2, hijau)
```

- Logika **active-HIGH**: pin HIGH (3,3 V) = LED nyala = jalur aman; pin LOW = LED mati = jalur kena api.
- Arus per LED ≈ (3,3 − ~2,0 V) / 220 Ω ≈ 6 mA, aman untuk pin ESP32 (rekomendasi ≤ 12 mA/pin).
- Mau lebih terang / lebih banyak LED per jalur: paralel LED, **tiap LED punya resistor sendiri**,
  dan jaga total arus per pin ≤ ~12 mA (±2 LED per pin). Lebih dari itu pakai transistor (mis. 2N2222/BC547:
  GPIO → 1 kΩ → basis, emitor → GND, LED + resistor ke 5 V lewat kolektor).
- Catu daya: ESP32 dari USB (5 V) atau power bank.

### Sender (ESP32 #1)

| Fungsi | GPIO ESP32 | Keterangan |
|---|---|---|
| Simulasi api | USB / Serial | Tidak butuh wiring — ketik perintah di Serial Monitor (115200) |
| Sensor api jalur 1 (opsional) | **GPIO 32** | Pin **DO** modul flame sensor jalur 1 |
| Sensor api jalur 2 (opsional) | **GPIO 33** | Pin **DO** modul flame sensor jalur 2 |
| Sensor VCC | **3V3** | Jangan 5 V, supaya output DO aman untuk ESP32 |
| Sensor GND | **GND** | |

Sensor hanya dibaca jika `#define USE_FLAME_SENSOR 1` di `sender.ino` (default `0` = simulasi saja).
Output `1` (HIGH) = api terdeteksi. Jika modul sensormu aktif LOW (umum untuk modul flame sensor
LM393), ubah `FLAME_ACTIVE_LEVEL` ke `LOW`.

## Cara simulasi "ada api di jalannya"

1. Upload `receiver.ino` ke ESP32 #2 dan `sender.ino` ke ESP32 #1. Keduanya harus
   `ESPNOW_CHANNEL` sama (1).
2. Buka Serial Monitor sender (115200, line ending **Newline**).
3. Tanpa mengetik apa-apa → kedua LED receiver **nyala**.
4. Ketik `1` (atau `block 1`) → LED jalur 1 **mati** (simulasi api di jalur 1). Ketik `1` lagi / `clear 1` → nyala lagi.
5. Ketik `2` (atau `block 2`) → LED jalur 2 mati. `reset` → kedua nyala lagi.
6. Cabut daya sender > 2 detik → kedua LED receiver berkedip (link putus).

## Catatan

- Kode belum diuji di hardware dari sesi ini (tidak ada ESP32/PlatformIO di lingkungan ini);
  logic diturunkan 1:1 dari Week6 yang sudah dites.
- Folder lama (Week2–Week6) tidak diubah.

---

# Dijkstra + graf 20 jalur (`dijkstra_graph/`)

Graf Week5 (12 junction + 3 exit, 20 jalur, 1 sensor per jalur). Sender menjalankan Dijkstra,
mengirim jalur tercepat ke exit lewat ESP-NOW, receiver menyalakan jalur itu **hijau**.
Input sensor api masih disimulasikan lewat teks di Serial Monitor sender.

- `sender/graph.h` — graf + Dijkstra (C++ murni, sudah diuji di PC)
- `sender/sender.ino` — perintah teks + kirim rute
- `receiver/receiver.ino` — nyalakan jalur rute; `USE_ADDRESSABLE` 0 = LED biasa, 1 = strip addressable

## Perintah di Serial Monitor sender (115200, Newline)

| Perintah | Arti |
|---|---|
| `room 4` | ruangan asal 0–7 (0,1=j1  2,3=j5  4,5=j8  6,7=j11) |
| `start j5` | atau pilih node asal langsung (j1–j12) |
| `block j1-j2` / `block 1` | sensor jalur itu = 1 (ada api), jalur dibuang dari graf |
| `clear j1-j2` | api padam |
| `reset` | semua jalur aman |
| `edges` | daftar 20 jalur: nomor, nama, bobot, status |
| `list` | rute sekarang |

Tiap perubahan: Dijkstra ulang → kirim ke receiver. Jika semua rute ke exit tertutup, semua jalur mati
dan Serial menampilkan `[BAHAYA]`.

## Demo cepat dengan 2 LED (GPIO 5 dan 18, wiring sama seperti di atas)

1. `room 0` → rute j1>j3>j2>e1 (bobot 40). LED GPIO 18 (jalur #2 j1-j3) **nyala**.
2. `block j1-j3` → rute pindah j1>j2>e1 (bobot 54). LED GPIO 18 **mati**, LED GPIO 5 (jalur #1 j1-j2) **nyala**.
3. `block j1-j2` → semua jalan dari j1 tertutup, kedua LED mati, `[BAHAYA]`.
4. `reset` → kembali ke langkah 1.

## 40 LED per jalur

Panjang tiap jalur beda-beda, sementara diasumsikan **40 LED** (`EDGE_LEN[]` di `receiver.ino`, bisa diubah per jalur).

- **Mode LED biasa (default, `USE_ADDRESSABLE 0`)**: 1 pin GPIO per jalur (tabel `EDGE_PIN[]`, `-1` = tidak dipasang).
  Satu pin tidak kuat menyalakan 40 LED (maks ±12 mA/pin), jadi pin menggerakkan transistor/MOSFET
  (mis. GPIO → 1 kΩ → basis 2N2222, LED hijau + resistor tiap LED ke 5 V lewat kolektor) untuk 1 grup 40 LED.
  20 jalur butuh 20 pin; tabel default hanya mengisi jalur #1 (GPIO5) dan #2 (GPIO18) untuk demo di atas.
- **Mode strip addressable (`USE_ADDRESSABLE 1`)**: semua jalur disambung 1 rantai di GPIO 5 (urutan = nomor jalur),
  total 20 × 40 = 800 LED. Butuh library Adafruit NeoPixel dan catu 5 V terpisah (suntik daya tiap beberapa meter);
  hanya jalur rute yang menyala sehingga arus jauh lebih kecil dari 800 LED penuh.
