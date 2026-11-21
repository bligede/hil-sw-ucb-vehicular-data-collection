// params.h: seluruh parameter yang dapat disetel, terkumpul di satu berkas.
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
// 1. Ruang aksi SW-UCB (Naskah 4.1.1, tidak berubah)
// ============================================================
#define N_ARMS        4
#define W_SIZE       50    // panjang sliding window: 50 keputusan terakhir, satu keputusan = satu siklus ACK, satu window untuk semua arm
// Parameter eksplorasi. Dipilih dengan sapuan xi dan W yang aturannya dikunci sebelum
// sapuan dijalankan (arahan artikel 2 Nov 2026 butir A.1), disetujui Pembimbing I pada
// 4 Nov 2026: xi = 0,05 dan W = 50. Nilai ini WAJIB sama dengan konstanta XI pada
// tools/prevalidasi_swucb.py dan dengan nilai harapan uji unit K1 sampai K7.
// Batas regret Garivier dan Moulines berlaku untuk xi > 1/2, sehingga tidak berlaku
// pada nilai ini; pemilihannya empiris.
#define XI            0.05f

// Arm: {TP dBm, CR (5..8 untuk 4/5..4/8)}
struct ArmConfig { int8_t tp_dbm; uint8_t cr; };
static const ArmConfig ARM_CONFIG[N_ARMS] = {
    {  5, 5 },   // Arm 1: daya sangat rendah, proteksi galat minimal
    { 10, 5 },   // Arm 2: transisi, jarak moderat LoS stabil
    { 14, 7 },   // Arm 3: mengimbangi redaman jarak menjauh
    { 20, 8 }    // Arm 4: worst-case, NLoS atau kecepatan tinggi
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
  #define BETA         1.0f   // D5: DIKONFIRMASI Pembimbing I, 5 Sep 2026.
                              // Pada 0,02 selisih reward Arm 1 vs Arm 2 di jarak
                              // dekat hanya 0,001, tenggelam dalam varians ukur.
                              // Syarat yang melekat: setelah E_MAKS_mJ diisi hasil
                              // Tahap 4.1, jalankan analisis sensitivitas dR
                              // (SOP butir 4.6) sebelum data lapangan diambil.
#endif

// Energi maksimum per transmisi (Arm 4), penyebut normalisasi.
// Rantai catu (D19 final, disetujui Pembimbing I 19 Nov 2026): sel 18650 (SEL-B1) -> TP4056 berproteksi
// (DW01 + FS8205) -> INA219 (modul cadangan) -> J5019 (penaik tegangan) -> saklar -> VIN ESP32.
// Panduan Perakitan lama menulis MT3608; yang terpasang sejak awal adalah J5019.
// Nilai di bawah: sweep dingin butir 5 (arahan Pembimbing I 15 Nov 2026) pada rakitan beku 20 Nov 2026,
// 21 Nov 2026 16:30-16:47, median Arm 4 80,500 mJ (n 341, p10-p90 80,33-80,62) dibulatkan ke atas
// ke 0,1 mJ; ulangan 20 Nov memberi median 80,45. Data: data/butir5/sweep_dingin_21nov_*.
// Riwayat: 93,7 mJ (Tahap 4.1, 25 Okt 2026) dipakai sampai 21 Nov 2026; nilai itu diukur dengan modul
// INA219 lama yang membaca sekitar 7 % terlalu tinggi dan tidak lagi berlaku untuk rakitan beku.
#define E_MAKS_mJ      80.5f
#define E_MAKS_TERUKUR 1      // 1 = nilai hasil ukur sweep dingin butir 5

#define REWARD_TIMEOUT (-1.0f) // penalti bila ACK tidak diterima; konsisten
                               // dengan batas bawah rentang reward [-1, +1]

#define ACK_SETIAP_N   10      // jendela evaluasi ACK (Naskah 4.1)
// Dihitung sejak paket ke-10 selesai dipancar. Bila paket akhir siklus hilang,
// ACK gateway-1.5 datang dari tenggat berbasis posisi paket, sekitar 150 sampai
// 260 ms sesudah node mulai mendengar (perhitungan di gateway/src/main.cpp).
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

// Ambang RSSI: WAJIB diganti hasil kalibrasi SOP Tahap 4.5
#define RSSI_DETECT   (-100)     // dBm
#define RSSI_EXIT     (-105)     // dBm
#define RSSI_TERKALIBRASI 0      // set 1 setelah diisi hasil Tahap 4.5

// ============================================================
// 4. FSM (Tabel 4.1 naskah + Catatan Perbaikan A.2)
// ============================================================
#define TIMEOUT_WINDOW_MS 240000UL // D9: 240 s; T_max terpanjang S3 = 216 s
#define COOLDOWN_MS        60000UL // D9: jeda sebelum boleh masuk ulang
#ifdef UJI_GUARD
  // HANYA untuk uji meja Tahap 4.4 (env uji_guard): ambang dinaikkan di atas
  // tegangan baterai penuh supaya masuk dan keluar guard terlihat tanpa
  // catu daya variabel. Menguji logika, bukan angka 3,3 V dan 3,5 V.
  #define V_BATT_MIN         4.10f
  #define V_BATT_PULIH       4.00f
  #define GUARD_SLEEP_S      10ULL
#elif defined(UJI_GUARD_NYATA)
  // HANYA untuk uji BATTERY_GUARD dengan sel lapangan (catatan Pembimbing I 2 Nov 2026 C.4; env
  // uji_guard_nyata): ambang ASLI 3,3 V dan 3,5 V, tetapi tidur guard dipersingkat ke 60 s supaya keluar dari
  // guard terlihat dalam satu sesi. Logika guard sama dengan firmware resmi.
  #define V_BATT_MIN         3.30f
  #define V_BATT_PULIH       3.50f
  #define GUARD_SLEEP_S      60ULL
#else
  #define V_BATT_MIN         3.30f   // masuk BATTERY_GUARD
  #define V_BATT_PULIH       3.50f   // keluar dari BATTERY_GUARD
  #define GUARD_SLEEP_S      3600ULL // tidur panjang saat baterai kritis
#endif
// Pembacaan tegangan di bawah nilai ini dianggap error baca, bukan baterai
// kosong: ESP32 dan modul penaik sudah mati jauh sebelum baterai turun ke 1 V.
#define VBATT_BACA_MIN     1.0f

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
// 5. Pengukuran energi (Dokumen Revisi Bagian C, D1)
// ============================================================
// Arus dicuplik SELAMA transmisi berlangsung, bukan sesudahnya. Mode konversi
// tunggal 12-bit (532 us), bukan rata-rata 128 cuplikan (68 ms) yang lebih
// panjang daripada sebagian waktu udara.
#define ENERGI_CUPLIK_SAAT_TX 1   // 0 = perilaku lama Panduan (untuk pembanding)
#define ENERGI_MAKS_SAMPEL  600   // batas atas cuplikan per transmisi (Arm 4 butuh sekitar 540)

// ============================================================
// 6. Muatan paket (Dokumen Revisi + Panduan Perakitan, D6)
// ============================================================
#define PAYLOAD_LEN   12
#define VBATT_OFFSET  2.50f  // byte 5 = (V - 2,50) * 100
#define VBATT_SKALA   100.0f
#define E_SKALA       10.0f  // byte 3..4 = energi mJ * 10 (satuan 0,1 mJ)

// ============================================================
// 7. Mode algoritma: SW-UCB (default) atau UCB1 pembanding (Naskah 2.1.13,
//    Rumusan Masalah 3 / Hipotesis Minor 1)
// ============================================================
// SW-UCB membatasi rata-rata dan isi logaritma ke W_SIZE keputusan terakhir.
// UCB1 memakai rata-rata kumulatif seluruh riwayat, tanpa batas jendela.
// Itulah SATU-SATUNYA perbedaan yang dimaksudkan: ruang aksi, fungsi reward,
// dan XI tetap sama persis pada kedua mode, supaya selisih hasil yang teramati
// murni berasal dari ada-tidaknya jendela geser (Bagian 6.10 naskah belajar).
//
// Aktifkan untuk fase pembanding pada Fase Uji Multi-Kecepatan (Subbab 4.8.5):
//   #define MODE_UCB1
#ifndef MODE_UCB1
  #define MODE_SW_UCB 1
#else
  #define MODE_SW_UCB 0
#endif

// Satuan keputusan agen (catatan Pembimbing I butir A.1, 26 Okt 2026).
// Naskah Bab III menetapkan satuan pengamatan agen adalah siklus ACK: satu keputusan,
// sepuluh paket dengan arm yang sama, satu reward. Karena itu select_arm() dipanggil
// sekali pada paket pertama tiap siklus, sehingga t, N, dan W semuanya berhitung dalam
// siklus. Firmware 1.0 memanggilnya pada setiap paket: t naik sepuluh per siklus dan
// ln(min(t, W)) sudah jenuh di W pada siklus ke-5, padahal N dan W berhitung dalam
// siklus. Disetujui Pembimbing I, 1 Nov 2026 (B.1).
//
// Firmware 1.1 menyediakan saklar T_PER_PAKET untuk mereproduksi log 1.0. Sejak
// 1.2 agennya berganti (window bersama, xi di dalam akar), sehingga saklar itu
// tidak lagi dapat mereproduksi apa pun dan dihapus. Log lama direproduksi oleh
// agen lama pada tools/replay_swucb.py (--agen lama).

// Mode sweep: lintasan eksplorasi tambahan (keputusan Pembimbing I, 27 Sep 2026).
// Arm TIDAK dipilih agen, melainkan bergiliran Arm 1, 2, 3, 4 per siklus ACK,
// sehingga setiap arm memperoleh siklus yang hampir sama banyak. Dipakai hanya
// untuk skenario yang cakupan arm-nya di bawah N_MIN_SIKLUS menurut
// tools/olah_siklus.py. Log mode ini disimpan terpisah dengan label Metode
// Sweep dan tidak masuk analisis hipotesis SW-UCB (Naskah Subbab 4.4.6).
// Agen tetap menerima reward agar log boot dan RTC RAM konsisten, tetapi
// keputusannya tidak dipakai.
//   #define MODE_SWEEP
#ifdef MODE_SWEEP
  #ifdef MODE_UCB1
    #error "MODE_SWEEP dan MODE_UCB1 tidak boleh aktif bersamaan"
  #endif
  #define MODE_SWEEP_AKTIF 1
#else
  #define MODE_SWEEP_AKTIF 0
#endif
#define N_MIN_SIKLUS  10   // batas kecukupan siklus ACK per arm per skenario (Subbab 4.4.6)

// ============================================================
// 8. Penanda kompilasi
// ============================================================
// 1.3: indeks paket di 4 bit atas byte 0 muatan, penanda siklus pada ACK 5 byte
// (catatan Pembimbing I 1 Nov 2026, B.3); wajib berpasangan dengan gateway-1.5.
#if MODE_SWEEP_AKTIF
  #define FW_VERSI    "node-1.3-sweep"
#elif MODE_SW_UCB
  #define FW_VERSI    "node-1.3-swucb"
#else
  #define FW_VERSI    "node-1.3-ucb1"
#endif
#define SERIAL_BAUD   115200
