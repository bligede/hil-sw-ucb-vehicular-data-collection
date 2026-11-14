// diag_radio.cpp - program diagnosis radio node (14 Nov 2026). Bukan firmware penelitian.
//
// Dipasang dengan env diag_radio:
//   pio run -e diag_radio -t upload
//
// 1. Membaca register versi SX1278 (0x42) lewat SPI sebelum dan sesudah LoRa.begin.
//    Nilai sehat 0x12. 0x00 atau 0xFF berarti SPI tidak tersambung atau RA-02 tidak
//    mendapat 3,3 V.
// 2. Mendengarkan beacon gateway (2 bait 0xBE 0xAC, tiap 1 detik) dan paket lain,
//    lalu mencetak panjang, isi, RSSI, dan SNR.
// 3. Tiap 3 detik mengirim satu paket 12 bait yang lolos validasi gateway (arm 1,
//    nomor urut naik), supaya LCD gateway menambah "Tot".

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "../src/config/pin_map.h"
#include "../src/config/params.h"
#include "../src/modules/payload.h"

static uint8_t baca_reg(uint8_t alamat) {
    digitalWrite(LORA_NSS, LOW);
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(alamat & 0x7F);
    uint8_t v = SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(LORA_NSS, HIGH);
    return v;
}

static bool radio_ok = false;
static uint16_t seq = 0;
static uint32_t t_kirim = 0;

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println("# diag_radio mulai");

    pinMode(LORA_NSS, OUTPUT);
    digitalWrite(LORA_NSS, HIGH);
    pinMode(LORA_RESET, OUTPUT);
    digitalWrite(LORA_RESET, LOW);
    delay(10);
    digitalWrite(LORA_RESET, HIGH);
    delay(10);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    for (int k = 0; k < 3; k++) {
        Serial.printf("# RegVersion mentah ke-%d = 0x%02X (sehat 0x12)\n", k + 1, baca_reg(0x42));
        delay(50);
    }

    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    radio_ok = LoRa.begin(LORA_FREQ_HZ);
    Serial.printf("# LoRa.begin = %s\n", radio_ok ? "BERHASIL" : "GAGAL");
    if (!radio_ok) return;
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW_HZ);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();
    LoRa.setTxPower(14);
    LoRa.receive();
    Serial.println("# mendengarkan; paket uji tiap 3 detik");
}

void loop() {
    if (!radio_ok) {
        delay(3000);
        Serial.printf("# radio GAGAL; RegVersion = 0x%02X\n", baca_reg(0x42));
        return;
    }
    int n = LoRa.parsePacket();
    if (n > 0) {
        Serial.printf("TERIMA %lu ms len=%d isi=", (unsigned long)millis(), n);
        while (LoRa.available()) Serial.printf("%02X", (uint8_t)LoRa.read());
        Serial.printf(" rssi=%d snr=%.1f\n", LoRa.packetRssi(), LoRa.packetSnr());
    }
    if (millis() - t_kirim >= 3000) {
        t_kirim = millis();
        Payload p{};
        p.arm = 1;
        p.seq = ++seq;
        p.e_prev_mJ = 0.0f;
        p.vbatt_V = 4.00f;
        uint8_t buf[12];
        payload_pack(p, buf);
        LoRa.beginPacket();
        LoRa.write(buf, 12);
        LoRa.endPacket();
        LoRa.receive();
        Serial.printf("KIRIM %lu ms seq=%u\n", (unsigned long)millis(), (unsigned)seq);
    }
}
