// unggah_sheets.h - pengiriman baris DATA/ACK gateway ke Google Sheets lewat
// web app Google Apps Script, memakai WiFi hotspot ponsel.
//
// Aktif hanya bila dikompilasi dengan -DUNGGAH_SHEETS (env esp32dev_sheets).
// Tanpa flag itu semua fungsi di bawah kosong, sehingga build bawaan gateway
// tidak berubah sedikit pun.
//
// Seluruh pekerjaan jaringan berjalan di satu task FreeRTOS pada core 0.
// loop() di core 1 hanya memasukkan baris ke antrean dan tidak pernah menunggu
// jaringan, sehingga beacon 1 detik dan penerimaan paket LoRa tidak terganggu.

#pragma once
#include <Arduino.h>

// Keadaan numerik untuk indikator; tidak perlu mem-parsing teks LCD/serial.
// pending adalah baris yang sudah keluar dari antrean tetapi masih menunggu
// konfirmasi POST. Jadi antre=0 belum tentu berarti seluruh baris tersimpan.
struct StatusUnggahSheets {
    bool aktif = false;
    bool siap = false;
    bool wifi = false;
    uint16_t antre = 0;
    uint16_t pending = 0;
    uint32_t terkirim = 0;
    uint32_t gagal = 0;
    uint32_t buang = 0;
    int http = 0;
};

#ifdef UNGGAH_SHEETS

// Membuat antrean dan task pengunggah. Panggil sekali di setup().
void sheets_mulai();

// Memasukkan satu baris ke antrean. Tidak memblokir. Bila antrean penuh, baris
// dibuang dan dihitung pada kolom "buang" di Sheets (serial tetap mencatatnya).
void sheets_antre_data(uint32_t ts_ms, const char* id_window, uint16_t seq,
                       uint8_t arm, float e_prev_mJ, float vbatt, int rssi,
                       float snr, int16_t ax, int16_t ay, int16_t az);
void sheets_antre_ack(uint32_t ts_ms, const char* id_window, uint8_t n_diterima);

// true bila sel B1 lembar KONTROL berubah sejak pembacaan sebelumnya. ID baru
// disalin ke 'keluar'. Penanda baru dihapus hanya saat fungsi ini dipanggil,
// jadi pemanggil boleh menundanya sampai tidak ada jendela ACK yang berjalan.
bool sheets_window_baru(char* keluar, size_t n);

// Ringkasan keadaan untuk perintah serial 'S'.
void sheets_status(char* keluar, size_t n);

// Ringkasan pendek untuk LCD, mis. "W+Q0" (WiFi tersambung, 0 baris antre) atau "W-Q12".
void sheets_ringkas(char* keluar, size_t n);

StatusUnggahSheets sheets_keadaan();

#else

inline void sheets_mulai() {}
inline void sheets_antre_data(uint32_t, const char*, uint16_t, uint8_t, float,
                              float, int, float, int16_t, int16_t, int16_t) {}
inline void sheets_antre_ack(uint32_t, const char*, uint8_t) {}
inline bool sheets_window_baru(char*, size_t) { return false; }
inline void sheets_status(char* keluar, size_t n) {
    if (n) snprintf(keluar, n, "unggah Sheets nonaktif (build tanpa -DUNGGAH_SHEETS)");
}
inline void sheets_ringkas(char* keluar, size_t n) { if (n) keluar[0] = '\0'; }
inline StatusUnggahSheets sheets_keadaan() { return StatusUnggahSheets{}; }

#endif
