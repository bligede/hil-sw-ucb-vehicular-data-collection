// fsm.h — Hybrid Finite State Machine sebagai supervisory control
//
// Empat state sesuai Tabel 4.1 naskah. BATTERY_GUARD berprioritas mutlak dan
// diperiksa pada setiap wake-up sebelum state lain dievaluasi.
//
// PENYIMPANGAN D9 terhadap Panduan HIL Subbab 2.5, menutup Catatan Perbaikan
// Subbab A.2: state CONTACT_WINDOW pada Panduan hanya punya satu jalan keluar,
// yaitu penurunan RSSI. Kondisi timeout tercantum di Tabel 4.1 naskah tetapi
// nilainya tidak pernah didefinisikan. Tanpa batas waktu, node akan terus
// memancar bila kendaraan berhenti di dalam zona jangkauan atau bila RSSI
// tertahan di atas ambang akibat pantulan sinyal.
//
// Jeda cooldown sesudah keluar lewat batas waktu juga tambahan: tanpa itu, node
// akan segera mendeteksi RSSI tinggi yang sama pada pemindaian berikutnya dan
// masuk kembali ke state yang baru saja ditinggalkannya.

#pragma once
#include <Arduino.h>

enum FSMState : uint8_t {
    STATE_IDLE           = 0,
    STATE_CONTACT_WINDOW = 1,
    STATE_REWARD_EVAL    = 2,
    STATE_BATTERY_GUARD  = 3
};

void fsm_init();
void fsm_tick();

const char* fsm_nama_state(FSMState s);
FSMState    fsm_state_sekarang();
