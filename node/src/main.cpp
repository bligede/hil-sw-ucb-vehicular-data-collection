// main.cpp — node sensor statis
//
// Variabel yang harus bertahan lintas siklus deep sleep dideklarasikan di sini
// dengan atribut RTC_DATA_ATTR. Ini batasan ESP32: hanya variabel global yang
// dapat dialokasikan di RTC RAM (Panduan HIL Subbab 2.2).
//
// Total RTC RAM yang dipakai matriks pembelajaran:
//   4 arm x 50 slot x 4 byte = 800 byte, jauh di bawah kapasitas 8 kB.
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

static const uint32_t PENANDA_SAH = 0x57554342UL;  // "WUCB"

SW_UCB_Agent agent(rtc_arms, &rtc_t_global);

// Cetak seluruh parameter aktif saat boot. SOP Subbab III.2 mensyaratkan nilai
// firmware diperiksa terhadap sheet 00-Konfigurasi lewat baris cetak ini.
static void cetak_parameter() {
    Serial.println();
    Serial.println("=====================================================");
    Serial.printf ("  Node sensor  %s\n", FW_VERSI);
    Serial.println("=====================================================");
    Serial.printf ("  W=%d  xi=%.2f  alpha=%.2f  beta=%.2f\n",
                   W_SIZE, XI, ALPHA, BETA);
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
        Serial.println("RTC RAM kosong atau rusak -> reset keadaan agen");
        agent.reset();
        rtc_pkt_seq           = 0;
        rtc_ack_counter       = 0;
        rtc_energi_akum_mJ    = 0.0;
        rtc_e_prev_mJ         = 0.0f;
        rtc_cooldown_sampai_s = 0;
        rtc_uptime_s          = 0;
        rtc_penanda_sah       = PENANDA_SAH;
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
