// radio.h — pembungkus modul LoRa: inisialisasi, pemindaian beacon, transmisi
//
// PENYIMPANGAN D2 terhadap Panduan HIL Subbab 2.4 (durasi pemindaian).
//
// Panduan memakai pemindaian 500 ms sementara gateway memancarkan beacon tiap
// 5 detik, sehingga satu pemindaian hanya punya peluang 10 persen beririsan
// dengan pancaran beacon. Digabung dengan siklus tidur 60 detik yang jauh lebih
// panjang daripada jendela kontak, peluang deteksi pada skenario S7 hanya
// 2 persen (Dokumen Kerja Revisi Subbab B.3).
//
// Dua syarat penetapan (Dokumen Kerja Revisi Subbab B.4):
//   Syarat 1: T_SCAN_MS >= selang beacon gateway
//   Syarat 2: T_SLEEP + 2*T_SCAN <= jendela kontak terpendek
//
// Transmisi memakai mode asinkron agar arus dapat dicuplik selama radio
// memancar. Ini prasyarat penyimpangan D1 di energy_meter.

#pragma once
#include <Arduino.h>
#include "payload.h"

bool radio_init();

// Terapkan TP dan CR sesuai indeks arm (0..N_ARMS-1).
void radio_set_arm(uint8_t arm_idx);

// Pindai beacon selama T_SCAN_MS. Mengembalikan RSSI terkuat yang tertangkap,
// atau -200 bila tidak ada beacon sama sekali.
int radio_scan_beacon();

bool radio_kendaraan_terdeteksi();

// Kirim satu paket sambil mencuplik arus. Mengembalikan energi terukur (mJ).
// Fungsi ini yang menyatukan D1 (pencuplikan saat TX) dan D7 (jeda antar-paket).
float radio_kirim_paket(const Payload& p);

// Tunggu ACK dari gateway sampai ACK_TIMEOUT_MS.
// Mengembalikan jumlah paket yang diterima gateway, atau -1 bila kehabisan waktu.
int radio_tunggu_ack();

int  radio_rssi_terakhir();
void radio_tidur();
