// payload.h — susunan muatan paket 12 byte
//
// PENYIMPANGAN D6 terhadap Panduan HIL Subbab 2.6.
//
// Panduan HIL mengirim arm, nomor urut, dan data akselerometer saja. INA219
// mengukur di sisi node, sedangkan pencatatan berlangsung di sisi gateway yang
// tersambung laptop. Tanpa medan energi di dalam paket, E_actual hanya dipakai
// internal oleh agen lalu hilang, dan analisis efisiensi energi pada Subbab
// 4.8.1 naskah tidak punya datanya.
//
// Energi yang dikirim adalah energi paket SEBELUMNYA, karena energi paket
// berjalan baru selesai dihitung sesudah transmisinya tuntas. Pergeseran satu
// paket ini dikoreksi saat pengolahan data (tools/ringkas_log.py), bukan saat
// perekaman.
//
//  Byte | Isi                              | Format
//  -----+----------------------------------+---------------------------------
//   0   | Nomor arm terpilih               | uint8, 1..4
//   1-2 | Nomor urut paket                 | uint16 big-endian
//   3-4 | Energi transmisi paket sebelumnya| uint16 BE, satuan 0,1 mJ
//   5   | Tegangan baterai                 | uint8, (V - 2,50) * 100
//   6-11| Percepatan x, y, z               | tiga int16 BE, satuan mg
//
// Ukuran 12 byte inilah yang dipakai pada seluruh perhitungan waktu udara di
// Dokumen Kerja Revisi (82,4 ms Arm 1 sampai 107,0 ms Arm 4).

#pragma once
#include <Arduino.h>

struct Payload {
    uint8_t  arm;        // 1..4 (bukan 0..3) agar sama dengan penomoran naskah
    uint16_t seq;
    float    e_prev_mJ;
    float    vbatt_V;
    int16_t  ax, ay, az; // mg
};

// Susun ke buffer 12 byte. Mengembalikan jumlah byte terisi.
uint8_t payload_pack(const Payload& p, uint8_t* buf);

// Bongkar buffer 12 byte. false bila panjangnya tidak sesuai.
bool payload_unpack(const uint8_t* buf, uint8_t len, Payload& p);
