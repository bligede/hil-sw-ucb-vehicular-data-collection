// uji_tx_arm.cpp - program uji: transmisi beruntun per arm untuk verifikasi daya pancar dan pemeriksaan
// silang INA219 terhadap Fluke (arahan Pembimbing I 19 Nov 2026 nomor 3 dan 4).
//
// Bukan firmware node. Pengaturan daya per arm diambil dari ARM_CONFIG (params.h) lewat setTxPower()
// pustaka yang sama dengan firmware, TIDAK diubah. Sesudah daya diset, RegPaConfig (0x09), RegPaDac
// (0x4D), dan RegOcp (0x0B) dibaca balik lewat SPI dan dicetak.
//
// Jadwal tetap supaya operator dapat membaca Fluke dengan stopwatch sejak saklar dinyalakan:
//   0-30 s           IDLE (radio standby)
//   lalu tiap arm k = 1..4 bergiliran:  TX arm k 40 s, IDLE 20 s   (satu putaran 240 s, berulang)
//   arm 1 mulai 30 s, arm 2 mulai 90 s, arm 3 mulai 150 s, arm 4 mulai 210 s, arm 1 lagi 270 s, ...
// Fase TX: paket 12 bait beruntun, SF8, BW 125 kHz, CR dan TP sesuai arm, sync word 0x34 supaya gateway
// tidak menerima. Baca Fluke pada paruh kedua tiap fase TX (tampilan sudah stabil).
//
// INA219 dicuplik terus sepanjang fase (rata-rata waktu, sebanding dengan Fluke yang merata-ratakan
// tampilannya) dan dicatat terpisah untuk cuplikan saat TxDone belum menyala (arus saat memancar saja).
// Nilai shunt firmware 0,1021 ohm dipakai apa adanya; koreksi kalibrasi dilakukan saat olah data.
//
//   pio run -e uji_tx_arm -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"
#include "../src/config/params.h"
#include "../src/config/protocol.h"

static INA219_WE ina(INA219_ADDR);
static const uint32_t AWAL_MS = 30000, TX_MS = 40000, JEDA_MS = 20000;

static uint8_t baca_reg(uint8_t a) {
    SPI.beginTransaction(SPISettings(8E6, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_NSS, LOW);
    SPI.transfer(a & 0x7F);
    uint8_t v = SPI.transfer(0);
    digitalWrite(LORA_NSS, HIGH);
    SPI.endTransaction();
    return v;
}

static void fase_idle(const char* nama, uint32_t lama_ms) {
    LoRa.idle();
    const uint32_t t0 = millis();
    double ji = 0; float vmin = 99; uint32_t n = 0;
    while (millis() - t0 < lama_ms) {
        ji += ina.getCurrent_mA();
        float v = ina.getBusVoltage_V(); if (v < vmin) vmin = v;
        n++;
        delay(20);
    }
    Serial.printf("%s,t_s=%.1f,lama_s=%.0f,i_rata_mA=%.1f,vbus_min_V=%.3f,n=%lu\n", nama, t0 / 1000.0,
                  lama_ms / 1000.0, n ? ji / n : 0.0, vmin, (unsigned long)n);
}

static void fase_tx(uint8_t k) {
    const ArmConfig& a = ARM_CONFIG[k];
    LoRa.idle();
    LoRa.setTxPower(a.tp_dbm);
    LoRa.setCodingRate4(a.cr);
    Serial.printf("REGPA,arm=%u,tp=%d,PaConfig=0x%02X,PaDac=0x%02X,Ocp=0x%02X,ModemConfig1=0x%02X\n",
                  (unsigned)(k + 1), (int)a.tp_dbm, baca_reg(0x09), baca_reg(0x4D), baca_reg(0x0B),
                  baca_reg(0x1D));
    const uint8_t buf[12] = {(uint8_t)(k + 1), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint32_t t0 = millis();
    double ji_semua = 0, ji_tx = 0; uint32_t n_semua = 0, n_tx = 0, paket = 0, gagal = 0;
    float vmin = 99;
    while (millis() - t0 < TX_MS) {
        LoRa.beginPacket();
        LoRa.write(buf, sizeof buf);
        LoRa.endPacket(true);
        const uint32_t ts = micros();
        bool selesai = false;
        while (micros() - ts < 500000UL) {
            const float i = ina.getCurrent_mA();
            const float v = ina.getBusVoltage_V(); if (v < vmin) vmin = v;
            ji_semua += i; n_semua++;
            if (LoRa.txDone()) { selesai = true; break; }
            ji_tx += i; n_tx++;
        }
        if (!selesai) gagal++;
        paket++;
        // celah antarpaket (beginPacket berikutnya) ikut tercuplik di rata-rata fase
        const float i = ina.getCurrent_mA(); ji_semua += i; n_semua++;
    }
    LoRa.idle();
    Serial.printf("TX,arm=%u,tp=%d,cr=4/%u,t_s=%.1f,paket=%lu,txdone_gagal=%lu,i_rata_fase_mA=%.1f,"
                  "i_rata_saat_tx_mA=%.1f,vbus_min_V=%.3f,n=%lu\n",
                  (unsigned)(k + 1), (int)a.tp_dbm, (unsigned)a.cr, t0 / 1000.0, (unsigned long)paket,
                  (unsigned long)gagal, n_semua ? ji_semua / n_semua : 0.0, n_tx ? ji_tx / n_tx : 0.0, vmin,
                  (unsigned long)n_semua);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    const bool ina_ok = ina.init();
    if (ina_ok) {
        ina.setPGain(INA219_PG_320);
        ina.setBusRange(INA219_BRNG_16);
        ina.setADCMode(INA219_BIT_MODE_12);
        ina.setMeasureMode(INA219_CONTINUOUS);
        ina.setShuntSizeInOhms(INA219_SHUNT_OHM);
    }
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    if (!LoRa.begin(LORA_FREQ_HZ)) { Serial.println("FATAL: LoRa"); while (true) delay(1000); }
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW_HZ);
    LoRa.setPreambleLength(LORA_PREAMBLE);
    LoRa.setSyncWord(0x34);
    LoRa.enableCrc();
    Serial.printf("# uji_tx_arm siap, INA219 %s, shunt firmware %.4f ohm; jadwal: IDLE 30 s, lalu tiap arm "
                  "TX 40 s + IDLE 20 s (arm 1 mulai 30 s, arm 2 90 s, arm 3 150 s, arm 4 210 s, berulang)\n",
                  ina_ok ? "ok" : "TIDAK ADA", (double)INA219_SHUNT_OHM);
    fase_idle("IDLE_AWAL", AWAL_MS);
}

void loop() {
    static uint32_t putaran = 0;
    putaran++;
    Serial.printf("# putaran %lu\n", (unsigned long)putaran);
    for (uint8_t k = 0; k < N_ARMS; k++) {
        fase_tx(k);
        fase_idle("IDLE", JEDA_MS);
    }
}
