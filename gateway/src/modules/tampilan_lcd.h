// tampilan_lcd.h - layar LCD 20x4 I2C (HD44780 + backpack PCF8574) pada gateway.
//
// Layar hanya untuk memastikan gateway hidup dan data masuk, dibaca saat
// kendaraan berhenti (SOP: pengendara tidak membaca layar selama bergerak).
// Bila layar tidak terpasang, modul menonaktifkan diri tanpa mengganggu
// firmware.
//
// Pemetaan backpack yang diasumsikan (umum pada modul "LCD I2C"):
//   P0=RS  P1=RW  P2=EN  P3=lampu latar  P4..P7=D4..D7
// Alamat dicari otomatis pada 0x20..0x27 dan 0x38..0x3F.
//
// Semua penulisan memakai Wire pada loop() (core 1) dan dibatasi satu baris
// per panggilan, sehingga beacon 1 detik dan penerimaan LoRa tidak terlambat
// lebih dari beberapa milidetik.

#pragma once
#include <Arduino.h>

struct TampilanGateway {
    const char* id_window;     // ID window yang berlaku
    uint16_t    n_diterima;    // paket diterima pada jendela berjalan
    uint8_t     n_per_jendela; // ACK_SETIAP_N
    uint32_t    pkt_total;     // total paket sejak gateway menyala
    bool        ada_paket;     // sudah pernah menerima paket
    int         rssi;          // paket terakhir
    float       snr;
    uint8_t     arm;
    uint32_t    umur_paket_s;  // detik sejak paket terakhir
    float       vnode;         // tegangan baterai node pada paket terakhir
    uint32_t    ack_total;     // ACK yang sudah dikirim
    uint8_t     ack_terakhir;  // isi ACK terakhir (paket yang diterima pada jendelanya)
    const char* ringkas_sheets; // mis. "W+Q0"; kosong bila build tanpa Sheets
};

// Mendeteksi dan menginisialisasi layar. Mengembalikan false bila tidak ada.
bool lcd_mulai();

// Menampilkan empat baris teks apa adanya (maksimal 20 karakter per baris).
// Dipakai untuk splash dan pesan FATAL; memblokir sampai selesai.
void lcd_pesan(const char* b1, const char* b2, const char* b3, const char* b4);

// Memperbarui tampilan operasional. Aman dipanggil tiap putaran loop():
// dibatasi waktu dan menulis paling banyak satu baris per panggilan.
void lcd_perbarui(const TampilanGateway& t);

// Menampilkan alamat I2C yang terdeteksi (0 bila tidak ada), untuk perintah 'S'.
uint8_t lcd_alamat();
