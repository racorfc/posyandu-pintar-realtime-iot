# File Binary Siap Flash - CYD Posyandu Hub (ESP32-2432S028R)

Folder ini berisi file binary terkompilasi resmi (*precompiled binaries*) yang telah di-flash langsung ke perangkat ESP32 CYD 2.8":

## 📦 Berkas Binary
1. **`firmware_cyd_posyandu_hub.bin`** (Offset: `0x10000`)
   - Berisi program utama CYD Hub dengan tema Dark & Light, Menu Slide 2, peredam kedip sprite TFT, tombol simpan ramping, dan sinkronisasi Firebase Realtime Database.
2. **`bootloader.bin`** (Offset: `0x1000`)
3. **`partitions.bin`** (Offset: `0x8000`)

---

## ⚡ Cara Flashing Cepat Menggunakan `esptool.py`

Jika ingin mem-flash langsung tanpa perlu membuka Arduino IDE / PlatformIO:

```bash
# Pastikan port CYD terhubung (misal /dev/ttyUSB0 di Linux atau COM3 di Windows)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware_cyd_posyandu_hub.bin
```

Atau bisa menggunakan **ESP Flash Download Tool** (GUI Windows) maupun **Web ESP Flash Tool** di browser Chrome.

---

## 📝 Kode Sumber Asli
Source code lengkap program ini tersedia di:
- **`cyd_posyandu_hub/cyd_posyandu_hub.ino`** (Untuk Arduino IDE)
- **`cyd_posyandu_hub/src/main.cpp`** (Untuk PlatformIO)
