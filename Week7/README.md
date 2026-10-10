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

---

# Dijkstra + LED strip WS2812B (`dijkstra_strip/`)

Versi untuk **LED strip addressable** (pengganti `dijkstra_graph` yang LED biasa). Graf, bobot, dan mapping
ruangan sudah dicocokkan dengan `Week4/graph-visualizer.png` dan `Week5/kodeIntegrasi.cpp` (20 jalur identik).

- `sender/` — sama seperti `dijkstra_graph` (perintah teks, Dijkstra di `graph.h`), tapi paketnya membawa
  **urutan jalur dari ruangan ke exit + arahnya**.
- `receiver/` — jalur rute **hijau**, dengan kepala terang yang berjalan searah rute (nyala sekuensial,
  sesuai proposal). Jalur lain mati. Link putus: titik biru redup tiap 10 LED.

Perintah sender dan cara upload sama seperti `dijkstra_graph` (`room 0`, `block j1-j3`, `reset`, ...).
Upload: `cd Week7/dijkstra_strip/receiver && pio run -t upload` (idem `sender`).

## Susunan strip

20 jalur disambung **1 rantai** di 1 pin data. Urutan rantai = nomor jalur (`edges` di sender), tiap jalur
`EDGE_LEN[i]` LED (sementara 40 → total 800 LED ≈ 13 m pada 60 LED/m, sesuai 15 m strip di proposal).

| # | Jalur | # | Jalur | # | Jalur | # | Jalur |
|---|---|---|---|---|---|---|---|
| 1 | j1-j2 | 6 | j4-j5 | 11 | j7-j9 | 16 | j11-j12 |
| 2 | j1-j3 | 7 | j4-j6 | 12 | j8-j9 | 17 | j2-e1 |
| 3 | j2-j3 | 8 | j5-j6 | 13 | j9-j10 | 18 | j4-e2 |
| 4 | j2-j12 | 9 | j6-j7 | 14 | j10-j11 | 19 | j6-e2 |
| 5 | j3-j4 | 10 | j7-j8 | 15 | j10-j12 | 20 | j12-e3 |

- Arah rantai tiap jalur = dari node pertama ke node kedua (jalur #1 mengalir dari j1 ke j2). Jika strip jalur itu
  terpasang kebalikannya, set `EDGE_REVERSED[i] = true` di `receiver.ino`; animasi tetap searah rute.
- Antar-jalur: DOUT strip jalur *i* disambung ke DIN strip jalur *i+1* dengan kabel (jalur tidak harus bersebelahan secara fisik).
- Panjang beda tiap jalur: ubah `EDGE_LEN[i]`. Total LED dihitung otomatis.

## Konfigurasi pin dan listrik (receiver)

| Sambungan | Ke |
|---|---|
| ESP32 **GPIO 5** | resistor 330 Ω → **DIN** strip jalur #1 (awal rantai) |
| ESP32 **GND** | GND strip **dan** GND power supply (harus satu ground) |
| Power supply **5 V** | +5V strip (bukan dari pin 5V ESP32) |
| Kapasitor 1000 µF 6,3 V+ | antara +5V dan GND di awal strip (ESP32 tetap dari USB) |

- Strip WS2812B dikendalikan data 5 V; data 3,3 V dari ESP32 biasanya cukup untuk kabel pendek. Kalau LED
  berkedip acak/tidak menyala, pasang level shifter (74HCT125/74AHCT125) di jalur data.
- Catu 5 V 30 A sesuai proposal. Hanya jalur rute yang menyala dan `LED_BRIGHTNESS` 60/255, jadi arus nyata jauh
  di bawah 800 LED penuh (±16 A). Suntik daya +5V/GND ke strip tiap beberapa jalur supaya tegangan tidak jatuh di ujung.

## Parameter di `receiver.ino`

`ANIMATE` (1 = kepala berjalan, 0 = hijau diam), `CHASE_LEN`, `CHASE_STEP_MS`, `SHOW_BLOCKED_RED`
(1 = jalur kena api merah), `LED_BRIGHTNESS`.

## Tes cepat dengan 1 strip pendek

Tanpa merakit 20 jalur: sambungkan **1 strip** (mis. 40 LED) ke GPIO 5 sebagai jalur #1 (awal rantai).
Di sender: `room 0`, lalu `block j1-j3` → rute pindah ke j1>j2>e1 sehingga jalur #1 menyala hijau dengan kepala berjalan
(jalur #17 ada di LED 641–680, tidak terlihat). `reset` → jalur #1 mati lagi.

Logic diuji di PC (Dijkstra, urutan/arah rute, render pixel receiver, link putus); belum diuji di ESP32 asli.
