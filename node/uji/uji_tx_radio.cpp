// uji_tx_radio.cpp - program uji: arus INA219 selama transmisi, dilaporkan lewat radio.
//
// Bukan firmware node. Dipakai saat node berjalan dari BATERAI (tanpa USB), sehingga
// hasilnya tidak bisa dibaca dari serial node. Tiap paket memuat hasil ukur transmisi
// SEBELUMNYA, memakai kolom paket yang sama dengan firmware agar gateway
// mencetaknya tanpa diubah:
//
//   kolom gateway   isi pada program ini
//   arm             arm yang dipakai (1..4), tetap 5 paket berturut-turut
//   e_prev_mJ       arus RATA-RATA selama transmisi sebelumnya, dalam mA
//   vbatt           tegangan bus INA219 TERENDAH selama transmisi sebelumnya (V)
//                   (paket pertama: tegangan siaga)
//   ax              arus PUNCAK selama transmisi sebelumnya, mA
//   ay              jumlah cuplikan INA219 selama transmisi sebelumnya
//   az              lama transmisi sebelumnya, 0,1 ms (satuan 100 us)
//
//   pio run -e uji_tx_radio -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"
#include "../src/modules/payload.h"

static INA219_WE ina(INA219_ADDR);
static const uint8_t TP[4] = {5, 10, 14, 20};
static const uint8_t CR[4] = {5, 5, 7, 8};

static float    s_mean = 0, s_peak = 0, s_dur_ms = 0;
static uint16_t s_n = 0;
static float    s_vmin = 0;     // tegangan bus terendah selama transmisi sebelumnya
static uint16_t s_seq = 0;

void setup() {
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    ina.init();
    ina.setPGain(INA219_PG_320);
    ina.setBusRange(INA219_BRNG_16);
    ina.setADCMode(INA219_BIT_MODE_12);
    ina.setMeasureMode(INA219_CONTINUOUS);
    ina.setShuntSizeInOhms(INA219_SHUNT_OHM);

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    while (!LoRa.begin(433E6)) delay(500);
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();
}

void loop() {
    for (int a = 0; a < 4; a++) {
        LoRa.setTxPower(TP[a]);
        LoRa.setCodingRate4(CR[a]);
        for (int k = 0; k < 5; k++) {
            delay(500);

            // tegangan bus saat siaga
            float vbus = ina.getBusVoltage_V();

            Payload p;
            p.arm = (uint8_t)(a + 1);
            p.seq = s_seq++;
            p.e_prev_mJ = s_mean;              // arus rata-rata transmisi sebelumnya (mA)
            p.vbatt_V = s_vmin > 0.0f ? s_vmin : vbus;
            p.ax = (int16_t)(s_peak + 0.5f);
            p.ay = (int16_t)s_n;
            p.az = (int16_t)(s_dur_ms * 10.0f + 0.5f);
            uint8_t buf[12];
            payload_pack(p, buf);

            LoRa.beginPacket();
            LoRa.write(buf, 12);
            LoRa.endPacket(true);

            uint32_t t0 = micros();
            uint32_t n = 0;
            double jumlah = 0;
            float puncak = -1e9f;
            float vmin = 99.0f;
            while (!LoRa.txDone() && (micros() - t0) < 500000UL) {
                float v = ina.getBusVoltage_V();
                if (v < vmin) vmin = v;
                float x = ina.getCurrent_mA();
                n++;
                jumlah += x;
                if (x > puncak) puncak = x;
            }
            s_dur_ms = (micros() - t0) / 1000.0f;
            s_n = (uint16_t)(n > 32000 ? 32000 : n);
            s_mean = n ? (float)(jumlah / n) : 0.0f;
            s_peak = n ? puncak : 0.0f;
            s_vmin = vmin < 90.0f ? vmin : 0.0f;
        }
    }
}
