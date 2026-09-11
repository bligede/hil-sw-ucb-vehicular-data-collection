#include "payload.h"
#include "../config/params.h"

uint8_t payload_pack(const Payload& p, uint8_t* buf) {
    buf[0] = p.arm;

    buf[1] = (uint8_t)(p.seq >> 8);
    buf[2] = (uint8_t)(p.seq & 0xFF);

    // Energi mJ -> satuan 0,1 mJ. Dibatasi agar tidak melampaui uint16.
    float e10 = p.e_prev_mJ * E_SKALA;
    if (e10 < 0.0f) e10 = 0.0f;
    if (e10 > 65535.0f) e10 = 65535.0f;
    uint16_t e_raw = (uint16_t)(e10 + 0.5f);
    buf[3] = (uint8_t)(e_raw >> 8);
    buf[4] = (uint8_t)(e_raw & 0xFF);

    // Tegangan -> (V - 2,50) * 100, jangkauan 2,50..5,05 V
    float v = (p.vbatt_V - VBATT_OFFSET) * VBATT_SKALA;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    buf[5] = (uint8_t)(v + 0.5f);

    buf[6]  = (uint8_t)((uint16_t)p.ax >> 8);
    buf[7]  = (uint8_t)((uint16_t)p.ax & 0xFF);
    buf[8]  = (uint8_t)((uint16_t)p.ay >> 8);
    buf[9]  = (uint8_t)((uint16_t)p.ay & 0xFF);
    buf[10] = (uint8_t)((uint16_t)p.az >> 8);
    buf[11] = (uint8_t)((uint16_t)p.az & 0xFF);

    return PAYLOAD_LEN;
}

bool payload_unpack(const uint8_t* buf, uint8_t len, Payload& p) {
    if (len != PAYLOAD_LEN) return false;

    p.arm = buf[0];
    p.seq = (uint16_t)((uint16_t)buf[1] << 8 | buf[2]);

    uint16_t e_raw = (uint16_t)((uint16_t)buf[3] << 8 | buf[4]);
    p.e_prev_mJ = (float)e_raw / E_SKALA;

    p.vbatt_V = (float)buf[5] / VBATT_SKALA + VBATT_OFFSET;

    p.ax = (int16_t)((uint16_t)buf[6]  << 8 | buf[7]);
    p.ay = (int16_t)((uint16_t)buf[8]  << 8 | buf[9]);
    p.az = (int16_t)((uint16_t)buf[10] << 8 | buf[11]);

    return true;
}
