// main.cpp - mobile gateway (On-Board Unit)
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
//
// Build esp32dev_sheets (-DUNGGAH_SHEETS) juga mengirim setiap baris DATA dan
// ACK ke Google Sheets lewat hotspot, dan membaca ID window dari sel B1 lembar
// KONTROL sehingga laptop tidak perlu dibawa. Serial tetap mencetak semuanya
// sebagai cadangan. Lihat modules/unggah_sheets.h dan apps_script/README.md.

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config/pin_map.h"
#include "config/protocol.h"
#include "modules/unggah_sheets.h"
#include "modules/indikator_led.h"
#include "modules/tampilan_lcd.h"

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

// Beacon diam selama window kontak berjalan (penyimpangan D18, 23 Okt 2026).
//
// Radio LoRa half-duplex: selama gateway memancar beacon (sekitar 52 ms), paket
// node yang tiba pada saat itu hilang, dan node mengirim tiap 0,5 detik tanpa
// tahu jadwal beacon. Pada uji meja 1 meter, 319 paket yang diterima tidak
// memuat satu pun paket pada jendela sekitar 100 ms di tiap siklus 1052 ms,
// dan PDR hanya 0,87 (Arm 1) sampai 0,81 (Arm 4) pada kanal yang bersih. Arm
// berpaket panjang (CR 4/7 dan 4/8) kehilangan paling banyak, jadi PDR
// terbiaskan oleh waktu udara, bukan oleh kanal.
//
// Node hanya memakai beacon untuk mendeteksi kendaraan saat IDLE; selama window
// ia memutuskan keluar dari RSSI ACK dan batas waktu, sehingga beacon tidak
// dibutuhkan. Beacon dilanjutkan bila tidak ada paket selama BEACON_SENYAP_MS.
// Set 0 untuk mengembalikan perilaku lama (beacon terus-menerus).
#ifndef BEACON_SENYAP_SAAT_JENDELA
#define BEACON_SENYAP_SAAT_JENDELA 1
#endif
#define BEACON_SENYAP_MS 2000UL

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

// Keadaan terakhir yang ditampilkan pada LCD.
static bool     ada_paket = false;
static int      rssi_akhir = 0;
static float    snr_akhir = 0.0f;
static uint8_t  arm_akhir = 0;
static float    vnode_akhir = 0.0f;
static uint32_t ack_total = 0;
static uint8_t  ack_terakhir = 0;
static uint16_t n_diterima = 0;         // paket yang benar-benar tiba
static uint32_t t_paket_terakhir = 0;   // untuk penutupan paksa saat diam

static void perbarui_indikator() {
    static uint32_t t_led = 0;
    const uint32_t now = millis();
    if (now - t_led < 20) return;
    t_led = now;
    const bool window_siap = id_window[0] != '\0' &&
                             strcmp(id_window, "BELUM-DISET") != 0;
    indikator_perbarui(window_siap, sheets_keadaan());
}

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

    indikator_ack();
    ack_total++;
    ack_terakhir = n_diterima;
    const uint32_t ts = millis();
    Serial.printf("ACK,%lu,%s,%u,,,,,,,,\n",
                  (unsigned long)ts, id_window, (unsigned)n_diterima);
    sheets_antre_ack(ts, id_window, n_diterima);
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
        char st[200];
        sheets_status(st, sizeof st);
        Serial.printf("# %s\n", st);
        Serial.printf("# lcd alamat=0x%02X\n", (unsigned)lcd_alamat());
        char led[180];
        indikator_status(led, sizeof led);
        Serial.printf("# %s\n", led);
    } else if (baris == "L" || baris.startsWith("L ")) {
        uint32_t durasi_ms = 1200;
        if (baris.length() > 1) {
            const char* arg = baris.c_str() + 2;
            char* akhir = nullptr;
            const unsigned long detik = strtoul(arg, &akhir, 10);
            if (akhir == arg || *akhir != '\0' || detik > 60) {
                Serial.println("# uji biled: 'L' cepat, 'L 20' selama 20 detik, 'L 0' selesai; rentang 0..60");
                return;
            }
            durasi_ms = detik * 1000UL;
        }
        indikator_uji(durasi_ms);
        Serial.printf("# uji biled %lu ms: LoRa merah/hijau, Sheets merah/hijau; penerimaan tetap berjalan\n",
                      (unsigned long)durasi_ms);
    } else {
        Serial.println("# perintah: 'W <id>' set window, 'S' status, 'L [detik]' uji biled");
    }
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    indikator_mulai();
    delay(200);

    // Layar lebih dulu supaya kegagalan LoRa tampil tanpa laptop.
    if (lcd_mulai()) {
        lcd_pesan("Gateway LoRa 1.1", "SF8 BW125 433MHz", "Menyiapkan radio...", "");
    } else {
        Serial.println("# LCD tidak terdeteksi, tampilan dinonaktifkan");
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQ_HZ)) {
        lcd_pesan("FATAL", "LoRa gagal init", "Cek 3V3 dan GND", "NSS RST MOSI MISO");
        indikator_radio_gagal();
        uint32_t t_fatal = millis() - 2000UL;
        while (true) {
            perbarui_indikator();
            if (millis() - t_fatal >= 2000UL) {
                Serial.println("FATAL: LoRa gagal inisialisasi");
                t_fatal = millis();
            }
            delay(5);  // beri waktu scheduler; hanya pada kondisi FATAL
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
    indikator_radio_siap();

    sheets_mulai();
    lcd_pesan("Gateway siap", "Menunggu node...", "", "");

    indikator_uji();
    Serial.println("# gateway-1.1 siap. Perintah: 'W <id_window>', 'S', 'L [detik]'");
    cetak_header_csv();
}

static void perbarui_lcd() {
    char ringkas[12];
    sheets_ringkas(ringkas, sizeof ringkas);
    TampilanGateway t;
    t.id_window = id_window;
    t.n_diterima = n_diterima;
    t.n_per_jendela = ACK_SETIAP_N;
    t.pkt_total = pkt_total;
    t.ada_paket = ada_paket;
    t.rssi = rssi_akhir;
    t.snr = snr_akhir;
    t.arm = arm_akhir;
    t.umur_paket_s = ada_paket ? (millis() - t_paket_terakhir) / 1000UL : 0;
    t.vnode = vnode_akhir;
    t.ack_total = ack_total;
    t.ack_terakhir = ack_terakhir;
    t.ringkas_sheets = ringkas;
    lcd_perbarui(t);
}

void loop() {
    perbarui_indikator();
    proses_perintah();
    perbarui_lcd();

    // ID window dari sel B1 lembar KONTROL. Diterapkan hanya saat tidak ada
    // jendela ACK berjalan, supaya pencacah tidak terpotong di tengah siklus.
    if (n_diterima == 0) {
        char baru[sizeof id_window];
        if (sheets_window_baru(baru, sizeof baru)) {
            strlcpy(id_window, baru, sizeof id_window);
            Serial.printf("# id_window=%s (dari Sheets) pencacah dikosongkan\n", id_window);
        }
    }

    // --- beacon periodik ---
    const bool jendela_aktif = BEACON_SENYAP_SAAT_JENDELA && pkt_total > 0 &&
                               (millis() - t_paket_terakhir) < BEACON_SENYAP_MS;
    if (!jendela_aktif && millis() - last_beacon_ms >= BEACON_SELANG_MS) {
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
    ada_paket = true;
    rssi_akhir = rssi;
    snr_akhir = snr;
    arm_akhir = arm;
    vnode_akhir = vbatt;
    t_paket_terakhir = millis();
    indikator_data();

    // Nomor urut dikirim node, sehingga kehilangan paket dapat direkonstruksi
    // dari lompatan seq sekalipun paketnya tidak pernah sampai (SOP III.1).
    const uint32_t ts = millis();
    Serial.printf("DATA,%lu,%s,%u,%u,%.1f,%.2f,%d,%.1f,%d,%d,%d\n",
                  (unsigned long)ts, id_window,
                  (unsigned)seq, (unsigned)arm, e_prev_mJ, vbatt,
                  rssi, snr, ax, ay, az);
    sheets_antre_data(ts, id_window, seq, arm, e_prev_mJ, vbatt, rssi, snr, ax, ay, az);

    // Jendela ditutup begitu rentang nomor urut mencapai sepuluh paket,
    // berapa pun yang benar-benar tiba. Pengurangan tak bertanda tetap benar
    // walau nomor urut sudah berputar melewati 65535.
    if ((uint16_t)(seq - seq_awal) >= (ACK_SETIAP_N - 1)) {
        kirim_ack((uint8_t)n_diterima);
        n_diterima = 0;
    }
}
