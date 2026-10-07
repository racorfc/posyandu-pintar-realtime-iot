/* =================================================================================
 * SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)
 * NODE 2: ESP32-C3 - Timbangan Digital Balita (HX711 Load Cell)
 * 
 * Fitur:
 * - Pembacaan berat badan presisi dalam Kilogram (kg).
 * - Tombol BOOT Multifungsi (GPIO 9):
 *     * Klik Cepat (< 1.5 detik) : TARE (Nol-kan timbangan).
 *     * Tahan Lama (>= 4 detik)  : KALIBRASI BEBAN ACUAN (misal 900g).
 * - Faktor kalibrasi tersimpan permanen di memori NVS Flash (Preferences.h).
 * - Menerima perintah TARE jarak jauh via ESP-NOW dari ESP32 CYD Gateway.
 * - Mengirimkan data berat badan secara real-time via ESP-NOW Broadcast.
 * 
 * Pinout ESP32-C3:
 * - HX711 DT   -> GPIO 4
 * - HX711 SCK  -> GPIO 5
 * - BOOT Key   -> GPIO 9
 * - VCC        -> 3.3V / 5V, GND -> GND
 * ================================================================================= */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "HX711.h"
#include <Preferences.h>

#define WIFI_CHANNEL 11

// ==========================================
// 1. KONFIGURASI HARDWARE & PIN
// ==========================================
const int LOADCELL_DOUT_PIN = 4; // DT HX711
const int LOADCELL_SCK_PIN  = 5; // SCK HX711
const int PIN_BOOT          = 9; // Tombol internal BOOT ESP32-C3

HX711 scale;
Preferences preferences;

// ==========================================
// 2. STRUKTUR PAKET ESP-NOW
// ==========================================
// Paket Kirim (Node 2 -> CYD Hub)
typedef struct struct_msg_berat {
  int node_id;      // 2 = Timbangan
  float berat_kg;   // Berat dalam satuan Kilogram (contoh: 12.45 kg)
  int status_kode;  // 0 = Normal, 1 = Tare Berhasil, 2 = Kalibrasi Berhasil
} struct_msg_berat;

struct_msg_berat myData;

// Paket Perintah Terima (CYD Hub -> Node 2)
typedef struct struct_cmd_tare {
  int target_node;  // 2
  int command;      // 1 = Tare, 2 = Calibrate
  float target_kg;  // Custom target weight (contoh: 57.4, 60.0, 19.0)
} struct_cmd_tare;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ==========================================
// 3. VARIABEL KALIBRASI & TIMING
// ==========================================
// Berdasarkan data riil beban 920g: Delta Raw = 2971 - 1100 = 1871 count
// Faktor per gram = 1871 / 920 = 2.0334 count/gram
float cal_factor = 2.0334;            // Nilai kalibrasi presisi beban 920g
const float BEBAN_KALIBRASI_G = 920.0;// Beban referensi 920 gram
float currentWeightGrams = 0.0;
float currentWeightKg = 0.0;

bool lastButtonState = HIGH;
unsigned long pressTime = 0;
bool actionDone = false;

unsigned long lastSendTime = 0;
unsigned long lastSerialTime = 0;

// ==========================================
// 4. CALLBACK PENERIMA ESP-NOW (REMOTE TARE & KALIBRASI)
// ==========================================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len >= 8) {
    struct_cmd_tare cmd;
    memset(&cmd, 0, sizeof(cmd));
    memcpy(&cmd, incomingData, min((size_t)len, sizeof(cmd)));

    if (cmd.target_node == 2) {
      if (cmd.command == 1) { // TARE
        Serial.println("\n[ESP-NOW WIRELESS] Menerima instruksi TARE dari CYD Hub!");
        scale.tare(20);
        currentWeightGrams = 0.0f;
        currentWeightKg = 0.0f;
        myData.status_kode = 1;
        esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
        Serial.println("[ESP-NOW WIRELESS] TARE Berhasil dieksekusi!");
      }
      else if (cmd.command == 2) { // KALIBRASI BEBAN KUSTOM
        float targetKg = cmd.target_kg;
        if (targetKg < 0.5f || targetKg > 200.0f) targetKg = 57.4f;

        Serial.printf("\n[ESP-NOW WIRELESS] Menerima instruksi KALIBRASI BEBAN: %.2f kg dari CYD Hub...\n", targetKg);
        delay(300);
        long raw_val = scale.get_value(25); // Ambil rata-rata 25 sampel presisi
        float targetGrams = targetKg * 1000.0f;
        cal_factor = (float)raw_val / targetGrams;

        scale.set_scale(cal_factor);
        preferences.putFloat("faktor", cal_factor);
        currentWeightGrams = targetGrams;
        currentWeightKg = targetKg;
        myData.status_kode = 2; // Kode Kalibrasi Sukses

        esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

        Serial.println("=====================================================");
        Serial.printf("[ESP-NOW WIRELESS] KALIBRASI SUKSES! Faktor: %.4f\n", cal_factor);
        Serial.printf("[ESP-NOW WIRELESS] Raw Delta: %ld untuk Beban %.2f kg\n", raw_val, targetKg);
        Serial.println("=====================================================");
      }
    }
  }
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n================================================");
  Serial.println("  [NODE 2] TIMBANGAN DIGITAL BALITA (HX711)    ");
  Serial.println("  Kalibrasi Beban Acuan: 920 Gram              ");
  Serial.println("================================================");

  pinMode(PIN_BOOT, INPUT_PULLUP);

  // A. Membaca Memori Flash NVS untuk Kalibrasi
  preferences.begin("timbangan", false);
  cal_factor = preferences.getFloat("faktor", 2.0334f);
  if (cal_factor == 420.0f || cal_factor <= 0.0f || cal_factor == 1.0f) {
    cal_factor = 2.0334f;
    preferences.putFloat("faktor", cal_factor);
  }
  Serial.printf("[NVS] Faktor Kalibrasi Aktif: %.4f (1871 count / 920g)\n", cal_factor);

  // B. Inisialisasi Modul HX711 & Auto-Tare
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  unsigned long startWait = millis();
  while (!scale.is_ready() && (millis() - startWait < 2000)) {
    delay(50);
  }
  delay(1000); // Tunggu filter internal HX711 stabil
  scale.set_scale(cal_factor);
  scale.tare(20); // Nol-kan beban saat pertama kali alat menyala (20 sampel)
  Serial.println("[OK] HX711 Siap & Auto-Tared (0.00 kg).");

  // C. Inisialisasi WiFi & ESP-NOW di Channel 11
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Inisialisasi ESP-NOW Gagal!");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Gagal Daftarkan Peer Broadcast!");
  } else {
    Serial.printf("[OK] ESP-NOW Siap Berkomunikasi di Channel %d.\n", WIFI_CHANNEL);
  }

  myData.node_id = 2;
  myData.berat_kg = 0.0;
  myData.status_kode = 0;
}

// ==========================================
// 6. LOOP UTAMA
// ==========================================
void loop() {
  // ----------------------------------------
  // A. INTERACTIVE SERIAL CALIBRATION COMMAND
  // ----------------------------------------
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      if (input.equalsIgnoreCase("t") || input.equalsIgnoreCase("tare") || input == "0") {
        Serial.println("\n>>> [TARE] Menol-kan timbangan...");
        scale.tare(20);
        currentWeightGrams = 0.0f;
        currentWeightKg = 0.0f;
        Serial.println(">>> [TARE SELESAI] Nilai timbangan sekarang: 0.00 kg.\n");
      } else {
        String numStr = input;
        if (numStr.startsWith("cal ") || numStr.startsWith("CAL ")) {
          numStr = numStr.substring(4);
        }
        float targetKg = numStr.toFloat();
        if (targetKg > 0.5f && targetKg <= 200.0f) {
          Serial.printf("\n>>> [MEMULAI KALIBRASI] Target Beban: %.2f kg\n", targetKg);
          Serial.println(">>> Harap berdiri diam & tenang di atas timbangan selama 2 detik...");
          delay(1500);
          long raw_val = scale.get_value(25); // Ambil rata-rata 25 sampel presisi
          float targetGrams = targetKg * 1000.0f;
          cal_factor = (float)raw_val / targetGrams;

          scale.set_scale(cal_factor);
          preferences.putFloat("faktor", cal_factor);
          currentWeightGrams = targetGrams;
          currentWeightKg = targetKg;

          Serial.println("=====================================================");
          Serial.printf(">>> [KALIBRASI SUKSES] Faktor Baru: %.4f\n", cal_factor);
          Serial.printf(">>> Raw Delta: %ld untuk Beban %.2f kg\n", raw_val, targetKg);
          Serial.println(">>> FAKTOR KALIBRASI TERSIMPAN DI MEMORI FLASH!");
          Serial.println("=====================================================\n");
          myData.status_kode = 2;
        } else {
          Serial.println(">>> [BANTUAN] Ketik 't' untuk Tare, atau ketik angka berat acuan (contoh: 57.4)");
        }
      }
    }
  }

  // ----------------------------------------
  // B. BACA DATA LOAD CELL DENGAN FILTER PEREDAM AYUNAN (EMA)
  // ----------------------------------------
  long currentRaw = 0;
  if (scale.is_ready()) {
    currentRaw = scale.get_value(1);
    float rawGrams = scale.get_units(3);

    // Filter EMA (Exponential Moving Average) 70% data lama + 30% data baru
    // Menghilangkan efek "ngayun" naik turun secara instan
    currentWeightGrams = (currentWeightGrams * 0.70f) + (rawGrams * 0.30f);

    float tempGrams = currentWeightGrams;
    // Filter anti-noise saat timbangan kosong (di bawah 50g dianggap nol)
    if (abs(tempGrams) < 50.0f) {
      tempGrams = 0.0f;
    }
    // Cegah angka negatif getaran lantai
    if (tempGrams < 0.0f && tempGrams > -100.0f) {
      tempGrams = 0.0f;
    }

    currentWeightKg = tempGrams / 1000.0f; // Konversi gram ke kilogram
  }

  // ----------------------------------------
  // C. SMART BUTTON BOOT (TARE & KALIBRASI FISIK)
  // ----------------------------------------
  int reading = digitalRead(PIN_BOOT);

  // Tombol mulai ditekan
  if (reading == LOW && lastButtonState == HIGH) {
    pressTime = millis();
    actionDone = false;
  }

  // 1. TAHAN LAMA (>= 4 DETIK) = KALIBRASI ULANG FISIK
  if (reading == LOW && !actionDone && (millis() - pressTime >= 4000)) {
    Serial.println("\n[KALIBRASI TOMBOL] Memulai kalibrasi fisik 57.4kg...");
    long raw_val = scale.get_value(20);
    cal_factor = (float)raw_val / (57.4f * 1000.0f);

    scale.set_scale(cal_factor);
    preferences.putFloat("faktor", cal_factor);

    Serial.printf("[KALIBRASI SELESAI] Faktor baru: %.4f tersimpan di NVS Flash!\n", cal_factor);
    myData.status_kode = 2;
    actionDone = true;
  }

  // 2. KLIK CEPAT (< 1.5 DETIK) = TARE (NOL-KAN)
  if (reading == HIGH && lastButtonState == LOW) {
    long duration = millis() - pressTime;
    if (duration > 50 && duration < 1500 && !actionDone) {
      Serial.println("\n[TARE TOMBOL] Menol-kan timbangan...");
      scale.tare(20);
      currentWeightGrams = 0.0f;
      myData.status_kode = 1;
      actionDone = true;
    }
  }
  lastButtonState = reading;

  // ----------------------------------------
  // D. TRANSMISI ESP-NOW (Tiap 200ms)
  // ----------------------------------------
  if (millis() - lastSendTime >= 200) {
    lastSendTime = millis();

    // Pastikan channel tetap 11
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    myData.node_id = 2;
    myData.berat_kg = currentWeightKg;

    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

    // Reset status kode setelah terkirim
    if (myData.status_kode != 0) {
      myData.status_kode = 0;
    }
  }

  // ----------------------------------------
  // E. SERIAL MONITOR DIAGNOSTIK (Tiap 500ms)
  // ----------------------------------------
  if (millis() - lastSerialTime >= 500) {
    lastSerialTime = millis();
    Serial.printf("[NODE 2] Berat: %.2f kg (%.0f gram | Raw: %ld) | Faktor: %.4f | ESP-NOW: TERKIRIM\n",
      currentWeightKg, currentWeightGrams, currentRaw, cal_factor);
  }
}
