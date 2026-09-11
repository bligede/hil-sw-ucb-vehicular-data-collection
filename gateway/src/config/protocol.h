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

// ACK gateway -> node (3 byte: penanda, penanda, jumlah paket diterima)
static const uint8_t ACK_B0 = 0xAC;
static const uint8_t ACK_B1 = 0x4B;

// Paket data node -> gateway dikenali dari panjangnya (PAYLOAD_LEN = 12 byte)
// dan byte pertama yang bernilai 1..4, sehingga tidak bertabrakan dengan
// penanda beacon (0xBE) maupun ACK (0xAC).

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
