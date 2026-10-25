// uji_rtc_ram.cpp - SOP Tahap 4.2: persistensi RTC RAM lintas deep sleep.
//
// Bukan firmware node. Dijalankan dari BATERAI saja (tanpa USB), sehingga hasilnya
// dikirim lewat radio ke gateway (kolom CSV gateway, lihat bawah).
//
// Alur satu siklus (diulang tanpa batas, SOP meminta sekurang-kurangnya 50):
//   1. Bangun dari deep sleep 5 detik.
//   2. Periksa SETIAP elemen keadaan agen di RTC RAM terhadap pola yang diharapkan:
//      buffer reward 4 x 50 slot, buf_head, n_in_window, mu, t_global, nomor urut
//      paket, akumulator energi, dan beberapa variabel lain. Hitung elemen yang rusak.
//   3. Laporkan lewat radio (tiga kali, agar sampai walau satu paket hilang).
//   4. Perbarui variabel dinamis (t_global, nomor urut, akumulator), lalu tidur lagi.
//
// Boot dingin yang bukan boot pertama (penanda tidak sah sesudah bangun dari timer)
// berarti RTC RAM hilang, misalnya karena catu daya mati sesaat pada beban ringan
// (J5019). Itu dicatat sebagai kegagalan.
//
// Kolom paket (gateway mencetak sebagai baris DATA):
//   arm        1 = siklus normal, 2 = RTC RAM hilang (boot dingin), 3 = boot pertama
//   seq        nomor siklus (dari RTC; 0 bila hilang)
//   e_prev_mJ  jumlah elemen RUSAK pada siklus ini (harus 0)
//   vbatt      tegangan bus INA219
//   ax         nomor siklus
//   ay         jumlah kumulatif elemen rusak sejak boot pertama
//   az         jumlah boot dingin tak terduga sejak boot pertama
//
//   pio run -e uji_rtc_ram -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include <esp_sleep.h>
#include "../src/config/pin_map.h"
#include "../src/modules/payload.h"
#include "../src/modules/SW_UCB_Agent.h"

// Lama tidur tiap siklus (detik). Bawaan 5 detik (SOP Tahap 4.2); env uji_rtc_60s dan
// uji_rtc_3600s memakai 60 dan 3600 detik untuk menguji cooldown dan guard pada J5019.
#ifndef UJI_TIDUR_S
#define UJI_TIDUR_S 5
#endif

static const uint32_t MAGIC = 0xA55A1234;

RTC_DATA_ATTR ArmState rtc_arms[N_ARMS];
RTC_DATA_ATTR uint32_t rtc_t_global = 0;
RTC_DATA_ATTR uint16_t rtc_pkt_seq = 0;
RTC_DATA_ATTR uint16_t rtc_ack_counter = 0;
RTC_DATA_ATTR double   rtc_energi_akum_mJ = 0.0;
RTC_DATA_ATTR float    rtc_vbatt = 0.0f;
RTC_DATA_ATTR float    rtc_e_prev_mJ = 0.0f;
RTC_DATA_ATTR uint32_t rtc_uptime_s = 0;
RTC_DATA_ATTR uint32_t rtc_penanda = 0;
RTC_DATA_ATTR uint32_t rtc_siklus = 0;
RTC_DATA_ATTR uint32_t rtc_rusak_total = 0;
RTC_DATA_ATTR uint32_t rtc_dingin_total = 0;

static INA219_WE ina(INA219_ADDR);

// Pola yang diharapkan. Nilai float dipilih supaya tepat dalam biner (kelipatan 1/64).
static float pola_buf(int a, int i) { return 1.0f + (float)a + (float)i * 0.015625f; }

static void isi_awal() {
    for (int a = 0; a < N_ARMS; a++) {
        for (int i = 0; i < W_SIZE; i++) rtc_arms[a].reward_buf[i] = pola_buf(a, i);
        rtc_arms[a].buf_head = (uint16_t)(7 + a);
        rtc_arms[a].n_in_window = (uint16_t)W_SIZE;
        rtc_arms[a].mu = 0.5f + (float)a;
    }
    rtc_t_global = 100000;
    rtc_pkt_seq = 40000;
    rtc_ack_counter = 7;
    rtc_energi_akum_mJ = 12345.678;
    rtc_vbatt = 3.875f;
    rtc_e_prev_mJ = 77.7f;
    rtc_uptime_s = 123456;
    rtc_siklus = 0;
    rtc_rusak_total = 0;
    rtc_dingin_total = 0;
    rtc_penanda = MAGIC;
}

static uint32_t periksa() {
    uint32_t rusak = 0;
    for (int a = 0; a < N_ARMS; a++) {
        for (int i = 0; i < W_SIZE; i++) if (rtc_arms[a].reward_buf[i] != pola_buf(a, i)) rusak++;
        if (rtc_arms[a].buf_head != (uint16_t)(7 + a)) rusak++;
        if (rtc_arms[a].n_in_window != (uint16_t)W_SIZE) rusak++;
        if (rtc_arms[a].mu != 0.5f + (float)a) rusak++;
    }
    if (rtc_t_global != 100000 + rtc_siklus) rusak++;
    if (rtc_pkt_seq != (uint16_t)(40000 + rtc_siklus)) rusak++;
    if (rtc_ack_counter != 7) rusak++;
    if (rtc_energi_akum_mJ != 12345.678 + (double)rtc_siklus) rusak++;
    if (rtc_vbatt != 3.875f) rusak++;
    if (rtc_e_prev_mJ != 77.7f) rusak++;
    if (rtc_uptime_s != 123456 + (uint32_t)UJI_TIDUR_S * rtc_siklus) rusak++;
    return rusak;
}

static void lapor(uint8_t arm_kode, uint16_t siklus, uint32_t rusak_ini) {
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    ina.init();
    ina.setPGain(INA219_PG_320);
    ina.setBusRange(INA219_BRNG_16);
    float vbus = ina.getBusVoltage_V();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    if (!LoRa.begin(433E6)) return;
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();
    LoRa.setTxPower(14);

    Payload p;
    p.arm = arm_kode;
    p.seq = siklus;
    p.e_prev_mJ = (float)rusak_ini;
    p.vbatt_V = vbus;
    p.ax = (int16_t)siklus;
    p.ay = (int16_t)(rtc_rusak_total > 32000 ? 32000 : rtc_rusak_total);
    p.az = (int16_t)(rtc_dingin_total > 32000 ? 32000 : rtc_dingin_total);
    uint8_t buf[12];
    payload_pack(p, buf);
    for (int k = 0; k < 3; k++) {
        LoRa.beginPacket();
        LoRa.write(buf, 12);
        LoRa.endPacket();
        delay(150);
    }
    LoRa.sleep();
}

void setup() {
    esp_sleep_wakeup_cause_t sebab = esp_sleep_get_wakeup_cause();
    bool dari_timer = (sebab == ESP_SLEEP_WAKEUP_TIMER);

    if (rtc_penanda != MAGIC) {
        if (dari_timer) {
            // Bangun dari timer tetapi RTC RAM kosong: catu daya sempat mati.
            uint32_t d = rtc_dingin_total;      // kemungkinan sudah nol
            isi_awal();
            rtc_dingin_total = d + 1;
            lapor(2, 0, 1);
        } else {
            isi_awal();
            lapor(3, 0, 0);                      // boot pertama
        }
    } else {
        uint32_t rusak = periksa();
        rtc_rusak_total += rusak;
        lapor(1, (uint16_t)rtc_siklus, rusak);
    }

    // perbarui variabel dinamis lalu tidur
    rtc_siklus++;
    rtc_t_global = 100000 + rtc_siklus;
    rtc_pkt_seq = (uint16_t)(40000 + rtc_siklus);
    rtc_energi_akum_mJ = 12345.678 + (double)rtc_siklus;
    rtc_uptime_s = 123456 + (uint32_t)UJI_TIDUR_S * rtc_siklus;

    esp_sleep_enable_timer_wakeup((uint64_t)UJI_TIDUR_S * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}
