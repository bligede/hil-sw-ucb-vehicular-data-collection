// uji_vin_tx.cpp - program uji: tegangan VIN ESP32 saat transmisi Arm 4 lawan saat idle
// (arahan Pembimbing I 15 Nov 2026 butir 1 / D19).
//
// Bukan firmware node. Node dicatu baterai lewat rantai TP4056 -> INA219 -> J5019, tanpa USB.
// Fluke 15B+ terlalu lambat untuk satu paket (~0,1 s), jadi program ini bergantian:
//   FASE IDLE  20 s  radio standby, CPU menunggu
//   FASE TX    20 s  paket Arm 4 (20 dBm, CR 4/8, SF8, BW 125 kHz, 12 bait) dikirim
//                    beruntun tanpa jeda, sehingga PA aktif hampir sepanjang fase
// Operator membaca VIN (probe di VIN dan GND ESP32) di kedua fase. Angka yang lebih rendah
// adalah fase TX. Tegangan fase TX di sini adalah kasus terburuk: PA bekerja hampir
// terus-menerus, sedangkan paket sebenarnya hanya sekitar 0,1 s.
//
// Sync word 0x34 (bukan 0x12) supaya gateway tidak menerima paket uji ini.
// Lewat USB (bila dicolok) dicetak arus INA219 rata-rata tiap fase, hanya sebagai catatan;
// angka VIN resmi tetap dibaca dengan Fluke saat node TIDAK dicolok USB.
//
//   pio run -e uji_vin_tx -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"

static INA219_WE ina(INA219_ADDR);
static const uint32_t FASE_MS = 20000;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    bool ina_ok = ina.init();
    if (ina_ok) {
        ina.setPGain(INA219_PG_320);
        ina.setBusRange(INA219_BRNG_16);
        ina.setADCMode(INA219_BIT_MODE_12);
        ina.setMeasureMode(INA219_CONTINUOUS);
        ina.setShuntSizeInOhms(INA219_SHUNT_OHM);
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    if (!LoRa.begin(433E6)) { Serial.println("FATAL: LoRa"); while (true) delay(1000); }
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x34);
    LoRa.enableCrc();
    LoRa.setTxPower(20);
    LoRa.setCodingRate4(8);
    LoRa.idle();
    Serial.printf("# uji_vin_tx siap, INA219 %s; fase IDLE dan TX masing-masing %lu s\n",
                  ina_ok ? "ok" : "TIDAK ADA", (unsigned long)(FASE_MS / 1000));
    Serial.println("fase,mulai_ms,paket,i_rata_mA,vbus_V");
}

void loop() {
    static uint32_t siklus = 0;
    const uint8_t buf[12] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // FASE IDLE
    uint32_t t0 = millis();
    double ji = 0, jv = 0; uint32_t n = 0;
    LoRa.idle();
    while (millis() - t0 < FASE_MS) {
        ji += ina.getCurrent_mA(); jv += ina.getBusVoltage_V(); n++;
        delay(50);
    }
    Serial.printf("IDLE,%lu,0,%.1f,%.3f\n", (unsigned long)t0, n ? ji / n : 0.0, n ? jv / n : 0.0);

    // FASE TX
    t0 = millis();
    ji = 0; jv = 0; n = 0;
    uint32_t paket = 0;
    while (millis() - t0 < FASE_MS) {
        LoRa.beginPacket();
        LoRa.write(buf, sizeof(buf));
        LoRa.endPacket(true);
        while (LoRa.isTransmitting()) {
            ji += ina.getCurrent_mA(); jv += ina.getBusVoltage_V(); n++;
        }
        paket++;
    }
    LoRa.idle();
    Serial.printf("TX,%lu,%lu,%.1f,%.3f\n", (unsigned long)t0, (unsigned long)paket,
                  n ? ji / n : 0.0, n ? jv / n : 0.0);
    siklus++;
}
