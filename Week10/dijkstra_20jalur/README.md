# Week 10 — Safe Exit Dijkstra 20 Jalur

Implementasi dua ESP32 untuk peta 15 node dan 20 jalur. Sender membaca 10 sensor api digital aktif-LOW pada edge 1–10, menerima pemblokiran manual edge 11–20 melalui Serial Monitor, lalu menghitung rute terpendek ke exit terdekat untuk 8 ruangan memakai Dijkstra. Hasil rute dikirim melalui ESP-NOW ke receiver.

Receiver mengendalikan 20 strip WS2812B: hijau untuk jalur rute aman dan merah untuk jalur yang diblokir atau tidak dipakai.

## Struktur

- `sender/`: unggah `sender.ino` ke ESP32 pengolah sensor dan Dijkstra.
- `receiver/`: unggah `receiver.ino` ke ESP32 pengendali LED. Dependensi `Adafruit NeoPixel` dipasang otomatis oleh PlatformIO.

## Penggunaan

1. Buka masing-masing folder `sender` dan `receiver` sebagai proyek PlatformIO.
2. Unggah receiver dan sender; keduanya menggunakan kanal ESP-NOW 1 dan broadcast, sehingga tidak perlu mengubah MAC address.
3. Buka Serial Monitor sender pada 115200 baud. Untuk edge 11–20, gunakan `<nomor> ON` untuk memblokir dan `<nomor> OFF` untuk membuka, misalnya `11 ON`.
4. Ketik `HELP` di Serial Monitor sender untuk daftar jalur manual dan instruksi lengkap.

## Catatan perangkat keras

- Sensor pada sender aktif-LOW dan masuk ke GPIO 4, 13, 14, 18, 19, 21, 22, 23, 25, dan 26.
- Satukan GND sensor, kedua ESP32, serta catu daya LED.
- WS2812B harus memakai catu daya 5 V eksternal yang memadai.
- Receiver memakai GPIO 1 dan GPIO 3 untuk dua strip. Lepaskan kabel data pada kedua pin tersebut ketika mengunggah program.
