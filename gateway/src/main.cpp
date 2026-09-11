// main.cpp — mobile gateway (On-Board Unit)
//
// Tugasnya empat: memancarkan beacon, menerima paket, mengirim ACK tiap 10
// paket, dan mencetak baris CSV ke serial untuk direkam laptop.
//
// PENYIMPANGAN D4 terhadap Panduan HIL Subbab 3.1: selang beacon diperpendek
// dari 5.000 ms ke 1.000 ms. Gateway memperoleh daya dari kendaraan dan laptop,
// sehingga memperpendek selang tidak menambah beban energi penelitian sama
// sekali, sedangkan node harus memindai selama minimal satu selang beacon agar
// deteksi terjamin.
//
// ID window diisi lewat serial sebelum tiap lintasan:
//   W <id>     menetapkan ID window, mengosongkan pencacah
//   S          menampilkan status
// SOP Subbab III.4 langkah 2 mensyaratkan ID diumumkan sebelum lintasan dimulai.

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config/pin_map.h"
#include "config/protocol.h"

#define SERIAL_BAUD    115200
#define LORA_FREQ_HZ   433E6
#define LORA_SF        8
#define LORA_BW_HZ     125E3
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE  8
#define PAYLOAD_LEN    12

// BEACON_SELANG_MS didefinisikan di config/protocol.h, bukan di sini, karena
// nilainya harus disepakati bersama node: durasi pemindaian node ditetapkan
// dari selang ini. Menyetel PAKAI_NILAI_PANDUAN wajib dilakukan pada kedua
// project sekaligus.
#define ACK_SETIAP_N     10
#define ACK_JEDA_MS      50UL     // beri waktu node masuk ke mode terima
#define ACK_IDLE_MS      800UL    // batas diam sebelum jendela ditutup paksa

static uint32_t last_beacon_ms = 0;
static uint32_t pkt_total = 0;
static char     id_window[32] = "BELUM-DISET";

// Pencacah jendela ACK.
//
// Jumlah paket yang DITERIMA tidak dapat dipakai untuk menentukan kapan ACK
// dikirim, karena node menghitung paket yang DIKIRIM. Bila keduanya dipakai
// bersamaan, ACK hanya terkirim ketika kesepuluh paket tiba utuh, sehingga PDR
// yang dilaporkan selalu bernilai 1,0 dan kehilangan paket tidak pernah
// terlihat. Lebih buruk lagi, sisa hitungan terbawa ke jendela berikutnya
// sehingga ACK menjadi tidak sejajar dengan arm yang sedang diuji.
//
// Karena itu penutupan jendela ditentukan oleh RENTANG nomor urut, bukan oleh
// jumlah penerimaan. Nomor urut sudah dibawa pada muatan paket, sehingga node
// tidak perlu diubah.
static uint16_t seq_awal = 0;           // nomor urut paket pertama pada jendela
static uint16_t n_diterima = 0;         // paket yang benar-benar tiba
static uint32_t t_paket_terakhir = 0;   // untuk penutupan paksa saat diam

static void cetak_header_csv() {
    Serial.println("jenis,ts_ms,id_window,seq,arm,e_prev_mJ,vbatt,rssi,snr,ax,ay,az");
}

static void kirim_ack(uint8_t n_diterima) {
    delay(ACK_JEDA_MS);
    LoRa.beginPacket();
    LoRa.write(ACK_B0);
    LoRa.write(ACK_B1);
    LoRa.write(n_diterima);
    LoRa.endPacket();
    LoRa.receive();

    Serial.printf("ACK,%lu,%s,%u,,,,,,,,\n",
                  (unsigned long)millis(), id_window, (unsigned)n_diterima);
}

static void proses_perintah() {
    if (!Serial.available()) return;

    String baris = Serial.readStringUntil('\n');
    baris.trim();
    if (baris.length() == 0) return;

    if (baris.startsWith("W ")) {
        String id = baris.substring(2);
        id.trim();
        id.toCharArray(id_window, sizeof(id_window));
        n_diterima = 0;
        Serial.printf("# id_window=%s pencacah dikosongkan\n", id_window);
    } else if (baris == "S") {
        Serial.printf("# id_window=%s total=%lu jendela_berjalan=%u seq_awal=%u\n",
                      id_window, (unsigned long)pkt_total,
                      (unsigned)n_diterima, (unsigned)seq_awal);
    } else {
        Serial.println("# perintah: 'W <id>' set window, 'S' status");
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQ_HZ)) {
        while (true) {
            Serial.println("FATAL: LoRa gagal inisialisasi");
            delay(2000);
        }
    }

    // Parameter WAJIB identik dengan node, jika tidak paket tidak akan terbaca.
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW_HZ);
    LoRa.setPreambleLength(LORA_PREAMBLE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(17);
    LoRa.enableCrc();
    LoRa.receive();

    Serial.println("# gateway-1.0 siap. Perintah: 'W <id_window>', 'S'");
    cetak_header_csv();
}

void loop() {
    proses_perintah();

    // --- beacon periodik ---
    if (millis() - last_beacon_ms >= BEACON_SELANG_MS) {
        LoRa.beginPacket();
        LoRa.write(BEACON_B0);
        LoRa.write(BEACON_B1);
        LoRa.endPacket();
        LoRa.receive();
        last_beacon_ms = millis();
    }

    // --- tutup jendela yang menggantung ---
    // Bila paket terakhir pada satu jendela hilang, rentang nomor urut tidak
    // pernah mencapai sepuluh dan ACK tidak akan pernah terkirim. Node akan
    // kehabisan waktu lalu menerima penalti, padahal sembilan paketnya tiba.
    // Penutupan paksa ini mengirim ACK berisi jumlah yang benar-benar diterima,
    // sehingga PDR tetap terhitung apa adanya.
    if (n_diterima > 0 && millis() - t_paket_terakhir >= ACK_IDLE_MS) {
        kirim_ack((uint8_t)n_diterima);
        n_diterima = 0;
    }

    // --- terima paket dari node ---
    int size = LoRa.parsePacket();
    if (size != PAYLOAD_LEN) {
        // Panjang lain berarti beacon pantulan atau derau; dibuang tanpa dicatat.
        if (size > 0) while (LoRa.available()) LoRa.read();
        return;
    }

    uint8_t buf[PAYLOAD_LEN];
    for (int i = 0; i < PAYLOAD_LEN && LoRa.available(); i++) {
        buf[i] = (uint8_t)LoRa.read();
    }

    int   rssi = LoRa.packetRssi();
    float snr  = LoRa.packetSnr();

    uint8_t  arm = buf[0];
    uint16_t seq = (uint16_t)((uint16_t)buf[1] << 8 | buf[2]);
    uint16_t e_raw = (uint16_t)((uint16_t)buf[3] << 8 | buf[4]);
    float e_prev_mJ = (float)e_raw / 10.0f;
    float vbatt = (float)buf[5] / 100.0f + 2.50f;
    int16_t ax = (int16_t)((uint16_t)buf[6]  << 8 | buf[7]);
    int16_t ay = (int16_t)((uint16_t)buf[8]  << 8 | buf[9]);
    int16_t az = (int16_t)((uint16_t)buf[10] << 8 | buf[11]);

    if (n_diterima == 0) seq_awal = seq;   // paket pertama pada jendela ini
    n_diterima++;
    pkt_total++;
    t_paket_terakhir = millis();

    // Nomor urut dikirim node, sehingga kehilangan paket dapat direkonstruksi
    // dari lompatan seq sekalipun paketnya tidak pernah sampai (SOP III.1).
    Serial.printf("DATA,%lu,%s,%u,%u,%.1f,%.2f,%d,%.1f,%d,%d,%d\n",
                  (unsigned long)millis(), id_window,
                  (unsigned)seq, (unsigned)arm, e_prev_mJ, vbatt,
                  rssi, snr, ax, ay, az);

    // Jendela ditutup begitu rentang nomor urut mencapai sepuluh paket,
    // berapa pun yang benar-benar tiba. Pengurangan tak bertanda tetap benar
    // walau nomor urut sudah berputar melewati 65535.
    if ((uint16_t)(seq - seq_awal) >= (ACK_SETIAP_N - 1)) {
        kirim_ack((uint8_t)n_diterima);
        n_diterima = 0;
    }
}
