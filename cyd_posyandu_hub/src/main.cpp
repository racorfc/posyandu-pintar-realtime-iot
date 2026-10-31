/* =================================================================================
 * SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)
 * MASTER HUB & GATEWAY: ESP32 CYD 2.8" (ESP32-2432S028R)
 * 
 * Fitur Lengkap:
 * 1. TOUCHSCREEN WIFI SCANNER & VIRTUAL QWERTY KEYBOARD:
 *    - Pengguna / Kader dapat mencari WiFi di sekitar dan memasukkan password
 *      langsung melalui keyboard sentuh di layar CYD tanpa perlu utak-atik kode!
 *    - Nama WiFi & Password tersimpan otomatis di NVS Flash (Preferences.h).
 * 2. MULTI-SCREEN SYSTEM:
 *    - SCREEN_DASHBOARD: Tampilan utama monitoring posyandu untuk kader.
 *    - SCREEN_TEST_NODE: Halaman Lab/Simulator interaktif untuk menguji tiap node
 *      (Timbangan, Tinggi, Oximeter, Suhu) via sentuhan tanpa sensor fisik C3.
 *    - SCREEN_WIFI_SCAN, KEYBOARD, CONNECTING: Manajemen WiFi interaktif.
 * 3. CORE 0 (FreeRTOS Background Task):
 *    - Streaming telemetri berkala (PATCH /posyandu/monitoring.json).
 *    - Simpan data balita permanen (POST /posyandu/pemeriksaan.json).
 *    - Remote control Tare dari Web Dashboard (GET /posyandu/control.json).
 * 4. CORE 1:
 *    - UI 320x240 Landscape ILI9341 & XPT2046 Touch Controller.
 *    - ESP-NOW Receiver 3 Node.
 * ================================================================================= */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

#define WIFI_FIXED_CHANNEL 11
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <vector>

// ==========================================
// 1. KONFIGURASI FIREBASE RTDB
// ==========================================
const char *RTDB_HOST            = "https://planning-with-ai-f6a7c-default-rtdb.asia-southeast1.firebasedatabase.app";
const char *RTDB_MONITORING_URL  = "https://planning-with-ai-f6a7c-default-rtdb.asia-southeast1.firebasedatabase.app/posyandu/monitoring.json";
const char *RTDB_PEMERIKSAAN_URL = "https://planning-with-ai-f6a7c-default-rtdb.asia-southeast1.firebasedatabase.app/posyandu/pemeriksaan.json";
const char *RTDB_CONTROL_URL     = "https://planning-with-ai-f6a7c-default-rtdb.asia-southeast1.firebasedatabase.app/posyandu/control.json";

// Default Fallback WiFi (Jika belum diset via Touchscreen)
String savedSSID = "";
String savedPass = "";

// ==========================================
// 2. PIN PERIPHERAL CYD 2.8"
// ==========================================
#define CYD_LED_R       4
#define CYD_LED_G       16
#define CYD_LED_B       17
#define CYD_SPEAKER     26

#define XPT2046_IRQ     36
#define XPT2046_MOSI    32
#define XPT2046_MISO    39
#define XPT2046_CLK     25
#define XPT2046_CS      33

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;

// ==========================================
// 3. PALET WARNA MONITOR MEDIS (DARK & LIGHT THEMES)
// ==========================================
bool isLightTheme = true;

uint16_t C_BG         = 0x24BD;  // Biru Muda Cerah (Sky Blue, tft.color565(40, 150, 235))
uint16_t C_CARD_BG    = 0x090A;  // Biru Tua Navy (tft.color565(10, 32, 80))
uint16_t C_BORDER     = 0x1E7F;  // Biru Muda Cerah (tft.color565(30, 200, 255))
uint16_t C_CYAN       = 0x2EBF;  // Biru Muda Aksen (tft.color565(40, 215, 255))
uint16_t C_GREEN      = 0x07E0;  // Hijau Neon Terang (Saved / Kunci)
uint16_t C_YELLOW     = 0xFDE0;  // Kuning Amber
uint16_t C_RED        = 0xFA88;  // Merah Alarm (Fever)
uint16_t C_WHITE      = 0xFFFF;  // Font Putih Murni di Area Biru Tua
uint16_t C_MUTED      = 0xAFBF;  // Biru Muda Terang (tft.color565(160, 225, 255))
uint16_t C_DARK_BAR   = 0x08C8;  // Tombol Biru Tua Navy (tft.color565(8, 25, 68))
uint16_t C_KEY_BG     = 0x090A;  // Key Biru Tua
uint16_t C_KEY_BORDER = 0x324A;  // Key Border

void applyTheme(bool light) {
  isLightTheme = light;
  if (!isLightTheme) {
    // TEMA DARK ASLI (100% PERSIS SEPERTI PAS AWAL MULA DARI BACKUP)
    C_BG         = 0x0863;  // Dark Navy Black (#090D1A)
    C_CARD_BG    = 0x10E5;  // Dark Slate Card (#10172A)
    C_BORDER     = 0x1D49;  // Deep Slate Border (#1E2947)
    C_CYAN       = 0x07FF;  // Neon Cyan (#00F0FF)
    C_GREEN      = 0x07E0;  // Bright Neon Green (#00FF66)
    C_YELLOW     = 0xFDE0;  // Electric Amber (#FFB800)
    C_RED        = 0xF800;  // True Crimson Red (#FF2A2A)
    C_WHITE      = 0xFFFF;  // Pure White
    C_MUTED      = 0x7C55;  // Muted Slate Grey (#7A8BA8)
    C_DARK_BAR   = 0x18E3;  // Dark Bar Track
    C_KEY_BG     = 0x1927;  // Key Dark Background
    C_KEY_BORDER = 0x324A;  // Key Border
  } else {
    // TEMA LIGHT (BG BELAKANG PUTIH, DI ATAS FONT KASIH BIRU MUDA, FONT ANGKA WARNA HITAM)
    C_BG         = 0xFFFF;                      // Putih Murni Paling Belakang
    C_CARD_BG    = 0xFFFF;                      // Kartu Putih Bersih
    C_BORDER     = tft.color565(0, 160, 240);   // Border Biru Muda Cerah
    C_CYAN       = tft.color565(0, 140, 230);   // Biru Muda Cerah (Label Judul di Atas Font)
    C_GREEN      = tft.color565(0, 140, 50);    // Hijau Emerald (Saat Data di-SAVE)
    C_YELLOW     = tft.color565(0, 140, 230);   // Biru Muda Cerah (Label Judul di Atas Font)
    C_RED        = tft.color565(220, 30, 30);   // Merah Alarm (Demam / Offline)
    C_WHITE      = 0x0000;                      // Font Teks Standar: HITAM PEKAT
    C_MUTED      = tft.color565(120, 145, 175); // Slate Soft Grey
    C_DARK_BAR   = tft.color565(240, 248, 255); // Tombol Biru Muda Lembut (Alice Blue)
    C_KEY_BG     = 0xFFFF;
    C_KEY_BORDER = tft.color565(0, 160, 240);
  }
}


// Sprite Buffers (Anti-Flicker & Zero Tearing)
TFT_eSprite sprValBerat  = TFT_eSprite(&tft);
TFT_eSprite sprValTinggi = TFT_eSprite(&tft);
TFT_eSprite sprValOxy    = TFT_eSprite(&tft);
TFT_eSprite sprValSuhu   = TFT_eSprite(&tft);

// ==========================================
// 4. SCREEN STATES & NAVIGASI
// ==========================================
#define SCREEN_DASHBOARD       0
#define SCREEN_TEST_NODE       1
#define SCREEN_WIFI_SCAN       2
#define SCREEN_WIFI_KEYBOARD   3
#define SCREEN_WIFI_CONNECTING 4
#define SCREEN_CALIBRATION     5
#define SCREEN_SLIDE2          6

int currentScreen = SCREEN_DASHBOARD;
bool needFullRedraw = true;

// Variabel Menu Kalibrasi
int calibTab = 0;                  // 0 = Timbangan, 1 = Pengukur Tinggi
float calibTargetKg = 5.0f;        // Target beban acuan (fleksibel kustom)
volatile float liveRawJarakTinggi = 0.0f; // Jarak mentah sensor HC-SR04 ke lantai
String calibStatusMsg = "";
unsigned long calibStatusTime = 0;

// Variabel Scanner & Keyboard WiFi
struct WifiItem {
  String ssid;
  int rssi;
  bool isEncrypted;
};
std::vector<WifiItem> wifiList;
int wifiPage = 0;
String selectedSSID = "";
String inputPassword = "";
int keyboardMode = 0; // 0 = abc (kecil), 1 = ABC (besar), 2 = ?#$ (simbol)
bool isShift = false;
bool showPassword = true;
volatile bool isScanningWifi = false;
bool typingSSID = false;

// Forward declarations UI
void drawDashboardHeader();
void drawDashboardCards();
void drawDashboardButtons(bool forceRedraw = false);
void updateCardBorders(bool forceRedraw = false);
void drawDashboardDynamic();
void drawDashboardScreen();
void drawSlide2Dynamic();
void drawSlide2Header();
void drawSlide2Static();
void drawSlide2Screen();
void drawTestNodeHeader();
void drawTestNodeDynamic();
void drawTestNodeStatic();
void drawTestNodeScreen();
void drawCalibrationHeader();
void drawCalibrationDynamic();
void drawCalibrationStatic();
void drawCalibrationScreen();
void drawToastNotification();

// ==========================================
// 5. STRUKTUR PROTOKOL ESP-NOW
// ==========================================
typedef struct struct_msg_oxy {
  int node_id;            // 1
  int bpm;
  int spo2;
  float suhu;
  bool finger_detected;
} struct_msg_oxy;

typedef struct struct_msg_berat {
  int node_id;            // 2
  float berat_kg;
  int status_kode;        // 0 = Normal, 1 = Tare Sukses, 2 = Kalibrasi Sukses
} struct_msg_berat;

typedef struct struct_msg_tinggi {
  int node_id;            // 3
  float tinggi_cm;
  int status_kode;
  float raw_jarak_cm;     // Jarak pantul mentah sensor ke lantai (cm)
} struct_msg_tinggi;

typedef struct struct_cmd_tare {
  int target_node;  // 2
  int command;      // 1 = Tare, 2 = Calibrate
  float target_kg;  // Custom target weight
} struct_cmd_tare;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==========================================
// 6. VARIABEL STATE SISTEM & TELEMETRI
// ==========================================
// Nilai sensor riil (Default 0.0 jika node sensor belum terhubung)
volatile float liveBeratKg = 0.0;
volatile float liveTinggiCm = 0.0;
volatile int   liveBpm = 0;
volatile int   liveSpO2 = 0;
volatile float liveSuhu = 0.0;
volatile bool  liveFinger = false;

unsigned long lastSeenNode1 = 0;
unsigned long lastSeenNode2 = 0;
unsigned long lastSeenNode3 = 0;

volatile bool triggerSaveCloud = false;
volatile bool triggerForcePush = false;
bool simTestActive = false; // Default FALSE: Mode real, nilai tetap 0 saat node belum aktif!

// Status Penguncian Data Sensor (3 Tombol SAVE di Layar CYD)
volatile bool  isLockedBerat = false;
volatile float savedBeratKg  = 0.0;

volatile bool  isLockedTinggi = false;
volatile float savedTinggiCm  = 0.0;

volatile bool  isLockedOxy = false;
volatile int   savedBpm    = 0;
volatile int   savedSpO2   = 0;
volatile float savedSuhu   = 0.0;

// Nilai interaktif khusus simulasi di TAB TEST NODE
float testBeratKg = 12.50;
float testTinggiCm = 85.0;
int   testBpm = 86;
int   testSpO2 = 99;
float testSuhu = 36.6;
bool  testFinger = true;

char toastMsg[44] = "";
unsigned long toastUntil = 0;
unsigned long lastTouchTime = 0;
unsigned long lastLcdRefresh = 0;

// ==========================================
// 7. FUNGSI FEEDBACK AUDIBLE & VISUAL
// ==========================================
void setRgbLed(bool r, bool g, bool b) {
  digitalWrite(CYD_LED_R, r ? LOW : HIGH);
  digitalWrite(CYD_LED_G, g ? LOW : HIGH);
  digitalWrite(CYD_LED_B, b ? LOW : HIGH);
}

void clickBeep(int ms = 8) {
  for (int i = 0; i < (ms * 3); i++) {
    digitalWrite(CYD_SPEAKER, HIGH);
    delayMicroseconds(160);
    digitalWrite(CYD_SPEAKER, LOW);
    delayMicroseconds(160);
  }
}

void successBeep() {
  clickBeep(12);
  delay(60);
  clickBeep(24);
}

// Forward Declaration GUI
void drawDashboardScreen();
void drawSlide2Screen();
void drawTestNodeScreen();
void drawCalibrationScreen();
void drawWifiScanScreen();
void drawKeyboardScreen();
void drawKeyboardInputBox();
void connectWifiAndVerify();
void startWifiScan();
void sendTareCommandToNode2();
void sendCalibrateCommandToNode2(float targetKg);

// ==========================================
// 8. CALLBACK PENERIMA ESP-NOW
// ==========================================
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
  if (len < sizeof(int)) return;

  int incomingId = 0;
  memcpy(&incomingId, incomingData, sizeof(int));

  if (incomingId == 1 && len >= sizeof(struct_msg_oxy)) {
    struct_msg_oxy n1;
    memcpy(&n1, incomingData, sizeof(struct_msg_oxy));
    liveBpm = n1.bpm;
    liveSpO2 = n1.spo2;
    liveSuhu = n1.suhu;
    liveFinger = n1.finger_detected;
    lastSeenNode1 = millis();
    simTestActive = false;
    Serial.printf("[CYD ESP-NOW] DITERIMA NODE 1 -> BPM: %d | SpO2: %d%% | Suhu: %.1f C | Jari: %s (RF Ch: %d)\n",
                  liveBpm, liveSpO2, liveSuhu, liveFinger ? "TEMPEL" : "LEPAS", WiFi.channel());
  }
  else if (incomingId == 2 && len >= sizeof(struct_msg_berat)) {
    struct_msg_berat n2;
    memcpy(&n2, incomingData, sizeof(struct_msg_berat));
    liveBeratKg = n2.berat_kg;
    lastSeenNode2 = millis();
    simTestActive = false;

    if (n2.status_kode == 1) {
      calibStatusMsg = "TARE BERHASIL (0.00 KG)";
      calibStatusTime = millis() + 4000;
      snprintf(toastMsg, sizeof(toastMsg), "NODE 2: TARE SUKSES (0.00 KG)!");
      toastUntil = millis() + 3000;
      successBeep();
    } else if (n2.status_kode == 2) {
      calibStatusMsg = "KALIBRASI SUKSES DISIMPAN!";
      calibStatusTime = millis() + 5000;
      snprintf(toastMsg, sizeof(toastMsg), "NODE 2: KALIBRASI BEBAN SUKSES!");
      toastUntil = millis() + 3500;
      successBeep();
    }
    Serial.printf("[CYD ESP-NOW] DITERIMA NODE 2 -> Berat: %.2f kg (Status: %d)\n", liveBeratKg, n2.status_kode);
  }
  else if (incomingId == 3 && len >= 12) {
    struct_msg_tinggi n3;
    memset(&n3, 0, sizeof(struct_msg_tinggi));
    memcpy(&n3, incomingData, min((size_t)len, sizeof(struct_msg_tinggi)));
    liveTinggiCm = n3.tinggi_cm;
    if (len >= (int)sizeof(struct_msg_tinggi)) {
      liveRawJarakTinggi = n3.raw_jarak_cm;
    } else {
      liveRawJarakTinggi = 200.0f - liveTinggiCm;
    }
    lastSeenNode3 = millis();
    simTestActive = false;
    Serial.printf("[CYD ESP-NOW] DITERIMA NODE 3 -> Tinggi: %.1f cm (Raw: %.1f cm)\n", liveTinggiCm, liveRawJarakTinggi);
  }
}

void sendTareCommandToNode2() {
  struct_cmd_tare cmd;
  cmd.target_node = 2;
  cmd.command = 1;
  cmd.target_kg = 0.0f;
  esp_now_send(broadcastAddress, (uint8_t *)&cmd, sizeof(cmd));
  Serial.println("[ESP-NOW] Mengirim Perintah TARE ke Node 2.");
}

void sendCalibrateCommandToNode2(float targetKg) {
  struct_cmd_tare cmd;
  cmd.target_node = 2;
  cmd.command = 2;
  cmd.target_kg = targetKg;
  esp_now_send(broadcastAddress, (uint8_t *)&cmd, sizeof(cmd));
  Serial.printf("[ESP-NOW] Mengirim Perintah KALIBRASI BEBAN (%.2f kg) ke Node 2.\n", targetKg);
}

// ==========================================
// 9. BACKGROUND TASK: FIREBASE RTDB (CORE 0)
// ==========================================
void firebaseIoTask(void *pvParameters) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4);
  HTTPClient https;
  https.setTimeout(4000);

  unsigned long lastTelemetrySync = 0;
  unsigned long lastControlCheck = 0;
  unsigned long lastWifiAttempt = 0;

  while (true) {
    // A. Auto Reconnect WiFi jika terputus (hanya jika TIDAK sedang memindai WiFi & ada SSID valid)
    if (!isScanningWifi && currentScreen != SCREEN_WIFI_SCAN && currentScreen != SCREEN_WIFI_KEYBOARD && currentScreen != SCREEN_WIFI_CONNECTING) {
      if (WiFi.status() != WL_CONNECTED) {
        setRgbLed(true, false, false);
        if (savedSSID.length() > 0 && savedSSID != "x" && millis() - lastWifiAttempt >= 25000) {
          lastWifiAttempt = millis();
          Serial.printf("[WIFI] Rekoneksi ke %s di Channel %d...\n", savedSSID.c_str(), WIFI_FIXED_CHANNEL);
          WiFi.disconnect(false, false);
          delay(50);
          WiFi.begin(savedSSID.c_str(), savedPass.c_str(), WIFI_FIXED_CHANNEL);
          esp_wifi_set_channel(WIFI_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
        }
      } else {
        setRgbLed(false, true, false);
      }
    }

    if (WiFi.status() == WL_CONNECTED && (currentScreen == SCREEN_DASHBOARD || currentScreen == SCREEN_SLIDE2 || currentScreen == SCREEN_TEST_NODE || currentScreen == SCREEN_CALIBRATION)) {
      unsigned long now = millis();

      // B. Simpan Pemeriksaan Balita (POST)
      if (triggerSaveCloud) {
        triggerSaveCloud = false;
        client.stop();
        if (https.begin(client, RTDB_PEMERIKSAAN_URL)) {
          https.addHeader("Content-Type", "application/json");

          float curB = isLockedBerat ? savedBeratKg : (simTestActive ? testBeratKg : liveBeratKg);
          float curT = isLockedTinggi ? savedTinggiCm : (simTestActive ? testTinggiCm : liveTinggiCm);
          int   curBpm = isLockedOxy ? savedBpm : (simTestActive ? testBpm : liveBpm);
          int   curSpO2 = isLockedOxy ? savedSpO2 : (simTestActive ? testSpO2 : liveSpO2);
          float curSuhu = isLockedOxy ? savedSuhu : (simTestActive ? testSuhu : liveSuhu);

          float imt = 0.0;
          if (curT > 20.0) {
            float tMeter = curT / 100.0f;
            imt = curB / (tMeter * tMeter);
          }

          char postPayload[350];
          snprintf(postPayload, sizeof(postPayload),
            "{\"timestamp\":{\".sv\":\"timestamp\"},\"sumber\":\"CYD_HUB\",\"berat_kg\":%.2f,\"tinggi_cm\":%.1f,\"imt\":%.1f,\"bpm\":%d,\"spo2\":%d,\"suhu\":%.1f}",
            curB, curT, imt, curBpm, curSpO2, curSuhu
          );

          int code = https.POST(postPayload);
          if (code == 200) {
            snprintf(toastMsg, sizeof(toastMsg), "SUKSES TERSIMPAN! (HTTP 200)");
            successBeep();
          } else {
            snprintf(toastMsg, sizeof(toastMsg), "GAGAL: HTTP %d", code);
          }
          toastUntil = millis() + 3500;
          https.end();
          client.stop();
        }
      }

      // C. Stream Telemetri Real-Time (PATCH)
      if (triggerForcePush || (now - lastTelemetrySync >= 1500)) {
        bool wasForced = triggerForcePush;
        triggerForcePush = false;
        lastTelemetrySync = now;

        // Status Node Sensor Real vs Simulasi
        bool n1Real = (millis() - lastSeenNode1 < 3500) && (lastSeenNode1 > 0);
        bool n2Real = (millis() - lastSeenNode2 < 3500) && (lastSeenNode2 > 0);
        bool n3Real = (millis() - lastSeenNode3 < 3500) && (lastSeenNode3 > 0);

        bool isSim = (currentScreen == SCREEN_TEST_NODE && simTestActive);
        bool n1Online = n1Real || isSim;
        bool n2Online = n2Real || isSim;
        bool n3Online = n3Real || isSim;

        float curB = isLockedBerat ? savedBeratKg : (simTestActive ? testBeratKg : liveBeratKg);
        float curT = isLockedTinggi ? savedTinggiCm : (simTestActive ? testTinggiCm : liveTinggiCm);
        int   curBpm = isLockedOxy ? savedBpm : (simTestActive ? testBpm : liveBpm);
        int   curSpO2 = isLockedOxy ? savedSpO2 : (simTestActive ? testSpO2 : liveSpO2);
        float curSuhu = isLockedOxy ? savedSuhu : (simTestActive ? testSuhu : liveSuhu);

        client.stop();
        if (https.begin(client, RTDB_MONITORING_URL)) {
          https.addHeader("Content-Type", "application/json");

          char patchPayload[560];
          snprintf(patchPayload, sizeof(patchPayload),
            "{\"hub_id\":\"CYD_POSYANDU_01\",\"cyd_online\":true,\"cyd_screen\":%d,"
            "\"berat\":%.2f,\"tinggi\":%.1f,\"bpm\":%d,\"spo2\":%d,\"suhu\":%.1f,\"finger\":%s,"
            "\"n1_online\":%s,\"n2_online\":%s,\"n3_online\":%s,\"sim_mode\":%s,"
            "\"locked_berat\":%s,\"locked_berat_val\":%.2f,"
            "\"locked_tinggi\":%s,\"locked_tinggi_val\":%.1f,"
            "\"locked_oxy\":%s,\"timestamp\":{\".sv\":\"timestamp\"}}",
            currentScreen,
            curB, curT, curBpm, curSpO2, curSuhu,
            liveFinger ? "true" : "false",
            n1Online ? "true" : "false",
            n2Online ? "true" : "false",
            n3Online ? "true" : "false",
            isSim ? "true" : "false",
            isLockedBerat ? "true" : "false", savedBeratKg,
            isLockedTinggi ? "true" : "false", savedTinggiCm,
            isLockedOxy ? "true" : "false"
          );

          int code = https.PATCH(patchPayload);
          if (wasForced) {
            if (code == 200) {
              snprintf(toastMsg, sizeof(toastMsg), "CLOUD PUSH SUKSES! (200)");
              clickBeep(15);
            } else {
              snprintf(toastMsg, sizeof(toastMsg), "PUSH GAGAL: HTTP %d", code);
            }
            toastUntil = millis() + 3000;
          }
          https.end();
          client.stop();
        }
      }

      // D. Cek Kontrol Jarak Jauh (GET)
      if (now - lastControlCheck >= 2000) {
        lastControlCheck = now;
        client.stop();
        if (https.begin(client, RTDB_CONTROL_URL)) {
          int code = https.GET();
          if (code == 200) {
            String resp = https.getString();
            if (resp.indexOf("\"tare\":true") >= 0 || resp.indexOf("\"tare\": true") >= 0) {
              sendTareCommandToNode2();
              liveBeratKg = 0.0;
              clickBeep(20);
              snprintf(toastMsg, sizeof(toastMsg), "TARE DARI WEB BERHASIL");
              toastUntil = millis() + 3000;

              https.end();
              client.stop();
              if (https.begin(client, RTDB_CONTROL_URL)) {
                https.addHeader("Content-Type", "application/json");
                https.PATCH("{\"tare\":false}");
              }
            }
          }
          https.end();
          client.stop();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ==========================================
// 10. SCREEN 1: DASHBOARD UTAMA
// ==========================================
void drawDashboardHeader() {
  uint16_t bgHeader = isLightTheme ? 0xFFFF : C_BG;
  tft.fillRect(0, 0, 320, 31, bgHeader);
  tft.drawFastHLine(0, 31, 320, C_BORDER);

  // Logo Medis [+] POSYANDU (X: 4..68)
  uint16_t colCross = isLightTheme ? tft.color565(0, 140, 230) : C_CYAN;
  tft.fillRect(8, 9, 3, 11, colCross);
  tft.fillRect(4, 13, 11, 3, colCross);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgHeader);
  tft.setTextSize(1);
  tft.drawString("POSYANDU", 19, 7);
  tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_CYAN, bgHeader);
  tft.drawString("HUB IoT", 19, 19);

  // Telemetry Badge Node 1, 2, 3 (X: 76..152, W: 76, H: 24, Y: 4)
  tft.drawRoundRect(76, 4, 76, 24, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? 0x0000 : C_MUTED, bgHeader);
  tft.drawString("1", 88, 10);
  tft.drawString("2", 114, 10);
  tft.drawString("3", 140, 10);

  // Status Bulatan Node
  bool n1On = (millis() - lastSeenNode1 < 6500) && (lastSeenNode1 > 0);
  bool n2On = (millis() - lastSeenNode2 < 6500) && (lastSeenNode2 > 0);
  bool n3On = (millis() - lastSeenNode3 < 6500) && (lastSeenNode3 > 0);
  tft.fillCircle(88, 20, 3, n1On ? C_GREEN : C_RED);
  tft.fillCircle(114, 20, 3, n2On ? C_GREEN : C_RED);
  tft.fillCircle(140, 20, 3, n3On ? C_GREEN : C_RED);

  // Tombol [ SLIDE 2 > ] di Header (X: 158..238, W: 80, H: 24, Y: 4)
  uint16_t bgS2 = isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR;
  tft.fillRoundRect(158, 4, 80, 24, 4, bgS2);
  tft.drawRoundRect(158, 4, 80, 24, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgS2);
  tft.drawString("SLIDE 2 >", 198, 16);

  // Tombol [ WIFI ] di pojok kanan atas (X: 244..316, W: 72, H: 24)
  bool wifiOn = (WiFi.status() == WL_CONNECTED);
  uint16_t bgWf = wifiOn ? (isLightTheme ? tft.color565(0, 140, 60) : tft.color565(0, 70, 45)) : (isLightTheme ? tft.color565(210, 25, 25) : tft.color565(70, 15, 15));
  tft.fillRoundRect(244, 4, 72, 24, 4, bgWf);
  tft.drawRoundRect(244, 4, 72, 24, 4, wifiOn ? C_GREEN : C_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(0xFFFF, bgWf);
  tft.drawString(wifiOn ? "WIFI OK" : "NO WIFI", 280, 16);
}

void drawDashboardCards() {
  // --- KARTU 1: BERAT BADAN (WT) (Y: 36..106, H: 70) ---
  tft.fillRoundRect(6, 36, 150, 70, 6, C_CARD_BG);
  tft.drawRoundRect(6, 36, 150, 70, 6, isLockedBerat ? C_GREEN : C_BORDER);
  tft.setTextDatum(TL_DATUM);
  uint16_t colTitle1 = isLightTheme ? tft.color565(0, 140, 230) : C_CYAN;
  tft.setTextColor(colTitle1, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("WT [BERAT]", 12, 40);

  // Tombol TARE di Pojok Kanan Kartu 1
  uint16_t bgTare = isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR;
  uint16_t colTareTxt = isLightTheme ? tft.color565(0, 140, 230) : C_WHITE;
  tft.fillRoundRect(104, 38, 48, 16, 3, bgTare);
  tft.drawRoundRect(104, 38, 48, 16, 3, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(colTareTxt, bgTare);
  tft.drawString(">0< TARE", 128, 46);

  // --- KARTU 2: TINGGI BADAN (HT) (Y: 36..106, H: 70) ---
  tft.fillRoundRect(164, 36, 150, 70, 6, C_CARD_BG);
  tft.drawRoundRect(164, 36, 150, 70, 6, isLockedTinggi ? C_GREEN : C_BORDER);
  tft.setTextDatum(TL_DATUM);
  uint16_t colTitle2 = isLightTheme ? tft.color565(0, 140, 230) : C_YELLOW;
  tft.setTextColor(colTitle2, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("HT [TINGGI]", 170, 40);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(120, 145, 175) : C_MUTED, C_CARD_BG);
  tft.drawString("[ cm ]", 308, 40);

  // --- KARTU 3: JANTUNG & SPO2 (PR / SpO2) (Y: 112..182, H: 70) ---
  tft.fillRoundRect(6, 112, 150, 70, 6, C_CARD_BG);
  tft.drawRoundRect(6, 112, 150, 70, 6, isLockedOxy ? C_GREEN : C_BORDER);
  tft.setTextDatum(TL_DATUM);
  uint16_t colTitle3 = isLightTheme ? tft.color565(0, 140, 230) : C_GREEN;
  tft.setTextColor(colTitle3, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("PR / SpO2", 12, 116);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(120, 145, 175) : C_MUTED, C_CARD_BG);
  tft.drawString("[ bpm/% ]", 150, 116);

  // --- KARTU 4: SUHU TUBUH (TEMP) (Y: 112..182, H: 70) ---
  tft.fillRoundRect(164, 112, 150, 70, 6, C_CARD_BG);
  tft.drawRoundRect(164, 112, 150, 70, 6, isLockedOxy ? C_GREEN : C_BORDER);
  tft.setTextDatum(TL_DATUM);
  uint16_t colTitle4 = isLightTheme ? tft.color565(0, 140, 230) : C_RED;
  tft.setTextColor(colTitle4, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("TEMP [SUHU]", 170, 116);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(120, 145, 175) : C_MUTED, C_CARD_BG);
  tft.drawString("[ `C ]", 308, 116);
}

void updateCardBorders(bool forceRedraw) {
  static bool lastB = false, lastT = false, lastO = false;
  static bool hasDrawnOnce = false;

  uint16_t brB = isLockedBerat ? C_GREEN : C_BORDER;
  uint16_t brT = isLockedTinggi ? C_GREEN : C_BORDER;
  uint16_t brO = isLockedOxy ? C_GREEN : C_BORDER;

  if (forceRedraw || !hasDrawnOnce || (lastB != isLockedBerat)) {
    lastB = isLockedBerat;
    tft.drawRoundRect(6, 36, 150, 70, 6, brB);
    tft.drawRoundRect(7, 37, 148, 68, 5, isLockedBerat ? C_GREEN : C_CARD_BG);
  }

  if (forceRedraw || !hasDrawnOnce || (lastT != isLockedTinggi)) {
    lastT = isLockedTinggi;
    tft.drawRoundRect(164, 36, 150, 70, 6, brT);
    tft.drawRoundRect(165, 37, 148, 68, 5, isLockedTinggi ? C_GREEN : C_CARD_BG);
  }

  if (forceRedraw || !hasDrawnOnce || (lastO != isLockedOxy)) {
    lastO = isLockedOxy;
    tft.drawRoundRect(6, 112, 150, 70, 6, brO);
    tft.drawRoundRect(164, 112, 150, 70, 6, brO);
    tft.drawRoundRect(7, 113, 148, 68, 5, isLockedOxy ? C_GREEN : C_CARD_BG);
    tft.drawRoundRect(165, 113, 148, 68, 5, isLockedOxy ? C_GREEN : C_CARD_BG);
  }

  hasDrawnOnce = true;
}

void drawDashboardButtons(bool forceRedraw) {
  static bool lastLockedBerat = false;
  static bool lastLockedTinggi = false;
  static bool lastLockedOxy = false;
  static bool lastAnyLocked = false;
  static bool hasDrawnOnce = false;

  bool anyLocked = isLockedBerat || isLockedTinggi || isLockedOxy;

  // 1. Tombol SAVE BERAT (X: 4..78, W: 74, H: 44, Y: 190)
  if (forceRedraw || !hasDrawnOnce || (lastLockedBerat != isLockedBerat)) {
    lastLockedBerat = isLockedBerat;
    uint16_t bgB1 = isLockedBerat ? (isLightTheme ? tft.color565(0, 140, 50) : tft.color565(0, 130, 80)) : C_DARK_BAR;
    uint16_t brB1 = isLockedBerat ? (isLightTheme ? tft.color565(0, 180, 80) : C_GREEN) : C_BORDER;
    tft.fillRoundRect(4, 190, 74, 44, 5, bgB1);
    tft.drawRoundRect(4, 190, 74, 44, 5, brB1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (isLockedBerat) {
      tft.setTextColor(0xFFFF, bgB1);
      tft.drawString("[* SAVED *]", 41, 202);
      tft.setTextColor(isLightTheme ? 0xFFFF : C_GREEN, bgB1);
      tft.drawString("BATAL >", 41, 222);
    } else {
      tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_CYAN, bgB1);
      tft.drawString("SAVE", 41, 202);
      tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB1);
      tft.drawString("BERAT", 41, 222);
    }
  }

  // 2. Tombol SAVE TINGGI (X: 81..155, W: 74, H: 44, Y: 190)
  if (forceRedraw || !hasDrawnOnce || (lastLockedTinggi != isLockedTinggi)) {
    lastLockedTinggi = isLockedTinggi;
    uint16_t bgB2 = isLockedTinggi ? (isLightTheme ? tft.color565(0, 140, 50) : tft.color565(0, 130, 80)) : C_DARK_BAR;
    uint16_t brB2 = isLockedTinggi ? (isLightTheme ? tft.color565(0, 180, 80) : C_GREEN) : C_BORDER;
    tft.fillRoundRect(81, 190, 74, 44, 5, bgB2);
    tft.drawRoundRect(81, 190, 74, 44, 5, brB2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (isLockedTinggi) {
      tft.setTextColor(0xFFFF, bgB2);
      tft.drawString("[* SAVED *]", 118, 202);
      tft.setTextColor(isLightTheme ? 0xFFFF : C_GREEN, bgB2);
      tft.drawString("BATAL >", 118, 222);
    } else {
      tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_YELLOW, bgB2);
      tft.drawString("SAVE", 118, 202);
      tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB2);
      tft.drawString("TINGGI", 118, 222);
    }
  }

  // 3. Tombol SAVE OXY (X: 158..232, W: 74, H: 44, Y: 190)
  if (forceRedraw || !hasDrawnOnce || (lastLockedOxy != isLockedOxy)) {
    lastLockedOxy = isLockedOxy;
    uint16_t bgB3 = isLockedOxy ? (isLightTheme ? tft.color565(0, 140, 50) : tft.color565(0, 130, 80)) : C_DARK_BAR;
    uint16_t brB3 = isLockedOxy ? (isLightTheme ? tft.color565(0, 180, 80) : C_GREEN) : C_BORDER;
    tft.fillRoundRect(158, 190, 74, 44, 5, bgB3);
    tft.drawRoundRect(158, 190, 74, 44, 5, brB3);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (isLockedOxy) {
      tft.setTextColor(0xFFFF, bgB3);
      tft.drawString("[* SAVED *]", 195, 202);
      tft.setTextColor(isLightTheme ? 0xFFFF : C_GREEN, bgB3);
      tft.drawString("BATAL >", 195, 222);
    } else {
      tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_GREEN, bgB3);
      tft.drawString("SAVE", 195, 202);
      tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB3);
      tft.drawString("OXY/SUHU", 195, 222);
    }
  }

  // 4. Tombol SUBMIT / KIRIM (X: 235..317, W: 82, H: 44, Y: 190)
  if (forceRedraw || !hasDrawnOnce || (lastAnyLocked != anyLocked)) {
    lastAnyLocked = anyLocked;
    uint16_t bgB4 = anyLocked ? (isLightTheme ? tft.color565(0, 140, 50) : tft.color565(0, 130, 80)) : C_DARK_BAR;
    uint16_t brB4 = anyLocked ? (isLightTheme ? tft.color565(0, 180, 80) : C_GREEN) : C_BORDER;
    tft.fillRoundRect(235, 190, 82, 44, 5, bgB4);
    tft.drawRoundRect(235, 190, 82, 44, 5, brB4);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    if (anyLocked) {
      tft.setTextColor(0xFFFF, bgB4);
      tft.drawString("[ KIRIM ]", 276, 202);
      tft.setTextColor(isLightTheme ? 0xFFFF : C_GREEN, bgB4);
      tft.drawString("SUBMIT >", 276, 222);
    } else {
      tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_CYAN, bgB4);
      tft.drawString("KIRIM", 276, 202);
      tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB4);
      tft.drawString("DASHBOARD", 276, 222);
    }
  }

  hasDrawnOnce = true;
}

void drawDashboardStatic() {
  tft.fillScreen(C_BG);
  drawDashboardHeader();
  drawDashboardCards();
}

void drawDashboardDynamic() {
  // Hanya gambar header jika toast tidak sedang aktif
  if (millis() >= toastUntil) {
    static bool lastN1 = false, lastN2 = false, lastN3 = false;
    bool n1On = (millis() - lastSeenNode1 < 6500) && (lastSeenNode1 > 0);
    bool n2On = (millis() - lastSeenNode2 < 6500) && (lastSeenNode2 > 0);
    bool n3On = (millis() - lastSeenNode3 < 6500) && (lastSeenNode3 > 0);

    if (lastN1 != n1On || lastN2 != n2On || lastN3 != n3On) {
      lastN1 = n1On; lastN2 = n2On; lastN3 = n3On;
      tft.fillCircle(88, 20, 3, n1On ? C_GREEN : C_RED);
      tft.fillCircle(114, 20, 3, n2On ? C_GREEN : C_RED);
      tft.fillCircle(140, 20, 3, n3On ? C_GREEN : C_RED);
    }

    static bool lastWifi = false;
    bool wifiOn = (WiFi.status() == WL_CONNECTED);
    if (lastWifi != wifiOn) {
      lastWifi = wifiOn;
      uint16_t bgWf = wifiOn ? (isLightTheme ? tft.color565(20, 150, 75) : tft.color565(0, 70, 45)) : (isLightTheme ? tft.color565(200, 30, 30) : tft.color565(70, 15, 15));
      tft.fillRoundRect(244, 4, 72, 24, 4, bgWf);
      tft.drawRoundRect(244, 4, 72, 24, 4, wifiOn ? C_GREEN : C_RED);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(0xFFFF, bgWf);
      tft.setTextSize(1);
      tft.drawString(wifiOn ? "WIFI OK" : "NO WIFI", 280, 16);
    }
  }

  // Update border & tombol secara diferensial (Zero Flicker)
  updateCardBorders(false);
  drawDashboardButtons(false);

  // --- KARTU 1: SPRITE BERAT BADAN (WT) ---
  sprValBerat.fillSprite(C_CARD_BG);
  if (isLockedBerat) {
    sprValBerat.setTextDatum(TL_DATUM);
    sprValBerat.setTextColor(C_GREEN, C_CARD_BG); // Hijau saat Disimpan
    sprValBerat.setTextSize(3);
    char strBerat[12];
    snprintf(strBerat, sizeof(strBerat), "%.2f", savedBeratKg);
    sprValBerat.drawString(strBerat, 6, 2);

    sprValBerat.setTextDatum(TR_DATUM);
    sprValBerat.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValBerat.setTextSize(1);
    sprValBerat.drawString("kg", 136, 6);

    sprValBerat.setTextDatum(BL_DATUM);
    sprValBerat.setTextColor(C_GREEN, C_CARD_BG);
    sprValBerat.drawString("[* LOCKED / SAVED]", 6, 42);
  } else {
    sprValBerat.setTextDatum(TL_DATUM);
    uint16_t numCol = isLightTheme ? 0x0000 : ((liveBeratKg > 0.0) ? C_CYAN : C_MUTED);
    sprValBerat.setTextColor(numCol, C_CARD_BG);
    sprValBerat.setTextSize(3);
    char strBerat[12];
    snprintf(strBerat, sizeof(strBerat), "%.2f", (simTestActive ? testBeratKg : liveBeratKg));
    sprValBerat.drawString(strBerat, 6, 2);

    sprValBerat.setTextDatum(TR_DATUM);
    sprValBerat.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValBerat.setTextSize(1);
    sprValBerat.drawString("kg", 136, 6);

    sprValBerat.setTextDatum(BL_DATUM);
    uint16_t tagCol = isLightTheme ? tft.color565(0, 140, 230) : ((liveBeratKg > 0.0) ? C_CYAN : C_MUTED);
    sprValBerat.setTextColor(tagCol, C_CARD_BG);
    sprValBerat.drawString((liveBeratKg > 0.0) ? "[~ MEASURING / LIVE]" : "[STANDBY / SIAP]", 6, 42);
  }
  sprValBerat.pushSprite(10, 56);

  // --- KARTU 2: SPRITE TINGGI BADAN (HT) ---
  sprValTinggi.fillSprite(C_CARD_BG);
  if (isLockedTinggi) {
    sprValTinggi.setTextDatum(TL_DATUM);
    sprValTinggi.setTextColor(C_GREEN, C_CARD_BG); // Hijau saat Disimpan
    sprValTinggi.setTextSize(3);
    char strTinggi[12];
    snprintf(strTinggi, sizeof(strTinggi), "%.1f", savedTinggiCm);
    sprValTinggi.drawString(strTinggi, 6, 2);

    sprValTinggi.setTextDatum(TR_DATUM);
    sprValTinggi.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValTinggi.setTextSize(1);
    sprValTinggi.drawString("cm", 136, 6);

    sprValTinggi.setTextDatum(BL_DATUM);
    sprValTinggi.setTextColor(C_GREEN, C_CARD_BG);
    sprValTinggi.drawString("[* LOCKED / SAVED]", 6, 42);
  } else {
    sprValTinggi.setTextDatum(TL_DATUM);
    uint16_t numCol = isLightTheme ? 0x0000 : ((liveTinggiCm > 0.0) ? C_YELLOW : C_MUTED);
    sprValTinggi.setTextColor(numCol, C_CARD_BG);
    sprValTinggi.setTextSize(3);
    char strTinggi[12];
    snprintf(strTinggi, sizeof(strTinggi), "%.1f", (simTestActive ? testTinggiCm : liveTinggiCm));
    sprValTinggi.drawString(strTinggi, 6, 2);

    sprValTinggi.setTextDatum(TR_DATUM);
    sprValTinggi.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValTinggi.setTextSize(1);
    sprValTinggi.drawString("cm", 136, 6);

    sprValTinggi.setTextDatum(BL_DATUM);
    uint16_t tagCol = isLightTheme ? tft.color565(0, 140, 230) : ((liveTinggiCm > 0.0) ? C_YELLOW : C_MUTED);
    sprValTinggi.setTextColor(tagCol, C_CARD_BG);
    sprValTinggi.drawString((liveTinggiCm > 0.0) ? "[~ MEASURING / LIVE]" : "[STANDBY / SIAP]", 6, 42);
  }
  sprValTinggi.pushSprite(168, 56);

  // --- KARTU 3: SPRITE JANTUNG & SPO2 (PR / SpO2) ---
  sprValOxy.fillSprite(C_CARD_BG);
  if (isLockedOxy) {
    char strBpm[10], strSpo2[10];
    snprintf(strBpm, sizeof(strBpm), "%d", savedBpm);
    snprintf(strSpo2, sizeof(strSpo2), "%d", savedSpO2);

    sprValOxy.setTextDatum(TL_DATUM);
    sprValOxy.setTextColor(C_GREEN, C_CARD_BG); // Hijau saat Disimpan
    sprValOxy.setTextSize(3);
    sprValOxy.drawString(strBpm, 4, 2);

    sprValOxy.setTextDatum(TL_DATUM);
    sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValOxy.setTextSize(1);
    sprValOxy.drawString("bpm", 44, 14);

    sprValOxy.drawFastVLine(67, 5, 24, C_BORDER);

    sprValOxy.setTextDatum(TL_DATUM);
    sprValOxy.setTextColor(C_GREEN, C_CARD_BG);
    sprValOxy.setTextSize(3);
    sprValOxy.drawString(strSpo2, 74, 2);

    sprValOxy.setTextDatum(TR_DATUM);
    sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValOxy.setTextSize(1);
    sprValOxy.drawString("%", 138, 6);

    sprValOxy.setTextDatum(BL_DATUM);
    sprValOxy.setTextColor(C_GREEN, C_CARD_BG);
    sprValOxy.setTextSize(1);
    sprValOxy.drawString("[* VITAL LOCKED]", 6, 42);
  } else {
    int curBpm = simTestActive ? testBpm : liveBpm;
    int curSpo2 = simTestActive ? testSpO2 : liveSpO2;
    if ((liveFinger || simTestActive) && (curBpm > 0 || curSpo2 > 0)) {
      char strBpm[10], strSpo2[10];
      snprintf(strBpm, sizeof(strBpm), "%d", curBpm);
      snprintf(strSpo2, sizeof(strSpo2), "%d", curSpo2);

      sprValOxy.setTextDatum(TL_DATUM);
      uint16_t numCol = isLightTheme ? 0x0000 : C_GREEN;
      sprValOxy.setTextColor(numCol, C_CARD_BG);
      sprValOxy.setTextSize(3);
      sprValOxy.drawString(strBpm, 4, 2);

      sprValOxy.setTextDatum(TL_DATUM);
      sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("bpm", 44, 14);

      sprValOxy.drawFastVLine(67, 5, 24, C_BORDER);

      sprValOxy.setTextDatum(TL_DATUM);
      sprValOxy.setTextColor(numCol, C_CARD_BG);
      sprValOxy.setTextSize(3);
      sprValOxy.drawString(strSpo2, 74, 2);

      sprValOxy.setTextDatum(TR_DATUM);
      sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("%", 138, 6);

      sprValOxy.setTextDatum(BL_DATUM);
      sprValOxy.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_GREEN, C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("[PULSE STABLE]", 6, 42);
    } else {
      sprValOxy.setTextDatum(TL_DATUM);
      uint16_t numCol = isLightTheme ? 0x0000 : C_MUTED;
      sprValOxy.setTextColor(numCol, C_CARD_BG);
      sprValOxy.setTextSize(3);
      sprValOxy.drawString("--", 4, 2);

      sprValOxy.setTextDatum(TL_DATUM);
      sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("bpm", 44, 14);

      sprValOxy.drawFastVLine(67, 5, 24, C_BORDER);

      sprValOxy.setTextDatum(TL_DATUM);
      sprValOxy.setTextColor(numCol, C_CARD_BG);
      sprValOxy.setTextSize(3);
      sprValOxy.drawString("--", 74, 2);

      sprValOxy.setTextDatum(TR_DATUM);
      sprValOxy.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("%", 138, 6);

      sprValOxy.setTextDatum(BL_DATUM);
      sprValOxy.setTextColor(isLightTheme ? tft.color565(210, 120, 0) : tft.color565(255, 220, 100), C_CARD_BG);
      sprValOxy.setTextSize(1);
      sprValOxy.drawString("[TEMPEL JARI SENSOR]", 6, 42);
    }
  }
  sprValOxy.pushSprite(10, 132);

  // --- KARTU 4: SPRITE SUHU TUBUH (TEMP) ---
  sprValSuhu.fillSprite(C_CARD_BG);
  float curS = simTestActive ? testSuhu : liveSuhu;
  if (isLockedOxy) {
    sprValSuhu.setTextDatum(TL_DATUM);
    sprValSuhu.setTextColor(C_GREEN, C_CARD_BG); // Hijau saat Disimpan
    sprValSuhu.setTextSize(3);
    char strSuhu[12];
    snprintf(strSuhu, sizeof(strSuhu), "%.1f", savedSuhu);
    sprValSuhu.drawString(strSuhu, 6, 2);

    sprValSuhu.setTextDatum(TR_DATUM);
    sprValSuhu.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValSuhu.setTextSize(1);
    sprValSuhu.drawString("`C", 136, 6);

    sprValSuhu.setTextDatum(BL_DATUM);
    sprValSuhu.setTextColor(C_GREEN, C_CARD_BG);
    sprValSuhu.drawString("[* LOCKED / SAVED]", 6, 42);
  } else {
    uint16_t numCol;
    if (isLightTheme) {
      numCol = (curS > 37.5) ? tft.color565(220, 30, 30) : 0x0000;
    } else {
      numCol = (curS <= 0.0) ? C_MUTED : ((curS > 37.5) ? C_RED : ((curS < 35.5) ? C_YELLOW : C_GREEN));
    }
    sprValSuhu.setTextDatum(TL_DATUM);
    sprValSuhu.setTextColor(numCol, C_CARD_BG);
    sprValSuhu.setTextSize(3);
    char strSuhu[12];
    snprintf(strSuhu, sizeof(strSuhu), "%.1f", curS);
    sprValSuhu.drawString(strSuhu, 6, 2);

    sprValSuhu.setTextDatum(TR_DATUM);
    sprValSuhu.setTextColor(isLightTheme ? 0x0000 : C_MUTED, C_CARD_BG);
    sprValSuhu.setTextSize(1);
    sprValSuhu.drawString("`C", 136, 6);

    sprValSuhu.setTextDatum(BL_DATUM);
    if (curS > 37.5) {
      sprValSuhu.setTextColor(C_RED, C_CARD_BG);
      sprValSuhu.drawString("[! FEBRILE / DEMAM]", 6, 42);
    } else if (curS >= 35.5) {
      sprValSuhu.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_GREEN, C_CARD_BG);
      sprValSuhu.drawString("[NORMAL / AFBRIL]", 6, 42);
    } else {
      sprValSuhu.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_MUTED, C_CARD_BG);
      sprValSuhu.drawString("[STANDBY / SIAP]", 6, 42);
    }
  }
  sprValSuhu.pushSprite(168, 132);
}

void drawDashboardScreen() {
  static int lastScreenRendered = -1;
  if (needFullRedraw || lastScreenRendered != currentScreen) {
    lastScreenRendered = currentScreen;
    drawDashboardStatic();
    drawDashboardButtons(true);
    updateCardBorders(true);
    needFullRedraw = false;
  }
  drawDashboardDynamic();
}

// ==========================================
// 10B. SCREEN SLIDE 2: MENU & PENGATURAN (KALIBRASI & LAB)
// ==========================================
void drawSlide2Header() {
  uint16_t bgHeader = isLightTheme ? 0xFFFF : C_BG;
  tft.fillRect(0, 0, 320, 31, bgHeader);
  tft.drawFastHLine(0, 31, 320, C_BORDER);

  // Tombol < SLIDE 1 (X: 6, Y: 4, W: 82, H: 24)
  uint16_t bgS1 = isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR;
  tft.fillRoundRect(6, 4, 82, 24, 4, bgS1);
  tft.drawRoundRect(6, 4, 82, 24, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgS1);
  tft.drawString("< SLIDE 1", 47, 16);

  // Judul Tengah
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgHeader);
  tft.setTextSize(1);
  tft.drawString("MENU & LAB (SLIDE 2)", 164, 16);

  drawSlide2Dynamic();
}

void drawSlide2Static() {
  tft.fillScreen(C_BG);
  drawSlide2Header();

  // --- 1. KARTU MENU KALIBRASI (X: 10..310, W: 300, H: 54, Y: 36..90) ---
  tft.fillRoundRect(10, 36, 300, 54, 6, C_CARD_BG);
  tft.drawRoundRect(10, 36, 300, 54, 6, C_BORDER);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_WHITE, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("1. MENU KALIBRASI SENSOR", 22, 44);
  tft.setTextColor(isLightTheme ? tft.color565(100, 120, 140) : C_MUTED, C_CARD_BG);
  tft.drawString("Loadcell (HX711) & Tiang Acuan 2.0M", 22, 64);

  // Tombol Buka > di kanan kartu 1
  uint16_t bgB1 = isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR;
  tft.fillRoundRect(242, 43, 60, 40, 4, bgB1);
  tft.drawRoundRect(242, 43, 60, 40, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB1);
  tft.drawString("BUKA >", 272, 63);

  // --- 2. KARTU MENU TEST LAB / SIMULATOR (X: 10..310, W: 300, H: 54, Y: 96..150) ---
  tft.fillRoundRect(10, 96, 300, 54, 6, C_CARD_BG);
  tft.drawRoundRect(10, 96, 300, 54, 6, C_BORDER);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_WHITE, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("2. TEST LAB SIMULATOR", 22, 104);
  tft.setTextColor(isLightTheme ? tft.color565(100, 120, 140) : C_MUTED, C_CARD_BG);
  tft.drawString("Uji Radio ESP-NOW & Push Firebase", 22, 124);

  // Tombol Buka > di kanan kartu 2
  uint16_t bgB2 = isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR;
  tft.fillRoundRect(242, 103, 60, 40, 4, bgB2);
  tft.drawRoundRect(242, 103, 60, 40, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgB2);
  tft.drawString("BUKA >", 272, 123);

  // --- 3. KARTU MENU TEMA LAYAR (DARK / LIGHT) (X: 10..310, W: 300, H: 58, Y: 156..214) ---
  tft.fillRoundRect(10, 156, 300, 58, 6, C_CARD_BG);
  tft.drawRoundRect(10, 156, 300, 58, 6, C_BORDER);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_WHITE, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("3. TEMA TAMPILAN", 22, 166);
  tft.setTextColor(isLightTheme ? tft.color565(100, 120, 140) : C_MUTED, C_CARD_BG);
  tft.drawString("Pilih Mode Kontras", 22, 186);

  // Tombol [ DARK ] (X: 156..226, W: 70, H: 38, Y: 166)
  uint16_t bgDark = (!isLightTheme) ? tft.color565(20, 60, 130) : (isLightTheme ? tft.color565(240, 248, 255) : C_DARK_BAR);
  uint16_t brDark = (!isLightTheme) ? C_CYAN : C_BORDER;
  tft.fillRoundRect(156, 166, 70, 38, 4, bgDark);
  tft.drawRoundRect(156, 166, 70, 38, 4, brDark);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor((!isLightTheme) ? 0xFFFF : (isLightTheme ? 0x0000 : C_MUTED), bgDark);
  tft.drawString((!isLightTheme) ? "[* DARK *]" : "DARK", 191, 185);

  // Tombol [ LIGHT ] (X: 232..302, W: 70, H: 38, Y: 166)
  uint16_t bgLight = (isLightTheme) ? tft.color565(0, 140, 230) : C_DARK_BAR;
  uint16_t brLight = (isLightTheme) ? 0x0000 : C_BORDER;
  tft.fillRoundRect(232, 166, 70, 38, 4, bgLight);
  tft.drawRoundRect(232, 166, 70, 38, 4, brLight);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor((isLightTheme) ? 0xFFFF : C_MUTED, bgLight);
  tft.drawString((isLightTheme) ? "[* LIGHT *]" : "LIGHT", 267, 185);

  // --- FOOTER INDIKATOR SLIDE (Y: 226..238) ---
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isLightTheme ? tft.color565(120, 140, 160) : C_MUTED, C_BG);
  tft.drawString("Slide 2 dari 2  |  Sentuh < SLIDE 1 untuk Pengukuran", 160, 226);
}

void drawSlide2Dynamic() {
  static bool lastWifi = false;
  static bool hasDrawn = false;
  bool wifiOn = (WiFi.status() == WL_CONNECTED);
  if (!hasDrawn || lastWifi != wifiOn) {
    hasDrawn = true;
    lastWifi = wifiOn;
    uint16_t bgWf = wifiOn ? (isLightTheme ? tft.color565(0, 140, 60) : tft.color565(0, 70, 45)) : (isLightTheme ? tft.color565(210, 25, 25) : tft.color565(70, 15, 15));
    tft.fillRoundRect(246, 4, 70, 24, 4, bgWf);
    tft.drawRoundRect(246, 4, 70, 24, 4, wifiOn ? C_GREEN : C_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFFF, bgWf);
    tft.setTextSize(1);
    tft.drawString(wifiOn ? "WIFI OK" : "NO WIFI", 281, 16);
  }
}

void drawSlide2Screen() {
  if (needFullRedraw) {
    drawSlide2Static();
    needFullRedraw = false;
  }
  drawSlide2Dynamic();
}

// ==========================================
// 11. SCREEN 2: TEST NODE / LAB SIMULATOR
// ==========================================
void drawTestNodeHeader() {
  tft.fillRect(0, 0, 320, 31, C_BG);
  tft.fillRoundRect(6, 4, 64, 24, 4, C_DARK_BAR);
  tft.drawRoundRect(6, 4, 64, 24, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_DARK_BAR);
  tft.drawString("< DASH", 38, 16);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_YELLOW, C_BG);
  tft.setTextSize(1);
  tft.drawString("LAB SIMULATOR (UJI IOT)", 76, 11);

  // Tombol Reset Nol (RST 0)
  tft.fillRoundRect(198, 4, 48, 24, 4, tft.color565(60, 20, 20));
  tft.drawRoundRect(198, 4, 48, 24, 4, C_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_WHITE, tft.color565(60, 20, 20));
  tft.drawString("RST 0", 222, 16);

  tft.drawFastHLine(0, 31, 320, C_BORDER);
}

void drawTestNodeStatic() {
  tft.fillScreen(C_BG);
  drawTestNodeHeader();

  // Row 1: Node 2 (Berat)
  tft.fillRoundRect(6, 35, 308, 44, 6, C_CARD_BG);
  tft.drawRoundRect(6, 35, 308, 44, 6, C_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("NODE 2 (BERAT):", 12, 41);

  const char* bLabels[4] = {"-1.0", "-0.1", "+0.1", "+1.0"};
  for (int i = 0; i < 4; i++) {
    int bx = 150 + (i * 40);
    tft.fillRoundRect(bx, 39, 36, 36, 4, C_DARK_BAR);
    tft.drawRoundRect(bx, 39, 36, 36, 4, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_DARK_BAR);
    tft.drawString(bLabels[i], bx + 18, 57);
  }

  // Row 2: Node 3 (Tinggi)
  tft.fillRoundRect(6, 83, 308, 44, 6, C_CARD_BG);
  tft.drawRoundRect(6, 83, 308, 44, 6, C_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED, C_CARD_BG);
  tft.setTextSize(1);
  tft.drawString("NODE 3 (TINGGI):", 12, 89);

  const char* tLabels[4] = {"-5.0", "-1.0", "+1.0", "+5.0"};
  for (int i = 0; i < 4; i++) {
    int tx = 150 + (i * 40);
    tft.fillRoundRect(tx, 87, 36, 36, 4, C_DARK_BAR);
    tft.drawRoundRect(tx, 87, 36, 36, 4, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_DARK_BAR);
    tft.drawString(tLabels[i], tx + 18, 105);
  }

  // Row 3: Node 1 (Oxy & Suhu) Container
  tft.fillRoundRect(6, 131, 308, 44, 6, C_CARD_BG);
  tft.drawRoundRect(6, 131, 308, 44, 6, C_BORDER);

  // Row 4: Aksi Cloud
  tft.fillRoundRect(6, 182, 150, 52, 6, tft.color565(15, 45, 80));
  tft.drawRoundRect(6, 182, 150, 52, 6, C_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, tft.color565(15, 45, 80));
  tft.drawString("PUSH TELEMETRI", 81, 200);
  tft.setTextColor(C_WHITE, tft.color565(15, 45, 80));
  tft.drawString("KE CLOUD RTDB", 81, 218);

  tft.fillRoundRect(164, 182, 150, 52, 6, tft.color565(0, 130, 80));
  tft.drawRoundRect(164, 182, 150, 52, 6, C_GREEN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, tft.color565(0, 130, 80));
  tft.drawString("SIMPAN BALITA", 239, 200);
  tft.setTextColor(C_WHITE, tft.color565(0, 130, 80));
  tft.drawString("KE PEMERIKSAAN", 239, 218);
}

void drawTestNodeDynamic() {
  bool wifiOn = (WiFi.status() == WL_CONNECTED);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(wifiOn ? C_GREEN : C_RED, C_BG);
  tft.setTextSize(1);
  tft.drawString(wifiOn ? "CLOUD OK" : "NO WIFI", 314, 11);

  // Update Nilai Test Berat
  tft.fillRect(12, 57, 130, 18, C_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_CYAN, C_CARD_BG);
  tft.setTextSize(2);
  char bufB[16];
  snprintf(bufB, sizeof(bufB), "%.2f kg", testBeratKg);
  tft.drawString(bufB, 12, 57);

  // Update Nilai Test Tinggi
  tft.fillRect(12, 105, 130, 18, C_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_YELLOW, C_CARD_BG);
  tft.setTextSize(2);
  char bufT[16];
  snprintf(bufT, sizeof(bufT), "%.1f cm", testTinggiCm);
  tft.drawString(bufT, 12, 105);

  // Update Tombol Jari
  tft.fillRoundRect(12, 137, 90, 32, 4, testFinger ? tft.color565(0, 80, 50) : C_DARK_BAR);
  tft.drawRoundRect(12, 137, 90, 32, 4, testFinger ? C_GREEN : C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(testFinger ? C_GREEN : C_MUTED, testFinger ? tft.color565(0, 80, 50) : C_DARK_BAR);
  tft.drawString(testFinger ? "JARI: ON" : "JARI: OFF", 57, 153);

  // Update Tombol Suhu
  tft.fillRoundRect(108, 137, 96, 32, 4, (testSuhu > 37.5) ? tft.color565(90, 20, 20) : C_DARK_BAR);
  tft.drawRoundRect(108, 137, 96, 32, 4, (testSuhu > 37.5) ? C_RED : C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor((testSuhu > 37.5) ? C_RED : C_WHITE, (testSuhu > 37.5) ? tft.color565(90, 20, 20) : C_DARK_BAR);
  char bufSuhu[16];
  snprintf(bufSuhu, sizeof(bufSuhu), "%.1f C", testSuhu);
  tft.drawString(bufSuhu, 156, 153);

  // Update Tombol Oxy
  tft.fillRoundRect(210, 137, 98, 32, 4, C_DARK_BAR);
  tft.drawRoundRect(210, 137, 98, 32, 4, C_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_DARK_BAR);
  char bufOxy[20];
  snprintf(bufOxy, sizeof(bufOxy), "%d | %d%%", testBpm, testSpO2);
  tft.drawString(bufOxy, 259, 153);
}

void drawTestNodeScreen() {
  if (needFullRedraw) {
    drawTestNodeStatic();
    needFullRedraw = false;
  }
  drawTestNodeDynamic();
}

// ==========================================
// 12. SCREEN 5: MENU KALIBRASI SENSOR (WIRELESS ESP-NOW)
// ==========================================
void drawCalibrationHeader() {
  // Header Bar (Y: 0..30)
  tft.fillRect(0, 0, 320, 30, C_BG);

  // Tombol < DASH
  tft.fillRoundRect(6, 4, 60, 24, 4, C_DARK_BAR);
  tft.drawRoundRect(6, 4, 60, 24, 4, C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_DARK_BAR);
  tft.drawString("< DASH", 36, 16);

  // Judul
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(1);
  tft.drawString("KALIBRASI", 72, 11);

  // Tab 1: 1. BERAT (X: 172..242)
  tft.fillRoundRect(172, 4, 70, 24, 4, (calibTab == 0) ? tft.color565(15, 45, 80) : C_DARK_BAR);
  tft.drawRoundRect(172, 4, 70, 24, 4, (calibTab == 0) ? C_CYAN : C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor((calibTab == 0) ? C_CYAN : C_MUTED, (calibTab == 0) ? tft.color565(15, 45, 80) : C_DARK_BAR);
  tft.drawString("1. BERAT", 207, 16);

  // Tab 2: 2. TINGGI (X: 246..314)
  tft.fillRoundRect(246, 4, 68, 24, 4, (calibTab == 1) ? tft.color565(50, 40, 10) : C_DARK_BAR);
  tft.drawRoundRect(246, 4, 68, 24, 4, (calibTab == 1) ? C_YELLOW : C_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor((calibTab == 1) ? C_YELLOW : C_MUTED, (calibTab == 1) ? tft.color565(50, 40, 10) : C_DARK_BAR);
  tft.drawString("2. TINGGI", 280, 16);

  tft.drawFastHLine(0, 31, 320, C_BORDER);
}

void drawCalibrationStatic() {
  tft.fillScreen(C_BG);
  drawCalibrationHeader();

  if (calibTab == 0) {
    // ----------------------------------------
    // TAB 0: KALIBRASI TIMBANGAN (HX711)
    // ----------------------------------------
    // Kartu 1: Live Berat
    tft.fillRoundRect(6, 35, 148, 46, 6, C_CARD_BG);
    tft.drawRoundRect(6, 35, 148, 46, 6, C_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD_BG);
    tft.setTextSize(1);
    tft.drawString("BERAT SAAT INI:", 12, 40);

    // Tombol TARE (Nol-kan)
    tft.fillRoundRect(160, 35, 154, 46, 6, tft.color565(40, 25, 60));
    tft.drawRoundRect(160, 35, 154, 46, 6, tft.color565(140, 90, 220));
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, tft.color565(40, 25, 60));
    tft.drawString("TARE (NOL)", 237, 51);
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(190, 160, 255), tft.color565(40, 25, 60));
    tft.drawString("NOL-KAN BEBAN KOSONG", 237, 70);

    // Kartu 2: Pengaturan Target Beban Acuan
    tft.fillRoundRect(6, 85, 308, 92, 6, C_CARD_BG);
    tft.drawRoundRect(6, 85, 308, 92, 6, C_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD_BG);
    tft.setTextSize(1);
    tft.drawString("TARGET BEBAN ACUAN:", 12, 90);

    // Stepper buttons (Y: 106..138)
    const char* stepLabels[6] = {"-5", "-1", "-0.1", "+0.1", "+1", "+5"};
    for (int i = 0; i < 6; i++) {
      int bx = 10 + (i * 51);
      tft.fillRoundRect(bx, 106, 46, 30, 4, C_DARK_BAR);
      tft.drawRoundRect(bx, 106, 46, 30, 4, C_BORDER);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(1);
      tft.setTextColor(C_WHITE, C_DARK_BAR);
      tft.drawString(stepLabels[i], bx + 23, 121);
    }

    // Quick Presets Row (Y: 142..170)
    const char* prLabels[6] = {"1kg", "5kg", "10kg", "20kg", "50kg", "60kg"};
    for (int i = 0; i < 6; i++) {
      int px = 10 + (i * 51);
      tft.fillRoundRect(px, 142, 46, 26, 4, tft.color565(20, 35, 45));
      tft.drawRoundRect(px, 142, 46, 26, 4, C_BORDER);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(1);
      tft.setTextColor(C_CYAN, tft.color565(20, 35, 45));
      tft.drawString(prLabels[i], px + 23, 155);
    }

    // Tombol Eksekusi Kalibrasi Beban (Y: 182..234)
    tft.fillRoundRect(6, 182, 308, 52, 6, tft.color565(0, 110, 60));
    tft.drawRoundRect(6, 182, 308, 52, 6, C_GREEN);
  } else {
    // ----------------------------------------
    // TAB 1: PENGUKUR TINGGI (HC-SR04) ACUAN 2.0 METER
    // ----------------------------------------
    // Kartu 1: Standar Acuan Tiang (200.0 cm)
    tft.fillRoundRect(6, 35, 148, 50, 6, C_CARD_BG);
    tft.drawRoundRect(6, 35, 148, 50, 6, C_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD_BG);
    tft.setTextSize(1);
    tft.drawString("ACUAN TIANG:", 12, 40);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_CARD_BG);
    tft.drawString("200.0 cm", 80, 64);

    // Kartu 2: Jarak Sensor Riil ke Lantai
    tft.fillRoundRect(160, 35, 154, 50, 6, C_CARD_BG);
    tft.drawRoundRect(160, 35, 154, 50, 6, C_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_MUTED, C_CARD_BG);
    tft.setTextSize(1);
    tft.drawString("JARAK KE LANTAI:", 166, 40);

    // Kartu 3: Status Penyesuaian Fisik Tiang (Adjuster Banner)
    tft.fillRoundRect(6, 90, 308, 58, 6, C_CARD_BG);
    tft.drawRoundRect(6, 90, 308, 58, 6, C_BORDER);

    // Kartu 4: Panduan Singkat
    tft.fillRoundRect(6, 154, 308, 80, 6, tft.color565(12, 22, 35));
    tft.drawRoundRect(6, 154, 308, 80, 6, C_BORDER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_YELLOW, tft.color565(12, 22, 35));
    tft.setTextSize(1);
    tft.drawString("PANDUAN SETTING TIANG 2.0 METER:", 14, 160);
    tft.setTextColor(C_WHITE, tft.color565(12, 22, 35));
    tft.drawString("- Tiang kosong tanpa balita harus tepat 200.0 cm.", 14, 176);
    tft.drawString("- Saat jarak pas 200 cm, tinggi balita di CYD = 0.0 cm.", 14, 192);
    tft.setTextColor(C_CYAN, tft.color565(12, 22, 35));
    tft.drawString("- Geser sensor/tiang sampai banner di atas HIJAU (PAS).", 14, 208);
  }
}

void drawCalibrationDynamic() {
  if (calibTab == 0) {
    // Update Live Berat
    tft.fillRect(12, 54, 136, 22, C_CARD_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(2);
    tft.setTextColor((liveBeratKg > 0.0) ? C_CYAN : C_WHITE, C_CARD_BG);
    char bufB[16];
    snprintf(bufB, sizeof(bufB), "%.2f kg", liveBeratKg);
    tft.drawString(bufB, 12, 54);

    // Update Target Beban
    tft.fillRect(170, 88, 138, 16, C_CARD_BG);
    tft.setTextDatum(TR_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW, C_CARD_BG);
    char bufT[24];
    snprintf(bufT, sizeof(bufT), "[ %.2f kg ]", calibTargetKg);
    tft.drawString(bufT, 306, 90);

    // Update Tombol Eksekusi
    tft.fillRect(10, 186, 300, 44, tft.color565(0, 110, 60));
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_WHITE, tft.color565(0, 110, 60));
    char bufBtn[36];
    snprintf(bufBtn, sizeof(bufBtn), "KALIBRASI %.2f KG", calibTargetKg);
    tft.drawString(bufBtn, 160, 200);

    tft.setTextSize(1);
    if (millis() < calibStatusTime) {
      tft.setTextColor(C_YELLOW, tft.color565(0, 110, 60));
      tft.drawString(calibStatusMsg, 160, 220);
    } else {
      tft.setTextColor(C_GREEN, tft.color565(0, 110, 60));
      tft.drawString("KIRIM FAKTOR BARU VIA ESP-NOW", 160, 220);
    }
  } else {
    // Update Jarak Riil ke Lantai
    tft.fillRect(166, 54, 142, 26, C_CARD_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_YELLOW, C_CARD_BG);
    char bufJ[16];
    snprintf(bufJ, sizeof(bufJ), "%.1f cm", liveRawJarakTinggi);
    tft.drawString(bufJ, 237, 64);

    // Update Banner Adjuster
    float dev = liveRawJarakTinggi - 200.0f;
    if (fabs(dev) <= 0.8f) {
      tft.fillRoundRect(10, 94, 300, 50, 6, tft.color565(0, 80, 45));
      tft.drawRoundRect(10, 94, 300, 50, 6, C_GREEN);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(C_GREEN, tft.color565(0, 80, 45));
      tft.drawString("PAS 2.0 METER (OK)", 160, 110);
      tft.setTextSize(1);
      tft.setTextColor(C_WHITE, tft.color565(0, 80, 45));
      tft.drawString("Posisi Sempurna | Layar CYD: 0.0 cm", 160, 130);
    } else if (dev < -0.8f) {
      tft.fillRoundRect(10, 94, 300, 50, 6, tft.color565(60, 45, 10));
      tft.drawRoundRect(10, 94, 300, 50, 6, C_YELLOW);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(C_YELLOW, tft.color565(60, 45, 10));
      char bufDev[32];
      snprintf(bufDev, sizeof(bufDev), "KURANG %.1f CM", fabs(dev));
      tft.drawString(bufDev, 160, 110);
      tft.setTextSize(1);
      tft.setTextColor(C_WHITE, tft.color565(60, 45, 10));
      tft.drawString("-> NAIKKAN TIANG / SENSOR KE ATAS", 160, 130);
    } else {
      tft.fillRoundRect(10, 94, 300, 50, 6, tft.color565(70, 20, 20));
      tft.drawRoundRect(10, 94, 300, 50, 6, C_RED);
      tft.setTextDatum(MC_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(C_RED, tft.color565(70, 20, 20));
      char bufDev[32];
      snprintf(bufDev, sizeof(bufDev), "LEBIH +%.1f CM", dev);
      tft.drawString(bufDev, 160, 110);
      tft.setTextSize(1);
      tft.setTextColor(C_WHITE, tft.color565(70, 20, 20));
      tft.drawString("-> TURUNKAN TIANG / SENSOR KE BAWAH", 160, 130);
    }
  }
}

void drawCalibrationScreen() {
  if (needFullRedraw) {
    drawCalibrationStatic();
    needFullRedraw = false;
  }
  drawCalibrationDynamic();
}

// ==========================================
// 12. SCREEN 3: WIFI SCANNER
// ==========================================
void startWifiScan() {
  isScanningWifi = true;
  currentScreen = SCREEN_WIFI_SCAN;

  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_CYAN, C_BG);
  tft.setTextSize(2);
  tft.drawString("MEMINDAI WIFI 2.4 GHz...", 160, 100);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED, C_BG);
  tft.drawString("Memindai frekuensi 2.4 GHz...", 160, 130);
  tft.setTextColor(C_YELLOW, C_BG);
  tft.drawString("Pastikan Hotspot / Wi-Fi 2.4 GHz Aktif", 160, 150);

  // Putuskan koneksi sementara agar radio bebas melompat channel
  WiFi.disconnect(false, false);
  delay(150);

  WiFi.scanDelete();
  wifiList.clear();
  wifiPage = 0;

  Serial.println("[WIFI SCAN] Memulai scan jaringan Wi-Fi 2.4 GHz...");
  // Gunakan dwell time 250ms per channel & show_hidden = true
  int n = WiFi.scanNetworks(false, true, false, 250);
  Serial.printf("[WIFI SCAN] Hasil scan awal: %d AP ditemukan.\n", n);

  if (n < 0) {
    Serial.println("[WIFI SCAN] Scan gagal/sibuk, mereset radio WiFi STA...");
    WiFi.disconnect(true, false);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(100);
    n = WiFi.scanNetworks(false, true, false, 300);
    Serial.printf("[WIFI SCAN] Hasil scan ulang: %d AP ditemukan.\n", n);
  }

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      s.trim();
      if (s.length() > 0) {
        bool exists = false;
        for (auto &w : wifiList) {
          if (w.ssid == s) { exists = true; break; }
        }
        if (!exists) {
          WifiItem it;
          it.ssid = s;
          it.rssi = WiFi.RSSI(i);
          it.isEncrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
          wifiList.push_back(it);
          Serial.printf("  -> [AP %02d] '%s' (RSSI: %d dBm, Ch: %d, %s)\n",
            i + 1, s.c_str(), it.rssi, WiFi.channel(i), it.isEncrypted ? "Terkunci" : "Terbuka");
        }
      }
    }
  }

  WiFi.scanDelete();
  isScanningWifi = false;
  drawWifiScanScreen();
}

void drawWifiScanScreen() {
  currentScreen = SCREEN_WIFI_SCAN;
  tft.fillScreen(C_BG);

  // Header Bar (Y: 0 to 30)
  tft.fillRect(0, 0, 320, 30, tft.color565(10, 16, 28));
  tft.drawFastHLine(0, 30, 320, C_BORDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_CYAN, tft.color565(10, 16, 28));
  tft.setTextSize(2);
  tft.drawString("PILIH WIFI", 8, 7);

  // Tombol [ MANUAL ] (X: 130, Y: 4, W: 60, H: 22)
  tft.fillRoundRect(130, 4, 60, 22, 4, C_DARK_BAR);
  tft.drawRoundRect(130, 4, 60, 22, 4, C_YELLOW);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_YELLOW, C_DARK_BAR);
  tft.drawString("MANUAL", 160, 15);

  // Tombol [ RESCAN ] (X: 196, Y: 4, W: 58, H: 22)
  tft.fillRoundRect(196, 4, 58, 22, 4, C_DARK_BAR);
  tft.drawRoundRect(196, 4, 58, 22, 4, C_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_DARK_BAR);
  tft.drawString("RESCAN", 225, 15);

  // Tombol [ BATAL ] (X: 258, Y: 4, W: 56, H: 22)
  tft.fillRoundRect(258, 4, 56, 22, 4, tft.color565(45, 15, 15));
  tft.drawRoundRect(258, 4, 56, 22, 4, C_RED);
  tft.setTextColor(C_WHITE, tft.color565(45, 15, 15));
  tft.drawString("BATAL", 286, 15);

  if (wifiList.empty()) {
    tft.fillRoundRect(12, 42, 296, 120, 8, C_CARD_BG);
    tft.drawRoundRect(12, 42, 296, 120, 8, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_CARD_BG);
    tft.drawString("Tidak ada WiFi terdeteksi otomatis.", 160, 62);
    tft.setTextColor(C_YELLOW, C_CARD_BG);
    tft.drawString("(ESP32 hanya mendukung sinyal 2.4 GHz)", 160, 82);
    tft.setTextColor(C_MUTED, C_CARD_BG);
    tft.drawString("Pastikan Hotspot HP / Router 2.4 GHz aktif,", 160, 102);
    tft.drawString("atau masukkan SSID secara manual di bawah:", 160, 120);

    // Tombol Besar [ + KETIK SSID MANUAL ] (X: 30, Y: 172, W: 260, H: 46)
    tft.fillRoundRect(30, 172, 260, 46, 6, tft.color565(15, 45, 80));
    tft.drawRoundRect(30, 172, 260, 46, 6, C_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, tft.color565(15, 45, 80));
    tft.drawString("+ KETIK SSID MANUAL", 160, 195);
    return;
  }

  // Tampilkan hingga 3 item per halaman (Y: 36, 88, 140)
  int startIdx = wifiPage * 3;
  for (int i = 0; i < 3; i++) {
    int idx = startIdx + i;
    int yPos = 36 + (i * 50);
    if (idx < (int)wifiList.size()) {
      tft.fillRoundRect(10, yPos, 300, 44, 6, C_CARD_BG);
      tft.drawRoundRect(10, yPos, 300, 44, 6, C_BORDER);

      // Sinyal Icon
      int rssi = wifiList[idx].rssi;
      uint16_t sigColor = (rssi >= -65) ? C_GREEN : ((rssi >= -80) ? C_YELLOW : C_RED);
      tft.fillCircle(24, yPos + 22, 6, sigColor);

      // SSID Name
      tft.setTextDatum(TL_DATUM);
      tft.setTextSize(2);
      tft.setTextColor(C_WHITE, C_CARD_BG);
      String dispName = wifiList[idx].ssid;
      if (dispName.length() > 16) dispName = dispName.substring(0, 15) + "..";
      tft.drawString(dispName, 38, yPos + 6);

      // Subtitle
      tft.setTextSize(1);
      tft.setTextColor(C_MUTED, C_CARD_BG);
      char infoBuf[32];
      snprintf(infoBuf, sizeof(infoBuf), "%d dBm  %s", rssi, wifiList[idx].isEncrypted ? "[Terkunci]" : "[Terbuka]");
      tft.drawString(infoBuf, 38, yPos + 27);
    }
  }

  // Baris Navigasi Halaman Bawah (Y: 194 to 234)
  int totalPages = (wifiList.size() + 2) / 3;
  if (wifiPage > 0) {
    tft.fillRoundRect(10, 194, 90, 38, 4, C_DARK_BAR);
    tft.drawRoundRect(10, 194, 90, 38, 4, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_DARK_BAR);
    tft.drawString("< PREV", 55, 213);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED, C_BG);
  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Hal %d / %d", wifiPage + 1, totalPages);
  tft.drawString(pageBuf, 160, 213);

  if (wifiPage < totalPages - 1) {
    tft.fillRoundRect(220, 194, 90, 38, 4, C_DARK_BAR);
    tft.drawRoundRect(220, 194, 90, 38, 4, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_DARK_BAR);
    tft.drawString("NEXT >", 265, 213);
  }
}

// ==========================================
// 13. SCREEN 4: TOUCH QWERTY KEYBOARD
// ==========================================
void drawKeyboardInputBox() {
  tft.fillRoundRect(6, 16, 248, 30, 4, C_CARD_BG);
  tft.drawRoundRect(6, 16, 248, 30, 4, C_CYAN);

  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE, C_CARD_BG);

  String displayPass = "";
  if (typingSSID || showPassword) {
    displayPass = inputPassword;
  } else {
    for (size_t i = 0; i < inputPassword.length(); i++) displayPass += "*";
  }

  if (displayPass.length() > 18) {
    displayPass = displayPass.substring(displayPass.length() - 18);
  }
  tft.drawString(displayPass + "_", 12, 31);

  // Tombol Status Kanan Box (SSID vs EYE)
  if (typingSSID) {
    tft.fillRoundRect(260, 16, 54, 30, 4, C_DARK_BAR);
    tft.drawRoundRect(260, 16, 54, 30, 4, C_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW, C_DARK_BAR);
    tft.drawString("SSID", 287, 31);
  } else {
    tft.fillRoundRect(260, 16, 54, 30, 4, showPassword ? C_CYAN : C_DARK_BAR);
    tft.drawRoundRect(260, 16, 54, 30, 4, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(showPassword ? C_BG : C_WHITE, showPassword ? C_CYAN : C_DARK_BAR);
    tft.drawString(showPassword ? "HIDE" : "SHOW", 287, 31);
  }
}

void drawKeyboardScreen() {
  currentScreen = SCREEN_WIFI_KEYBOARD;
  tft.fillScreen(C_BG);

  tft.fillRect(0, 0, 320, 48, tft.color565(10, 16, 28));
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(typingSSID ? C_YELLOW : C_CYAN, tft.color565(10, 16, 28));
  String headerText = typingSSID ? "KETIK NAMA WIFI (SSID):" : ("PASSWORD UNTUK: " + selectedSSID);
  if (headerText.length() > 34) headerText = headerText.substring(0, 32) + "..";
  tft.drawString(headerText, 10, 4);

  drawKeyboardInputBox();

  // Row 0: Angka 1..0 (Y: 52, H: 30)
  const char row0[] = {'1','2','3','4','5','6','7','8','9','0'};
  for (int i = 0; i < 10; i++) {
    int kx = 6 + (i * 31);
    tft.fillRoundRect(kx, 52, 28, 30, 4, C_KEY_BG);
    tft.drawRoundRect(kx, 52, 28, 30, 4, C_KEY_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_KEY_BG);
    tft.drawChar(row0[i], kx + 14, 67);
  }

  // Row 1: q..p / Q..P / Simbol (Y: 86, H: 30)
  const char row1_l[] = {'q','w','e','r','t','y','u','i','o','p'};
  const char row1_u[] = {'Q','W','E','R','T','Y','U','I','O','P'};
  const char row1_s[] = {'!','@','#','$','%','^','&','*','(',')'};
  for (int i = 0; i < 10; i++) {
    int kx = 6 + (i * 31);
    tft.fillRoundRect(kx, 86, 28, 30, 4, C_KEY_BG);
    tft.drawRoundRect(kx, 86, 28, 30, 4, C_KEY_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor((keyboardMode == 2) ? C_YELLOW : C_WHITE, C_KEY_BG);
    char c = (keyboardMode == 2) ? row1_s[i] : ((keyboardMode == 1) ? row1_u[i] : row1_l[i]);
    tft.drawChar(c, kx + 14, 101);
  }

  // Row 2: a..l @ / A..L - / Simbol (Y: 120, H: 30)
  const char row2_l[] = {'a','s','d','f','g','h','j','k','l','@'};
  const char row2_u[] = {'A','S','D','F','G','H','J','K','L','-'};
  const char row2_s[] = {'-','_','=','+','[',']','{','}','\\','/'};
  for (int i = 0; i < 10; i++) {
    int kx = 6 + (i * 31);
    tft.fillRoundRect(kx, 120, 28, 30, 4, C_KEY_BG);
    tft.drawRoundRect(kx, 120, 28, 30, 4, C_KEY_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor((keyboardMode == 2) ? C_YELLOW : C_WHITE, C_KEY_BG);
    char c = (keyboardMode == 2) ? row2_s[i] : ((keyboardMode == 1) ? row2_u[i] : row2_l[i]);
    tft.drawChar(c, kx + 14, 135);
  }

  // Row 3: [SHIFT/ABC] z..m / simbol . [DEL] (Y: 154, H: 30)
  bool isShiftActive = (keyboardMode == 1);
  uint16_t shiftBg = (keyboardMode == 2) ? tft.color565(20, 50, 30) : (isShiftActive ? C_CYAN : C_KEY_BG);
  uint16_t shiftBorder = (keyboardMode == 2) ? C_GREEN : (isShiftActive ? C_WHITE : C_KEY_BORDER);
  uint16_t shiftText = (keyboardMode == 2) ? C_GREEN : (isShiftActive ? C_BG : C_WHITE);
  tft.fillRoundRect(6, 154, 38, 30, 4, shiftBg);
  tft.drawRoundRect(6, 154, 38, 30, 4, shiftBorder);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(shiftText, shiftBg);
  tft.drawString((keyboardMode == 2) ? "ABC" : "SHIFT", 25, 169);

  const char row3_l[] = {'z','x','c','v','b','n','m'};
  const char row3_u[] = {'Z','X','C','V','B','N','M'};
  const char row3_s[] = {':',';','"','\'','~','?','<'};
  for (int i = 0; i < 7; i++) {
    int kx = 48 + (i * 31);
    tft.fillRoundRect(kx, 154, 28, 30, 4, C_KEY_BG);
    tft.drawRoundRect(kx, 154, 28, 30, 4, C_KEY_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor((keyboardMode == 2) ? C_YELLOW : C_WHITE, C_KEY_BG);
    char c = (keyboardMode == 2) ? row3_s[i] : ((keyboardMode == 1) ? row3_u[i] : row3_l[i]);
    tft.drawChar(c, kx + 14, 169);
  }

  tft.fillRoundRect(265, 154, 24, 30, 4, C_KEY_BG);
  tft.drawRoundRect(265, 154, 24, 30, 4, C_KEY_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_KEY_BG);
  tft.drawString(".", 277, 169);

  tft.fillRoundRect(292, 154, 22, 30, 4, tft.color565(60, 20, 20));
  tft.drawRoundRect(292, 154, 22, 30, 4, C_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, tft.color565(60, 20, 20));
  tft.drawString("<", 303, 169);

  // Row 4: [BATAL] [?#$ / ABC] [ _ ] [SPASI] [CONNECT / LANJUT] (Y: 190, H: 42)
  // 1. Tombol BATAL (x: 6, w: 48)
  tft.fillRoundRect(6, 190, 48, 42, 4, tft.color565(45, 15, 15));
  tft.drawRoundRect(6, 190, 48, 42, 4, C_RED);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, tft.color565(45, 15, 15));
  tft.drawString("BATAL", 30, 211);

  // 2. Tombol SYM Toggle (x: 58, w: 44)
  bool isSym = (keyboardMode == 2);
  tft.fillRoundRect(58, 190, 44, 42, 4, isSym ? tft.color565(20, 45, 20) : C_DARK_BAR);
  tft.drawRoundRect(58, 190, 44, 42, 4, isSym ? C_GREEN : C_YELLOW);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(isSym ? C_GREEN : C_YELLOW, isSym ? tft.color565(20, 45, 20) : C_DARK_BAR);
  tft.drawString(isSym ? "ABC" : "?#$", 80, 211);

  // 3. Tombol UNDERSCORE [ _ ] (x: 106, w: 32)
  tft.fillRoundRect(106, 190, 32, 42, 4, C_KEY_BG);
  tft.drawRoundRect(106, 190, 32, 42, 4, C_CYAN);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_KEY_BG);
  tft.drawString("_", 122, 206);

  // 4. Tombol SPASI (x: 142, w: 68)
  tft.fillRoundRect(142, 190, 68, 42, 4, C_KEY_BG);
  tft.drawRoundRect(142, 190, 68, 42, 4, C_KEY_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED, C_KEY_BG);
  tft.drawString("SPASI", 176, 211);

  // 5. Tombol LANJUT / CONNECT (x: 214, w: 100)
  tft.fillRoundRect(214, 190, 100, 42, 4, typingSSID ? C_CYAN : C_GREEN);
  tft.drawRoundRect(214, 190, 100, 42, 4, C_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(C_BG, typingSSID ? C_CYAN : C_GREEN);
  tft.drawString(typingSSID ? "LANJUT" : "CONNECT", 264, 211);
}

// ==========================================
// 14. SCREEN 5: VERIFIKASI KONEKSI WIFI
// ==========================================
void connectWifiAndVerify() {
  currentScreen = SCREEN_WIFI_CONNECTING;
  tft.fillScreen(C_BG);

  tft.fillRoundRect(16, 50, 288, 130, 8, C_CARD_BG);
  tft.drawRoundRect(16, 50, 288, 130, 8, C_CYAN);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_CARD_BG);
  tft.drawString("MENGHUBUNGKAN...", 160, 80);

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_CARD_BG);
  tft.drawString("SSID: " + selectedSSID, 160, 110);

  WiFi.disconnect();
  delay(100);
  WiFi.begin(selectedSSID.c_str(), inputPassword.c_str());

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 25) {
    delay(500);
    retries++;
    tft.fillRect(20, 140, 280, 20, C_CARD_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(C_YELLOW, C_CARD_BG);
    tft.drawString("Mencoba (" + String(retries / 2) + "s)...", 160, 150);
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Simpan ke NVS Flash permanen
    prefs.begin("wifi_cfg", false);
    prefs.putString("ssid", selectedSSID);
    prefs.putString("pass", inputPassword);
    prefs.end();

    savedSSID = selectedSSID;
    savedPass = inputPassword;

    tft.fillRoundRect(16, 50, 288, 130, 8, C_GREEN);
    tft.drawRoundRect(16, 50, 288, 130, 8, C_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_BG, C_GREEN);
    tft.drawString("BERHASIL TERHUBUNG!", 160, 85);
    tft.setTextSize(1);
    tft.drawString("IP: " + WiFi.localIP().toString(), 160, 120);
    tft.drawString("Koneksi Cloud Firebase Aktif", 160, 145);
    successBeep();
    delay(1500);

    currentScreen = SCREEN_DASHBOARD;
    needFullRedraw = true;
  } else {
    tft.fillRoundRect(16, 50, 288, 130, 8, tft.color565(60, 20, 20));
    tft.drawRoundRect(16, 50, 288, 130, 8, C_RED);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(C_RED, tft.color565(60, 20, 20));
    tft.drawString("GAGAL TERHUBUNG!", 160, 80);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, tft.color565(60, 20, 20));
    tft.drawString("Password salah atau sinyal lemah.", 160, 115);

    tft.fillRoundRect(16, 190, 140, 42, 6, C_DARK_BAR);
    tft.drawRoundRect(16, 190, 140, 42, 6, C_BORDER);
    tft.setTextColor(C_WHITE, C_DARK_BAR);
    tft.drawString("COBA LAGI", 86, 211);

    tft.fillRoundRect(164, 190, 140, 42, 6, C_DARK_BAR);
    tft.drawRoundRect(164, 190, 140, 42, 6, C_CYAN);
    tft.setTextColor(C_CYAN, C_DARK_BAR);
    tft.drawString("PILIH LAIN", 234, 211);
  }
}

void drawToastNotification() {
  static bool wasToastActive = false;
  bool isToastActive = (toastUntil > 0 && millis() < toastUntil);

  if (isToastActive) {
    // Clinical Alert Ticker di Header Bar (Y: 0..31) tanpa menimpa kartu vital sign
    uint16_t bgToast = isLightTheme ? 0xFFFF : C_CARD_BG;
    tft.fillRect(0, 0, 320, 31, bgToast);
    tft.drawFastHLine(0, 31, 320, C_BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(isLightTheme ? tft.color565(0, 140, 230) : C_CYAN, bgToast);
    tft.drawString("[ NOTIFIKASI SISTEM ]", 160, 8);
    tft.setTextColor(isLightTheme ? 0x0000 : C_WHITE, bgToast);
    tft.drawString(toastMsg, 160, 21);
    wasToastActive = true;
  } else if (wasToastActive) {
    wasToastActive = false;
    toastUntil = 0;
    // Pulihkan header sesuai layar aktif secara halus (Flicker-Free, No fillScreen)
    if (currentScreen == SCREEN_DASHBOARD) {
      drawDashboardHeader();
    } else if (currentScreen == SCREEN_SLIDE2) {
      drawSlide2Header();
    } else if (currentScreen == SCREEN_TEST_NODE) {
      drawTestNodeHeader();
    } else if (currentScreen == SCREEN_CALIBRATION) {
      drawCalibrationHeader();
    }
  }
}

// ==========================================
// 15. DETEKSI SENTUHAN (TOUCH CONTROLLER)
// ==========================================
void checkTouch() {
  if (!ts.touched()) return;

  TS_Point p = ts.getPoint();
  if (p.z < 200) return;

  if (millis() - lastTouchTime < 200) return;
  lastTouchTime = millis();

  int tx = map(p.x, 200, 3700, 0, 320);
  int ty = map(p.y, 240, 3800, 0, 240);
  tx = constrain(tx, 0, 319);
  ty = constrain(ty, 0, 239);

  // ----------------------------------------
  // A. TOUCH: SCREEN_DASHBOARD
  // ----------------------------------------
  if (currentScreen == SCREEN_DASHBOARD) {
    // 1. Tombol [ SLIDE 2 > ] di Header (X: 156..240, Y: 2..32)
    if (tx >= 156 && tx <= 240 && ty >= 2 && ty <= 32) {
      clickBeep(12);
      currentScreen = SCREEN_SLIDE2;
      needFullRedraw = true;
      return;
    }

    // 2. Tombol [ WIFI ] Pojok Kanan Atas (X: 242..318, Y: 2..32)
    if (tx >= 242 && tx <= 318 && ty >= 2 && ty <= 32) {
      clickBeep(12);
      startWifiScan();
      return;
    }

    // 4. Tombol TARE Pojok Kanan Atas Kartu 1 Berat (X: 100..156, Y: 34..60)
    if (tx >= 100 && tx <= 156 && ty >= 34 && ty <= 60) {
      clickBeep(15);
      sendTareCommandToNode2();
      liveBeratKg = 0.0;
      if (isLockedBerat) {
        savedBeratKg = 0.0;
      }
      snprintf(toastMsg, sizeof(toastMsg), "TIMBANGAN DI-TARE (0.00 KG)");
      toastUntil = millis() + 2500;
      triggerForcePush = true;
      return;
    }

    // 5. BARIS 4 TOMBOL BAWAH SINKRONISASI CYD (Y: 184..239)
    if (ty >= 184 && ty <= 239) {
      // Tombol 1: SAVE BERAT (X: 2..78)
      if (tx >= 2 && tx <= 78) {
        clickBeep(15);
        isLockedBerat = !isLockedBerat;
        if (isLockedBerat) {
          savedBeratKg = (simTestActive ? testBeratKg : liveBeratKg);
          snprintf(toastMsg, sizeof(toastMsg), "BERAT DI-SAVE (%.2f KG)", savedBeratKg);
        } else {
          snprintf(toastMsg, sizeof(toastMsg), "KUNCI BERAT DIBATALKAN");
        }
        toastUntil = millis() + 2000;
        triggerForcePush = true;
        return;
      }

      // Tombol 2: SAVE TINGGI (X: 79..155)
      if (tx >= 79 && tx <= 155) {
        clickBeep(15);
        isLockedTinggi = !isLockedTinggi;
        if (isLockedTinggi) {
          savedTinggiCm = (simTestActive ? testTinggiCm : liveTinggiCm);
          snprintf(toastMsg, sizeof(toastMsg), "TINGGI DI-SAVE (%.1f CM)", savedTinggiCm);
        } else {
          snprintf(toastMsg, sizeof(toastMsg), "KUNCI TINGGI DIBATALKAN");
        }
        toastUntil = millis() + 2000;
        triggerForcePush = true;
        return;
      }

      // Tombol 3: SAVE OXY (X: 156..232)
      if (tx >= 156 && tx <= 232) {
        clickBeep(15);
        isLockedOxy = !isLockedOxy;
        if (isLockedOxy) {
          savedBpm = (simTestActive ? testBpm : liveBpm);
          savedSpO2 = (simTestActive ? testSpO2 : liveSpO2);
          savedSuhu = (simTestActive ? testSuhu : liveSuhu);
          snprintf(toastMsg, sizeof(toastMsg), "OXY/SUHU DI-SAVE (%d BPM)", savedBpm);
        } else {
          snprintf(toastMsg, sizeof(toastMsg), "KUNCI OXY DIBATALKAN");
        }
        toastUntil = millis() + 2000;
        triggerForcePush = true;
        return;
      }

      // Tombol 4: SUBMIT / KIRIM KE DASHBOARD (X: 233..319)
      if (tx >= 233 && tx <= 319) {
        clickBeep(25);
        float curB = isLockedBerat ? savedBeratKg : (simTestActive ? testBeratKg : liveBeratKg);
        float curT = isLockedTinggi ? savedTinggiCm : (simTestActive ? testTinggiCm : liveTinggiCm);

        if (curB <= 0.0 && curT <= 0.0 && !simTestActive) {
          snprintf(toastMsg, sizeof(toastMsg), "BELUM ADA DATA SENSOR BALITA!");
          toastUntil = millis() + 2500;
          return;
        }

        // Kunci nilai aktif jika belum dikunci agar snapshot akurat
        if (!isLockedBerat && liveBeratKg > 0.0) {
          isLockedBerat = true;
          savedBeratKg = liveBeratKg;
        }
        if (!isLockedTinggi && liveTinggiCm > 0.0) {
          isLockedTinggi = true;
          savedTinggiCm = liveTinggiCm;
        }
        if (!isLockedOxy && (liveBpm > 0 || liveSuhu > 0.0)) {
          isLockedOxy = true;
          savedBpm = liveBpm;
          savedSpO2 = liveSpO2;
          savedSuhu = liveSuhu;
        }

        triggerSaveCloud = true;
        triggerForcePush = true;
        snprintf(toastMsg, sizeof(toastMsg), "MENGIRIM KE FIREBASE...");
        toastUntil = millis() + 3500;
        return;
      }
    }
  }

  // ----------------------------------------
  // A2. TOUCH: SCREEN_SLIDE2 (MENU & LAB)
  // ----------------------------------------
  else if (currentScreen == SCREEN_SLIDE2) {
    // 1. Tombol < SLIDE 1 Pojok Kiri Atas (X: 2..95, Y: 2..32)
    if (tx >= 2 && tx <= 95 && ty >= 2 && ty <= 32) {
      clickBeep(12);
      currentScreen = SCREEN_DASHBOARD;
      needFullRedraw = true;
      return;
    }

    // 2. Tombol [ WIFI ] Pojok Kanan Atas (X: 242..318, Y: 2..32)
    if (tx >= 242 && tx <= 318 && ty >= 2 && ty <= 32) {
      clickBeep(12);
      startWifiScan();
      return;
    }

    // 3. Kartu Menu 1: MENU KALIBRASI (X: 10..310, Y: 36..90)
    if (tx >= 10 && tx <= 310 && ty >= 36 && ty <= 90) {
      clickBeep(15);
      currentScreen = SCREEN_CALIBRATION;
      calibTab = 0;
      needFullRedraw = true;
      return;
    }

    // 4. Kartu Menu 2: TEST LAB SIMULATOR (X: 10..310, Y: 96..150)
    if (tx >= 10 && tx <= 310 && ty >= 96 && ty <= 150) {
      clickBeep(15);
      currentScreen = SCREEN_TEST_NODE;
      needFullRedraw = true;
      return;
    }

    // 5. Kartu Menu 3: PILIH TEMA DARK (X: 156..226, Y: 156..216)
    if (tx >= 156 && tx <= 226 && ty >= 156 && ty <= 216) {
      if (isLightTheme) {
        clickBeep(15);
        applyTheme(false);
        prefs.begin("sys_cfg", false);
        prefs.putBool("light_theme", false);
        prefs.end();
        needFullRedraw = true;
        drawSlide2Screen();
      }
      return;
    }

    // 6. Kartu Menu 3: PILIH TEMA LIGHT (X: 232..306, Y: 156..216)
    if (tx >= 232 && tx <= 306 && ty >= 156 && ty <= 216) {
      if (!isLightTheme) {
        clickBeep(15);
        applyTheme(true);
        prefs.begin("sys_cfg", false);
        prefs.putBool("light_theme", true);
        prefs.end();
        needFullRedraw = true;
        drawSlide2Screen();
      }
      return;
    }

    // 7. Toggle Tema saat menyentuh teks kiri Kartu 3 (X: 10..155, Y: 156..216)
    if (tx >= 10 && tx <= 155 && ty >= 156 && ty <= 216) {
      clickBeep(15);
      applyTheme(!isLightTheme);
      prefs.begin("sys_cfg", false);
      prefs.putBool("light_theme", isLightTheme);
      prefs.end();
      needFullRedraw = true;
      drawSlide2Screen();
      return;
    }

    // 8. Sentuh footer bawah untuk kembali ke Slide 1 (Y: 220..239)
    if (ty >= 220 && ty <= 239) {
      clickBeep(10);
      currentScreen = SCREEN_DASHBOARD;
      needFullRedraw = true;
      return;
    }
  }

  // ----------------------------------------
  // B. TOUCH: SCREEN_TEST_NODE
  // ----------------------------------------
  else if (currentScreen == SCREEN_TEST_NODE) {
    // Tombol < DASH (X: 6 to 74, Y: 4 to 32) -> Kembali ke SLIDE 2
    if (tx >= 6 && tx <= 74 && ty >= 4 && ty <= 32) {
      clickBeep(10);
      currentScreen = SCREEN_SLIDE2;
      needFullRedraw = true;
      simTestActive = false; // NONAKTIFKAN MODE SIMULASI SEKETIKA!

      // Jika node sensor riil belum ada, kembalikan nilai ke 0
      if (millis() - lastSeenNode1 >= 3500 || lastSeenNode1 == 0) {
        liveBpm = 0; liveSpO2 = 0; liveSuhu = 0.0; liveFinger = false;
      }
      if (millis() - lastSeenNode2 >= 3500 || lastSeenNode2 == 0) {
        liveBeratKg = 0.0;
      }
      if (millis() - lastSeenNode3 >= 3500 || lastSeenNode3 == 0) {
        liveTinggiCm = 0.0;
      }

      // Kirim force push ke Firebase agar status node di cloud langsung berubah ke OFFLINE
      triggerForcePush = true;
      snprintf(toastMsg, sizeof(toastMsg), "KEMBALI KE MENU SLIDE 2");
      toastUntil = millis() + 2000;
      return;
    }

    // Tombol RST 0 (X: 190 to 252, Y: 2 to 32)
    if (tx >= 190 && tx <= 252 && ty >= 2 && ty <= 32) {
      clickBeep(15);
      simTestActive = false;
      liveBeratKg = 0.0;
      liveTinggiCm = 0.0;
      liveBpm = 0;
      liveSpO2 = 0;
      liveSuhu = 0.0;
      liveFinger = false;
      testBeratKg = 0.0;
      testTinggiCm = 0.0;
      testBpm = 0;
      testSpO2 = 0;
      testSuhu = 0.0;
      testFinger = false;
      triggerForcePush = true;
      snprintf(toastMsg, sizeof(toastMsg), "DATA DIRESET KE 0 (REAL MODE)");
      toastUntil = millis() + 2500;
      return;
    }

    // Kontrol Berat (Y: 35 to 79)
    if (ty >= 35 && ty <= 79) {
      if (tx >= 150 && tx <= 186) { clickBeep(8); testBeratKg -= 1.0; if (testBeratKg < 0) testBeratKg = 0; return; }
      if (tx >= 190 && tx <= 226) { clickBeep(8); testBeratKg -= 0.1; if (testBeratKg < 0) testBeratKg = 0; return; }
      if (tx >= 230 && tx <= 266) { clickBeep(8); testBeratKg += 0.1; return; }
      if (tx >= 270 && tx <= 312) { clickBeep(8); testBeratKg += 1.0; return; }
    }

    // Kontrol Tinggi (Y: 83 to 127)
    if (ty >= 83 && ty <= 127) {
      if (tx >= 150 && tx <= 186) { clickBeep(8); testTinggiCm -= 5.0; if (testTinggiCm < 0) testTinggiCm = 0; return; }
      if (tx >= 190 && tx <= 226) { clickBeep(8); testTinggiCm -= 1.0; if (testTinggiCm < 0) testTinggiCm = 0; return; }
      if (tx >= 230 && tx <= 266) { clickBeep(8); testTinggiCm += 1.0; return; }
      if (tx >= 270 && tx <= 312) { clickBeep(8); testTinggiCm += 5.0; return; }
    }

    // Kontrol Oxy & Suhu (Y: 131 to 175)
    if (ty >= 131 && ty <= 175) {
      if (tx >= 12 && tx <= 102) { clickBeep(12); testFinger = !testFinger; return; }
      if (tx >= 108 && tx <= 204) {
        clickBeep(12);
        testSuhu = (testSuhu <= 37.0) ? 38.2 : 36.6;
        return;
      }
      if (tx >= 210 && tx <= 312) {
        clickBeep(12);
        if (testSpO2 == 99) { testSpO2 = 94; testBpm = 112; }
        else { testSpO2 = 99; testBpm = 86; }
        return;
      }
    }

    // Tombol Aksi Bawah (Y: 182 to 234)
    if (ty >= 182 && ty <= 234) {
      if (tx >= 6 && tx <= 156) {
        clickBeep(15);
        simTestActive = true;
        liveBeratKg = testBeratKg;
        liveTinggiCm = testTinggiCm;
        liveBpm = testBpm;
        liveSpO2 = testSpO2;
        liveSuhu = testSuhu;
        liveFinger = testFinger;
        triggerForcePush = true;
        snprintf(toastMsg, sizeof(toastMsg), "MENGIRIM TELEMETRI SIMULASI...");
        toastUntil = millis() + 3000;
        return;
      }
      if (tx >= 164 && tx <= 314) {
        clickBeep(20);
        simTestActive = true;
        liveBeratKg = testBeratKg;
        liveTinggiCm = testTinggiCm;
        liveBpm = testBpm;
        liveSpO2 = testSpO2;
        liveSuhu = testSuhu;
        liveFinger = testFinger;
        triggerSaveCloud = true;
        snprintf(toastMsg, sizeof(toastMsg), "MENYIMPAN BALITA KE CLOUD...");
        toastUntil = millis() + 3500;
        return;
      }
    }
  }

  // ----------------------------------------
  // C. TOUCH: SCREEN_CALIBRATION
  // ----------------------------------------
  else if (currentScreen == SCREEN_CALIBRATION) {
    // Tombol < DASH (X: 6 to 66, Y: 2 to 32) -> Kembali ke SLIDE 2
    if (tx >= 6 && tx <= 66 && ty >= 2 && ty <= 32) {
      clickBeep(10);
      currentScreen = SCREEN_SLIDE2;
      needFullRedraw = true;
      return;
    }

    // Tab 1: 1. BERAT (X: 170 to 242, Y: 2 to 32)
    if (tx >= 170 && tx <= 242 && ty >= 2 && ty <= 32) {
      if (calibTab != 0) {
        clickBeep(10);
        calibTab = 0;
        needFullRedraw = true;
      }
      return;
    }

    // Tab 2: 2. TINGGI (X: 246 to 316, Y: 2 to 32)
    if (tx >= 246 && tx <= 316 && ty >= 2 && ty <= 32) {
      if (calibTab != 1) {
        clickBeep(10);
        calibTab = 1;
        needFullRedraw = true;
      }
      return;
    }

    // Interaksi Khusus Tab 0: Timbangan
    if (calibTab == 0) {
      // Tombol TARE (X: 160 to 314, Y: 35 to 81)
      if (tx >= 160 && tx <= 314 && ty >= 35 && ty <= 81) {
        clickBeep(15);
        sendTareCommandToNode2();
        calibStatusMsg = "MENGIRIM PERINTAH TARE...";
        calibStatusTime = millis() + 3000;
        snprintf(toastMsg, sizeof(toastMsg), "MENGIRIM TARE KE TIMBANGAN...");
        toastUntil = millis() + 2500;
        return;
      }

      // Baris Tombol Stepper (Y: 104 to 138)
      if (ty >= 104 && ty <= 138) {
        if (tx >= 8 && tx <= 58)        { clickBeep(8); calibTargetKg -= 5.0f; }
        else if (tx >= 59 && tx <= 109) { clickBeep(8); calibTargetKg -= 1.0f; }
        else if (tx >= 110 && tx <= 160){ clickBeep(8); calibTargetKg -= 0.1f; }
        else if (tx >= 161 && tx <= 211){ clickBeep(8); calibTargetKg += 0.1f; }
        else if (tx >= 212 && tx <= 262){ clickBeep(8); calibTargetKg += 1.0f; }
        else if (tx >= 263 && tx <= 314){ clickBeep(8); calibTargetKg += 5.0f; }
        calibTargetKg = constrain(calibTargetKg, 0.1f, 150.0f);
        return;
      }

      // Baris Tombol Preset (Y: 140 to 172)
      if (ty >= 140 && ty <= 172) {
        if (tx >= 8 && tx <= 58)        { clickBeep(10); calibTargetKg = 1.0f; }
        else if (tx >= 59 && tx <= 109) { clickBeep(10); calibTargetKg = 5.0f; }
        else if (tx >= 110 && tx <= 160){ clickBeep(10); calibTargetKg = 10.0f; }
        else if (tx >= 161 && tx <= 211){ clickBeep(10); calibTargetKg = 20.0f; }
        else if (tx >= 212 && tx <= 262){ clickBeep(10); calibTargetKg = 50.0f; }
        else if (tx >= 263 && tx <= 314){ clickBeep(10); calibTargetKg = 60.0f; }
        return;
      }

      // Tombol Eksekusi Kalibrasi Beban (X: 6 to 314, Y: 180 to 236)
      if (tx >= 6 && tx <= 314 && ty >= 180 && ty <= 236) {
        clickBeep(20);
        sendCalibrateCommandToNode2(calibTargetKg);
        calibStatusMsg = "MENGIRIM KALIBRASI...";
        calibStatusTime = millis() + 4000;
        snprintf(toastMsg, sizeof(toastMsg), "MENGIRIM KALIBRASI %.2f KG...", calibTargetKg);
        toastUntil = millis() + 3000;
        return;
      }
    }
  }

  // ----------------------------------------
  // D. TOUCH: SCREEN_WIFI_SCAN
  // ----------------------------------------
  else if (currentScreen == SCREEN_WIFI_SCAN) {
    // Tombol MANUAL di Header (X: 125 to 192, Y: 2 to 28)
    if (tx >= 125 && tx <= 192 && ty >= 2 && ty <= 28) {
      clickBeep();
      typingSSID = true;
      selectedSSID = "";
      inputPassword = "";
      drawKeyboardScreen();
      return;
    }
    // Tombol RESCAN (X: 194 to 255, Y: 2 to 28)
    if (tx >= 194 && tx <= 255 && ty >= 2 && ty <= 28) {
      clickBeep();
      startWifiScan();
      return;
    }
    // Tombol BATAL (X: 256 to 318, Y: 2 to 28)
    if (tx >= 256 && tx <= 318 && ty >= 2 && ty <= 28) {
      clickBeep();
      currentScreen = SCREEN_DASHBOARD;
      needFullRedraw = true;
      return;
    }

    // Jika wifiList kosong, tombol besar [ + KETIK SSID MANUAL ] (X: 25 to 295, Y: 165 to 225)
    if (wifiList.empty()) {
      if (tx >= 25 && tx <= 295 && ty >= 165 && ty <= 225) {
        clickBeep();
        typingSSID = true;
        selectedSSID = "";
        inputPassword = "";
        drawKeyboardScreen();
        return;
      }
      return;
    }

    // Pilih WiFi Item 0 (Y: 36 to 82)
    if (tx >= 10 && tx <= 310 && ty >= 36 && ty <= 82) {
      int idx = wifiPage * 3 + 0;
      if (idx < (int)wifiList.size()) {
        clickBeep();
        selectedSSID = wifiList[idx].ssid;
        inputPassword = "";
        typingSSID = false;
        drawKeyboardScreen();
        return;
      }
    }
    // Pilih WiFi Item 1 (Y: 86 to 132)
    if (tx >= 10 && tx <= 310 && ty >= 86 && ty <= 132) {
      int idx = wifiPage * 3 + 1;
      if (idx < (int)wifiList.size()) {
        clickBeep();
        selectedSSID = wifiList[idx].ssid;
        inputPassword = "";
        typingSSID = false;
        drawKeyboardScreen();
        return;
      }
    }
    // Pilih WiFi Item 2 (Y: 136 to 182)
    if (tx >= 10 && tx <= 310 && ty >= 136 && ty <= 182) {
      int idx = wifiPage * 3 + 2;
      if (idx < (int)wifiList.size()) {
        clickBeep();
        selectedSSID = wifiList[idx].ssid;
        inputPassword = "";
        typingSSID = false;
        drawKeyboardScreen();
        return;
      }
    }
    // Navigasi Prev/Next
    int totalPages = (wifiList.size() + 2) / 3;
    if (wifiPage > 0 && tx >= 10 && tx <= 100 && ty >= 194 && ty <= 234) {
      clickBeep();
      wifiPage--;
      drawWifiScanScreen();
      return;
    }
    if (wifiPage < totalPages - 1 && tx >= 220 && tx <= 310 && ty >= 194 && ty <= 234) {
      clickBeep();
      wifiPage++;
      drawWifiScanScreen();
      return;
    }
  }

  // ----------------------------------------
  // D. TOUCH: SCREEN_WIFI_KEYBOARD
  // ----------------------------------------
  else if (currentScreen == SCREEN_WIFI_KEYBOARD) {
    // Tombol EYE Show/Hide (hanya aktif saat mode password)
    if (tx >= 260 && tx <= 316 && ty >= 16 && ty <= 48) {
      clickBeep();
      if (!typingSSID) {
        showPassword = !showPassword;
        drawKeyboardInputBox();
      }
      return;
    }

    // Row 0: Angka 1..0
    if (ty >= 50 && ty <= 84) {
      const char r0[] = {'1','2','3','4','5','6','7','8','9','0'};
      int kIdx = (tx - 6) / 31;
      if (kIdx >= 0 && kIdx < 10) {
        clickBeep();
        inputPassword += r0[kIdx];
        drawKeyboardInputBox();
        return;
      }
    }

    // Row 1: q..p / Q..P / Simbol
    if (ty >= 85 && ty <= 118) {
      const char r1_l[] = {'q','w','e','r','t','y','u','i','o','p'};
      const char r1_u[] = {'Q','W','E','R','T','Y','U','I','O','P'};
      const char r1_s[] = {'!','@','#','$','%','^','&','*','(',')'};
      int kIdx = (tx - 6) / 31;
      if (kIdx >= 0 && kIdx < 10) {
        clickBeep();
        char c = (keyboardMode == 2) ? r1_s[kIdx] : ((keyboardMode == 1) ? r1_u[kIdx] : r1_l[kIdx]);
        inputPassword += c;
        drawKeyboardInputBox();
        return;
      }
    }

    // Row 2: a..l @ / A..L - / Simbol
    if (ty >= 119 && ty <= 152) {
      const char r2_l[] = {'a','s','d','f','g','h','j','k','l','@'};
      const char r2_u[] = {'A','S','D','F','G','H','J','K','L','-'};
      const char r2_s[] = {'-','_','=','+','[',']','{','}','\\','/'};
      int kIdx = (tx - 6) / 31;
      if (kIdx >= 0 && kIdx < 10) {
        clickBeep();
        char c = (keyboardMode == 2) ? r2_s[kIdx] : ((keyboardMode == 1) ? r2_u[kIdx] : r2_l[kIdx]);
        inputPassword += c;
        drawKeyboardInputBox();
        return;
      }
    }

    // Row 3: Shift/ABC, z..m / simbol, ., DEL
    if (ty >= 153 && ty <= 188) {
      // Tombol Shift / ABC
      if (tx >= 4 && tx <= 46) {
        clickBeep();
        if (keyboardMode == 2) {
          keyboardMode = 0;
        } else if (keyboardMode == 1) {
          keyboardMode = 0;
        } else {
          keyboardMode = 1;
        }
        isShift = (keyboardMode == 1);
        drawKeyboardScreen();
        return;
      }
      // 7 Karakter Tengah
      const char r3_l[] = {'z','x','c','v','b','n','m'};
      const char r3_u[] = {'Z','X','C','V','B','N','M'};
      const char r3_s[] = {':',';','"','\'','~','?','<'};
      int kIdx = (tx - 48) / 31;
      if (kIdx >= 0 && kIdx < 7) {
        clickBeep();
        char c = (keyboardMode == 2) ? r3_s[kIdx] : ((keyboardMode == 1) ? r3_u[kIdx] : r3_l[kIdx]);
        inputPassword += c;
        drawKeyboardInputBox();
        return;
      }
      // Titik [.]
      if (tx >= 263 && tx <= 290) {
        clickBeep();
        inputPassword += ".";
        drawKeyboardInputBox();
        return;
      }
      // Backspace [<]
      if (tx >= 291 && tx <= 318) {
        clickBeep();
        if (inputPassword.length() > 0) {
          inputPassword.remove(inputPassword.length() - 1);
          drawKeyboardInputBox();
        }
        return;
      }
    }

    // Row 4: BATAL, SYM(?#$), UNDERSCORE(_), SPASI, CONNECT / LANJUT
    if (ty >= 189 && ty <= 238) {
      // 1. BATAL
      if (tx >= 4 && tx <= 55) {
        clickBeep();
        typingSSID = false;
        keyboardMode = 0;
        isShift = false;
        drawWifiScanScreen();
        return;
      }
      // 2. SYM TOGGLE (?#$ / ABC)
      if (tx >= 56 && tx <= 103) {
        clickBeep();
        if (keyboardMode == 2) {
          keyboardMode = 0;
        } else {
          keyboardMode = 2;
        }
        isShift = (keyboardMode == 1);
        drawKeyboardScreen();
        return;
      }
      // 3. UNDERSCORE (_)
      if (tx >= 104 && tx <= 139) {
        clickBeep();
        inputPassword += "_";
        drawKeyboardInputBox();
        return;
      }
      // 4. SPASI
      if (tx >= 140 && tx <= 211) {
        clickBeep();
        inputPassword += " ";
        drawKeyboardInputBox();
        return;
      }
      // 5. CONNECT / LANJUT
      if (tx >= 212 && tx <= 318) {
        clickBeep();
        if (typingSSID) {
          if (inputPassword.length() > 0) {
            selectedSSID = inputPassword;
            inputPassword = "";
            typingSSID = false;
            keyboardMode = 0;
            isShift = false;
            drawKeyboardScreen();
          } else {
            snprintf(toastMsg, sizeof(toastMsg), "SSID TIDAK BOLEH KOSONG!");
            toastUntil = millis() + 2000;
          }
        } else {
          keyboardMode = 0;
          isShift = false;
          connectWifiAndVerify();
        }
        return;
      }
    }
  }

  // ----------------------------------------
  // E. TOUCH: SCREEN_WIFI_CONNECTING (RETRY)
  // ----------------------------------------
  else if (currentScreen == SCREEN_WIFI_CONNECTING) {
    if (tx >= 16 && tx <= 156 && ty >= 190 && ty <= 232) {
      clickBeep();
      connectWifiAndVerify();
      return;
    }
    if (tx >= 164 && tx <= 304 && ty >= 190 && ty <= 232) {
      clickBeep();
      drawWifiScanScreen();
      return;
    }
  }
}

// ==========================================
// 16. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(CYD_LED_R, OUTPUT);
  pinMode(CYD_LED_G, OUTPUT);
  pinMode(CYD_LED_B, OUTPUT);
  pinMode(CYD_SPEAKER, OUTPUT);
  setRgbLed(false, false, true); // Biru

  // Membaca Preferensi Tema NVS
  prefs.begin("sys_cfg", false);
  isLightTheme = prefs.getBool("light_theme", true);
  prefs.end();
  applyTheme(isLightTheme);

  // Layar TFT CYD
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true); // KRUSIAL: Memperbaiki pembalikan warna panel CYD ILI9341!
  tft.fillScreen(C_BG);

  // Inisialisasi Sprite Buffer Nilai (Flicker-Free Direct Memory Buffer 142x44)
  sprValBerat.createSprite(142, 44);
  sprValTinggi.createSprite(142, 44);
  sprValOxy.createSprite(142, 44);
  sprValSuhu.createSprite(142, 44);

  // Touchscreen XPT2046
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  // Membaca WiFi tersimpan di NVS Flash
  prefs.begin("wifi_cfg", false);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID == "x") {
    savedSSID = "";
    prefs.begin("wifi_cfg", false);
    prefs.putString("ssid", "");
    prefs.end();
  }

  // Mode WiFi & ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, false);
  delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_FIXED_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (savedSSID.length() > 0 && savedSSID != "x") {
    Serial.printf("[WIFI] Menghubungkan ke tersimpan: %s (Channel Target %d)\n", savedSSID.c_str(), WIFI_FIXED_CHANNEL);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str(), WIFI_FIXED_CHANNEL);
  } else {
    Serial.println("[WIFI] Belum ada kredensial tersimpan (Standby di Channel 11).");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] Inisialisasi ESP-NOW Gagal!");
  } else {
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = WIFI_FIXED_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ERROR] Gagal Daftarkan Peer Broadcast!");
    } else {
      Serial.println("[OK] Peer Broadcast ESP-NOW Terdaftar di Channel 11.");
    }

    Serial.println("[OK] ESP-NOW Receiver & Transmitter Siap.");
  }

  drawDashboardScreen();
  clickBeep(10);

  // FreeRTOS Task Firebase RTDB di Core 0
  xTaskCreatePinnedToCore(
    firebaseIoTask,
    "FirebaseTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println("[CYD HUB] Smart Health Posyandu with WiFi Scanner & Test Bench Ready!");
}

// ==========================================
// 17. LOOP UTAMA
// ==========================================
void loop() {
  checkTouch();

  // Toast notification alert ticker (otomatis pulihkan header saat habis waktu tanpa kedip)
  drawToastNotification();

  if (millis() - lastLcdRefresh >= 200) {
    lastLcdRefresh = millis();
    if (currentScreen == SCREEN_DASHBOARD) {
      drawDashboardScreen();
    } else if (currentScreen == SCREEN_SLIDE2) {
      drawSlide2Screen();
    } else if (currentScreen == SCREEN_TEST_NODE) {
      drawTestNodeScreen();
    } else if (currentScreen == SCREEN_CALIBRATION) {
      drawCalibrationScreen();
    }
  }

  // Diagnostik Serial tiap 2.5 Detik
  static unsigned long lastDiagLog = 0;
  if (millis() - lastDiagLog >= 2500) {
    lastDiagLog = millis();
    unsigned long n1Age = lastSeenNode1 > 0 ? (millis() - lastSeenNode1) : 999999;
    Serial.printf("[CYD STATUS] WiFi: %s | Status: %d | Radio Ch: %d | Node1 LastSeen: %lu ms lalu (BPM: %d, SpO2: %d%%, Suhu: %.1f C, Jari: %s)\n",
      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "DISCONNECTED",
      WiFi.status(),
      WiFi.channel(),
      n1Age,
      liveBpm,
      liveSpO2,
      liveSuhu,
      liveFinger ? "TEMPEL" : "LEPAS"
    );
  }
}
