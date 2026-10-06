# 🏥 SMART HEALTH POSYANDU IoT (V4 FIREBASE ECOSYSTEM)

Sistem pemantauan kesehatan balita, rekam medis **KMS Digital**, dan **Deteksi Dini Stunting** berbasis **Multi-Node ESP32-C3**, **ESP32 CYD (Cheap Yellow Display) 2.8" Touchscreen Hub**, dan **Google Firebase Realtime Database (RTDB)**.

---

## 🌟 1. Arsitektur Sistem

```
 +------------------------+     +------------------------+     +------------------------+
 |   NODE 1 (ESP32-C3)    |     |   NODE 2 (ESP32-C3)    |     |   NODE 3 (ESP32-C3)    |
 | - MAX30102 (BPM & SpO2)|     | - HX711 Load Cell      |     | - HC-SR04 Ultrasonik   |
 | - DS18B20 (Suhu Tubuh) |     | - Smart Tare Button    |     | - Filter Tiang Balita  |
 +------------------------+     +------------------------+     +------------------------+
              │                              │                              │
              │ ESP-NOW Broadcast            │ ESP-NOW Broadcast            │ ESP-NOW Broadcast
              │ (<5ms, Tanpa Router)         │ (Instan Tare 2-Arah)         │ (Tinggi Akurat)
              └──────────────────────────────┼──────────────────────────────┘
                                             ▼
                        +-----------------------------------------+
                        |  GATEWAY HUB: ESP32 CYD 2.8" TOUCH TFT  |
                        |                                         |
                        |  [CORE 1 - Foreground]:                 |
                        |  - Display 320x240 Anti-Flicker         |
                        |  - Tombol Sentuh: [TARE] & [SIMPAN]     |
                        |  - Watchdog Status 3 Node               |
                        |                                         |
                        |  [CORE 0 - FreeRTOS Background]:        |
                        |  - Auto-reconnect WiFi Watchdog         |
                        |  - HTTPS REST API Client ke Firebase:   |
                        |    * PATCH /posyandu/monitoring.json    |
                        |    * POST  /posyandu/pemeriksaan.json   |
                        |    * GET   /posyandu/control.json       |
                        +-----------------------------------------+
                                             │
                                             │ HTTPS Cloud Sync
                                             ▼
                        +-----------------------------------------+
                        |      FIREBASE REALTIME DATABASE         |
                        +-----------------------------------------+
                                             ▲
                                             │ WebSocket / EventStream
                        +-----------------------------------------+
                        |     WEB DASHBOARD KMS & STUNTING        |
                        | - Live Vital Gauges                     |
                        | - Formulir Identitas Balita             |
                        | - Evaluasi Gizi & Stunting (Kemenkes)   |
                        | - Rekap Riwayat & Ekspor CSV            |
                        +-----------------------------------------+
```

---

## 📂 2. Struktur Direktori Proyek

```
V4_CYD_FIREBASE/
├── cyd_posyandu_hub/            <- Firmware ESP32 CYD 2.8" (Gateway Hub & Layar Sentuh)
│   ├── cyd_posyandu_hub.ino     <- Kode utama (Dual Core FreeRTOS + TFT_eSPI)
│   ├── User_Setup.h             <- Konfigurasi Driver ILI9341 & XPT2046
│   └── platformio.ini           <- Konfigurasi kompilasi PlatformIO
├── node1_oxymeter/              <- Firmware ESP32-C3 Oximeter & Suhu Tubuh
│   └── node1_oxymeter.ino
├── node2_berat/                 <- Firmware ESP32-C3 Timbangan HX711 Load Cell
│   └── node2_berat.ino
├── node3_tinggi/                <- Firmware ESP32-C3 Pengukur Tinggi HC-SR04
│   └── node3_tinggi.ino
├── web_dashboard/               <- Web Dashboard Realtime (KMS Digital & Stunting)
│   └── index.html               <- Single-page app (Tailwind CSS + Firebase JS SDK)
└── README.md                    <- Panduan lengkap ini
```

---

## 🔌 3. Rangkaian & Pinout Hardware

### A. Node 1: Oximeter & Suhu Tubuh (ESP32-C3)
| Komponen | Pin ESP32-C3 | Keterangan |
| :--- | :--- | :--- |
| **MAX30102 SDA** | **GPIO 1** | I2C Data Sensor Oksigen & Jantung |
| **MAX30102 SCL** | **GPIO 2** | I2C Clock Sensor |
| **DS18B20 Data** | **GPIO 4** | Pasang pull-up resistor 4.7kΩ ke 3.3V |
| **VCC & GND** | **3.3V & GND** | Hubungkan ke jalur daya bersama |

### B. Node 2: Timbangan Balita (ESP32-C3)
| Komponen | Pin ESP32-C3 | Keterangan |
| :--- | :--- | :--- |
| **HX711 DT** | **GPIO 4** | Jalur Data Load Cell |
| **HX711 SCK** | **GPIO 5** | Jalur Clock Load Cell |
| **Tombol BOOT** | **GPIO 9** | *Klik Cepat*: Tare (0.00 kg). *Tahan 4 Detik*: Kalibrasi 900g |
| **VCC & GND** | **5V / 3.3V & GND** | Daya HX711 |

### C. Node 3: Pengukur Tinggi Badan (ESP32-C3)
| Komponen | Pin ESP32-C3 | Keterangan |
| :--- | :--- | :--- |
| **HC-SR04 TRIG** | **GPIO 1** | Trigger pulsa ultrasonik |
| **HC-SR04 ECHO** | **GPIO 10** | Echo input (Voltage divider R1=1k, R2=2k jika HC-SR04 5V) |
| **VCC & GND** | **5V & GND** | Daya Sensor HC-SR04 |

### E. Fitur Khusus: Lab Simulator Node di CYD (Tanpa Sensor Fisik)
Bagi Anda yang modul sensor fisiknya (Node 1, 2, 3) belum dirakit:
1. Pada layar utama CYD, sentuh tombol kanan bawah **`[ TAB LAB TEST NODE > ]`**.
2. Anda akan masuk ke halaman **Simulator Interaktif**:
   * **Node 2 (Timbangan)**: Sentuh `[-1.0]`, `[-0.1]`, `[+0.1]`, `[+1.0]` untuk menaik-turunkan angka berat badan balita.
   * **Node 3 (Tinggi)**: Sentuh `[-5.0]`, `[-1.0]`, `[+1.0]`, `[+5.0]` untuk mengatur tinggi badan.
   * **Node 1 (Oximeter & Suhu)**: Sentuh `[JARI: ON/OFF]`, tombol suhu (Normal 36.6°C / Demam 38.2°C), dan tombol SpO2/BPM.
   * **Tombol Aksi**: Sentuh **`[ PUSH TELEMETRI KE CLOUD RTDB ]`** atau **`[ SIMPAN BALITA KE PEMERIKSAAN ]`**.
3. Buka Web Dashboard di laptop/HP, Anda akan melihat angka di web bergerak seketika secara real-time mengikuti sentuhan tangan Anda di CYD!
4. Sentuh tombol **`< DASH`** di pojok kiri atas CYD untuk kembali ke layar utama.

---

## 🚀 5. Cara Kompilasi & Flash Firmware

1. Buka [Firebase Console](https://console.firebase.google.com/) dan buat proyek baru (misal: `smart-health-posyandu`).
2. Masuk ke menu **Build > Realtime Database**, lalu klik **Create Database**.
3. Pilih lokasi database: **Singapore (`asia-southeast1`)** atau **United States**.
4. Pilih **Start in test mode** (atau ubah aturan *Rules* menjadi seperti di bawah ini agar ESP32 & Web bisa membaca/menulis data):
   ```json
   {
     "rules": {
       ".read": true,
       ".write": true
     }
   }
   ```
5. URL database Firebase Anda yang sudah terpasang:
   `https://planning-with-ai-f6a7c-default-rtdb.asia-southeast1.firebasedatabase.app`
6. URL tersebut sudah langsung ditanamkan pada:
   * **`cyd_posyandu_hub/cyd_posyandu_hub.ino`**
   * **`web_dashboard/index.html`**

---

## 🚀 5. Cara Kompilasi & Flash Firmware

### Menggunakan Arduino IDE:
1. Pastikan Anda sudah menginstall board **esp32** by Espressif Systems di Boards Manager.
2. Install library yang dibutuhkan via **Library Manager**:
   - `SparkFun MAX3010x Pulse and Proximity Sensor Library`
   - `OneWire` by Paul Stoffregen
   - `DallasTemperature` by Miles Burton
   - `HX711` by Bogdan Necula
   - `TFT_eSPI` by Bodmer (Salin isi `cyd_posyandu_hub/User_Setup.h` ke folder library Arduino `libraries/TFT_eSPI/User_Setup.h`)
   - `XPT2046_Touchscreen` by Paul Stoffregen
   - `ArduinoJson` by Benoit Blanchon (Versi 6.x)
3. Buka masing-masing file `.ino` dan upload ke board yang bersangkutan:
   * `node1_oxymeter.ino` -> Board: **ESP32C3 Dev Module**
   * `node2_berat.ino` -> Board: **ESP32C3 Dev Module**
   * `node3_tinggi.ino` -> Board: **ESP32C3 Dev Module**
   * `cyd_posyandu_hub.ino` -> Board: **ESP32 Dev Module** (atau NodeMCU-32S)

### Menggunakan PlatformIO (Khusus CYD Hub):
1. Buka folder `cyd_posyandu_hub` di VS Code dengan ekstensi PlatformIO.
2. Hubungkan ESP32 CYD ke port USB.
3. Jalankan perintah:
   ```bash
   pio run -t upload
   ```

---

## 💻 6. Menggunakan Web Dashboard Posyandu

1. Cukup buka file `web_dashboard/index.html` menggunakan browser Google Chrome, Edge, atau Firefox di Laptop maupun Tablet/HP Kader.
2. Masukkan URL Firebase Anda pada tombol **⚙️ Setelan Firebase**.
3. **Fitur Unggulan**:
   * **Live Vitals**: Begitu balita berdiri di tiang atau naik timbangan, angka di web langsung bergerak secara instan (*real-time*).
   * **Formulir Balita & Evaluasi Otomatis**: Masukkan Nama, Usia (Bulan), dan Jenis Kelamin. Sistem langsung menghitung:
     * **IMT (Indeks Massa Tubuh)**.
     * **Status Stunting**: Normal / Pendek (*Stunted*) / Sangat Pendek (*Severely Stunted*).
     * **Status Gizi**: Gizi Buruk / Gizi Kurang / Gizi Baik / Berisiko Gizi Lebih.
     * **Saran Tindakan**: Rekomendasi intervensi PMT dan rujukan Puskesmas.
   * **Tombol Simpan**: Menyimpan data balita ke cloud dengan satu klik.
   * **Ekspor CSV**: Menghasilkan file spreadsheet rekap posyandu siap cetak atau dikirim ke Puskesmas.
   * **Remote Tare**: Menol-kan timbangan fisik dari jarak jauh melalui browser.
