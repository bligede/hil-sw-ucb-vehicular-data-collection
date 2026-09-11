// SW_UCB_Agent.h — Sliding-Window Upper Confidence Bound
//
// Sesuai Panduan HIL Subbab 2.1: logika matematis dipisahkan dari operasi
// perangkat keras agar dapat diuji secara satuan.
//
// Keadaan agen disimpan pada struct ArmState yang dialokasikan di RTC RAM oleh
// main.cpp (Panduan HIL 2.2). Kelas ini hanya memegang penunjuk ke sana, karena
// hanya variabel global yang dapat ditempatkan di RTC RAM ESP32.
//
// Persamaan (Garivier & Moulines, 2011; Naskah 3.2):
//   I(a,t) = mu(a,t,W) + xi * sqrt( ln( min(t, W) ) / N(a,t,W) )

#pragma once
#include <Arduino.h>
#include "../config/params.h"

struct ArmState {
    float reward_buf[W_SIZE]; // buffer melingkar reward
    uint16_t buf_head;        // posisi tulis berikutnya
    uint16_t n_in_window;     // jumlah entri sah di dalam window
    float mu;                 // rata-rata reward di dalam window
};

class SW_UCB_Agent {
public:
    // states dan t_global menunjuk ke variabel RTC_DATA_ATTR di main.cpp
    SW_UCB_Agent(ArmState* states, uint32_t* t_global);

    // Pilih arm berskor tertinggi. Menaikkan t_global.
    uint8_t select_arm();

    // Masukkan reward ke buffer arm, perbarui rata-rata.
    void update(uint8_t arm, float reward);

    // Skor I(a,t). Mengembalikan INF bila arm belum pernah dicoba.
    float score(uint8_t arm) const;

    // Kosongkan seluruh keadaan pembelajaran.
    void reset();

    float mu(uint8_t arm) const { return _s[arm].mu; }
    uint16_t count(uint8_t arm) const { return _s[arm].n_in_window; }
    uint32_t t() const { return *_t; }

    // Arm dengan rata-rata reward tertinggi (untuk pencatatan, bukan keputusan).
    uint8_t arm_dominan() const;

private:
    ArmState* _s;
    uint32_t* _t;
};
