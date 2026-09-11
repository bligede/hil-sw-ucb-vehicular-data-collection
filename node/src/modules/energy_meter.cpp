#include "energy_meter.h"
#include <INA219_WE.h>
#include "../config/pin_map.h"
#include "../config/params.h"

static INA219_WE ina219(INA219_ADDR);

static float    s_vbus_V        = 3.70f;  // tegangan bus terakhir
static double   s_akum_mJ       = 0.0;    // akumulator energi transmisi berjalan
static uint32_t s_t_sampel_us   = 0;      // waktu cuplikan sebelumnya
static uint32_t s_t_mulai_us    = 0;
static uint16_t s_n_sampel      = 0;
static float    s_i_terakhir_mA = 0.0f;   // arus cuplikan terakhir

bool energy_init() {
    if (!ina219.init()) return false;

    // Rentang shunt +-320 mV pada shunt 0,1 ohm memberi batas ukur 3,2 A.
    // Arus baterai puncak yang diperkirakan 286 mA menghasilkan 28,6 mV,
    // jauh di dalam rentang (Panduan Perakitan Subbab II.1).
    ina219.setPGain(PG_320);
    ina219.setBusRange(BRNG_16);

    // Konversi tunggal 12-bit = 532 us. Lihat catatan D1 pada header.
    ina219.setADCMode(BIT_MODE_12);
    ina219.setMeasureMode(CONTINUOUS);
    ina219.setShuntSizeInOhms(0.1f);

    s_vbus_V = ina219.getBusVoltage_V();
    return true;
}

void energy_begin() {
    s_vbus_V        = ina219.getBusVoltage_V();  // sekali saja, lihat header
    s_akum_mJ       = 0.0;
    s_n_sampel      = 0;
    s_i_terakhir_mA = 0.0f;
    s_t_mulai_us    = micros();
    s_t_sampel_us   = s_t_mulai_us;
}

void energy_sample() {
    // Batas cuplikan hanya menghentikan PEMBACAAN sensor, bukan penjumlahan
    // energinya. Bila penjumlahan ikut berhenti, sisa rentang transmisi sesudah
    // batas tercapai tidak terhitung sama sekali dan energinya kurang catat
    // tanpa gejala apa pun. Sesudah batas, arus terakhir tetap dipakai.
    if (s_n_sampel < ENERGI_MAKS_SAMPEL) {
        s_i_terakhir_mA = ina219.getCurrent_mA();
        s_n_sampel++;
    }

    uint32_t sekarang_us = micros();

    // Integrasi persegi panjang: daya sesaat dikali selang sejak cuplikan lalu.
    // micros() berputar tiap ~71 menit; pengurangan unsigned tetap benar.
    uint32_t dt_us = sekarang_us - s_t_sampel_us;
    s_t_sampel_us = sekarang_us;

    float p_mW = s_vbus_V * s_i_terakhir_mA;
    s_akum_mJ += (double)p_mW * ((double)dt_us / 1000000.0);
}

float energy_end_mJ() {
    // Cuplikan penutup agar sisa rentang sesudah cuplikan terakhir ikut terhitung.
    energy_sample();

    if (s_n_sampel == 0) return 0.0f;
    return (float)s_akum_mJ;
}

float energy_vbatt_V() { return s_vbus_V; }

float energy_baca_vbatt_V() {
    s_vbus_V = ina219.getBusVoltage_V();
    return s_vbus_V;
}

uint16_t energy_jumlah_sampel() { return s_n_sampel; }
