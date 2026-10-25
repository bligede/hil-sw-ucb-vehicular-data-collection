// kalibrasi_ina219.cpp - Tahap 4.1 SOP: verifikasi INA219 terhadap multimeter.
//
// Program uji, BUKAN firmware node. Dipasang dengan env kalibrasi_ina219:
//   pio run -e kalibrasi_ina219 -t upload
//
// Konfigurasi INA219 sama persis dengan modules/energy_meter.cpp (gain 320 mV,
// rentang bus 16 V, konversi 12 bit, shunt 0,1 ohm), supaya yang diverifikasi
// adalah pengaturan yang dipakai pada pengukuran energi sebenarnya.
//
// Keluaran satu baris per detik, CSV:
//   detik,vbus_V,vshunt_mV,i_mA_rata,i_mA_sd,i_mA_min,i_mA_maks,n
// i_mA_rata adalah rata-rata 200 cuplikan, cocok dibandingkan dengan angka
// multimeter DC yang stabil. Perintah serial 'Z' mencatat nilai saat ini
// sebagai nol (beban terputus) dan menguranginya pada baris berikutnya.

#include <Arduino.h>
#include <Wire.h>
#include <INA219_WE.h>
#include "../src/config/pin_map.h"

static INA219_WE ina(INA219_ADDR);
static float nol_mA = 0.0f;

static void ukur(float& rata, float& sd, float& mn, float& mx, float& vsh_mV, float& vbus, int n) {
    double s = 0, s2 = 0;
    mn = 1e9f; mx = -1e9f;
    for (int i = 0; i < n; i++) {
        float x = ina.getCurrent_mA();
        s += x; s2 += (double)x * x;
        if (x < mn) mn = x;
        if (x > mx) mx = x;
        delayMicroseconds(800);
    }
    rata = (float)(s / n);
    double var = s2 / n - (s / n) * (s / n);
    sd = var > 0 ? (float)sqrt(var) : 0.0f;
    vsh_mV = ina.getShuntVoltage_mV();
    vbus = ina.getBusVoltage_V();
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
    if (!ina.init()) {
        while (true) {
            Serial.println("FATAL: INA219 tidak menjawab di 0x40");
            delay(2000);
        }
    }
    ina.setPGain(INA219_PG_320);
    ina.setBusRange(INA219_BRNG_16);
    ina.setADCMode(INA219_BIT_MODE_12);
    ina.setMeasureMode(INA219_CONTINUOUS);
    ina.setShuntSizeInOhms(INA219_SHUNT_OHM);
    Serial.println("# kalibrasi_ina219 siap. 'Z' = catat nol (beban terputus)");
    Serial.println("detik,vbus_V,vshunt_mV,i_mA_rata,i_mA_sd,i_mA_min,i_mA_maks,n");
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == 'Z' || c == 'z') {
            float r, sd, mn, mx, vs, vb;
            ukur(r, sd, mn, mx, vs, vb, 400);
            nol_mA = r;
            Serial.printf("# nol dicatat: %.3f mA\n", nol_mA);
        }
    }
    float r, sd, mn, mx, vs, vb;
    ukur(r, sd, mn, mx, vs, vb, 200);
    Serial.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,200\n", (unsigned long)(millis() / 1000),
                  vb, vs, r - nol_mA, sd, mn - nol_mA, mx - nol_mA);
    delay(700);
}
