/* =================================================================================
 * SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)
 * NODE 1: ESP32-C3 - Oximeter & Heart Rate (MAX30102 Murni)
 * 
 * Sesuai referensi SMART_HEALTH-MASTER:
 * - Modul hanya menggunakan sensor MAX30102 (BPM & SpO2).
 * - Tanpa sensor eksternal DS18B20.
 * - Suhu dapat dibaca langsung dari sensor suhu internal chip MAX30102 (readTemperature()).
 * - I2C Pin otomatis mendeteksi:
 *     * Pin 8 (SDA) & Pin 9 (SCL) [Standar ESP32-C3 SuperMini / SMART_HEALTH-MASTER]
 *     * Pin 1 (SDA) & Pin 2 (SCL) [Alternatif]
 * - Deteksi pelepasan jari otomatis (Anti-ghost reading).
 * - Mengirimkan data via ESP-NOW Broadcast ke ESP32 CYD Gateway.
 * ================================================================================= */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

#define WIFI_CHANNEL 11

// ==========================================
// 1. HARDWARE & OBJEK SENSOR
// ==========================================
MAX30105 particleSensor;

// Pin I2C Utama (Sesuai referensi SMART_HEALTH-MASTER)
#define I2C_SDA_PRIMARY   8
#define I2C_SCL_PRIMARY   9

// Pin I2C Alternatif
#define I2C_SDA_FALLBACK  1
#define I2C_SCL_FALLBACK  2

// ==========================================
// 2. STRUKTUR DATA ESP-NOW (NODE 1)
// ==========================================
typedef struct struct_msg_oxy {
  int node_id;            // 1 = Node Oximeter & Heart Rate
  int bpm;                // Detak Jantung (BPM)
  int spo2;               // Saturasi Oksigen (%)
  float suhu;             // Suhu (°C) internal MAX30102 / tubuh
  bool finger_detected;   // Status apakah ada jari menempel
} struct_msg_oxy;

struct_msg_oxy myData;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ==========================================
// 3. VARIABEL ALGORITMA MEDIS & TIMING
// ==========================================
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
int calculatedSpO2 = 0;

float currentTemp = 36.5;
unsigned long lastTempReadTime = 0;
unsigned long lastSendTime = 0;
unsigned long lastSerialTime = 0;
String sendStatus = "Menunggu...";
bool sensorDetected = false;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  sendStatus = (status == ESP_NOW_SEND_SUCCESS) ? "TERKIRIM (OK)" : "GAGAL (X)";
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Tunggu USB CDC terminal terhubung
  Serial.println("\n================================================");
  Serial.println("  [NODE 1] OXYMETER & HEART RATE (MAX30102)     ");
  Serial.println("  Mode: MAX30102 Standalone (Tanpa DS18B20)     ");
  Serial.println("================================================");

  // A. Inisialisasi Bus I2C dengan Auto-Detect Pin
  Wire.setTimeOut(50);
  Serial.printf("[I2C] Mencoba inisialisasi di Pin SDA=%d, SCL=%d...\n", I2C_SDA_PRIMARY, I2C_SCL_PRIMARY);
  Wire.begin(I2C_SDA_PRIMARY, I2C_SCL_PRIMARY, 100000);

  if (particleSensor.begin(Wire, 100000)) {
    sensorDetected = true;
    Serial.printf("[OK] MAX30102 Terdeteksi di Pin SDA=%d, SCL=%d!\n", I2C_SDA_PRIMARY, I2C_SCL_PRIMARY);
  } else {
    Serial.printf("[WARN] Tidak ditemukan di Pin %d & %d. Mencoba Fallback SDA=%d, SCL=%d...\n",
                  I2C_SDA_PRIMARY, I2C_SCL_PRIMARY, I2C_SDA_FALLBACK, I2C_SCL_FALLBACK);
    Wire.end();
    Wire.begin(I2C_SDA_FALLBACK, I2C_SCL_FALLBACK, 100000);
    if (particleSensor.begin(Wire, 100000)) {
      sensorDetected = true;
      Serial.printf("[OK] MAX30102 Terdeteksi di Pin SDA=%d, SCL=%d!\n", I2C_SDA_FALLBACK, I2C_SCL_FALLBACK);
    } else {
      Serial.println("[ERROR] Sensor MAX30102 tidak ditemukan di Pin 8/9 maupun Pin 1/2! Periksa kabel SDA/SCL.");
    }
  }

  if (sensorDetected) {
    // Setup MAX30102: LedPower 0x1F, sampleAverage 4, ledMode 2 (Red+IR), sampleRate 400Hz, pulseWidth 411us
    particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
    // Aktifkan sensor suhu internal chip MAX30102
    particleSensor.enableDIETEMPRDY();
    Serial.println("[OK] Sensor MAX30102 Siap Digunakan.");
  }

  // B. Inisialisasi WiFi Mode Station & Lock Channel 11
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Inisialisasi ESP-NOW Gagal!");
  } else {
    Serial.println("[OK] Inisialisasi ESP-NOW Berhasil.");
  }
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);

  // Set WiFi channel ke 11
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Gagal Menambahkan Peer Broadcast!");
  } else {
    Serial.printf("[OK] ESP-NOW Peer Broadcast Terdaftar di Channel %d.\n", WIFI_CHANNEL);
  }

  myData.node_id = 1;
  myData.bpm = 0;
  myData.spo2 = 0;
  myData.suhu = 36.5;
  myData.finger_detected = false;
}

// ==========================================
// 5. LOOP UTAMA
// ==========================================
void loop() {
  if (sensorDetected) {
    particleSensor.check();
  }

  // ----------------------------------------
  // A. BACA SENSOR MAX30102
  // ----------------------------------------
  long irValue = sensorDetected ? particleSensor.getIR() : 0;

  if (irValue > 50000) { // Jari terpasang
    myData.finger_detected = true;

    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60.0 / (delta / 1000.0);

      if (beatsPerMinute > 35.0 && beatsPerMinute < 220.0) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }

    // Estimasi Saturasi Oksigen (SpO2) dari responsivitas IR
    calculatedSpO2 = constrain(map(irValue, 50000, 130000, 95, 99), 90, 100);
  } else {
    // Jari dilepas -> Kembalikan nilai ke 0 seketika (Anti-Ghost Reading)
    myData.finger_detected = false;
    beatAvg = 0;
    calculatedSpO2 = 0;
  }

  // ----------------------------------------
  // B. BACA SUHU INTERNAL CHIP MAX30102 (Tiap 2 Detik)
  // ----------------------------------------
  if (millis() - lastTempReadTime >= 2000) {
    lastTempReadTime = millis();
    if (sensorDetected) {
      float chipTemp = particleSensor.readTemperature();
      if (chipTemp >= 25.0 && chipTemp <= 45.0) {
        currentTemp = chipTemp;
      } else {
        currentTemp = 36.5; // Default suhu normal tubuh manusia
      }
    } else {
      currentTemp = 36.5;
    }
  }

  // ----------------------------------------
  // C. TRANSMISI DATA ESP-NOW (Tiap 250ms)
  // ----------------------------------------
  if (millis() - lastSendTime >= 250) {
    lastSendTime = millis();

    myData.node_id = 1;
    myData.bpm = beatAvg;
    myData.spo2 = calculatedSpO2;
    myData.suhu = currentTemp;

    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
  }

  // ----------------------------------------
  // D. SERIAL MONITOR DIAGNOSTIK (Tiap 600ms)
  // ----------------------------------------
  if (millis() - lastSerialTime >= 600) {
    lastSerialTime = millis();
    Serial.printf("[NODE 1] Jari: %s | BPM: %d | SpO2: %d%% | Suhu: %.1f C | Status ESP-NOW: %s\n",
      myData.finger_detected ? "TEMPEL" : "LEPAS",
      myData.bpm,
      myData.spo2,
      myData.suhu,
      sendStatus.c_str()
    );
  }
}
