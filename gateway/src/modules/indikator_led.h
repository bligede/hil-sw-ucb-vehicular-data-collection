// Dua biled: aktivitas LoRa dan koneksi/unggah Sheets. Semua pola memakai
// millis(), tanpa delay tambahan pada jalur penerimaan paket.
#pragma once
#include <Arduino.h>
#include "unggah_sheets.h"

void indikator_mulai();
void indikator_radio_siap();
void indikator_radio_gagal();
void indikator_data();
void indikator_ack();
// Default 1,2 detik; durasi lain menahan tiap warna selama seperempat durasi.
// Durasi 0 mengakhiri uji; maksimal 60 detik. Radio/jaringan tetap berjalan.
void indikator_uji(uint32_t durasi_ms = 1200);
void indikator_perbarui(bool window_siap, const StatusUnggahSheets& sheets);
void indikator_status(char* keluar, size_t n);
