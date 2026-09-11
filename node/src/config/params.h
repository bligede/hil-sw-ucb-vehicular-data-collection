// params.h — seluruh parameter yang dapat disetel, terkumpul di satu berkas.
//
// SOP Subbab I.2 mensyaratkan nilai yang menyimpang dari Panduan HIL ditempatkan
// sebagai konstanta yang mudah diubah, bukan tersebar di dalam kode, sampai
// Pembimbing I menyetujui penyimpangannya.
//
// SOP gerbang G5: seluruh nilai di berkas ini dibekukan setelah Fase Stabilisasi
// ditutup. Nilainya wajib sama dengan sheet 00-Konfigurasi pada buku kerja.
//
// Setiap baris bertanda D<n> adalah penyimpangan terhadap Panduan HIL.
// Untuk menjalankan nilai asli Panduan (mis. saat menunggu persetujuan),
// definisikan PAKAI_NILAI_PANDUAN sebelum menyertakan berkas ini.
//
// ---------------------------------------------------------------------------
// CAKUPAN PAKAI_NILAI_PANDUAN
//
// Yang IKUT dikembalikan ke nilai Panduan:
//   D2  T_SCAN_MS            durasi pemindaian
//   D3  T_SLEEP_S            durasi tidur node
//   D4  BEACON_SELANG_MS     selang beacon gateway (di protocol.h)
//   D5  BETA                 bobot energi pada reward
//   D7  JEDA_ANTAR_PAKET_MS  jeda antar-paket
//
// Sakelar ini WAJIB disetel pada KEDUA project sekaligus, karena D2 dan D4
// sepasang. Menyetel di satu sisi saja menghasilkan konfigurasi yang tidak
// mewakili rancangan mana pun.
//
// Yang SENGAJA TIDAK ikut, beserta alasannya:
//
//   D1  ENERGI_CUPLIK_SAAT_TX
//       Bukan pilihan penyetelan, melainkan perbaikan kekeliruan pengukuran.
//       Mengembalikannya menghasilkan data yang salah, bukan sekadar berbeda:
//       energi terbaca adalah arus mode siaga dan hampir seragam antar-arm.
//       Sakelarnya berdiri sendiri, dipakai hanya untuk membuktikan selisihnya
//       pada pengujian Tahap 4.1, bukan untuk pengambilan data.
//
//   D9  TIMEOUT_WINDOW_MS, COOLDOWN_MS
//       Diperintahkan Catatan Perbaikan Subbab A.2, yang menurut urutan
//       kewenangan pada SOP berada DI ATAS Panduan HIL. Panduan disusun
//       sebelum catatan tersebut terbit, jadi ketiadaan batas waktu di sana
//       bukan pilihan rancangan melainkan kekosongan yang sudah ditutup.
//
//   D6  Muatan paket 12 byte
//       Tanpa medan energi, E_actual tidak pernah sampai ke berkas log dan
//       analisis Subbab 4.8.1 naskah tidak punya datanya sama sekali.
//       Mengembalikannya membuat seluruh fase pengambilan data sia-sia.
// ---------------------------------------------------------------------------

#pragma once

// ============================================================
// 1. Ruang aksi SW-UCB (Naskah 4.1.1 — tidak berubah)
// ============================================================
#define N_ARMS        4
#define W_SIZE       50    // ukuran sliding window (transmisi)
#define XI            0.5f // parameter eksplorasi

// Arm: {TP dBm, CR (5..8 untuk 4/5..4/8)}
struct ArmConfig { int8_t tp_dbm; uint8_t cr; };
static const ArmConfig ARM_CONFIG[N_ARMS] = {
    {  5, 5 },   // Arm 1 — daya sangat rendah, proteksi galat minimal
    { 10, 5 },   // Arm 2 — transisi, jarak moderat LoS stabil
    { 14, 7 },   // Arm 3 — mengimbangi redaman jarak menjauh
    { 20, 8 }    // Arm 4 — worst-case, NLoS atau kecepatan tinggi
};

// Parameter LoRa yang dikunci sebagai variabel kontrol
#define LORA_FREQ_HZ   433E6
#define LORA_SF        8
#define LORA_BW_HZ     125E3
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE  8

// ============================================================
// 2. Fungsi reward (Catatan Perbaikan A.1 + Dokumen Revisi A.4)
// ============================================================
// PDR dipakai sebagai rasio [0,1], BUKAN persen. Energi dinormalisasi ke [0,1]
// terhadap E_MAKS. Dengan kedua komponen sekala sama, bobot setara masuk akal.
#define ALPHA          1.0f
#ifdef PAKAI_NILAI_PANDUAN
  #define BETA         0.02f  // nilai Panduan HIL 2.7
#else
  #define BETA         1.0f   // D5 — DIKONFIRMASI Pembimbing I, 5 Sep 2026.
                              // Pada 0,02 selisih reward Arm 1 vs Arm 2 di jarak
                              // dekat hanya 0,001, tenggelam dalam varians ukur.
                              // Syarat yang melekat: setelah E_MAKS_mJ diisi hasil
                              // Tahap 4.1, jalankan analisis sensitivitas dR
                              // (SOP butir 4.6) sebelum data lapangan diambil.
#endif

// Energi maksimum per transmisi (Arm 4), penyebut normalisasi.
// WAJIB diganti hasil pengukuran SOP Tahap 4.1 sebelum eksperimen utama.
// Nilai awal di bawah adalah ESTIMASI dari topologi daya pada Panduan Perakitan
// Subbab II.1 (baterai -> INA219 -> MT3608 5 V -> VIN).
#define E_MAKS_mJ      113.3f
#define E_MAKS_TERUKUR 0      // set 1 setelah diisi hasil Tahap 4.1

#define REWARD_TIMEOUT (-1.0f) // penalti bila ACK tidak diterima; konsisten
                               // dengan batas bawah rentang reward [-1, +1]

#define ACK_SETIAP_N   10      // jendela evaluasi ACK (Naskah 4.1)
#define ACK_TIMEOUT_MS 2000UL

// ============================================================
// 3. Deteksi kendaraan (Dokumen Revisi Bagian B)
// ============================================================
// Dua syarat penetapan:
//   Syarat 1: T_SCAN >= selang beacon gateway  -> beacon pasti tertangkap
//   Syarat 2: T_SLEEP + 2*T_SCAN <= jendela kontak terpendek (12 s pada S7)
//             3 s + 2*1,2 s = 5,4 s; margin 2,2x.
//             Matriks skenario memakai lintasan DUA sisi, yaitu dari batas
//             jangkauan di satu sisi node sampai batas jangkauan di sisi
//             lainnya, sama seperti penurunan TIMEOUT_WINDOW_MS di bawah
//             (x_maks 300 m untuk jarak kontak 150 m). Karena itu jarak
//             kontak 50 m pada 8,33 m/s memberi 100/8,33 = 12 s, bukan 6 s.
#ifdef PAKAI_NILAI_PANDUAN
  #define T_SCAN_MS     500UL    // nilai Panduan HIL 2.4
  #define T_SLEEP_S      60ULL   // nilai Panduan HIL 2.5
#else
  #define T_SCAN_MS    1200UL    // D2
  #define T_SLEEP_S       3ULL   // D3
#endif

// Ambang RSSI — WAJIB diganti hasil kalibrasi SOP Tahap 4.5
#define RSSI_DETECT   (-100)     // dBm
#define RSSI_EXIT     (-105)     // dBm
#define RSSI_TERKALIBRASI 0      // set 1 setelah diisi hasil Tahap 4.5

// ============================================================
// 4. FSM (Tabel 4.1 naskah + Catatan Perbaikan A.2)
// ============================================================
#define TIMEOUT_WINDOW_MS 240000UL // D9 — 240 s; T_max terpanjang S3 = 216 s
#define COOLDOWN_MS        60000UL // D9 — jeda sebelum boleh masuk ulang
#define V_BATT_MIN         3.30f   // masuk BATTERY_GUARD
#define V_BATT_PULIH       3.50f   // keluar dari BATTERY_GUARD
#define GUARD_SLEEP_S      3600ULL // tidur panjang saat baterai kritis

// Jeda antar-paket di dalam CONTACT_WINDOW (D7).
// Panduan HIL tidak menetapkan jeda sama sekali, sehingga node memancar hampir
// tanpa henti dan siklus kerja pancar mencapai 71%. Pada 0,5 s siklus kerja
// turun ke 21% dan skenario S7 masih menyisakan dua siklus evaluasi ACK.
#ifdef PAKAI_NILAI_PANDUAN
  #define JEDA_ANTAR_PAKET_MS 0UL     // Panduan HIL tidak mengatur jeda
#else
  #define JEDA_ANTAR_PAKET_MS 500UL   // D7
#endif

// ============================================================
// 5. Pengukuran energi (Dokumen Revisi Bagian C — D1)
// ============================================================
// Arus dicuplik SELAMA transmisi berlangsung, bukan sesudahnya. Mode konversi
// tunggal 12-bit (532 us), bukan rata-rata 128 cuplikan (68 ms) yang lebih
// panjang daripada sebagian waktu udara.
#define ENERGI_CUPLIK_SAAT_TX 1   // 0 = perilaku lama Panduan (untuk pembanding)
#define ENERGI_MAKS_SAMPEL  400   // batas atas cuplikan per transmisi

// ============================================================
// 6. Muatan paket (Dokumen Revisi + Panduan Perakitan — D6)
// ============================================================
#define PAYLOAD_LEN   12
#define VBATT_OFFSET  2.50f  // byte 5 = (V - 2,50) * 100
#define VBATT_SKALA   100.0f
#define E_SKALA       10.0f  // byte 3..4 = energi mJ * 10 (satuan 0,1 mJ)

// ============================================================
// 7. Penanda kompilasi
// ============================================================
#define FW_VERSI      "node-1.0"
#define SERIAL_BAUD   115200
