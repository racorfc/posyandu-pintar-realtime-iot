/* =================================================================================
 * SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)
 * NODE 3: ESP32-C3 - Pengukur Tinggi Badan Balita (HC-SR04 / VL6180X)
 * 
 * Fitur:
 * - Pengukuran jarak pantul ke puncak kepala balita.
 * - Perhitungan tinggi balita otomatis: (Tinggi Tiang Acuan - Jarak Pantul).
 * - Filter Moving Average untuk kestabilan pembacaan milimeter/sentimeter.
 * - Deteksi otomatis ketika tidak ada balita di bawah tiang (tinggi kembali ke 0).
 * - Mengirimkan data tinggi badan (cm) via ESP-NOW Broadcast ke CYD Hub.
 * 
 * Pinout ESP32-C3 (HC-SR04):
 * - TRIG Pin -> GPIO 1
 * - ECHO Pin -> GPIO 10 (dengan voltage divider R1=1k, R2=2k jika sensor 5V)
 * - VCC      -> 5V / 3.3V, GND -> GND
 * ================================================================================= */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define WIFI_CHANNEL 11

// ==========================================
// 1. KONFIGURASI HARDWARE & PARAMETER TIANG
// ==========================================
#define TRIG_PIN 1   // GPIO 1 -> Pin Trig HC-SR04
#define ECHO_PIN 10  // GPIO 10 -> Pin Echo HC-SR04

// Sesuaikan konstanta ini dengan tinggi fisik tiang pengukur Posyandu Anda:
const float TINGGI_TIANG_CM = 200.0; // Standar Acuan Tiang: 200.0 cm (2.0 Meter)
const float BATAS_MIN_BALITA = 30.0;  // Batas minimum tinggi bayi wajar (cm)
const float BATAS_MAX_BALITA = 190.0; // Batas maksimum tinggi wajar (cm)

// ==========================================
// 2. STRUKTUR PAKET ESP-NOW
// ==========================================
typedef struct struct_msg_tinggi {
  int node_id;         // 3 = Pengukur Tinggi Badan
  float tinggi_cm;     // Tinggi balita (cm) -> 200 - jarak (jika pas 200cm, nilai = 0cm)
  int status_kode;     // 0 = Normal, 1 = Kosong / Di Luar Rentang
  float raw_jarak_cm;  // Jarak pantul mentah sensor ke lantai (cm)
} struct_msg_tinggi;

struct_msg_tinggi myData;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ==========================================
// 3. VARIABEL FILTER & TIMING
// ==========================================
const int FILTER_SAMPLES = 5;
float distanceBuffer[FILTER_SAMPLES];
int bufIndex = 0;

float currentTinggiCm = 0.0;
float lastDistanceMeasured = 0.0;
unsigned long lastMeasureTime = 0;
unsigned long lastSendTime = 0;
unsigned long lastSerialTime = 0;

// ==========================================
// 4. FUNGSI PENGUKURAN JARAK ULTRASONIK
// ==========================================
float ukurJarakCm() {
  // 1. Bersihkan pin Trigger
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // 2. Kirim pulsa ultrasonik 10us
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 3. Baca waktu pantul pulsa (timeout 25ms ~ 4 meter)
  long duration = pulseIn(ECHO_PIN, HIGH, 25000);

  if (duration == 0) {
    return -1.0; // Sensor timeout / tidak ada pantulan
  }

  // Kecepatan suara 0.0343 cm/us dibagi 2 (bolak-balik)
  float jarakCm = (duration * 0.0343) / 2.0;
  return jarakCm;
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n================================================");
  Serial.println("  [NODE 3] PENGUKUR TINGGI BALITA (HC-SR04)    ");
  Serial.println("================================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Inisialisasi buffer filter
  for (int i = 0; i < FILTER_SAMPLES; i++) {
    distanceBuffer[i] = TINGGI_TIANG_CM;
  }

  // Inisialisasi WiFi Station & ESP-NOW di Channel 11
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Inisialisasi ESP-NOW Gagal!");
    return;
  }

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Gagal Daftarkan Peer Broadcast!");
  } else {
    Serial.printf("[OK] ESP-NOW Node 3 Siap Beroperasi di Channel %d.\n", WIFI_CHANNEL);
  }

  myData.node_id = 3;
  myData.tinggi_cm = 0.0;
  myData.status_kode = 1;
}

// ==========================================
// 6. LOOP UTAMA
// ==========================================
void loop() {
  // ----------------------------------------
  // A. BACA SENSOR & HITUNG TINGGI (Tiap 100ms)
  // ----------------------------------------
  if (millis() - lastMeasureTime >= 100) {
    lastMeasureTime = millis();

    float jarakPantul = ukurJarakCm();

    if (jarakPantul > 5.0 && jarakPantul < (TINGGI_TIANG_CM + 10.0)) {
      // Masukkan ke buffer rata-rata
      distanceBuffer[bufIndex] = jarakPantul;
      bufIndex = (bufIndex + 1) % FILTER_SAMPLES;

      float avgJarak = 0.0;
      for (int i = 0; i < FILTER_SAMPLES; i++) {
        avgJarak += distanceBuffer[i];
      }
      avgJarak /= (float)FILTER_SAMPLES;
      lastDistanceMeasured = avgJarak;

      // Hitung tinggi badan balita:
      float hitungTinggi = TINGGI_TIANG_CM - avgJarak;

      // Cek apakah ada objek/balita yang wajar:
      if (hitungTinggi >= BATAS_MIN_BALITA && hitungTinggi <= BATAS_MAX_BALITA) {
        currentTinggiCm = hitungTinggi;
        myData.status_kode = 0; // Terdeteksi balita
      } else {
        // Objek terlalu dekat dengan lantai atau tidak ada balita
        currentTinggiCm = 0.0;
        myData.status_kode = 1;
      }
    } else {
      // Sensor out of range / tiang kosong
      lastDistanceMeasured = jarakPantul;
      currentTinggiCm = 0.0;
      myData.status_kode = 1;
    }
  }

  // ----------------------------------------
  // B. TRANSMISI DATA ESP-NOW (Tiap 250ms)
  // ----------------------------------------
  if (millis() - lastSendTime >= 250) {
    lastSendTime = millis();

    // Pastikan channel tetap 11
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    myData.node_id = 3;
    myData.tinggi_cm = currentTinggiCm;
    myData.raw_jarak_cm = lastDistanceMeasured;

    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
  }

  // ----------------------------------------
  // C. SERIAL MONITOR DIAGNOSTIK (Tiap 500ms)
  // ----------------------------------------
  if (millis() - lastSerialTime >= 500) {
    lastSerialTime = millis();
    Serial.printf("[NODE 3] Jarak Sensor: %.1f cm | Tinggi: %.1f cm | Status: %s | ESP-NOW: TERKIRIM\n",
      lastDistanceMeasured,
      currentTinggiCm,
      myData.status_kode == 0 ? "ADA BALITA" : "STANDBY / KOSONG");
  }
}
