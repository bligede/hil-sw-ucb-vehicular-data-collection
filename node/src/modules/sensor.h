// sensor.h — ADXL345 sebagai sensor payload
//
// Data getaran permukaan jalan menjadi muatan yang dikirim node ke gateway.
// Isinya tidak memengaruhi keputusan algoritma; yang penting muatannya mewakili
// keperluan aplikasi yang nyata, bukan data buatan (Naskah 4.6).
//
// Catatan pemasangan (Panduan Perakitan Rakitan F langkah 3): modul harus
// terpasang kaku ke dasar enclosure, bukan digantung pada kabel, agar
// pembacaannya dapat diulang.
//
// Catatan pustaka: nama fungsi pada contoh Panduan HIL perlu disesuaikan dengan
// versi pustaka SparkFun ADXL345 yang terpasang. Pembungkus di bawah menyatukan
// perbedaan itu di satu tempat sehingga sisa kode tidak perlu ikut berubah.

#pragma once
#include <Arduino.h>

bool sensor_init();

// Baca percepatan tiga sumbu dalam mg. Mengembalikan false bila pembacaan gagal;
// pada kondisi itu nilai keluaran diisi nol agar transmisi tetap berjalan.
bool sensor_baca_mg(int16_t& ax, int16_t& ay, int16_t& az);
