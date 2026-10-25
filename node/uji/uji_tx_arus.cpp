// uji_tx_arus.cpp - program uji: apakah pencuplikan INA219 selama transmisi LoRa berjalan?
//
// Bukan firmware node. Mengulang urutan radio_kirim_paket() untuk tiap arm dan
// mencetak: berapa kali loop isTransmitting() berjalan, lama transmisi, jumlah
// cuplikan INA219, arus rata-rata dan puncak selama transmisi.
//
//   pio run -e uji_tx_arus -t upload
//
// Keluaran CSV: arm,tp_dbm,cr,iterasi,durasi_ms,sampel,i_rata_mA,i_puncak_mA,i_idle_mA

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"

static INA219_WE ina(INA219_ADDR);
static const uint8_t TP[4] = {5, 10, 14, 20};
static const uint8_t CR[4] = {5, 5, 7, 8};

void setup() {
    Serial.begin(115200);
    delay(1500);
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    if (!ina.init()) { Serial.println("FATAL: INA219"); while (true) delay(1000); }
    ina.setPGain(INA219_PG_320);
    ina.setBusRange(INA219_BRNG_16);
    ina.setADCMode(INA219_BIT_MODE_12);
    ina.setMeasureMode(INA219_CONTINUOUS);
    ina.setShuntSizeInOhms(INA219_SHUNT_OHM);

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    if (!LoRa.begin(433E6)) { Serial.println("FATAL: LoRa"); while (true) delay(1000); }
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();
    Serial.println("# uji_tx_arus siap");
    Serial.println("arm,tp_dbm,cr,iterasi,durasi_ms,sampel,i_rata_mA,i_puncak_mA,i_idle_mA");
}

// Membaca satu register SX127x langsung lewat SPI (sama dengan LoRa::readRegister).
static uint8_t rd(uint8_t a) {
    SPI.beginTransaction(SPISettings(8E6, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_NSS, LOW);
    SPI.transfer(a & 0x7f);
    uint8_t v = SPI.transfer(0);
    digitalWrite(LORA_NSS, HIGH);
    SPI.endTransaction();
    return v;
}

// Jejak register selama dan sesudah transmisi: RegOpMode (0x01) dan RegIrqFlags (0x12).
static void jejak(uint8_t tp) {
    LoRa.beginPacket();
    uint8_t buf[12] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    LoRa.write(buf, 12);
    uint8_t sebelum = rd(0x01);
    LoRa.endPacket(true);
    uint32_t t0 = micros();
    Serial.printf("# jejak TP=%d: opmode sebelum=0x%02X", tp, sebelum);
    const uint32_t cek_ms[] = {0, 2, 10, 30, 60, 100, 150, 250};
    uint8_t op[8], irq[8]; uint32_t at[8];
    for (int i = 0; i < 8; i++) {
        while ((micros() - t0) < cek_ms[i] * 1000UL) {}
        op[i] = rd(0x01); irq[i] = rd(0x12); at[i] = micros() - t0;
    }
    for (int i = 0; i < 8; i++)
        Serial.printf(" | %lums op=0x%02X irq=0x%02X", (unsigned long)(at[i] / 1000), op[i], irq[i]);
    Serial.printf(" | isTransmitting=%d\n", (int)LoRa.isTransmitting());
}

void loop() {
    static bool jejak_sekali = false;
    if (!jejak_sekali) {
        jejak_sekali = true;
        delay(500);
        for (int a = 0; a < 4; a++) {
            LoRa.setTxPower(TP[a]);
            LoRa.setCodingRate4(CR[a]);
            jejak(TP[a]);
            delay(400);
        }
    }
    uint8_t buf[12] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int a = 0; a < 4; a++) {
        LoRa.setTxPower(TP[a]);
        LoRa.setCodingRate4(CR[a]);
        for (int k = 0; k < 3; k++) {
            delay(300);
            float idle = ina.getCurrent_mA();
            LoRa.beginPacket();
            LoRa.write(buf, 12);
            LoRa.endPacket(true);
            uint32_t t0 = micros();
            uint32_t iter = 0, n = 0;
            double jumlah = 0;
            float puncak = -1e9f;
            while (LoRa.isTransmitting()) {
                iter++;
                float x = ina.getCurrent_mA();
                n++;
                jumlah += x;
                if (x > puncak) puncak = x;
            }
            uint32_t dur = micros() - t0;
            Serial.printf("%d,%d,%d,%lu,%.1f,%lu,%.1f,%.1f,%.1f\n", a + 1, TP[a], CR[a],
                          (unsigned long)iter, dur / 1000.0f, (unsigned long)n,
                          n ? (float)(jumlah / n) : 0.0f, n ? puncak : 0.0f, idle);
        }
    }
    delay(1500);
}
