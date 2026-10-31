/* =================================================================================
 * SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)
 * NODE 2: ESP32-C3 - Timbangan Digital 40KG (Single Load Cell HX711)
 * 
 * Spesifikasi Hardware:
 * - 1x Single Load Cell Batang (Kapasitas Maksimal 40 KG)
 * - Pinout ESP32-C3:
 *     * HX711 DT   -> GPIO 4
 *     * HX711 SCK  -> GPIO 5
 *     * BOOT Key   -> GPIO 9
 *     * VCC        -> 5V (atau 3V3), GND -> GND
 * - Wiring 4 Kabel Load Cell 40KG ke HX711:
 *     * MERAH  -> E+ (Excitation +)
 *     * HITAM  -> E- (Excitation -)
 *     * HIJAU  -> A+ (Signal +)
 *     * PUTIH  -> A- (Signal -)
 * 
 * Fitur:
 * - Pembacaan berat badan presisi 0.00 s/d 40.00 kg.
 * - Kalibrasi Fleksibel via Serial Monitor (Ketik 't' untuk Tare, ketik angka misal '5' untuk 5kg).
 * - Kalibrasi Fleksibel Wireless via ESP-NOW dari Layar CYD Gateway (Channel 11).
 * - Tombol BOOT Fisik (GPIO 9):
 *     * Klik Cepat (< 1.5 detik) : TARE (Nol-kan).
 *     * Tahan Lama (>= 4 detik)  : Kalibrasi Beban Acuan.
 * - Faktor Kalibrasi tersimpan permanen di memori NVS Flash (Preferences.h).
 * ================================================================================= */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "HX711.h"
#include <Preferences.h>

#define WIFI_CHANNEL 11

// ==========================================
// 1. KONFIGURASI PIN & HARDWARE
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
  float berat_kg;   // Berat dalam satuan Kilogram
  int status_kode;  // 0 = Normal, 1 = Tare Berhasil, 2 = Kalibrasi Berhasil
} struct_msg_berat;

struct_msg_berat myData;

// Paket Perintah Terima (CYD Hub -> Node 2)
typedef struct struct_cmd_tare {
  int target_node;  // 2
  int command;      // 1 = Tare, 2 = Calibrate
  float target_kg;  // Custom target weight (contoh: 5.0, 10.0, 20.0, 40.0)
} struct_cmd_tare;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

// ==========================================
// 3. VARIABEL STATE & KALIBRASI
// ==========================================
float cal_factor = 25.0f;            // Default count per gram untuk loadcell 40kg
float currentWeightGrams = 0.0f;
float currentWeightKg = 0.0f;

bool lastButtonState = HIGH;
unsigned long pressTime = 0;
bool actionDone = false;

unsigned long lastSendTime = 0;
unsigned long lastSerialTime = 0;

// ==========================================
// 4. FUNGSI TARE & KALIBRASI
// ==========================================
void executeTare() {
  Serial.println("\n>>> [TARE] Menol-kan timbangan 40kg...");
  if (scale.is_ready()) {
    scale.tare(10);
    currentWeightGrams = 0.0f;
    currentWeightKg = 0.0f;
    myData.status_kode = 1;
    Serial.println(">>> [TARE SUKSES] Timbangan sekarang: 0.00 kg.\n");
  } else {
    Serial.println(">>> [TARE GAGAL] Modul HX711 belum siap!\n");
  }
}

void executeCalibration(float targetKg) {
  if (targetKg < 0.1f || targetKg > 50.0f) {
    Serial.println(">>> [ERROR] Beban kalibrasi harus antara 0.1 kg s/d 50.0 kg!");
    return;
  }

  Serial.printf("\n=====================================================\n");
  Serial.printf(">>> [MEMULAI KALIBRASI LOAD CELL 40KG]\n");
  Serial.printf(">>> Target Beban Acuan: %.2f kg (%.0f gram)\n", targetKg, targetKg * 1000.0f);
  Serial.println(">>> Harap letakkan beban stabil di atas timbangan...");
  delay(1500);

  if (scale.is_ready()) {
    long raw_val = scale.get_value(20); // Ambil rata-rata 20 sampel delta
    float targetGrams = targetKg * 1000.0f;

    if (abs(raw_val) > 100) {
      cal_factor = (float)abs(raw_val) / targetGrams;
      scale.set_scale(cal_factor);
      preferences.putFloat("faktor", cal_factor);

      currentWeightGrams = targetGrams;
      currentWeightKg = targetKg;
      myData.status_kode = 2;

      Serial.println("=====================================================");
      Serial.printf(">>> [KALIBRASI SUKSES!] Faktor Baru: %.4f count/gram\n", cal_factor);
      Serial.printf(">>> Raw Delta: %ld untuk Beban %.2f kg\n", raw_val, targetKg);
      Serial.println(">>> TERSIMPAN PERMANEN DI MEMORI FLASH NVS!");
      Serial.println("=====================================================\n");
    } else {
      Serial.printf(">>> [KALIBRASI GAGAL] Delta raw terlalu kecil (%ld). Pastikan beban terpasang!\n\n", raw_val);
    }
  } else {
    Serial.println(">>> [KALIBRASI GAGAL] Modul HX711 tidak merespons!\n\n");
  }
}

// ==========================================
// 5. CALLBACK PENERIMA ESP-NOW (DARI CYD HUB)
// ==========================================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len >= 8) {
    struct_cmd_tare cmd;
    memset(&cmd, 0, sizeof(cmd));
    memcpy(&cmd, incomingData, min((size_t)len, sizeof(cmd)));

    if (cmd.target_node == 2) {
      if (cmd.command == 1) { // TARE WIRELESS
        Serial.println("\n[ESP-NOW WIRELESS] Menerima instruksi TARE dari CYD Hub!");
        executeTare();
        esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
      }
      else if (cmd.command == 2) { // KALIBRASI WIRELESS
        float targetKg = cmd.target_kg;
        if (targetKg < 0.1f || targetKg > 50.0f) targetKg = 5.0f;

        Serial.printf("\n[ESP-NOW WIRELESS] Menerima instruksi KALIBRASI: %.2f kg dari CYD Hub...\n", targetKg);
        executeCalibration(targetKg);
        esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
      }
    }
  }
}

// ==========================================
// 6. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println("  [NODE 2] TIMBANGAN DIGITAL LOAD CELL 40 KG (HX711)   ");
  Serial.println("  Kapasitas Maksimal: 40.00 KG                         ");
  Serial.println("  Pinout: DT = GPIO 4 | SCK = GPIO 5                   ");
  Serial.println("=======================================================");

  pinMode(PIN_BOOT, INPUT_PULLUP);

  // A. Membaca Memori Flash NVS untuk Kalibrasi
  preferences.begin("timbangan", false);
  cal_factor = preferences.getFloat("faktor", 25.0f);
  if (cal_factor <= 0.001f || cal_factor > 1000.0f) {
    cal_factor = 25.0f;
  }
  Serial.printf("[NVS] Faktor Kalibrasi Aktif: %.4f count/gram\n", cal_factor);

  // B. Inisialisasi Modul HX711 (fastProcessor = true untuk ESP32 160MHz)
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN, true);
  
  unsigned long startWait = millis();
  while (!scale.is_ready() && (millis() - startWait < 1500)) {
    delay(50);
  }

  scale.set_scale(cal_factor);

  if (scale.is_ready()) {
    delay(300);
    scale.tare(10); // Nol-kan beban saat alat pertama menyala
    Serial.println("[OK] HX711 Terdeteksi & Auto-Tared (0.00 kg).");
  } else {
    Serial.println("[WARNING] HX711 belum merespons saat boot. Melanjutkan tanpa freeze...");
  }

  // C. Inisialisasi WiFi & ESP-NOW di Channel 11
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Inisialisasi ESP-NOW Gagal!");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.printf("[OK] ESP-NOW Channel %d Siap (Broadcast Ready).\n", WIFI_CHANNEL);
    }
  }

  myData.node_id = 2;
  myData.berat_kg = 0.0f;
  myData.status_kode = 0;
  Serial.println("=======================================================\n");
  Serial.println(">>> PETUNJUK KALIBRASI SERIAL:");
  Serial.println(">>> 1. Ketik 't' lalu Enter untuk TARE (Nol-kan)");
  Serial.println(">>> 2. Ketik angka bobot (contoh: '5' atau '10.5') lalu Enter untuk KALIBRASI\n");
}

// ==========================================
// 7. LOOP UTAMA
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
        executeTare();
      } else {
        String numStr = input;
        if (numStr.startsWith("cal ") || numStr.startsWith("CAL ")) {
          numStr = numStr.substring(4);
        }
        float targetKg = numStr.toFloat();
        if (targetKg >= 0.1f && targetKg <= 50.0f) {
          executeCalibration(targetKg);
        } else {
          Serial.println(">>> [BANTUAN SERIAL]");
          Serial.println(">>> Ketik 't' lalu Enter untuk TARE (Nol-kan)");
          Serial.println(">>> Ketik angka berat beban (contoh: 5 atau 10.5) untuk KALIBRASI");
        }
      }
    }
  }

  // ----------------------------------------
  // B. BACA DATA LOAD CELL 40KG DENGAN FILTER EMA
  // ----------------------------------------
  long currentRawDelta = 0;
  bool hxReady = scale.is_ready();
  if (hxReady) {
    currentRawDelta = scale.get_value(1);
    float rawGrams = scale.get_units(2);

    // Filter EMA (Exponential Moving Average) 70% lama + 30% baru
    currentWeightGrams = (currentWeightGrams * 0.70f) + (rawGrams * 0.30f);

    float tempGrams = currentWeightGrams;
    // Filter anti-noise timbangan kosong (< 30g dianggap 0)
    if (abs(tempGrams) < 30.0f) {
      tempGrams = 0.0f;
    }
    // Cegah angka negatif getaran
    if (tempGrams < 0.0f && tempGrams > -100.0f) {
      tempGrams = 0.0f;
    }

    currentWeightKg = tempGrams / 1000.0f;

    // Batas maksimal load cell 40kg (proteksi over-capacity display)
    if (currentWeightKg > 45.0f) {
      currentWeightKg = 45.0f;
    }
  }

  // ----------------------------------------
  // C. SMART BUTTON BOOT (GPIO 9)
  // ----------------------------------------
  int reading = digitalRead(PIN_BOOT);

  if (reading == LOW && lastButtonState == HIGH) {
    pressTime = millis();
    actionDone = false;
  }

  // 1. TAHAN LAMA (>= 4 DETIK) = KALIBRASI ACUAN (5 KG)
  if (reading == LOW && !actionDone && (millis() - pressTime >= 4000)) {
    Serial.println("\n[KALIBRASI TOMBOL] Memulai kalibrasi beban acuan 5.0 kg...");
    executeCalibration(5.0f);
    actionDone = true;
  }

  // 2. KLIK CEPAT (< 1.5 DETIK) = TARE (NOL-KAN)
  if (reading == HIGH && lastButtonState == LOW) {
    long duration = millis() - pressTime;
    if (duration > 50 && duration < 1500 && !actionDone) {
      executeTare();
      actionDone = true;
    }
  }
  lastButtonState = reading;

  // ----------------------------------------
  // D. TRANSMISI ESP-NOW (Tiap 200ms)
  // ----------------------------------------
  if (millis() - lastSendTime >= 200) {
    lastSendTime = millis();

    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    myData.node_id = 2;
    myData.berat_kg = currentWeightKg;

    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

    if (myData.status_kode != 0) {
      myData.status_kode = 0;
    }
  }

  // ----------------------------------------
  // E. SERIAL MONITOR DIAGNOSTIK (Tiap 400ms)
  // ----------------------------------------
  if (millis() - lastSerialTime >= 400) {
    lastSerialTime = millis();
    if (hxReady) {
      Serial.printf("[NODE 2 40KG] Berat: %.2f kg (%.0f g) | Raw Delta: %ld | Faktor: %.4f | ESP-NOW: OK\n",
        currentWeightKg, currentWeightGrams, currentRawDelta, cal_factor);
    } else {
      Serial.printf("[NODE 2 WARNING] HX711 TIDAK READY (Cek kabel DT di GPIO4, SCK di GPIO5, VCC 5V)\n");
    }
  }
}
