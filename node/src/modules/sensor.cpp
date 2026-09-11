#include "sensor.h"
#include <Wire.h>
#include "../config/pin_map.h"

// Register ADXL345 (Analog Devices, 2015)
static const uint8_t REG_DEVID       = 0x00;
static const uint8_t REG_POWER_CTL   = 0x2D;
static const uint8_t REG_DATA_FORMAT = 0x31;
static const uint8_t REG_DATAX0      = 0x32;
static const uint8_t DEVID_ADXL345   = 0xE5;

// Rentang +-16 g pada resolusi penuh: 3,9 mg per LSB (Naskah Tabel 2.3)
static const float MG_PER_LSB = 3.9f;

static bool tulis_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool baca_reg(uint8_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADXL345_ADDR, (int)n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

bool sensor_init() {
    uint8_t id = 0;
    if (!baca_reg(REG_DEVID, &id, 1)) return false;

    // Alamat menjawab tetapi identitasnya bukan ADXL345: biasanya pin SDO tidak
    // tersambung ke GND, atau CS mengambang sehingga modul memakai mode SPI.
    if (id != DEVID_ADXL345) return false;

    if (!tulis_reg(REG_DATA_FORMAT, 0x0B)) return false;  // +-16 g, resolusi penuh
    if (!tulis_reg(REG_POWER_CTL,   0x08)) return false;  // mode ukur
    return true;
}

bool sensor_baca_mg(int16_t& ax, int16_t& ay, int16_t& az) {
    ax = ay = az = 0;

    uint8_t b[6];
    if (!baca_reg(REG_DATAX0, b, 6)) return false;

    // ADXL345 mengeluarkan little-endian, dua byte per sumbu.
    int16_t rx = (int16_t)((uint16_t)b[1] << 8 | b[0]);
    int16_t ry = (int16_t)((uint16_t)b[3] << 8 | b[2]);
    int16_t rz = (int16_t)((uint16_t)b[5] << 8 | b[4]);

    ax = (int16_t)(rx * MG_PER_LSB);
    ay = (int16_t)(ry * MG_PER_LSB);
    az = (int16_t)(rz * MG_PER_LSB);
    return true;
}
