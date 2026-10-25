// pin_map.h - pemetaan pin mobile gateway
//
// LoRa SX1278 (RA-02) memakai SPI VSPI. Tiga pin SPI dituliskan eksplisit agar
// perakitan tidak menebak (penyimpangan D12, Panduan Perakitan Bagian V).
//
// PERINGATAN D13: GPIO2 adalah pin strapping ESP32. Bila DIO0 bernilai tinggi
// saat reset, papan dapat gagal boot atau gagal diprogram. Gejalanya: papan
// hanya bisa diprogram setelah kabel DIO0 dilepas atau tombol BOOT ditahan.
// Bila itu terjadi, pindahkan LORA_DIO0 ke GPIO27, catat perubahannya, dan
// laporkan ke Pembimbing I. Pin ini harus sama di node dan gateway.

#pragma once

// ---------- LoRa SX1278 (SPI) ----------
#define LORA_NSS      5
#define LORA_RESET   14
#define LORA_DIO0     2    // lihat PERINGATAN D13 di atas
#define LORA_SCK     18
#define LORA_MISO    19
#define LORA_MOSI    23

// ---------- I2C: LCD 20x4 dengan backpack PCF8574 ----------
// Gateway tidak memakai INA219 maupun ADXL345. Layar bersifat opsional: bila
// tidak terpasang, modul tampilan menonaktifkan diri saat boot.
#define I2C_SDA      21
#define I2C_SCL      22

// ---------- Dua biled indikator: LoRa dan Google Sheets ----------
// Wiring pengguna: LED1 merah=26/hijau=25, LED2 merah=32/hijau=27.
// Kedua biled 3 kaki, common GND, dengan resistor (dikonfirmasi pengguna).
// LED1 untuk LoRa; LED2 untuk Sheets. -1/-1 menonaktifkan satu biled.
// Jangan gunakan pin SPI/I2C/serial.
//
// Biled 3 kaki: MERAH/HIJAU = masing-masing anoda/katoda warna.
//   kaki bersama GND: AKTIF_LOW=0; kaki bersama 3V3: AKTIF_LOW=1.
// Biled 2 kaki (antiparalel): DUA_KAKI=1; MERAH adalah kaki yang HIGH
//   saat warna merah menyala, HIJAU kaki yang LOW. AKTIF_LOW diabaikan.
// Wajib memakai resistor pembatas arus / modul dengan resistor bawaan.
// Semua opsi dapat dioverride dengan -D... pada build_flags.
#ifndef LED_LORA_MERAH
#define LED_LORA_MERAH       26
#endif
#ifndef LED_LORA_HIJAU
#define LED_LORA_HIJAU       25
#endif
#ifndef LED_LORA_AKTIF_LOW
#define LED_LORA_AKTIF_LOW    0
#endif
#ifndef LED_LORA_DUA_KAKI
#define LED_LORA_DUA_KAKI     0
#endif

#ifndef LED_SHEETS_MERAH
#define LED_SHEETS_MERAH     32
#endif
#ifndef LED_SHEETS_HIJAU
#define LED_SHEETS_HIJAU     27
#endif
#ifndef LED_SHEETS_AKTIF_LOW
#define LED_SHEETS_AKTIF_LOW  0
#endif
#ifndef LED_SHEETS_DUA_KAKI
#define LED_SHEETS_DUA_KAKI   0
#endif
