// SW_UCB_Agent.h — Sliding-Window Upper Confidence Bound, dengan mode UCB1
// pembanding (MODE_SW_UCB, params.h Bagian 7)
//
// Sesuai Panduan HIL Subbab 2.1: logika matematis dipisahkan dari operasi
// perangkat keras agar dapat diuji secara satuan.
//
// Keadaan agen disimpan pada struct AgentState yang dialokasikan di RTC RAM oleh
// main.cpp (Panduan HIL 2.2). Kelas ini hanya memegang penunjuk ke sana, karena
// hanya variabel global yang dapat ditempatkan di RTC RAM ESP32.
//
// Mode SW-UCB, Persamaan (2.5)-(2.6) naskah (Garivier & Moulines, 2011):
//   I(a,t) = mu(a,t,W) + sqrt( xi * ln( min(t, W) ) / N(a,t,W) )
// Window satu untuk semua arm dan bergeser bersama waktu: N(a,t,W) dan
// mu(a,t,W) dihitung dari keputusan nomor t-W+1 sampai t (W keputusan
// terakhir), bukan dari W reward terakhir tiap arm. Satu keputusan adalah satu
// siklus ACK (catatan Pembimbing I, 1 Nov 2026, butir A.1 dan B.1).
// Mode UCB1, Persamaan (2.4) naskah, dengan xi di tempat yang sama:
//   I(a,t) = Q(a) + sqrt( xi * ln(t) / N(a) )
//
// Aturan N = 0: arm yang tidak memiliki entri di dalam window langsung dipilih;
// bila lebih dari satu, arm berindeks terkecil. Garivier & Moulines tidak
// mengatur kasus ini, sehingga aturannya ditetapkan di Bab III.
//
// t adalah jumlah keputusan sejak reset agen terakhir. Siklus yang diputus
// timeout_window tetap menaikkan t (select_arm() sudah dipanggil) tetapi tidak
// menambah entri, sehingga waktu tetap bergeser walau tidak ada reward.
//
// AgentState memuat field yang berbeda menurut mode. Berkas biner yang
// dikompilasi untuk satu mode TIDAK KOMPATIBEL dibaca sebagai mode lain; ini
// sejalan dengan sifatnya sebagai pilihan kompilasi, bukan sakelar runtime.

#pragma once
#include <Arduino.h>
#include "../config/params.h"

struct AgentState {
    uint32_t t;                  // jumlah keputusan sejak reset agen
#if MODE_SW_UCB
    // Buffer melingkar satu entri per keputusan yang memperoleh reward. Karena
    // satu keputusan menghasilkan paling banyak satu entri, W_SIZE slot cukup
    // untuk memuat seluruh window. Entri lama yang tersisa di buffer disaring
    // menurut nomor keputusannya, bukan menurut posisinya.
    uint32_t t_entri[W_SIZE];    // nomor keputusan (1, 2, ...) pemilik entri
    float    reward[W_SIZE];
    uint8_t  arm[W_SIZE];
    uint16_t head;               // posisi tulis berikutnya
    uint16_t isi;                // jumlah slot terisi, paling banyak W_SIZE
#else
    uint32_t n_total[N_ARMS];        // jumlah percobaan tiap arm, seluruh riwayat
    float    mu_cumulative[N_ARMS];  // rata-rata reward tiap arm, seluruh riwayat
#endif
};

class SW_UCB_Agent {
public:
    // s menunjuk ke variabel RTC_DATA_ATTR di main.cpp. w adalah panjang window
    // dalam keputusan; selain untuk uji unit, selalu W_SIZE.
    SW_UCB_Agent(AgentState* s, uint16_t w = W_SIZE);

    // Pilih arm berskor tertinggi, lalu naikkan t. Skor dan N keempat arm saat
    // keputusan itu disimpan untuk dicetak (skor_terakhir, n_terakhir).
    uint8_t select_arm();

    // Catat reward keputusan yang sedang berjalan (nomor t) untuk arm tersebut.
    void update(uint8_t arm, float reward);

    // Skor I(a,t) pada keadaan sekarang. INF bila N(a) = 0.
    float score(uint8_t arm) const;

    // Kosongkan seluruh keadaan pembelajaran, termasuk t.
    void reset();

    // Rata-rata dan jumlah entri arm menurut mode aktif: di dalam window pada
    // SW-UCB, seluruh riwayat pada UCB1. Dipakai untuk pencatatan log.
    float mu(uint8_t arm) const;
    uint32_t count(uint8_t arm) const;
    uint32_t t() const { return _s->t; }

    // Skor dan N tiap arm pada keputusan terakhir, untuk replay berbasis indeks.
    float skor_terakhir(uint8_t arm) const { return _skor[arm]; }
    uint32_t n_terakhir(uint8_t arm) const { return _n[arm]; }

    // Arm dengan rata-rata reward tertinggi (untuk pencatatan, bukan keputusan).
    uint8_t arm_dominan() const;

private:
    // N dan jumlah reward tiap arm di dalam window (SW-UCB) atau seluruh
    // riwayat (UCB1), pada keadaan sekarang.
    void statistik(uint32_t n[N_ARMS], float jumlah[N_ARMS]) const;
    float skor_dari(uint32_t n, float jumlah) const;

    AgentState* _s;
    uint16_t _w;
    float _skor[N_ARMS];
    uint32_t _n[N_ARMS];
};
