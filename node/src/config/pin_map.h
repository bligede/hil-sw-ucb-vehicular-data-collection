// pin_map.h — pemetaan pin node sensor
//
// Sumber: Panduan HIL Subbab 1.3 (NSS, RESET, DIO0, SDA, SCL, alamat I2C).
// Tiga pin SPI di bawah tidak disebut Panduan HIL karena pustaka LoRa memakai
// nilai bawaan VSPI. Dituliskan eksplisit di sini agar perakitan tidak menebak
// (penyimpangan D12, Panduan Perakitan Bagian V).
//
// PERINGATAN D13: GPIO2 adalah pin strapping ESP32. Bila DIO0 bernilai tinggi
// saat reset, papan dapat gagal boot atau gagal diprogram. Gejalanya: papan
// hanya bisa diprogram setelah kabel DIO0 dilepas. Bila itu terjadi, pindahkan
// LORA_DIO0 ke GPIO27, catat perubahannya, dan laporkan ke Pembimbing I.

#pragma once

// ---------- LoRa SX1278 (SPI) ----------
#define LORA_NSS      5    // Panduan HIL 1.3
#define LORA_RESET   14    // Panduan HIL 1.3
#define LORA_DIO0     2    // Panduan HIL 1.3 — lihat PERINGATAN D13 di atas
#define LORA_SCK     18    // bawaan VSPI (D12)
#define LORA_MISO    19    // bawaan VSPI (D12)
#define LORA_MOSI    23    // bawaan VSPI (D12)

// ---------- I2C bersama: INA219 + ADXL345 ----------
#define I2C_SDA      21    // Panduan HIL 1.3
#define I2C_SCL      22    // Panduan HIL 1.3
#define I2C_FREQ_HZ  400000UL

#define INA219_ADDR   0x40 // A0 dan A1 ke GND (Panduan HIL 1.3)
#define ADXL345_ADDR  0x53 // SDO ke GND (Panduan HIL 1.3)
                           // CS ADXL345 WAJIB ke 3V3 agar modul memakai I2C,
                           // bukan SPI. Bila mengambang, alamat tidak muncul.
