// uji_catu_spi.cpp - program uji gateway: catu 5 V, rail 3,3 V saat TX, dan keandalan SPI
// (arahan Pembimbing I 15 Nov 2026 butir 6, tiga syarat 9 Nov).
//
// Bukan firmware gateway. Sesudah uji, firmware 1.4 dipasang lagi dari
// data/build/gateway-1.4_firmware.bin dan diperiksa dengan verify_flash.
//
// Hasil tampil di LCD, jadi gateway dapat dicatu charger HP tanpa laptop:
//   baris 1  fase (IDLE/TX), detik dalam fase, nomor putaran
//   baris 2  jumlah baca SPI kumulatif (ribuan) dan jumlah baca SALAH
//   baris 3  paket TX kumulatif, init radio gagal
//   baris 4  fase sebelumnya: baca SPI dan salah pada fase itu
// Fase bergantian, masing-masing 20 s:
//   IDLE  radio standby; SPI dibaca terus: RegVersion (0x42, harus 0x12) dan uji tulis-baca
//         RegFifoAddrPtr (0x0D) dengan pola berganti.
//   TX    paket 12 bait dikirim beruntun pada 17 dBm (daya ACK/beacon gateway), SF8, BW 125 kHz;
//         selama tiap transmisi RegVersion dibaca terus. Sync word 0x34 supaya node tidak menerima.
// Operator mengukur dengan Fluke: VIN (5 V) dan 3V3 terhadap GND di kedua fase.
// Serial (bila tersambung) mencetak satu baris CSV per fase.
//
//   pio run -e uji_catu_spi -t upload

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "../src/config/pin_map.h"
#include "../src/modules/tampilan_lcd.h"

static const uint32_t FASE_MS = 20000;
static const SPISettings SPI_SET(8E6, MSBFIRST, SPI_MODE0);   // sama dengan pustaka LoRa

static uint32_t baca_total = 0, salah_total = 0, tx_total = 0, init_gagal = 0;

static uint8_t rd(uint8_t a) {
    SPI.beginTransaction(SPI_SET);
    digitalWrite(LORA_NSS, LOW);
    SPI.transfer(a & 0x7F);
    uint8_t v = SPI.transfer(0);
    digitalWrite(LORA_NSS, HIGH);
    SPI.endTransaction();
    return v;
}

static void wr(uint8_t a, uint8_t v) {
    SPI.beginTransaction(SPI_SET);
    digitalWrite(LORA_NSS, LOW);
    SPI.transfer(a | 0x80);
    SPI.transfer(v);
    digitalWrite(LORA_NSS, HIGH);
    SPI.endTransaction();
}

// TxDone dibaca langsung dari RegIrqFlags (0x12, bit 0x08) lalu dihapus; pustaka LoRa versi
// gateway tidak punya txDone(), dan isTransmitting() bisa false sesaat sesudah endPacket(true).
static bool tx_selesai() {
    if (rd(0x12) & 0x08) { wr(0x12, 0x08); return true; }
    return false;
}

static void tampil(const char* fase, uint32_t detik, uint32_t putaran,
                   uint32_t baca_lalu, uint32_t salah_lalu) {
    char b1[21], b2[21], b3[21], b4[21];
    snprintf(b1, sizeof b1, "%s %2lus put %lu", fase, (unsigned long)detik, (unsigned long)putaran);
    snprintf(b2, sizeof b2, "SPI %luk slh %lu", (unsigned long)(baca_total / 1000),
             (unsigned long)salah_total);
    snprintf(b3, sizeof b3, "TX %lu initgg %lu", (unsigned long)tx_total, (unsigned long)init_gagal);
    snprintf(b4, sizeof b4, "lalu %luk slh %lu", (unsigned long)(baca_lalu / 1000),
             (unsigned long)salah_lalu);
    lcd_pesan(b1, b2, b3, b4);
}

static void radio_siap() {
    while (!LoRa.begin(433E6)) {
        init_gagal++;
        char b3[21];
        snprintf(b3, sizeof b3, "init gagal %lu", (unsigned long)init_gagal);
        lcd_pesan("UJI CATU+SPI GW", "LoRa gagal init", b3, "coba lagi 1 s");
        Serial.printf("# LoRa gagal init ke-%lu\n", (unsigned long)init_gagal);
        delay(1000);
    }
    LoRa.setSpreadingFactor(8);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x34);
    LoRa.setTxPower(17);
    LoRa.enableCrc();
    LoRa.idle();
}

void setup() {
    Serial.begin(115200);
    lcd_mulai();
    lcd_pesan("UJI CATU+SPI GW", "menyiapkan radio", "", "");
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    radio_siap();
    Serial.printf("# uji_catu_spi siap, RegVersion=0x%02X, init gagal sebelum siap=%lu\n",
                  rd(0x42), (unsigned long)init_gagal);
    Serial.println("putaran,fase,baca_spi,salah_spi,paket_tx,init_gagal_total");
}

void loop() {
    static uint32_t putaran = 0;
    static uint32_t baca_lalu = 0, salah_lalu = 0;
    putaran++;

    // FASE IDLE
    LoRa.idle();
    uint32_t t0 = millis(), t_lcd = 0, baca = 0, salah = 0;
    uint8_t pola = 0;
    while (millis() - t0 < FASE_MS) {
        for (int k = 0; k < 200; k++) {
            if (rd(0x42) != 0x12) salah++;
            pola = (uint8_t)(pola * 37 + 11);
            wr(0x0D, pola);
            if (rd(0x0D) != pola) salah++;
            baca += 2;
        }
        if (millis() - t_lcd >= 1000) {
            t_lcd = millis();
            baca_total += baca; salah_total += salah;
            tampil("IDLE", (millis() - t0) / 1000, putaran, baca_lalu, salah_lalu);
            baca_total -= baca; salah_total -= salah;
        }
    }
    baca_total += baca; salah_total += salah;
    Serial.printf("%lu,IDLE,%lu,%lu,0,%lu\n", (unsigned long)putaran, (unsigned long)baca,
                  (unsigned long)salah, (unsigned long)init_gagal);
    baca_lalu = baca; salah_lalu = salah;

    // FASE TX
    t0 = millis(); t_lcd = 0; baca = 0; salah = 0;
    uint32_t paket = 0;
    const uint8_t buf[12] = {0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    while (millis() - t0 < FASE_MS) {
        LoRa.beginPacket();
        LoRa.write(buf, sizeof buf);
        LoRa.endPacket(true);
        uint32_t ts = micros();
        while (!tx_selesai() && micros() - ts < 500000UL) {
            if (rd(0x42) != 0x12) salah++;
            baca++;
        }
        paket++;
        if (millis() - t_lcd >= 1000) {
            t_lcd = millis();
            baca_total += baca; salah_total += salah; tx_total += paket;
            tampil("TX  ", (millis() - t0) / 1000, putaran, baca_lalu, salah_lalu);
            baca_total -= baca; salah_total -= salah; tx_total -= paket;
        }
    }
    LoRa.idle();
    baca_total += baca; salah_total += salah; tx_total += paket;
    Serial.printf("%lu,TX,%lu,%lu,%lu,%lu\n", (unsigned long)putaran, (unsigned long)baca,
                  (unsigned long)salah, (unsigned long)paket, (unsigned long)init_gagal);
    baca_lalu = baca; salah_lalu = salah;
}
