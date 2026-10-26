// main.cpp: node sensor statis
//
// Variabel yang harus bertahan lintas siklus deep sleep dideklarasikan di sini
// dengan atribut RTC_DATA_ATTR. Ini batasan ESP32: hanya variabel global yang
// dapat dialokasikan di RTC RAM (Panduan HIL Subbab 2.2).
//
// Total RTC RAM yang dipakai matriks pembelajaran (params.h Bagian 7):
//   mode SW-UCB: 4 arm x (50 slot x 4 byte + 8 byte) = 832 byte
//   mode UCB1:   4 arm x 8 byte                      =  32 byte
// ditambah 1 byte giliran arm untuk mode sweep; semuanya jauh di bawah
// kapasitas 8 kB.
//
// Uji persistensi 50 siklus ada pada SOP Tahap 4.2.

#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include "config/pin_map.h"
#include "config/params.h"
#include "modules/SW_UCB_Agent.h"
#include "modules/energy_meter.h"
#include "modules/radio.h"
#include "modules/sensor.h"
#include "modules/fsm.h"

// ---------------- keadaan yang bertahan di RTC RAM ----------------
RTC_DATA_ATTR ArmState rtc_arms[N_ARMS];
RTC_DATA_ATTR uint32_t rtc_t_global          = 0;
RTC_DATA_ATTR uint16_t rtc_pkt_seq           = 0;
RTC_DATA_ATTR uint16_t rtc_ack_counter       = 0;
RTC_DATA_ATTR double   rtc_energi_akum_mJ    = 0.0;
RTC_DATA_ATTR uint8_t  rtc_arm_sekarang      = 0;
RTC_DATA_ATTR float    rtc_vbatt             = 3.70f;
RTC_DATA_ATTR float    rtc_e_prev_mJ         = 0.0f;
RTC_DATA_ATTR uint32_t rtc_cooldown_sampai_s = 0;
RTC_DATA_ATTR uint32_t rtc_uptime_s          = 0;
RTC_DATA_ATTR bool     rtc_dalam_guard       = false;
RTC_DATA_ATTR uint32_t rtc_penanda_sah       = 0;
RTC_DATA_ATTR uint8_t  rtc_sweep_arm         = 0;   // giliran arm, mode sweep

static const uint32_t PENANDA_SAH = 0x57554342UL;  // "WUCB"

SW_UCB_Agent agent(rtc_arms, &rtc_t_global);

// Cetak seluruh parameter aktif saat boot. SOP Subbab III.2 mensyaratkan nilai
// firmware diperiksa terhadap sheet 00-Konfigurasi lewat baris cetak ini.
static void cetak_parameter() {
    Serial.println();
    Serial.println("=====================================================");
    Serial.printf ("  Node sensor  %s\n", FW_VERSI);
    Serial.println("=====================================================");
#if MODE_SWEEP_AKTIF
    Serial.printf ("  mode=SWEEP (arm bergiliran per siklus ACK, bukan keputusan agen)\n");
    Serial.printf ("  label log: Metode Sweep; tidak masuk analisis hipotesis\n");
    Serial.printf ("  alpha=%.2f  beta=%.2f\n", ALPHA, BETA);
#elif MODE_SW_UCB
    Serial.printf ("  mode=SW-UCB (jendela W, Persamaan 2.5-2.6)\n");
    Serial.printf ("  W=%d  xi=%.2f  alpha=%.2f  beta=%.2f\n",
                   W_SIZE, XI, ALPHA, BETA);
#else
    Serial.printf ("  mode=UCB1 (kumulatif tanpa jendela, Persamaan 2.4) -- "
                   "W_SIZE tidak dipakai\n");
    Serial.printf ("  xi=%.2f  alpha=%.2f  beta=%.2f\n", XI, ALPHA, BETA);
#endif
    Serial.printf ("  E_maks=%.1f mJ  %s\n", E_MAKS_mJ,
                   E_MAKS_TERUKUR ? "(terukur Tahap 4.1)"
                                  : "(ESTIMASI - belum diukur)");
    Serial.printf ("  RSSI detect=%d exit=%d  %s\n", RSSI_DETECT, RSSI_EXIT,
                   RSSI_TERKALIBRASI ? "(terkalibrasi Tahap 4.5)"
                                     : "(DEFAULT - belum dikalibrasi)");
    Serial.printf ("  scan=%lums sleep=%llus jeda_paket=%lums\n",
                   (unsigned long)T_SCAN_MS, (unsigned long long)T_SLEEP_S,
                   (unsigned long)JEDA_ANTAR_PAKET_MS);
    Serial.printf ("  timeout_window=%lums cooldown=%lums\n",
                   (unsigned long)TIMEOUT_WINDOW_MS, (unsigned long)COOLDOWN_MS);
    Serial.printf ("  ADXL345 offset OFS x=%d y=%d z=%d (15,6 mg/LSB)\n",
                   (int)ADXL345_OFS_X, (int)ADXL345_OFS_Y, (int)ADXL345_OFS_Z);
    Serial.printf ("  cuplik_saat_tx=%d  ack_tiap=%d paket\n",
                   ENERGI_CUPLIK_SAAT_TX, ACK_SETIAP_N);
    for (int a = 0; a < N_ARMS; a++) {
        Serial.printf("  Arm %d: TP=%d dBm CR=4/%d\n",
                      a + 1, ARM_CONFIG[a].tp_dbm, ARM_CONFIG[a].cr);
    }

#if !E_MAKS_TERUKUR
    Serial.println("  PERINGATAN: E_maks masih estimasi. Selesaikan Tahap 4.1");
    Serial.println("              sebelum pengambilan data (SOP gerbang G4).");
#endif
#if !RSSI_TERKALIBRASI
    Serial.println("  PERINGATAN: threshold RSSI belum dikalibrasi (Tahap 4.5).");
#endif
#if !ENERGI_CUPLIK_SAAT_TX
    Serial.println("  PERINGATAN: pencuplikan saat TX dimatikan. Energi akan");
    Serial.println("              seragam antar-arm. Hanya untuk pembanding.");
#endif
    Serial.println("=====================================================");
}

// Kembalikan seluruh keadaan belajar ke nol. Nomor urut paket ikut kembali ke nol,
// sehingga paket pertama sesudah reset bernomor 0 dan terbaca di log gateway sebagai
// lompatan nomor urut ke bawah (gateway mencetak baris "# NODE_RESET"). Dengan itu
// setiap rekaman dapat dipastikan berangkat dari keadaan awal yang sama (A.1).
static void reset_keadaan_agen(const char* sebab) {
    Serial.printf("RESET AGEN sebab=\"%s\": t, N, isi window, seq, penghitung ACK = 0\n", sebab);
    agent.reset();
    rtc_pkt_seq           = 0;
    rtc_ack_counter       = 0;
    rtc_energi_akum_mJ    = 0.0;
    rtc_e_prev_mJ         = 0.0f;
    rtc_arm_sekarang      = 0;
    rtc_cooldown_sampai_s = 0;
    rtc_uptime_s          = 0;
    rtc_sweep_arm         = 0;
    rtc_penanda_sah       = PENANDA_SAH;
}

// Operator menahan tombol BOOT (GPIO0) di antara dua skenario. Node bangun dari tidur
// tiap T_SLEEP_S detik, jadi tombol perlu ditahan sedikit lebih lama dari itu. Tombol
// ini tidak berpengaruh pada boot dingin (di sana BOOT berarti mode unggah). Dibaca dua
// kali berjarak 50 ms agar sentuhan sesaat tidak menghapus pembelajaran.
static bool reset_diminta_operator() {
    pinMode(PIN_RESET_AGEN, INPUT_PULLUP);
    if (digitalRead(PIN_RESET_AGEN) != LOW) return false;
    delay(50);
    return digitalRead(PIN_RESET_AGEN) == LOW;
}

static void henti_fatal(const char* pesan) {
    // Kegagalan periferal tidak boleh berlanjut diam-diam: data yang terekam
    // sesudahnya akan tampak sah padahal tidak. Node dihentikan dan alasannya
    // dicetak berulang agar terlihat di serial monitor.
    while (true) {
        Serial.printf("FATAL: %s\n", pesan);
        delay(2000);
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);

    bool bangun_dari_sleep =
        (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (!bangun_dari_sleep) cetak_parameter();

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);

    if (!energy_init()) henti_fatal("INA219 tidak menjawab di 0x40");
    if (!sensor_init()) henti_fatal("ADXL345 tidak menjawab di 0x53 "
                                    "(periksa SDO ke GND dan CS ke 3V3)");
    if (!radio_init())  henti_fatal("LoRa gagal inisialisasi "
                                    "(periksa NSS, RESET, dan tiga pin SPI)");

    // Nyalakan pertama kali, atau RTC RAM rusak: mulai pembelajaran dari nol.
    if (rtc_penanda_sah != PENANDA_SAH) {
        reset_keadaan_agen("RTC RAM kosong atau rusak");
    } else if (reset_diminta_operator()) {
        reset_keadaan_agen("tombol BOOT ditahan");
    } else if (bangun_dari_sleep) {
        Serial.printf("Bangun: t=%lu seq=%u vbatt=%.2fV\n",
                      (unsigned long)rtc_t_global, (unsigned)rtc_pkt_seq,
                      rtc_vbatt);
    }

    fsm_init();
}

void loop() {
    fsm_tick();
}
