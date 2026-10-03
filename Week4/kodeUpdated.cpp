#include <Adafruit_NeoPixel.h>

// Konfigurasi Pin ESP32
#define LED_PIN 13   // Pin Data LED Strip WS2812B
#define NUM_LEDS 7   // Jumlah LED (7 butir)
#define MQ2_PIN 34   // Sensor Asap (Pin GPIO 34)
#define FLAME_PIN 35 // Sensor Api (Pin GPIO 35)

// Ambang Batas Deteksi Sensor
const int THRESHOLD_FLAME = 1500; // Flame Sensor: Nilai di bawah angka ini menandakan adanya api
const int DELTA_MQ2 = 200;        // MQ-2: Kenaikan nilai minimum dari kondisi udara normal

int baselineMQ2 = 0;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup()
{
  Serial.begin(115200);
  strip.begin();
  strip.clear();
  strip.show(); // Memastikan seluruh LED mati pada saat awal

  Serial.println("==============================================");
  Serial.println("Proses Kalibrasi Udara Normal (Harap Tunggu 5 Detik)...");

  // Mengambil nilai rata-rata udara bersih awal untuk sensor MQ-2
  long totalMQ2 = 0;
  for (int i = 0; i < 50; i++)
  {
    totalMQ2 += analogRead(MQ2_PIN);
    delay(100);
  }
  baselineMQ2 = totalMQ2 / 50;

  Serial.print("Kalibrasi Selesai. Nilai Acuan Udara: ");
  Serial.println(baselineMQ2);
  Serial.println("==============================================");
}

void loop()
{
  int nilaiMQ2 = analogRead(MQ2_PIN);
  int nilaiFlame = analogRead(FLAME_PIN);

  // Evaluasi Kondisi Bahaya
  bool terdeteksiAsap = (nilaiMQ2 > (baselineMQ2 + DELTA_MQ2));
  bool terdeteksiApi = (nilaiFlame < THRESHOLD_FLAME);

  if (terdeteksiAsap || terdeteksiApi)
  {
    Serial.print("TERDETEKSI BAHAYA! Asap: ");
    Serial.print(nilaiMQ2);
    Serial.print(" | Api: ");
    Serial.println(nilaiFlame);

    // Jalankan animasi sekuensial warna merah ke satu arah (LED index 0 ke 6)
    animasiMerahSekuensial();
  }
  else
  {
    Serial.println("Kondisi Aman. LED Matikan.");
    strip.clear();
    strip.show();
    delay(200); // Penundaan pembacaan ulang saat kondisi aman
  }
}

// Fungsi Animasi Sekuensial Warna Merah ke Satu Arah
void animasiMerahSekuensial()
{
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++)
  {
    strip.setPixelColor(i, strip.Color(255, 0, 0)); // Warna Merah (Red=255, Green=0, Blue=0)
    strip.show();
    delay(100); // Kecepatan pergerakan nyala LED (milidetik)
  }
  delay(150); // Jeda singkat sebelum mengulang animasi dari awal
}