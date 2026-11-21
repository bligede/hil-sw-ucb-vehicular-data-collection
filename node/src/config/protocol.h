// protocol.h — penanda paket, dipakai bersama oleh node dan gateway.
//
// Berkas ini WAJIB identik pada kedua project. Bila salah satu diubah tanpa
// yang lain, node dan gateway tetap terkompilasi tetapi tidak akan pernah
// saling mengenali paket, dan gejalanya menyerupai masalah jangkauan radio.

#pragma once
#include <Arduino.h>

// Beacon gateway -> node (2 byte)
static const uint8_t BEACON_B0 = 0xBE;
static const uint8_t BEACON_B1 = 0xAC;

// ACK gateway -> node (5 byte, sejak gateway-1.5 dan node-1.3):
//   byte 0-1  penanda ACK_B0, ACK_B1
//   byte 2    n, jumlah paket sah yang diterima gateway pada siklus itu
//   byte 3-4  penanda siklus (uint16 big-endian): nomor urut paket berindeks 0
//             dari siklus yang di-ACK, seq_awal = seq - indeks
// Node hanya menerima ACK sepanjang ACK_LEN yang penanda siklusnya sama dengan
// siklusnya sendiri (catatan Pembimbing I 1 Nov 2026, B.3). ACK 3 byte milik
// gateway lama tidak diterima node-1.3.
static const uint8_t ACK_B0 = 0xAC;
static const uint8_t ACK_B1 = 0x4B;
static const uint8_t ACK_LEN = 5;

// Paket data node -> gateway dikenali dari panjangnya (PAYLOAD_LEN = 12 byte).
// Byte pertama memuat dua medan (sejak node-1.3; panjang muatan tetap 12 byte
// supaya waktu udara dan energi per transmisi tidak berubah):
//   4 bit bawah  arm, 1..4
//   4 bit atas   indeks paket di dalam siklus ACK, 0..9
//   b0 = (indeks << 4) | arm, sehingga arm = b0 & 0x0F dan indeks = b0 >> 4.
// Nilai b0 berada di 0x01..0x94 (paling besar indeks 9, arm 4), tidak pernah
// sama dengan penanda beacon (0xBE) maupun ACK (0xAC); panjang beacon (2) dan
// ACK (5) juga berbeda dari 12.
static const uint8_t PAKET_ARM_MASK    = 0x0F;
static const uint8_t PAKET_INDEKS_GESER = 4;

// ---------------------------------------------------------------------------
// Selang pancaran beacon (D4).
//
// Nilai ini tinggal di berkas protokol, bukan di firmware gateway, karena
// merupakan parameter yang HARUS disepakati kedua sisi. Durasi pemindaian node
// (T_SCAN_MS) ditetapkan dari nilai ini lewat Syarat 1: durasi pindai harus
// melampaui selang beacon agar tiap pemindaian dijamin memuat sekurang-kurangnya
// satu pancaran.
//
// Karena keduanya sepasang, PAKAI_NILAI_PANDUAN wajib disetel pada KEDUA project
// secara bersamaan. Menyetel di satu sisi saja menghasilkan pasangan yang tidak
// mewakili rancangan mana pun: pindai 500 ms melawan beacon 1.000 ms mengunci
// peluang deteksi di 50 persen, bukan 10 persen seperti rancangan Panduan dan
// bukan 100 persen seperti rancangan revisi.
#ifdef PAKAI_NILAI_PANDUAN
  #define BEACON_SELANG_MS 5000UL   // nilai Panduan HIL 3.1
#else
  #define BEACON_SELANG_MS 1000UL   // D4
#endif
