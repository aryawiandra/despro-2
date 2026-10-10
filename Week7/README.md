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

- `sender/` — baca sensor, Dijkstra di `graph.h`. Jalur yang kena api **merah**; **rute tercepat dari setiap ruangan
  ke exit hijau** (beberapa jalur hijau bersamaan); jalur lain mati.
- `receiver/` — jalur hijau dengan kepala terang yang mengalir searah exit (nyala sekuensial, sesuai proposal).
  Link putus: titik biru redup tiap 10 LED.

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
Di sender: `block j1-j3` → jalur #1 (j1>j2) menyala hijau dengan kepala berjalan (jalur #2 yang di LED 41–80 merah bila
terpasang). `reset` → jalur #1 mati lagi (karena j1>j3 kembali jadi pilihan terbaik).

Logic diuji di PC (Dijkstra, urutan/arah rute, render pixel receiver, link putus); belum diuji di ESP32 asli.

---

# KONFIGURASI AKHIR — `dijkstra_strip` dengan 20 sensor api (2 ESP32)

Alur: 20 sensor api (1 per jalur, langsung ke GPIO, tanpa multiplexer) → **ESP32 #1 sender** (baca sensor, Dijkstra) → ESP-NOW → **ESP32 #2 receiver**
(LED strip 20 jalur × 40 LED). Jalur rute hijau, jalur kena api dibuang dari graf dan rute otomatis dihitung ulang.

## ESP32 #1 — SENDER (20 sensor langsung ke GPIO + Dijkstra)

Tanpa multiplexer: tiap sensor api (pin **DO** modul) disambung ke **1 pin GPIO** sendiri. Tabelnya ada di
`SENSOR_PIN[]` di `sender.ino` (indeks = nomor jalur − 1).

| Jalur | Sensor di | GPIO | Jalur | Sensor di | GPIO |
|---|---|---|---|---|---|
| #1 | j1-j2 | **4** | #11 | j7-j9 | **25** |
| #2 | j1-j3 | **13** | #12 | j8-j9 | **26** |
| #3 | j2-j3 | **14** | #13 | j9-j10 | **27** |
| #4 | j2-j12 | **16** | #14 | j10-j11 | **32** |
| #5 | j3-j4 | **17** | #15 | j10-j12 | **33** |
| #6 | j4-j5 | **18** | #16 | j11-j12 | **34** * |
| #7 | j4-j6 | **19** | #17 | j2-e1 | **35** * |
| #8 | j5-j6 | **21** | #18 | j4-e2 | **36 (VP)** * |
| #9 | j6-j7 | **22** | #19 | j6-e2 | **39 (VN)** * |
| #10 | j7-j8 | **23** | #20 | j12-e3 | **15** ** |

\* GPIO 34, 35, 36, 39 hanya input dan **tidak punya pull-up internal**: pasang resistor **10 kΩ dari pin itu ke 3V3**
(4 resistor). Sensor lain memakai pull-up internal ESP32 (`INPUT_PULLUP`).
\** GPIO 15 adalah pin strapping: aman karena sensor aktif LOW dan idle-nya HIGH saat boot (kalau ada api saat boot,
paling-paling log boot di Serial tidak tampil). Pin yang sengaja **tidak dipakai**: 0, 1, 2, 3, 5, 6–12 (boot, UART USB, flash).

Tiap modul sensor: `VCC` → rel 3,3 V, `GND` → GND, `DO` → GPIO di tabel (pin `AO` tidak dipakai). Modul umumnya
**aktif LOW** (DO = 0 saat ada api), sesuai `SENSOR_ACTIVE_LEVEL LOW`; kalau modulmu aktif HIGH, ubah ke `HIGH`.
Cek label pin di board-mu: beberapa board 30-pin tidak memunculkan semua GPIO di atas (mis. 36/39 berlabel VP/VN).
Pin bisa diganti seenaknya di `SENSOR_PIN[]` (hindari pin terlarang di atas).

**Catatan daya sensor:** 20 modul ≈ 15–20 mA tiap modul ≈ 0,3–0,4 A. Itu terlalu berat untuk regulator 3V3 di board
ESP32 saat WiFi aktif. Pakai regulator/buck **3,3 V terpisah (≥1 A)** dari catu 5 V untuk rel sensor, GND-nya
**disambung** ke GND ESP32. Jangan beri sensor 5 V: output DO bisa 5 V dan melebihi batas pin ESP32 (3,3 V).
Kabel: 20 sensor × 3 kawat; pakai terminal block / PCB bolong untuk rel 3,3 V dan GND bersama.

Sensor yang belum dipasang: set `-1` di `SENSOR_PIN[]` untuk jalur itu (tidak dibaca, selalu "aman").

## ESP32 #2 — RECEIVER (LED strip)

| Pin ESP32 | Ke |
|---|---|
| GPIO 5 | resistor 330 Ω → DIN strip jalur #1 (awal rantai) |
| GND | GND strip dan GND catu 5 V (satu ground) |

Strip: +5V dari catu 5 V (30 A di proposal), kapasitor 1000 µF antara +5V dan GND di awal strip, suntik +5V/GND
tiap beberapa jalur. Rantai: DOUT jalur *i* → DIN jalur *i+1*, urut #1–#20, tiap jalur 40 LED
(`EDGE_LEN[]`, `EDGE_REVERSED[]` bila ada strip terpasang kebalikan). ESP32 receiver cukup dari USB.

## Input ruangan asal

Default (`room all`): rute tercepat dari setiap ruangan (0–7, lewat junction j1, j5, j8, j11) ke exit dihitung sekaligus,
jadi tidak perlu memilih ruangan — 10 jalur hijau saat aman, dan berubah otomatis saat ada api.
Serial sender mencetak rute tercepat **tiap ruangan** ke exit terdekatnya beserta bobotnya (bobot jalur + 1 untuk penghubung ruangan), mis. `Ruang 0: j1 > j3 > j2 > e1 (bobot 41)`. Untuk melihat rute satu ruangan saja: `room N`
(0,1 → j1; 2,3 → j5; 4,5 → j8; 6,7 → j11) atau `start j5`; `room all` kembali ke default.
Ubah default lewat `DEFAULT_SHOW_ALL` di `sender.ino`. Junction yang terputus dari semua exit dicetak di Serial
(`Ruangan di junction ini TERPUTUS dari exit`).

## Upload dan jalankan

```
cd "Week7/dijkstra_strip/receiver" && pio run -t upload      # colok ESP32 receiver
cd "Week7/dijkstra_strip/sender"   && pio run -t upload && pio device monitor   # colok ESP32 sender
```

Perintah di monitor sender: `sensors` (bacaan mentah 20 sensor), `edges`, `list`, `room N`, serta simulasi tanpa
sensor `block j1-j3` / `clear j1-j3` / `reset`.

## Urutan tes sebelum demo

1. **Sensor:** ketik `sensors` di monitor sender. Semua jalur harus `aman`. Dekatkan api ke satu sensor: baris jalur itu
   jadi `API` dan monitor menampilkan `[SENSOR] api di: ...`. Kalau ada jalur yang `API` padahal tidak ada api: cek pull-up
   10 kΩ (untuk GPIO 34/35/36/39), polaritas `SENSOR_ACTIVE_LEVEL`, atau pin yang salah sambung.
2. **Rute:** tanpa api, 10 jalur hijau (j1>j3, j3>j2, j2>e1, j5>j4, j4>e2, j8>j7, j7>j6, j6>e2, j11>j12, j12>e3). Dekatkan api ke sensor j1-j3 → jalur #2 **merah**
   dan j1 berpindah ke j1>j2 (hijau) dalam < 1 detik.
3. **Receiver:** jalur hijau dengan kepala terang mengalir menuju exit; jalur api merah; jalur lain mati.
4. **Semua tertutup:** api di semua jalan keluar (j2-e1, j4-e2, j6-e2, j12-e3) → `[BAHAYA]` di sender, tidak ada jalur hijau, jalur api merah.
5. **Link putus:** cabut sender > 2 detik → receiver menampilkan titik biru redup di tiap jalur.

Logic (Dijkstra, scan + debounce sensor, pemetaan pin ke jalur, render receiver) sudah diuji di PC dengan mock;
rangkaian dan strip asli belum diuji di sesi ini.
