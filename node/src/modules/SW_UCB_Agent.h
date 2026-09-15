// SW_UCB_Agent.h — Sliding-Window Upper Confidence Bound, dengan mode UCB1
// pembanding (MODE_SW_UCB, params.h Bagian 7)
//
// Sesuai Panduan HIL Subbab 2.1: logika matematis dipisahkan dari operasi
// perangkat keras agar dapat diuji secara satuan.
//
// Keadaan agen disimpan pada struct ArmState yang dialokasikan di RTC RAM oleh
// main.cpp (Panduan HIL 2.2). Kelas ini hanya memegang penunjuk ke sana, karena
// hanya variabel global yang dapat ditempatkan di RTC RAM ESP32.
//
// Mode SW-UCB, Persamaan (2.5)-(2.6) naskah (Garivier & Moulines, 2011):
//   I(a,t) = mu(a,t,W) + xi * sqrt( ln( min(t, W) ) / N(a,t,W) )
// Mode UCB1, Persamaan (2.4) naskah:
//   I(a,t) = Q(a) + xi * sqrt( ln(t) / N(a) )
// dengan Q(a) diperbarui inkremental: Q_t+1(a) = Q_t(a) + (1/N)*(R - Q_t(a)).
//
// ArmState memuat field yang berbeda menurut mode, supaya tiap mode hanya
// mengalokasikan yang benar-benar dipakainya di RTC RAM: mode SW-UCB perlu
// buffer melingkar 50 slot per arm (~200 byte), mode UCB1 hanya perlu
// pencacah dan rata-rata kumulatif (~8 byte). Konsekuensinya, berkas biner
// yang dikompilasi untuk satu mode TIDAK KOMPATIBEL dibaca sebagai mode lain
// -- ini sejalan dengan sifatnya sebagai pilihan kompilasi (params.h Bagian 7),
// bukan sakelar yang berubah saat program berjalan.

#pragma once
#include <Arduino.h>
#include "../config/params.h"

struct ArmState {
#if MODE_SW_UCB
    float reward_buf[W_SIZE]; // buffer melingkar reward
    uint16_t buf_head;        // posisi tulis berikutnya
    uint16_t n_in_window;     // jumlah entri sah di dalam window
    float mu;                 // rata-rata reward di dalam window
#else
    uint32_t n_total;         // jumlah percobaan arm ini, seluruh riwayat
    float mu_cumulative;      // rata-rata reward arm ini, seluruh riwayat
#endif
};

class SW_UCB_Agent {
public:
    // states dan t_global menunjuk ke variabel RTC_DATA_ATTR di main.cpp
    SW_UCB_Agent(ArmState* states, uint32_t* t_global);

    // Pilih arm berskor tertinggi. Menaikkan t_global.
    uint8_t select_arm();

    // Masukkan reward ke arm: buffer jendela (SW-UCB) atau rata-rata
    // kumulatif (UCB1), menurut MODE_SW_UCB.
    void update(uint8_t arm, float reward);

    // Skor I(a,t). Mengembalikan INF bila arm belum pernah dicoba.
    float score(uint8_t arm) const;

    // Kosongkan seluruh keadaan pembelajaran.
    void reset();

    // Rata-rata dan jumlah percobaan arm menurut mode aktif -- jendela pada
    // SW-UCB, seluruh riwayat pada UCB1. Dipakai untuk pencatatan log.
    float mu(uint8_t arm) const;
    uint32_t count(uint8_t arm) const;
    uint32_t t() const { return *_t; }

    // Arm dengan rata-rata reward tertinggi (untuk pencatatan, bukan keputusan).
    uint8_t arm_dominan() const;

private:
    ArmState* _s;
    uint32_t* _t;
};
