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

## Susunan strip: satu pin per jalur

Tiap jalur = 1 strip terpisah (`EDGE_LEN[i]` LED, sementara 40) dengan **pin data sendiri**, tanpa disambung ke strip lain.
Warna tiap LED ditentukan dari data yang dikirim ke pin itu (hijau, merah, atau mati), jadi pin = jalur.

| Jalur | Strip | Pin ESP32 | Jalur | Strip | Pin ESP32 |
|---|---|---|---|---|---|
| #1 | j1-j2 | **GPIO 4** | #11 | j7-j9 | **GPIO 23** |
| #2 | j1-j3 | **GPIO 5** | #12 | j8-j9 | **GPIO 25** |
| #3 | j2-j3 | **GPIO 13** | #13 | j9-j10 | **GPIO 26** |
| #4 | j2-j12 | **GPIO 14** | #14 | j10-j11 | **GPIO 27** |
| #5 | j3-j4 | **GPIO 16** | #15 | j10-j12 | **GPIO 32** |
| #6 | j4-j5 | **GPIO 17** | #16 | j11-j12 | **GPIO 33** |
| #7 | j4-j6 | **GPIO 18** | #17 | j2-e1 | **GPIO 15** (pin boot) |
| #8 | j5-j6 | **GPIO 19** | #18 | j4-e2 | **GPIO 2** (pin boot, LED onboard) |
| #9 | j6-j7 | **GPIO 21** | #19 | j6-e2 | **GPIO 12** (pin boot) |
| #10 | j7-j8 | **GPIO 22** | #20 | j12-e3 | **GPIO 0** (pin boot, tombol BOOT) |

Tabelnya `EDGE_PIN[]` di `receiver.ino` (indeks = nomor jalur − 1); ganti sesuai pin yang kamu pakai.

- **Keterbatasan ESP32:** hanya 16 pin output yang bersih (4, 5, 13, 14, 16–19, 21–23, 25–27, 32, 33). Empat jalur terakhir
  terpaksa memakai pin boot (15, 2, 12, 0). Itu aman selama DIN strip tidak menarik pin ke level tinggi saat reset
  (DIN WS2812B berimpedansi tinggi, ditambah resistor 330 Ω). Kalau **upload gagal** atau ESP32 tidak mau boot,
  cabut kabel data dari GPIO 0, 2, dan 12 sementara.
- **Pin yang jangan dipakai:** GPIO 1 dan 3 (USB serial), 6–11 (flash), 34–39 (hanya input). Kode menolak pin yang tidak valid
  atau ganda dan menampilkan pesan di Serial.
- **Driver output:** library Adafruit NeoPixel hanya bisa melayani 8 pin sekaligus, jadi file ini memakai driver bit-bang
  paralel sendiri yang mengirim ke 20 pin serentak (±1,2 ms per update untuk 40 LED, interrupt dimatikan sebentar).
  Logika bit/pin sudah diuji di PC, tetapi **timing sinyal belum bisa diuji tanpa strip dan ESP32 asli**. Kalau LED berkedip
  acak atau salah warna, lihat bagian "Kalau LED aneh" di bawah.
- **Mode cadangan (`OUTPUT_MODE 0`):** strip digabung dalam rantai DOUT → DIN, tiap rantai 1 pin (`CHAINS[]`), memakai
  library Adafruit (maks 8 rantai). Berguna kalau kekurangan pin.
- Arah data tiap strip = dari node pertama ke node kedua nama jalurnya (jalur #1 mengalir dari j1 ke j2). Jika strip dipasang
  kebalikannya, set `EDGE_REVERSED[i] = true`; animasi tetap searah rute.
- Panjang beda tiap jalur: ubah `EDGE_LEN[i]` (maks `MAX_EDGE_LEN` = 64 LED per jalur).

## Konfigurasi pin dan listrik (receiver)

| Sambungan | Ke |
|---|---|
| ESP32 GPIO sesuai tabel di atas | resistor 330 Ω lalu **DIN** strip jalur yang sesuai (20 kabel data) |
| ESP32 **GND** | GND semua strip **dan** GND catu 5 V (satu ground) |
| Catu **5 V** | +5V tiap strip, langsung dari rel catu (bukan dari pin ESP32) |
| Kapasitor 1000 µF 6,3 V+ | antara +5V dan GND di catu (ESP32 tetap dari USB) |

- Data 3,3 V dari ESP32 biasanya cukup untuk kabel pendek. Kalau LED berkedip acak atau salah warna, pasang level shifter
  (74HCT245 atau 74AHCT125) di jalur data.
- Catu 5 V 30 A sesuai proposal. Hanya jalur hijau/merah yang menyala dan `LED_BRIGHTNESS` 60/255, jadi arus nyata jauh
  di bawah 800 LED penuh (±16 A).

**Kalau LED aneh:** (1) pastikan GND catu dan GND ESP32 tersambung; (2) pasang resistor 330 Ω di tiap kabel data;
(3) coba level shifter 74HCT245; (4) kurangi `LED_BRIGHTNESS`; (5) uji dulu 1 strip di GPIO 4 (jalur #1).

## Parameter di `receiver.ino`

`ANIMATE` (1 = kepala berjalan, 0 = hijau diam), `CHASE_LEN`, `CHASE_STEP_MS`, `SHOW_BLOCKED_RED`
(1 = jalur kena api merah), `LED_BRIGHTNESS`.

## Tes cepat dengan 1 strip pendek

Tanpa merakit 20 jalur: sambungkan **1 strip** (mis. 40 LED) ke **GPIO 4** (jalur #1).
Di sender: `block j1-j3` → jalur #1 (j1>j2) menyala hijau dengan kepala berjalan. `reset` → jalur #1 mati lagi (karena j1>j3
kembali jadi pilihan terbaik). Jalur #2 (j1-j3) merah hanya terlihat bila strip kedua dipasang di GPIO 5.

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

20 strip, **1 pin data per strip** (tabel pin di bagian "Susunan strip: satu pin per jalur"): GPIO → resistor 330 Ω → DIN strip.
GND ESP32 disambung ke GND semua strip dan GND catu 5 V. Strip dapat +5V langsung dari catu 5 V (30 A di proposal), kapasitor
1000 µF antara +5V dan GND. Tiap jalur 40 LED (`EDGE_LEN[]`, `EDGE_REVERSED[]` bila ada strip terpasang kebalikan).
ESP32 receiver cukup dari USB.

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

---

# UJI 5 STRIP (`dijkstra_5jalur/`) — peta lengkap, 5 strip LED, 5 pin, 5 sensor

Folder `dijkstra_5jalur/` adalah **kode baru khusus uji 5 jalur / 5 pin**. Kode 20 jalur (`dijkstra_strip/`), `dijkstra_graph/`,
dan `led_biasa_2jalur/` tidak diubah.

**Cara kerja:** Dijkstra tetap berjalan di **peta lengkap 20 jalur** (sender, `graph.h`). Strip LED fisik hanya ada di 5 jalur
dengan panjang berbeda, dan receiver hanya menampilkan 5 jalur itu. Sensor api asli juga hanya di 5 jalur itu; api di jalur
lain disimulasikan lewat teks (`block j6-j7`).

| Jalur | Nama | Bobot | LED | Strip (receiver) | Sensor api (sender) |
|---|---|---|---|---|---|
| #3 | j2-j3 | 12 | **7** | **GPIO 4** → 330 Ω → DIN | **GPIO 32** ← DO |
| #11 | j7-j9 | 12 | **8** | **GPIO 5** → 330 Ω → DIN | **GPIO 33** ← DO |
| #15 | j10-j12 | 12 | **8** | **GPIO 13** → 330 Ω → DIN | **GPIO 25** ← DO |
| #18 | j4-e2 | 6 | **6** | **GPIO 14** → 330 Ω → DIN | **GPIO 26** ← DO |
| #19 | j6-e2 | 6 | **5** | **GPIO 16** → 330 Ω → DIN | **GPIO 27** ← DO |

Total 34 LED. Panjang LED ada di `STRIPS[]` (`receiver.ino`); pin sensor di `SENSOR_PIN[]` (`sender.ino`).

## Tampilan

| Kondisi jalur | Tampilan strip |
|---|---|
| Aman, menuju exit (rute tercepat dari ruangan) | **Hijau** redup + kepala terang yang berjalan sekuensial searah exit |
| Kena api (sensor atau teks = 1) | **Merah berkedip** (nyala 400 ms, padam 400 ms) |
| Aman tapi bukan rute | Mati |
| Link putus > 2 detik | Titik biru redup tiap 10 LED |

Parameter di `receiver.ino`: `BLINK_MS`, `ANIMATE` (0 = hijau diam), `CHASE_LEN`, `CHASE_STEP_MS`, `LED_BRIGHTNESS`.

## Skenario (hasil tes di PC; `room all`, semua ruangan dipandu ke exit terdekat)

| Api di (cara) | Strip hijau | Strip merah berkedip | Keterangan |
|---|---|---|---|
| tanpa api | #3, #18, #19 | – | ruang 0,1: j1>j3>j2>e1; ruang 2,3: j5>j4>e2; ruang 4,5: j8>j7>j6>e2 |
| j6-j7 (#9, teks) | #3, #11, #15, #18 | – | ruang 4,5 pindah ke e3 lewat j8>j7>j9>j10>j12>e3; #19 mati |
| j7-j8 (#10, teks) | #3, #15, #18 | – | ruang 4,5 lewat j8>j9>j10>j12>e3 |
| j2-e1 (#17, teks) | #18, #19 | – | ruang 0,1 pindah lewat j3>j4>e2; #3 mati |
| j4-e2 (#18, sensor) | #3, #19 | #18 | ruang 2,3 lewat j5>j4>j6>e2 |
| j6-e2 (#19, sensor) | #3, #18 | #19 | ruang 4,5 lewat j8>j7>j6>j4>e2 |
| j4-e2 dan j6-e2 | #3 | #18, #19 | tinggal jalur ke e1 dan e3 |

## Rangkaian ESP32 #2 — RECEIVER (5 strip)

| Dari | Ke |
|---|---|
| GPIO 4 | resistor 330 Ω → **DIN** strip #3 (j2-j3, 7 LED) |
| GPIO 5 | resistor 330 Ω → **DIN** strip #11 (j7-j9, 8 LED) |
| GPIO 13 | resistor 330 Ω → **DIN** strip #15 (j10-j12, 8 LED) |
| GPIO 14 | resistor 330 Ω → **DIN** strip #18 (j4-e2, 6 LED) |
| GPIO 16 | resistor 330 Ω → **DIN** strip #19 (j6-e2, 5 LED) |
| GND ESP32 | GND semua strip |
| Pin VIN / 5V ESP32 (rel USB 5 V) | +5V semua strip (paralel) |
| Kapasitor 470–1000 µF | antara +5V dan GND di dekat strip (kaki panjang ke +5V) |
| USB ESP32 | laptop |

Arah data strip = dari node pertama ke node kedua nama jalur (#3 mengalir dari j2 ke j3). Kalau terpasang kebalikannya,
set `reversed = true` di `STRIPS[]`. Kalau LED berkedip acak atau salah warna: pasang level shifter 74HCT245 di tiap kabel data.

## Rangkaian ESP32 #1 — SENDER (5 sensor)

| Sensor di jalur | Pin **DO** sensor ke |
|---|---|
| #3 j2-j3 | GPIO 32 |
| #11 j7-j9 | GPIO 33 |
| #15 j10-j12 | GPIO 25 |
| #18 j4-e2 | GPIO 26 |
| #19 j6-e2 | GPIO 27 |
| VCC semua sensor | 3V3 ESP32 |
| GND semua sensor | GND ESP32 |

Pin ini punya pull-up internal, jadi tidak perlu resistor tambahan. Modul flame sensor aktif LOW (DO = 0 saat ada api);
kalau modulmu aktif HIGH ubah `SENSOR_ACTIVE_LEVEL`. Pin `AO` tidak dipakai. Jalur lain (tanpa sensor, pin = -1) hanya bisa
disimulasikan lewat teks.

Perintah sender (Serial Monitor 115200, Newline): `room all`, `room N`, `block j6-j7`, `clear j6-j7`, `reset`, `edges`,
`sensors`, `list`.
Upload: `cd Week7/dijkstra_5jalur/receiver && pio run -t upload` (idem `sender`).

## Daya: cukup dari laptop?

**Ya, untuk uji ini cukup dari USB laptop**, karena hanya 34 LED. Perkiraan (WS2812B, `LED_BRIGHTNESS` 60/255, 20 mA per warna
pada kecerahan penuh):

| Beban | Arus kira-kira |
|---|---|
| 34 LED merah menyala semua (skenario terburuk) | ±0,16 A |
| Hijau redup + kepala terang (normal) | ±0,05–0,1 A |
| LED mati (±1 mA/LED) | ±0,03 A |
| ESP32 receiver (WiFi) | ±0,15–0,25 A |
| **Total terburuk receiver** | **±0,45 A** (di bawah 0,5 A port USB 2.0) |
| ESP32 sender + 5 sensor | ±0,35 A |

Syaratnya `LED_BRIGHTNESS` tetap ≤ 60. Kalau dinaikkan ke 255, merah penuh 34 LED bisa menarik ±0,7 A dan melewati batas USB.
Kalau ESP32 receiver reset sendiri atau LED berkedip acak, pakai power bank atau catu 5 V ≥ 1 A untuk strip (GND disambung ke ESP32).
Angka di atas perkiraan dari datasheet, bukan hasil ukur.
