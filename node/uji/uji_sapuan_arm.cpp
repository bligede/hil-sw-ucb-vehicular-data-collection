// uji_sapuan_arm.cpp - program uji: SNR per arm per menit, urutan arm 3, 1, 4, 2
// (arahan Pembimbing I 15 Nov 2026 butir 3 / C.3, anomali Arm 3).
//
// Bukan firmware node. Node dicatu baterai, tanpa USB. Gateway merekam RSSI/SNR tiap paket
// (build Sheets atau serial). Susunan:
//   - tunggu MULAI_TUNDA_S detik sesudah dinyalakan (operator menutup enclosure, menjauh)
//   - PUTARAN kali: blok arm 3, 1, 4, 2, masing-masing BLOK_S detik
//   - dalam blok, satu paket tiap JEDA_MS (arm tetap), lalu node berhenti mengirim
// Parameter radio sama dengan firmware node: SF8, BW 125 kHz, preamble 8, sync 0x12, CRC,
// TP/CR per arm {5/5, 10/5, 14/7, 20/8}.
//
// Kolom paket (format firmware, agar gateway mencetaknya tanpa diubah):
//   arm        arm blok ini (1..4)
//   seq        nomor urut paket, naik terus mulai 0
//   e_prev_mJ  arus RATA-RATA selama transmisi paket sebelumnya, mA (paket pertama: 0)
//   vbatt      tegangan bus INA219 terendah selama transmisi sebelumnya, V (pertama: siaga)
//   ax         nomor putaran (1..PUTARAN)
//   ay         arus PUNCAK selama transmisi sebelumnya, mA
//   az         lama transmisi sebelumnya, satuan 0,1 ms
// Jeda 3 s > JARAK_PAKET_MIN_MS gateway (150 ms) dan > ACK_IDLE_MS (1,4 s), jadi tiap paket
// menjadi satu siklus gateway sendiri dan lolos validasi 1.4. Run 01 (16 Nov) memakai jeda 2 s:
// sekitar 42 % paket hilang, diduga bertabrakan dengan ACK gateway (~2 s sesudah paket), dan
// isTransmitting() yang langsung false membuat kolom arus/lama transmisi 0; keduanya diperbaiki
// (jeda 3 s, txDone()).
// Catatan gateway-1.5 (B.3, 21 Nov 2026): ACK_IDLE_MS sudah tidak ada. Program ini mengisi byte 0
// hanya dengan nomor arm, jadi gateway-1.5 membaca indeks 0 dan seq_awal = seq; tiap paket tetap
// menjadi siklus sendiri dan siklus lama ditutup saat paket berikutnya (seq_awal lain) datang,
// sebelum tenggat ~5,5 s. Program ini tidak mendengarkan ACK, jadi hasilnya tidak berubah.
//
//   pio run -e uji_sapuan_arm -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"
#include "../src/modules/payload.h"

static const uint8_t  URUTAN[4]   = {3, 1, 4, 2};
static const uint8_t  TP[5]       = {0, 5, 10, 14, 20};   // indeks = nomor arm
static const uint8_t  CR[5]       = {0, 5, 5, 7, 8};
static const uint32_t MULAI_TUNDA_S = 30;
static const uint32_t BLOK_S      = 60;
static const uint32_t JEDA_MS     = 3000;
static const uint8_t  PUTARAN     = 5;

static INA219_WE ina(INA219_ADDR);
static bool ina_ok = false;
static float    s_mean = 0, s_peak = 0, s_dur_ms = 0, s_vmin = 0;
static uint16_t s_seq = 0;

static void kirim(uint8_t arm, uint8_t putaran) {
    float vbus = ina_ok ? ina.getBusVoltage_V() : 0.0f;
    Payload p;
    p.arm = arm;
    p.seq = s_seq++;
    p.e_prev_mJ = s_mean;
    p.vbatt_V = s_vmin > 0.0f ? s_vmin : vbus;
    p.ax = putaran;
    p.ay = (int16_t)(s_peak + 0.5f);
    p.az = (int16_t)(s_dur_ms * 10.0f + 0.5f);
    uint8_t buf[12];
    payload_pack(p, buf);

    LoRa.beginPacket();
    LoRa.write(buf, 12);
    LoRa.endPacket(true);
    uint32_t t0 = micros();
    uint32_t n = 0;
    double jumlah = 0;
    float puncak = -1e9f, vmin = 99.0f;
    while (!LoRa.txDone() && (micros() - t0) < 500000UL) {
        if (ina_ok) {
            float v = ina.getBusVoltage_V();
            if (v < vmin) vmin = v;
            float x = ina.getCurrent_mA();
            jumlah += x;
            if (x > puncak) puncak = x;
        }
        n++;
    }
    s_dur_ms = (micros() - t0) / 1000.0f;
    s_mean = (ina_ok && n) ? (float)(jumlah / n) : 0.0f;
    s_peak = (ina_ok && n) ? puncak : 0.0f;
    s_vmin = (ina_ok && vmin < 90.0f) ? vmin : 0.0f;
    Serial.printf("%u,%u,%u,%.1f,%.1f\n", putaran, arm, p.seq, s_mean, s_dur_ms);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    ina_ok = ina.init();
    if (ina_ok) {
        ina.setPGain(INA219_PG_320);
        ina.setBusRange(INA219_BRNG_16);
        ina.setADCMode(INA219_BIT_MODE_12);
        ina.setMeasureMode(INA219_CONTINUOUS);
        ina.setShuntSizeInOhms(INA219_SHUNT_OHM);
    }
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    while (!LoRa.begin(433E6)) delay(500);
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();
    LoRa.idle();
    Serial.printf("# uji_sapuan_arm: INA219 %s, urutan 3,1,4,2, blok %lu s, jeda %lu ms, %u putaran, mulai %lu s lagi\n",
                  ina_ok ? "ok" : "TIDAK ADA", (unsigned long)BLOK_S, (unsigned long)JEDA_MS,
                  PUTARAN, (unsigned long)MULAI_TUNDA_S);
    Serial.println("putaran,arm,seq,i_rata_prev_mA,dur_prev_ms");
    delay(MULAI_TUNDA_S * 1000UL);
}

void loop() {
    static bool selesai = false;
    if (selesai) { delay(1000); return; }
    for (uint8_t r = 1; r <= PUTARAN; r++) {
        for (uint8_t b = 0; b < 4; b++) {
            const uint8_t arm = URUTAN[b];
            LoRa.setTxPower(TP[arm]);
            LoRa.setCodingRate4(CR[arm]);
            const uint32_t t_blok = millis();
            uint32_t t_kirim = t_blok;
            while (millis() - t_blok < BLOK_S * 1000UL) {
                kirim(arm, r);
                t_kirim += JEDA_MS;
                while ((int32_t)(millis() - t_kirim) < 0) delay(5);
            }
        }
    }
    LoRa.sleep();
    Serial.println("# selesai");
    selesai = true;
}
