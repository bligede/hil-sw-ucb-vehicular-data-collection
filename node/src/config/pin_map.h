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

// ---------- Tombol reset agen ----------
// Tombol BOOT bawaan papan (GPIO0). Ditahan saat node bangun dari tidur, tombol ini
// mengembalikan seluruh keadaan belajar ke nol (lihat reset_keadaan_agen di main.cpp).
#define PIN_RESET_AGEN 0

// Hambatan shunt efektif INA219 hasil kalibrasi Tahap 4.1 (23 Okt 2026).
// Beban resistor 390, 330, dan 390//330 ohm terukur 10,40, 12,37, dan 22,68 mA
// (tegangan Fluke 15B+ dibagi nilai resistor terukur). Dengan 0,100 ohm,
// INA219 membaca 1,101, 1,103, dan 1,108 kali nilai itu, yaitu galat gain
// konsisten sekitar 10 persen, sehingga shunt efektif modul ini 0,1104 ohm.
// Ganti nilainya bila modul INA219 diganti.
#define INA219_SHUNT_OHM  0.1104f
// Koreksi offset ADXL345, ditulis ke register OFSX/OFSY/OFSZ saat sensor_init().
// Satuan register 15,6 mg per LSB (rentang -128..127), nilai ditambahkan ke
// keluaran sensor. Modul GY-291 yang dipakai penelitian ini memiliki offset
// Z sekitar +1,6 g (uji balik 21 Okt 2026: Z 679,5 LSB menghadap atas dan
// 153,2 LSB menghadap bawah, sensitivitas 263 LSB per g), jauh di luar
// spesifikasi. Nilai nominal -104 hanya menghapus sekitar 91 persen offset
// (modul ini memakai gain register sekitar 14,2 mg/LSB, bukan 15,6), sehingga
// disetel -114 setelah pengukuran ulang. Sumbu X dan Y tidak dikoreksi karena
// uji balik belum memisahkan offset dari kemiringan papan. Ganti modul bila
// perlu, lalu hitung ulang nilai ini.
#define ADXL345_OFS_X    0
#define ADXL345_OFS_Y    0
#define ADXL345_OFS_Z  (-114)

#define ADXL345_ADDR  0x53 // SDO ke GND (Panduan HIL 1.3)
                           // CS ADXL345 WAJIB ke 3V3 agar modul memakai I2C,
                           // bukan SPI. Bila mengambang, alamat tidak muncul.
