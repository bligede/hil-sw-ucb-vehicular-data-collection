#include "fsm.h"
#include <esp_sleep.h>
#include "../config/params.h"
#include "SW_UCB_Agent.h"
#include "energy_meter.h"
#include "radio.h"
#include "sensor.h"

// ---- keadaan yang bertahan lintas deep sleep, didefinisikan di main.cpp ----
// Atribut RTC_DATA_ATTR hanya dipasang pada definisinya, bukan pada deklarasi
// extern ini, karena atribut penempatan section tidak berlaku untuk deklarasi.
extern ArmState rtc_arms[N_ARMS];
extern uint32_t rtc_t_global;
extern uint16_t rtc_pkt_seq;
extern uint16_t rtc_ack_counter;
extern double   rtc_energi_akum_mJ;
extern uint8_t  rtc_arm_sekarang;
extern float    rtc_vbatt;
extern float    rtc_e_prev_mJ;
extern uint32_t rtc_cooldown_sampai_s;
extern uint32_t rtc_uptime_s;
extern bool     rtc_dalam_guard;

extern SW_UCB_Agent agent;

static FSMState s_state = STATE_IDLE;
static uint32_t s_t_masuk_window = 0;

const char* fsm_nama_state(FSMState s) {
    switch (s) {
        case STATE_IDLE:           return "IDLE";
        case STATE_CONTACT_WINDOW: return "CONTACT_WINDOW";
        case STATE_REWARD_EVAL:    return "REWARD_EVAL";
        case STATE_BATTERY_GUARD:  return "BATTERY_GUARD";
    }
    return "?";
}

FSMState fsm_state_sekarang() { return s_state; }

void fsm_init() { s_state = STATE_IDLE; }

// Tidur dalam, sekaligus mencatat waktu agar cooldown tetap terhitung meski
// millis() ikut hilang bersama RAM utama.
//
// Waktu terjaga ikut ditambahkan, bukan hanya durasi tidurnya. Satu siklus
// IDLE menghabiskan sekitar 1,2 detik untuk memindai, dan bila itu diabaikan,
// cooldown 60 detik berjalan jauh lebih lama daripada yang ditetapkan.
static void tidur(uint64_t detik) {
    rtc_uptime_s += (uint32_t)(millis() / 1000UL) + (uint32_t)detik;
    radio_tidur();
    Serial.flush();
    esp_deep_sleep((uint64_t)detik * 1000000ULL);
}

// ------------------------- CONTACT_WINDOW ---------------------------------
static void kirim_satu_paket() {
    uint8_t arm_idx = agent.select_arm();
    rtc_arm_sekarang = arm_idx;

    int16_t ax, ay, az;
    sensor_baca_mg(ax, ay, az);

    Payload p;
    p.arm       = (uint8_t)(arm_idx + 1);   // muatan memakai penomoran 1..4
    p.seq       = rtc_pkt_seq;
    p.e_prev_mJ = rtc_e_prev_mJ;            // energi paket sebelumnya (D6)
    p.vbatt_V   = rtc_vbatt;
    p.ax = ax; p.ay = ay; p.az = az;

    float e_mJ = radio_kirim_paket(p);

    rtc_e_prev_mJ = e_mJ;                   // dikirim pada paket berikutnya
    rtc_energi_akum_mJ += (double)e_mJ;
    rtc_pkt_seq++;
    rtc_ack_counter++;

    Serial.printf("TX seq=%u arm=%u e=%.2fmJ n=%u\n",
                  (unsigned)p.seq, (unsigned)p.arm, e_mJ,
                  (unsigned)energy_jumlah_sampel());

    if (rtc_ack_counter >= ACK_SETIAP_N) s_state = STATE_REWARD_EVAL;
}

// -------------------------- REWARD_EVAL -----------------------------------
//
// PENYIMPANGAN D15 terhadap Panduan HIL Subbab 2.5.
//
// Panduan memanggil beacon_scan_rssi() sesudah SETIAP paket untuk memeriksa
// apakah kendaraan masih di dalam jangkauan. Dengan durasi pemindaian 1.200 ms
// (D2), satu pemeriksaan seperti itu tiga kali lebih lama daripada seluruh
// siklus transmisi, sehingga laju paket runtuh dan jumlah siklus ACK per
// jendela kontak jatuh jauh di bawah perhitungan pada Dokumen Kerja Revisi.
//
// Penggantinya memakai keterangan yang sudah tersedia tanpa biaya tambahan:
// RSSI paket ACK, dan kegagalan menerima ACK itu sendiri. Kepergian kendaraan
// terdeteksi paling lambat satu siklus ACK, tanpa satu pun pemindaian tambahan.
//
// Catatan: kegagalan ACK tetap diberi penalti reward. Kegagalan itu memang
// peristiwa transmisi yang gagal, jadi sah dipelajari agen. Konsekuensinya,
// siklus terakhir tiap jendela kontak cenderung membawa satu penalti akibat
// kendaraan menjauh, bukan akibat pilihan arm. Pengaruhnya melemah seiring
// bertambahnya jumlah jendela dan perlu disebut di Bab Pembahasan.
static void evaluasi_reward() {
    int n_diterima = radio_tunggu_ack();
    float reward;
    bool kendaraan_pergi = false;

    if (n_diterima < 0) {
        reward = REWARD_TIMEOUT;
        kendaraan_pergi = true;
        Serial.println("ACK timeout -> penalti, keluar contact window");
    } else {
        float pdr = (float)n_diterima / (float)ACK_SETIAP_N;      // rasio [0,1]
        float e_rata = (float)(rtc_energi_akum_mJ / (double)ACK_SETIAP_N);

        // Normalisasi energi ke [0,1] terhadap batas atas empiris (Catatan
        // Perbaikan A.1). Tanpa ini, PDR dan energi berada pada skala yang jauh
        // berbeda dan salah satu komponen mendominasi secara trivial.
        float e_norm = e_rata / E_MAKS_mJ;
        if (e_norm > 1.0f) e_norm = 1.0f;
        if (e_norm < 0.0f) e_norm = 0.0f;

        reward = ALPHA * pdr - BETA * e_norm;

        int rssi_ack = radio_rssi_terakhir();
        Serial.printf("REWARD pdr=%.2f e_rata=%.2fmJ e_norm=%.3f r=%.3f rssi=%d\n",
                      pdr, e_rata, e_norm, reward, rssi_ack);

        if (rssi_ack < RSSI_EXIT) {
            kendaraan_pergi = true;
            Serial.printf("CONTACT_WINDOW keluar: rssi ACK=%d\n", rssi_ack);
        }
    }

    agent.update(rtc_arm_sekarang, reward);

    rtc_ack_counter = 0;
    rtc_energi_akum_mJ = 0.0;
    s_state = kendaraan_pergi ? STATE_IDLE : STATE_CONTACT_WINDOW;
}

// ------------------------------ tick --------------------------------------
void fsm_tick() {
    // --- BATTERY_GUARD: prioritas mutlak, diperiksa lebih dahulu ---
    //
    // Keadaan guard disimpan di RTC RAM, bukan pada variabel biasa. Deep sleep
    // menghapus RAM utama dan memulai program dari awal, sehingga penanda yang
    // disimpan sebagai variabel statis akan hilang dan node menganggap dirinya
    // tidak pernah masuk guard.
    //
    // Tanpa penanda itu, satu-satunya syarat masuk maupun keluar adalah ambang
    // 3,3 V, sehingga node kembali memancar begitu tegangan menyentuh 3,3 V
    // lagi. Tabel 4.1 naskah menetapkan syarat keluar pada 3,5 V, dan pita
    // 0,2 V itulah yang mencegah node berkedip masuk-keluar guard saat baterai
    // hampir habis dan tegangannya naik-turun mengikuti beban.
    rtc_vbatt = energy_baca_vbatt_V();

    if (rtc_dalam_guard) {
        if (rtc_vbatt > V_BATT_PULIH) {
            rtc_dalam_guard = false;
            s_state = STATE_IDLE;
            Serial.printf("BATTERY_GUARD pulih vbatt=%.2fV\n", rtc_vbatt);
        }
        else {
            Serial.printf("BATTERY_GUARD tetap vbatt=%.2fV (pulih di %.2fV)\n",
                          rtc_vbatt, V_BATT_PULIH);
            tidur(GUARD_SLEEP_S);
            return;  // tidak tercapai; deep sleep memulai ulang program
        }
    }
    else if (rtc_vbatt < V_BATT_MIN) {
        rtc_dalam_guard = true;
        s_state = STATE_BATTERY_GUARD;
        Serial.printf("BATTERY_GUARD masuk vbatt=%.2fV -> tidur %lus\n",
                      rtc_vbatt, (unsigned long)GUARD_SLEEP_S);
        tidur(GUARD_SLEEP_S);
        return;  // tidak tercapai
    }

    switch (s_state) {

    case STATE_IDLE: {
        // Cooldown sesudah keluar lewat batas waktu (D9)
        if (rtc_uptime_s < rtc_cooldown_sampai_s) {
            uint32_t sisa = rtc_cooldown_sampai_s - rtc_uptime_s;
            Serial.printf("COOLDOWN sisa %us\n", (unsigned)sisa);
            tidur(sisa < T_SLEEP_S ? T_SLEEP_S : (uint64_t)sisa);
            return;
        }

        int rssi = radio_scan_beacon();
        if (rssi > RSSI_DETECT) {
            rtc_ack_counter = 0;
            rtc_energi_akum_mJ = 0.0;
            s_t_masuk_window = millis();
            s_state = STATE_CONTACT_WINDOW;
            Serial.printf("CONTACT_WINDOW masuk rssi=%d t=%lu\n",
                          rssi, (unsigned long)rtc_t_global);
        } else {
            tidur(T_SLEEP_S);
        }
        break;
    }

    case STATE_CONTACT_WINDOW: {
        // Satu-satunya kondisi keluar yang diperiksa di sini adalah batas waktu.
        // Kepergian kendaraan ditangani di REWARD_EVAL (lihat catatan D15).
        if (millis() - s_t_masuk_window >= TIMEOUT_WINDOW_MS) {
            Serial.println("CONTACT_WINDOW keluar: timeout_window");
            rtc_cooldown_sampai_s = rtc_uptime_s + (COOLDOWN_MS / 1000UL);
            rtc_ack_counter = 0;
            s_state = STATE_IDLE;
            break;
        }

        kirim_satu_paket();

        // Jeda hanya bila masih akan mengirim paket berikutnya; bila sudah
        // saatnya evaluasi ACK, node harus segera masuk mode terima.
        if (s_state == STATE_CONTACT_WINDOW) delay(JEDA_ANTAR_PAKET_MS);  // D7
        break;
    }

    case STATE_REWARD_EVAL:
        evaluasi_reward();
        break;

    // STATE_BATTERY_GUARD tidak muncul di sini. Seluruh penanganannya sudah
    // selesai di awal fungsi ini, yang selalu berakhir dengan deep sleep atau
    // dengan pemulihan ke IDLE, sehingga alur tidak pernah sampai ke switch
    // dalam keadaan guard.
    }
}
