// energy_meter.h — pengukuran energi transmisi dengan INA219
//
// PENYIMPANGAN D1 terhadap Panduan HIL Subbab 2.3.
//
// Panduan HIL memanggil pembacaan INA219 SESUDAH LoRa.endPacket() selesai.
// Karena endPacket() memblokir sampai transmisi tuntas, radio sudah kembali ke
// mode siaga ketika arus dibaca. Nilai yang terekam adalah arus siaga, lalu
// dikalikan durasi transmisi. Akibatnya E_actual bukan hanya terlalu kecil,
// tetapi hampir seragam untuk keempat arm, sebab arus siaga tidak bergantung
// pada TP maupun CR. Komponen energi pada fungsi reward berubah menjadi derau
// dan Hipotesis Minor 2 tidak dapat diuji.
//
// Modul ini menggantinya dengan integrasi daya sepanjang rentang transmisi:
//   E = sum( V_bus * I_i * dt_i )
// Mode ADC diubah ke konversi tunggal 12-bit (532 us). Mode SAMPLE_MODE_128
// pada Panduan memerlukan 68,1 ms per pembacaan, lebih panjang daripada waktu
// udara sebagian arm (82 ms untuk Arm 1), sehingga satu pasang pembacaan tidak
// selesai di dalam rentang transmisi.
//
// Tegangan bus dibaca sekali sebelum transmisi karena tegangan baterai praktis
// tetap sepanjang rentang 100 ms, sehingga tiap cuplikan hanya perlu satu
// transaksi I2C untuk arus.
//
// Gerbang uji (SOP Tahap 4.1): rasio energi Arm 4 terhadap Arm 1 harus minimal
// 2,0. Rasio mendekati 1,0 berarti pencuplikan masih di luar rentang transmisi.

#pragma once
#include <Arduino.h>

bool  energy_init();          // false bila INA219 tidak menjawab di I2C
void  energy_begin();         // panggil TEPAT sebelum LoRa.beginPacket()
void  energy_sample();        // panggil berulang selama radio memancar
float energy_end_mJ();        // panggil sesudah transmisi tuntas

float energy_vbatt_V();       // tegangan bus terakhir (untuk BATTERY_GUARD)
float energy_baca_vbatt_V();  // pembacaan tegangan baru, di luar transmisi
uint16_t energy_jumlah_sampel(); // cuplikan pada transmisi terakhir; nol
                                 // menandakan pencuplikan tidak berjalan
