#include "radio.h"
#include <SPI.h>
#include <LoRa.h>
#include "../config/pin_map.h"
#include "../config/params.h"
#include "../config/protocol.h"
#include "energy_meter.h"

static int s_rssi_terakhir = -200;

bool radio_init() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQ_HZ)) return false;

    // SF dan BW dikunci sebagai variabel kontrol (Naskah 1.5).
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW_HZ);
    LoRa.setPreambleLength(LORA_PREAMBLE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.enableCrc();

    radio_set_arm(0);
    return true;
}

void radio_set_arm(uint8_t arm_idx) {
    if (arm_idx >= N_ARMS) arm_idx = 0;
    LoRa.setTxPower(ARM_CONFIG[arm_idx].tp_dbm);
    LoRa.setCodingRate4(ARM_CONFIG[arm_idx].cr);
}

int radio_scan_beacon() {
    LoRa.receive();
    uint32_t t0 = millis();
    int best = -200;

    while (millis() - t0 < T_SCAN_MS) {
        int size = LoRa.parsePacket();
        if (size >= 2) {
            uint8_t b0 = (uint8_t)LoRa.read();
            uint8_t b1 = (uint8_t)LoRa.read();
            while (LoRa.available()) LoRa.read();  // buang sisa

            if (b0 == BEACON_B0 && b1 == BEACON_B1) {
                int rssi = LoRa.packetRssi();
                if (rssi > best) best = rssi;
            }
        }
    }

    LoRa.idle();
    if (best > -200) s_rssi_terakhir = best;
    return best;
}

bool radio_kendaraan_terdeteksi() {
    return radio_scan_beacon() > RSSI_DETECT;
}

float radio_kirim_paket(const Payload& p) {
    uint8_t buf[PAYLOAD_LEN];
    payload_pack(p, buf);

    radio_set_arm((uint8_t)(p.arm - 1));  // arm pada muatan bernomor 1..4

    energy_begin();
    LoRa.beginPacket();
    LoRa.write(buf, PAYLOAD_LEN);

#if ENERGI_CUPLIK_SAAT_TX
    // Mode asinkron: endPacket kembali segera, radio masih memancar.
    // Inilah yang memungkinkan pencuplikan arus berlangsung SELAMA transmisi
    // (penyimpangan D1). Tanpa mode ini, energi terbaca adalah arus siaga.
    LoRa.endPacket(true);
    while (LoRa.isTransmitting()) {
        energy_sample();
    }
#else
    // Perilaku lama Panduan HIL, disediakan hanya sebagai pembanding pada
    // pengujian Tahap 4.1. Jangan dipakai untuk pengambilan data.
    LoRa.endPacket();
#endif

    return energy_end_mJ();
}

int radio_tunggu_ack() {
    LoRa.receive();
    uint32_t t0 = millis();
    int hasil = -1;

    while (millis() - t0 < ACK_TIMEOUT_MS) {
        int size = LoRa.parsePacket();
        if (size >= 3) {
            uint8_t b0 = (uint8_t)LoRa.read();
            uint8_t b1 = (uint8_t)LoRa.read();
            uint8_t n  = (uint8_t)LoRa.read();
            while (LoRa.available()) LoRa.read();

            if (b0 == ACK_B0 && b1 == ACK_B1) {
                hasil = (int)n;
                s_rssi_terakhir = LoRa.packetRssi();
                break;
            }
        }
    }

    LoRa.idle();
    return hasil;
}

int radio_rssi_terakhir() { return s_rssi_terakhir; }

void radio_tidur() { LoRa.sleep(); }
