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
#include <esp_system.h>
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

// ---------------------------------------------------------------------------
// Penutupan siklus berbasis posisi paket (gateway-1.5, catatan Pembimbing I
// 1 Nov 2026 B.3; penyimpangan D16).
//
// Sejarah: gateway 1.2 sampai 1.4 menutup siklus yang paket akhirnya hilang
// sesudah ACK_IDLE_MS tanpa paket (800 ms, lalu 1400 ms untuk uji laboratorium).
// Batas diam itu tidak tahu berapa paket yang masih akan datang: dua paket
// hilang berturut-turut di tengah siklus sudah membuat ACK terkirim dini dan
// tidak didengar node. Mekanisme itu dihapus.
//
// Sekarang byte 0 muatan membawa indeks paket di siklus (0..9) dan siklus
// dikenali dari seq_awal = seq - indeks. Paket berindeks 9 langsung di-ACK
// (sesudah ACK_JEDA_MS). Untuk paket terakhir yang diterima berindeks k < 9:
//
//   tenggat = t_terima + (9 - k) * T_int + ACK_JEDA_MS + MARGIN
//   MARGIN  = 100 ms + 2 % dari (9 - k) * T_int
//   T_int   = (t_terakhir - t_pertama) / (indeks_terakhir - indeks_pertama)
//             bila siklus sudah memuat >= 2 paket; bila baru satu, nilai bawaan
//             per arm (T_INT_BAWAAN_MS, terukur pada TAHAP43_RESMI_02: 585 ms
//             Arm 1, 601 ms Arm 3, 609 ms Arm 4).
//
// Bila millis() melewati tenggat dan siklus belum di-ACK, ACK dikirim berisi
// jumlah paket sah siklus itu, tanpa jeda tambahan (ACK_JEDA_MS sudah masuk ke
// tenggat). Tenggat terpanjang terjadi bila hanya paket indeks 0 yang tiba pada
// Arm 4: 9 x 610 + 50 + (100 + 110) = 5.750 ms sesudah paket itu diterima.
//
// Kesesuaian dengan node (ACK_TIMEOUT_MS 2000 ms). Waktu terima di gateway kira-kira
// sama dengan akhir pancaran paket di node. Node selesai memancar paket ke-10 sekitar
// t_terima + (9 - k) * T_nyata, lalu dalam beberapa ms mulai mendengar selama 2000 ms.
// Bila T_int = T_nyata, ACK tenggat mulai dipancar ACK_JEDA_MS + MARGIN sesudah node
// mulai mendengar, yaitu 150 ms (k = 8) sampai 260 ms (k = 0), dan selesai sekitar 62 ms
// kemudian (5 byte, SF8, CR 4/5). Batas kesalahan T_int:
//   - ACK terlalu dini (node belum mendengar) bila T_nyata melebihi T_int lebih dari
//     (150 + 0,02 x (9 - k) x T_int) / (9 - k): sekitar 28 ms per paket pada k = 0,
//     87 ms pada k = 7. Selisih nilai bawaan per arm terhadap ukuran di atas <= 2 ms.
//   - ACK terlalu lambat (node sudah berhenti mendengar) bila T_int melebihi T_nyata
//     lebih dari sekitar (2000 - 62 - 260) / 9 = 186 ms per paket pada k = 0.
// Bila paket berindeks 9 ternyata tiba sesudah ACK tenggat terkirim, paket itu
// ditolak (alasan sesudah_ack) supaya jumlah DATA tercatat tetap sama dengan n pada ACK.
static const uint16_t T_INT_BAWAAN_MS[4] = {585, 585, 600, 610};   // Arm 1..4
#define TENGGAT_MARGIN_TETAP_MS   100UL
#define TENGGAT_MARGIN_PERSEN     2UL

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
//
// BEACON_SENYAP_MS harus lebih panjang daripada tenggat ACK terpanjang (5.750 ms,
// lihat penutupan siklus di atas) ditambah waktu pancar ACK (~62 ms), supaya
// beacon tidak pernah dipancar selama siklus masih menunggu paket yang belum
// datang dan tidak menimpa paket berikutnya. 2000 ms (gateway 1.2 sampai 1.4)
// sudah terlampaui bila tiga paket berturut-turut hilang; 7000 ms (gateway-1.5)
// memberi cadangan sekitar 1,2 s. Akibatnya, sesudah node keluar window, beacon
// baru kembali 7 s sesudah paket terakhir, sehingga deteksi ulang oleh node bisa
// tertunda sampai satu putaran pindai dan tidur IDLE (sekitar 4,2 s).
#ifndef BEACON_SENYAP_SAAT_JENDELA
#define BEACON_SENYAP_SAAT_JENDELA 1
#endif
#define BEACON_SENYAP_MS 7000UL

static uint32_t last_beacon_ms = 0;
static uint32_t pkt_total = 0;
static char     id_window[32] = "BELUM-DISET";
// ID window baru dari perintah W atau Sheets menunggu di sini sampai tidak ada
// siklus ACK yang berjalan. Pergantian ID tidak pernah menyentuh pencacah siklus
// (catatan Pembimbing I, 1 Nov 2026, A.2): mengosongkannya di tengah siklus
// membuat ACK melaporkan jumlah yang salah dan mencemari reward.
static char     id_tertunda[32] = "";

// Pencacah jendela ACK.
//
// Jumlah paket yang DITERIMA tidak dapat dipakai untuk menentukan kapan ACK
// dikirim, karena node menghitung paket yang DIKIRIM. Bila keduanya dipakai
// bersamaan, ACK hanya terkirim ketika kesepuluh paket tiba utuh, sehingga PDR
// yang dilaporkan selalu bernilai 1,0 dan kehilangan paket tidak pernah
// terlihat. Lebih buruk lagi, sisa hitungan terbawa ke jendela berikutnya
// sehingga ACK menjadi tidak sejajar dengan arm yang sedang diuji.
//
// Karena itu penutupan jendela ditentukan oleh POSISI paket di siklus, bukan oleh
// jumlah penerimaan. Sejak gateway-1.5 paket membawa indeksnya (0..9), siklus
// dikenali dari seq_awal = seq - indeks, dan ACK membawa seq_awal sebagai penanda
// siklus. Siklus yang sudah di-ACK tetap diingat (seq_awal dan mask_indeks) untuk
// menolak duplikat dan paket yang tiba sesudah ACK-nya.
static bool     ada_siklus = false;     // sudah pernah ada siklus sejak gateway menyala
static bool     siklus_terbuka = false; // siklus seq_awal belum di-ACK
static uint16_t seq_awal = 0;           // penanda siklus: seq paket berindeks 0
static uint16_t mask_indeks = 0;        // bit k = paket berindeks k sudah diterima
static uint8_t  indeks_pertama = 0;     // indeks dan waktu paket pertama dan terakhir
static uint8_t  indeks_terakhir = 0;    //   yang diterima pada siklus terbuka, untuk T_int
static uint32_t t_pertama = 0;
static uint32_t t_terakhir = 0;
static uint32_t tenggat_ack = 0;        // batas kirim ACK siklus terbuka (millis)
static uint16_t seq_lalu = 0;           // nomor urut paket diterima sebelumnya
static bool     ada_seq_lalu = false;   // sudah ada paket sejak gateway menyala
static uint32_t tolak_total = 0;        // paket ditolak validasi (lihat alasan_tolak)
static uint32_t radio_reinit_total = 0; // radio diinisialisasi ulang karena kacau

// Keadaan terakhir yang ditampilkan pada LCD.
static bool     ada_paket = false;
static int      rssi_akhir = 0;
static float    snr_akhir = 0.0f;
static uint8_t  arm_akhir = 0;
static float    vnode_akhir = 0.0f;
static uint32_t ack_total = 0;
static uint8_t  ack_terakhir = 0;
static uint16_t n_diterima = 0;         // paket sah yang tiba pada siklus terbuka
static uint32_t t_paket_terakhir = 0;   // untuk beacon diam (D18) dan LCD

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

// ACK 5 byte (protocol.h): penanda, jumlah paket, penanda siklus seq_awal.
// Baris CSV: "ACK,ts,id,n,seq_awal,,,,,,,"; kolom ke-4 tetap n seperti gateway
// lama, dibaca olah_siklus.py, ringkas_log.py, dan replay_swucb.py.
static void kirim_ack(uint8_t n, uint16_t penanda_siklus, uint32_t jeda_ms) {
    if (jeda_ms) delay(jeda_ms);
    LoRa.beginPacket();
    LoRa.write(ACK_B0);
    LoRa.write(ACK_B1);
    LoRa.write(n);
    LoRa.write((uint8_t)(penanda_siklus >> 8));
    LoRa.write((uint8_t)(penanda_siklus & 0xFF));
    LoRa.endPacket();
    LoRa.receive();

    indikator_ack();
    ack_total++;
    ack_terakhir = n;
    const uint32_t ts = millis();
    Serial.printf("ACK,%lu,%s,%u,%u,,,,,,,\n",
                  (unsigned long)ts, id_window, (unsigned)n, (unsigned)penanda_siklus);
    sheets_antre_ack(ts, id_window, n);
}

// Kirim ACK siklus terbuka lalu tandai sudah di-ACK. seq_awal dan mask_indeks
// sengaja dibiarkan, untuk menolak duplikat dan paket yang terlambat.
static void tutup_siklus(uint32_t jeda_ms) {
    if (!siklus_terbuka) return;
    kirim_ack((uint8_t)n_diterima, seq_awal, jeda_ms);
    siklus_terbuka = false;
    n_diterima = 0;
}

static void proses_perintah() {
    if (!Serial.available()) return;

    String baris = Serial.readStringUntil('\n');
    baris.trim();
    if (baris.length() == 0) return;

    if (baris.startsWith("W ")) {
        String id = baris.substring(2);
        id.trim();
        id.toCharArray(id_tertunda, sizeof(id_tertunda));
        Serial.printf("# id_window=%s diterima, berlaku pada siklus berikutnya\n", id_tertunda);
    } else if (baris == "S") {
        Serial.printf("# id_window=%s total=%lu jendela_berjalan=%u seq_awal=%u terbuka=%d tolak=%lu radio_reinit=%lu\n",
                      id_window, (unsigned long)pkt_total,
                      (unsigned)n_diterima, (unsigned)seq_awal, siklus_terbuka ? 1 : 0,
                      (unsigned long)tolak_total, (unsigned long)radio_reinit_total);
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

// Inisialisasi radio. Dipanggil saat menyala dan saat radio terdeteksi kacau.
static bool radio_mulai() {
    if (!LoRa.begin(LORA_FREQ_HZ)) return false;
    // Parameter WAJIB identik dengan node, jika tidak paket tidak akan terbaca.
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW_HZ);
    LoRa.setPreambleLength(LORA_PREAMBLE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(17);
    LoRa.enableCrc();
    LoRa.receive();
    return true;
}

// Validasi paket (1 Nov 2026). Rekaman SWEEP_EMAKS_01 memperlihatkan radio
// gateway yang kacau: sekitar 160 "paket" berjarak 5 sampai 40 ms (satu paket
// LoRa SF8 butuh sedikitnya 80 ms) dengan isi mustahil, dan semuanya ikut
// terhitung di ACK sehingga reward node tercemar. Paket yang gagal salah satu
// pemeriksaan di bawah ditolak, dicatat sebagai baris "# TOLAK", dan tidak
// menyentuh pencacah siklus maupun penanda NODE_RESET.
#define JARAK_PAKET_MIN_MS  150UL   // jarak sah terpendek ~200 ms (ACK lalu paket pertama siklus berikut); satu paket LoRa >= 82 ms
#define E_RAW_MAKS          3000U   // 300 mJ; energi terukur paling besar ~115 mJ
#define VBATT_RAW_MAKS      200U    // 4,50 V; baterai Li-ion penuh 4,2 V
#define TOLAK_BERUNTUN_MAKS 5       // penolakan beruntun sebelum radio diinisialisasi ulang

static uint32_t t_paket_sah = 0;     // waktu paket sah terakhir
static bool     ada_paket_sah = false;
static uint32_t tolak_beruntun = 0;

// Sejak gateway-1.5 (B.3) arm diambil dari 4 bit bawah byte 0 dan indeks dari 4 bit
// atas. Pemeriksaan siklus memakai seq_awal = seq - indeks:
//   indeks          indeks di luar 0..9
//   duplikat        indeks yang sama sudah diterima pada siklus yang sama (seq sama)
//   sesudah_ack     siklus paket ini sudah di-ACK (paket datang terlambat)
//   di_luar_siklus  paket milik siklus yang lebih lama dari siklus terakhir, sampai
//                   tiga siklus ke belakang. Nomor urut 0..9 dikecualikan, karena itu
//                   tanda reset agen pada node (NODE_RESET), dan selisih yang lebih
//                   jauh dianggap siklus baru (node reset sementara gateway jauh).
// Paket dengan seq_awal lain yang lolos dianggap membuka siklus baru.
static const char* alasan_tolak(const uint8_t* buf, uint16_t seq) {
    const uint8_t arm    = buf[0] & PAKET_ARM_MASK;
    const uint8_t indeks = buf[0] >> PAKET_INDEKS_GESER;
    const uint16_t e_raw = (uint16_t)((uint16_t)buf[3] << 8 | buf[4]);
    if (arm < 1 || arm > 4) return "arm";
    if (indeks >= ACK_SETIAP_N) return "indeks";
    if (buf[5] > VBATT_RAW_MAKS) return "vbatt";
    if (e_raw > E_RAW_MAKS) return "energi";
    if (ada_paket_sah && millis() - t_paket_sah < JARAK_PAKET_MIN_MS) return "terlalu_rapat";
    if (!ada_siklus) return nullptr;
    const uint16_t awal_paket = (uint16_t)(seq - indeks);
    if (awal_paket == seq_awal) {
        if (mask_indeks & (1u << indeks)) return "duplikat";
        if (!siklus_terbuka) return "sesudah_ack";
        return nullptr;
    }
    const int16_t beda = (int16_t)(uint16_t)(awal_paket - seq_awal);
    if (beda < 0 && beda >= -(int16_t)(3 * ACK_SETIAP_N) && seq >= ACK_SETIAP_N) return "di_luar_siklus";
    return nullptr;
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    indikator_mulai();
    delay(200);

    // Layar lebih dulu supaya kegagalan LoRa tampil tanpa laptop.
    if (lcd_mulai()) {
        lcd_pesan("Gateway LoRa 1.5", "SF8 BW125 433MHz", "Menyiapkan radio...", "");
    } else {
        Serial.println("# LCD tidak terdeteksi, tampilan dinonaktifkan");
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

    if (!radio_mulai()) {
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

    indikator_radio_siap();

    sheets_mulai();
    lcd_pesan("Gateway siap", "Menunggu node...", "", "");

    indikator_uji();
    // Setiap gateway menyala tercatat di log beserta sebabnya, supaya reset
    // gateway di tengah skenario terlihat (A.2). Menurut SOP, skenario yang
    // memuat baris ini di tengah rekaman diulang seluruhnya.
    Serial.printf("# GATEWAY_BOOT sebab=%d penutupan=posisi\n", (int)esp_reset_reason());
    Serial.println("# gateway-1.5 siap. Perintah: 'W <id_window>', 'S', 'L [detik]'");
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
    {
        char baru[sizeof id_window];
        if (sheets_window_baru(baru, sizeof baru)) strlcpy(id_tertunda, baru, sizeof id_tertunda);
    }
    if (!siklus_terbuka && id_tertunda[0] != '\0') {
        strlcpy(id_window, id_tertunda, sizeof id_window);
        id_tertunda[0] = '\0';
        Serial.printf("# id_window=%s berlaku\n", id_window);
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

    // --- tutup siklus pada tenggat berbasis posisi paket ---
    // Bila paket akhir siklus hilang, paket berindeks 9 tidak pernah tiba dan ACK
    // harus dikirim dari tenggat, berisi jumlah yang benar-benar diterima, supaya
    // PDR tetap terhitung apa adanya. ACK_JEDA_MS sudah termasuk di tenggat.
    if (siklus_terbuka && (int32_t)(millis() - tenggat_ack) >= 0) {
        tutup_siklus(0);
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

    const uint32_t t_terima = millis();   // kira-kira akhir pancaran paket di node
    int   rssi = LoRa.packetRssi();
    float snr  = LoRa.packetSnr();

    uint8_t  arm    = buf[0] & PAKET_ARM_MASK;
    uint8_t  indeks = buf[0] >> PAKET_INDEKS_GESER;
    uint16_t seq = (uint16_t)((uint16_t)buf[1] << 8 | buf[2]);
    uint16_t e_raw = (uint16_t)((uint16_t)buf[3] << 8 | buf[4]);
    float e_prev_mJ = (float)e_raw / 10.0f;
    float vbatt = (float)buf[5] / 100.0f + 2.50f;
    int16_t ax = (int16_t)((uint16_t)buf[6]  << 8 | buf[7]);
    int16_t ay = (int16_t)((uint16_t)buf[8]  << 8 | buf[9]);
    int16_t az = (int16_t)((uint16_t)buf[10] << 8 | buf[11]);

    const char* alasan = alasan_tolak(buf, seq);
    if (alasan) {
        tolak_total++;
        // Paket sah yang datang sesudah ACK siklusnya bukan tanda radio kacau,
        // jadi tidak ikut memicu inisialisasi ulang radio.
        if (strcmp(alasan, "sesudah_ack") != 0) tolak_beruntun++;
        Serial.printf("# TOLAK alasan=%s seq=%u arm=%u indeks=%u rssi=%d ts=%lu\n", alasan,
                      (unsigned)seq, (unsigned)arm, (unsigned)indeks, rssi,
                      (unsigned long)millis());
        // Penolakan beruntun berarti radio sendiri yang kacau, bukan satu paket
        // rusak: radio diinisialisasi ulang. Siklus yang sedang berjalan tetap
        // dihitung dari paket sah yang sudah diterima.
        if (tolak_beruntun >= TOLAK_BERUNTUN_MAKS) {
            radio_reinit_total++;
            const bool ok = radio_mulai();
            Serial.printf("# RADIO_REINIT ke=%lu berhasil=%d tolak_total=%lu\n",
                          (unsigned long)radio_reinit_total, ok ? 1 : 0,
                          (unsigned long)tolak_total);
            tolak_beruntun = 0;
        }
        return;
    }
    tolak_beruntun = 0;
    t_paket_sah = millis();
    ada_paket_sah = true;

    // Reset agen pada node (tombol BOOT atau boot dingin) mengembalikan nomor urut ke
    // nol. Nomor urut yang turun ke rentang 0..9 dari nilai di luar rentang itu dan
    // di luar ujung putaran 16 bit ditandai di log, supaya awal tiap skenario terbaca.
    if (ada_seq_lalu && seq < ACK_SETIAP_N && seq_lalu >= ACK_SETIAP_N && seq_lalu < 65526U) {
        Serial.printf("# NODE_RESET seq_sebelum=%u seq_baru=%u id_window=%s\n",
                      (unsigned)seq_lalu, (unsigned)seq, id_window);
    }
    seq_lalu = seq;
    ada_seq_lalu = true;

    // Pencacahan per siklus. Paket dengan penanda siklus lain menutup siklus lama
    // lebih dulu (ACK-nya dikirim tanpa jeda bila belum; node sudah berada di siklus
    // berikutnya, jadi ACK ini hanya untuk log), lalu membuka siklus baru.
    const uint16_t awal_paket = (uint16_t)(seq - indeks);
    if (!ada_siklus || awal_paket != seq_awal) {
        tutup_siklus(0);
        ada_siklus = true;
        siklus_terbuka = true;
        seq_awal = awal_paket;
        mask_indeks = 0;
        n_diterima = 0;
        indeks_pertama = indeks;
        t_pertama = t_terima;
    }
    mask_indeks |= (uint16_t)(1u << indeks);
    indeks_terakhir = indeks;
    t_terakhir = t_terima;
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

    // Paket berindeks 9 menutup siklus seketika (sesudah ACK_JEDA_MS), berapa pun
    // yang benar-benar tiba. Untuk indeks k < 9, tenggat dihitung dari sisa paket
    // yang belum datang (lihat penjelasan "Penutupan siklus berbasis posisi paket" di awal berkas).
    if (indeks >= ACK_SETIAP_N - 1) {
        tutup_siklus(ACK_JEDA_MS);
    } else {
        uint32_t t_int = T_INT_BAWAAN_MS[(arm - 1) & 0x03];
        if (n_diterima >= 2 && indeks_terakhir > indeks_pertama) {
            t_int = (t_terakhir - t_pertama) / (uint32_t)(indeks_terakhir - indeks_pertama);
        }
        const uint32_t sisa_ms = (uint32_t)(ACK_SETIAP_N - 1 - indeks) * t_int;
        const uint32_t margin  = TENGGAT_MARGIN_TETAP_MS + sisa_ms * TENGGAT_MARGIN_PERSEN / 100UL;
        tenggat_ack = t_terima + sisa_ms + ACK_JEDA_MS + margin;
    }
}
