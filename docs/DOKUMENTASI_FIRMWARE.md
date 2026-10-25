# Dokumentasi Kode Firmware Node dan Gateway

Arsitektur Edge-AI untuk Kendali Transmisi Otonom LoRa pada Pengumpulan Data Kendaraan: Implementasi Algoritma Sliding-Window Upper Confidence Bound pada Kanal Non-Stasioner Kendaraan.

Penulis: I Gede Wahyu Wiranata (NIM 2481711013), Magister Teknik Elektro, Universitas Udayana.
Versi firmware: node-1.0-swucb, node-1.0-ucb1, node-1.0-sweep, dan gateway-1.1. Tanggal dokumen: 25 Oktober 2026.

## 1. Ikhtisar sistem

Firmware ini menjalankan eksperimen Hardware-in-the-Loop (HIL) untuk menguji algoritma Sliding-Window Upper Confidence Bound (SW-UCB). Sebuah node sensor statis di tepi jalan memilih pasangan daya pancar (TP) dan coding rate (CR) setiap kali kendaraan yang membawa gateway melintas. Gateway memancarkan beacon, menerima paket, membalas ACK, dan mencatat setiap paket beserta RSSI dan SNR-nya.

```
 node sensor statis                                gateway bergerak (di kendaraan)
 ESP32 + RA-02 (SX1278) + INA219 + ADXL345         ESP32 + RA-02 (SX1278)
 daya: baterai 18650 -> INA219 -> J5019 -> VIN     tambahan: LCD 20x4, dua biled
        |                                                  |
        |  paket data 12 byte  -------------------->       |--> serial USB: CSV ke laptop
        |  <------------------- beacon 2 byte, ACK 3 byte  |--> WiFi hotspot: Google Sheets (opsional)
        433 MHz, SF8, BW 125 kHz
```

Alur kerja satu kontak: node tidur dan memindai beacon secara berkala (state IDLE). Begitu beacon cukup kuat, node masuk CONTACT_WINDOW dan mengirim paket satu per satu. Setiap sepuluh paket gateway membalas ACK berisi jumlah paket yang ia terima. Node menghitung reward dari PDR dan energi, memperbarui agen SW-UCB, lalu memilih arm berikutnya. Node keluar dari window bila ACK tidak datang, RSSI ACK turun di bawah ambang keluar, atau batas waktu tercapai.

Hal yang membedakan firmware ini dari rancangan awal (Panduan HIL) dicatat sebagai penyimpangan D1 sampai D19 (Bagian 8).

## 2. Struktur repositori

```
firmware/
  node/                          proyek PlatformIO node sensor
    platformio.ini               env produksi dan env program uji
    lib/LoRa/                     salinan lokal pustaka LoRa (dua perubahan kecil, Bagian 7)
    src/main.cpp                 setup, keadaan RTC RAM, pencetakan parameter
    src/config/params.h          semua parameter yang dapat disetel
    src/config/pin_map.h         pin, alamat I2C, koreksi INA219 dan ADXL345
    src/config/protocol.h        penanda beacon dan ACK
    src/modules/fsm.*            mesin keadaan (IDLE, CONTACT_WINDOW, REWARD_EVAL, BATTERY_GUARD)
    src/modules/SW_UCB_Agent.*   agen SW-UCB dan UCB1
    src/modules/energy_meter.*   integrasi energi transmisi dengan INA219
    src/modules/radio.*          pembungkus LoRa: pindai beacon, kirim paket, tunggu ACK
    src/modules/sensor.*         akselerometer ADXL345 lewat I2C
    src/modules/payload.*        penyusunan paket 12 byte
    uji/                         program uji laboratorium (bukan firmware penelitian)
  gateway/                       proyek PlatformIO gateway
    platformio.ini               env esp32dev dan esp32dev_sheets
    src/main.cpp                 beacon, penerimaan paket, ACK, CSV serial, perintah serial
    src/config/                  pin_map.h, protocol.h, secrets.example.h
    src/modules/unggah_sheets.*  unggah ke Google Sheets (task terpisah)
    src/modules/tampilan_lcd.*   LCD I2C 20x4
    src/modules/indikator_led.*  dua biled indikator
    apps_script/Code.gs          penerima di Google Apps Script
  tools/                         alat bantu Python (pengolahan log, pra-validasi, gambar)
  docs/                          dokumen ini
```

## 3. Membangun dan memasang

Alat yang dipakai adalah PlatformIO dengan framework Arduino untuk ESP32. Node dan gateway adalah dua proyek terpisah; buka masing-masing di jendela sendiri.

| Proyek | Env | Fungsi |
|---|---|---|
| node | `esp32dev` | firmware penelitian, mode SW-UCB (bawaan) |
| node | `esp32dev` dengan `-DMODE_UCB1` | pembanding UCB1 |
| node | `esp32dev` dengan `-DMODE_SWEEP` | arm bergiliran per siklus ACK (lintasan eksplorasi) |
| node | `kalibrasi_ina219` | program uji kalibrasi INA219 (Tahap 4.1) |
| node | `uji_tx_arus`, `uji_tx_radio` | uji pencuplikan arus selama transmisi |
| node | `uji_rtc_ram`, `uji_rtc_60s`, `uji_rtc_3600s` | uji persistensi RTC RAM (Tahap 4.2) |
| node | `uji_guard` | uji logika BATTERY_GUARD (Tahap 4.4) |
| gateway | `esp32dev` | gateway dengan keluaran serial |
| gateway | `esp32dev_sheets` | gateway dengan unggah ke Google Sheets |

Contoh perintah:

```
cd node
pio run -t upload                                     # firmware SW-UCB
PLATFORMIO_BUILD_FLAGS="-DMODE_SWEEP" pio run -t upload   # mode sweep
cd ../gateway
pio run -e esp32dev_sheets -t upload
```

Saklar kompilasi lain: `-DPAKAI_NILAI_PANDUAN` (nilai asli Panduan HIL; wajib di kedua proyek sekaligus) dan `-DBEACON_SENYAP_SAAT_JENDELA=0` (gateway kembali memancar beacon terus-menerus).

Catatan pemasangan: pada papan node, kabel DIO0 RA-02 tersolder ke GPIO2 (pin strapping), sehingga tombol BOOT harus ditahan saat upload. Firmware tidak memakai DIO0 sama sekali, karena pustaka LoRa dipakai dengan polling.

Rahasia (kata sandi hotspot, token, alamat web app) berada di `gateway/src/config/secrets.h`. Berkas ini masuk `.gitignore`; salin dari `secrets.example.h`.

## 4. Protokol dan format data

### 4.1 Parameter radio

Frekuensi 433 MHz, spreading factor 8, bandwidth 125 kHz, preamble 8 simbol, sync word 0x12, CRC aktif. Node dan gateway wajib memakai nilai yang sama persis. Coding rate dan daya pancar node ditentukan oleh arm:

| Arm | TP (dBm) | CR | Waktu udara terukur (ms) |
|---|---|---|---|
| 1 | 5 | 4/5 | 82,7 |
| 2 | 10 | 4/5 | 82,7 |
| 3 | 14 | 4/7 | 99,1 |
| 4 | 20 | 4/8 | 107,3 |

Gateway memancarkan beacon dan ACK pada 17 dBm, CR 4/5.

### 4.2 Paket node ke gateway (12 byte)

| Byte | Isi | Format |
|---|---|---|
| 0 | nomor arm | uint8, 1 sampai 4 |
| 1 sampai 2 | nomor urut paket | uint16 big-endian |
| 3 sampai 4 | energi transmisi paket sebelumnya | uint16 big-endian, satuan 0,1 mJ |
| 5 | tegangan baterai | uint8, (V - 2,50) x 100 |
| 6 sampai 11 | percepatan x, y, z | tiga int16 big-endian, satuan mg |

Energi yang dikirim adalah energi paket sebelumnya, karena energi paket berjalan baru selesai dihitung sesudah transmisinya tuntas (penyimpangan D6). Pengolah log menggesernya kembali.

### 4.3 Beacon dan ACK

Beacon: 2 byte (0xBE, 0xAC), dipancarkan gateway tiap 1 detik. Pada gateway, selang efektifnya sekitar 1052 ms karena waktu pancar ikut terhitung. ACK: 3 byte (0xAC, 0x4B, n), dengan n jumlah paket yang gateway terima pada jendela ACK itu.

Paket data dikenali dari panjangnya yang tepat 12 byte; panjang lain dianggap derau dan dibuang.

### 4.4 Baris CSV gateway

Gateway mencetak satu baris CSV ke serial untuk tiap DATA dan ACK, dengan kepala:

```
jenis,ts_ms,id_window,seq,arm,e_prev_mJ,vbatt,rssi,snr,ax,ay,az
```

| Kolom | Arti |
|---|---|
| jenis | `DATA` atau `ACK` |
| ts_ms | milidetik sejak gateway menyala |
| id_window | ID lintasan, diisi lewat perintah `W` atau sel B1 Sheets; `BELUM-DISET` bila belum |
| seq | nomor urut paket (pada ACK: jumlah paket diterima) |
| arm | arm pada paket (kosong pada ACK) |
| e_prev_mJ | energi paket sebelumnya, mJ |
| vbatt | tegangan baterai node, V |
| rssi, snr | RSSI (dBm) dan SNR (dB) paket di gateway |
| ax, ay, az | percepatan, mg |

### 4.5 Aturan penutupan jendela ACK

Gateway menutup jendela dan mengirim ACK bila rentang nomor urut paket mencapai sepuluh (selisih `seq - seq_awal >= 9`), berapa pun paket yang benar-benar tiba. Bila paket terakhir hilang, jendela ditutup paksa setelah 800 ms tanpa paket. Dengan cara ini kehilangan paket tetap tampak pada PDR (penyimpangan D16).

### 4.6 Perintah serial gateway (115200 baud)

| Perintah | Fungsi |
|---|---|
| `W <id>` | tetapkan ID window dan kosongkan pencacah |
| `S` | cetak status: ID, total paket, status unggah Sheets, alamat LCD, status biled |
| `L [detik]` | uji biled (bawaan 1,2 detik; `L 0` mengakhiri; maksimum 60) |

## 5. Firmware node

### 5.1 Alur program

`setup()` menyalakan serial, menginisialisasi bus I2C (400 kHz), INA219, ADXL345, dan radio LoRa. Kegagalan salah satu periferal menghentikan node dengan pesan `FATAL`, supaya data sesudahnya tidak tampak sah padahal tidak. Bila penanda RTC RAM tidak sah (nyala pertama atau RTC RAM rusak), keadaan agen dan pencacah dikosongkan. Parameter aktif dicetak saat boot dingin agar dapat dicocokkan dengan buku kerja. `loop()` hanya memanggil `fsm_tick()`.

Deep sleep menghapus RAM biasa dan memulai program dari awal. Karena itu seluruh keadaan yang harus bertahan dideklarasikan dengan atribut `RTC_DATA_ATTR`:

| Variabel | Isi |
|---|---|
| `rtc_arms[4]` | keadaan agen: buffer reward 50 slot per arm, posisi tulis, jumlah entri, rata-rata (mode UCB1: jumlah dan rata-rata kumulatif) |
| `rtc_t_global` | pencacah waktu agen |
| `rtc_pkt_seq` | nomor urut paket |
| `rtc_ack_counter` | paket terkirim pada jendela ACK berjalan |
| `rtc_energi_akum_mJ` | energi terakumulasi pada jendela ACK berjalan |
| `rtc_arm_sekarang` | arm yang dipakai siklus berjalan |
| `rtc_vbatt`, `rtc_e_prev_mJ` | tegangan bus terakhir dan energi paket sebelumnya |
| `rtc_cooldown_sampai_s`, `rtc_uptime_s` | waktu cooldown dan waktu hidup kumulatif |
| `rtc_dalam_guard` | status BATTERY_GUARD |
| `rtc_sweep_arm` | giliran arm pada mode sweep |
| `rtc_penanda_sah` | penanda keabsahan RTC RAM (0x57554342) |

Pembelajaran SW-UCB memakai sekitar 832 byte dari 8 kB RTC RAM.

### 5.2 Mesin keadaan (fsm.cpp)

Empat state:

| State | Fungsi |
|---|---|
| IDLE | siaga berdaya rendah: memindai beacon 1,2 detik, tidur 3 detik |
| CONTACT_WINDOW | mengirim paket satu per satu dengan jeda 0,5 detik |
| REWARD_EVAL | menunggu ACK, menghitung reward, memperbarui agen |
| BATTERY_GUARD | tidur panjang saat baterai kritis |

Setiap `fsm_tick()` mulai dengan memeriksa BATTERY_GUARD, yang berprioritas mutlak. Tegangan bus INA219 dibaca; bila di bawah 3,3 V node masuk guard dan tidur 3600 detik. Bila sedang dalam guard, node baru keluar ketika tegangan melampaui 3,5 V. Status guard disimpan di RTC RAM supaya bertahan lintas tidur, dan pita 0,2 V mencegah node berkedip masuk-keluar.

Transisi dan kondisinya:

| Dari | Ke | Kondisi |
|---|---|---|
| IDLE | CONTACT_WINDOW | RSSI beacon terkuat pada satu pemindaian melampaui -100 dBm (`RSSI_DETECT`) |
| IDLE | IDLE (tidur) | tidak ada beacon cukup kuat: tidur 3 detik; atau cooldown masih berjalan |
| CONTACT_WINDOW | REWARD_EVAL | sepuluh paket terkirim (`ACK_SETIAP_N`) |
| CONTACT_WINDOW | IDLE | batas waktu 240 detik (`TIMEOUT_WINDOW_MS`), lalu cooldown 60 detik (D9) |
| REWARD_EVAL | CONTACT_WINDOW | ACK diterima dan RSSI ACK di atas -105 dBm (`RSSI_EXIT`) |
| REWARD_EVAL | IDLE | ACK tidak diterima dalam 2 detik (reward -1), atau RSSI ACK di bawah -105 dBm (D15) |
| semua | BATTERY_GUARD | tegangan bus di bawah 3,3 V |

Kepergian kendaraan dideteksi pada evaluasi ACK, tanpa pemindaian beacon tambahan (D15).

### 5.3 Agen SW-UCB (SW_UCB_Agent.cpp)

Agen memegang penunjuk ke `ArmState` di RTC RAM. Skor tiap arm pada mode SW-UCB:

```
I(a, t) = mu(a, W) + XI * sqrt( ln( min(t, W) ) / N(a, W) )
```

dengan `mu` rata-rata reward di dalam jendela, `N` jumlah entri sah di dalam jendela, `W = 50`, dan `XI = 0,5`. Arm yang belum pernah dicoba mendapat skor tak hingga, sehingga dicoba lebih dulu. Pada `t < 2` bonus eksplorasi ditiadakan. `update()` menulis reward ke buffer melingkar (entri terlama tertimpa bila penuh) dan menghitung ulang rata-rata dari isi buffer, bukan secara inkremental, agar penimpaan tidak meninggalkan akumulasi error. `select_arm()` memilih skor tertinggi lalu menaikkan `t`.

Mode UCB1 (pembanding) memakai rata-rata kumulatif dan `ln(t)` tanpa batas jendela; hanya itu perbedaannya, supaya selisih hasil murni berasal dari jendela geser.

Mode sweep tidak memakai keputusan agen: arm bergantian 1, 2, 3, 4 per siklus ACK (`rtc_sweep_arm`), dan agen tetap menerima reward supaya keadaan RTC konsisten.

### 5.4 Fungsi reward

Dihitung di `evaluasi_reward()` untuk tiap siklus ACK:

```
PDR    = n_diterima / 10
E_rata = energi_terakumulasi / 10
E_norm = min( max( E_rata / E_MAKS, 0 ), 1 )
reward = ALPHA * PDR - BETA * E_norm            (ALPHA = 1, BETA = 1)
```

Bila ACK tidak diterima, reward bernilai -1 (`REWARD_TIMEOUT`). `E_MAKS_mJ` adalah energi per transmisi pada TP 20 dBm hasil pengukuran Tahap 4.1 (nilai kerja 93,7 mJ).

### 5.5 Pengukuran energi (energy_meter.cpp)

Energi satu transmisi dihitung sebagai jumlah daya sesaat dikali selang waktu:

```
E = sum( V_bus * I_i * dt_i )
```

`energy_begin()` dipanggil tepat sebelum `beginPacket()`: membaca tegangan bus sekali, mengosongkan akumulator, dan mencatat waktu. Selama radio memancar, `energy_sample()` dipanggil berulang (maksimum 600 cuplikan); tiap panggilan membaca arus INA219, lalu menambahkan `V_bus * I * dt` dengan dt sejak cuplikan sebelumnya (integrasi persegi panjang). `energy_end_mJ()` mengambil satu cuplikan penutup supaya sisa rentang ikut terhitung.

INA219 diatur gain 320 mV, rentang bus 16 V, konversi tunggal 12-bit (532 mikrodetik), mode kontinu, dengan hambatan shunt efektif 0,1104 ohm hasil kalibrasi (`INA219_SHUNT_OHM` pada `pin_map.h`). Pembacaan berada di sisi baterai, sebelum modul penaik tegangan J5019, sehingga energi yang terukur adalah energi di terminal baterai.

### 5.6 Radio (radio.cpp)

`radio_kirim_paket()` mengatur TP dan CR sesuai arm, memanggil `energy_begin()`, mengirim paket secara asinkron (`endPacket(true)`), lalu mencuplik energi sampai metode `txDone()` melaporkan flag TxDone (batas pengaman 500 ms). `radio_scan_beacon()` mendengarkan selama 1,2 detik dan mengembalikan RSSI beacon terkuat. `radio_tunggu_ack()` menunggu paket ACK sampai 2 detik dan mengembalikan jumlah paket yang diterima gateway, atau -1.

### 5.7 Sensor (sensor.cpp)

ADXL345 diakses langsung lewat I2C (alamat 0x53; tanpa pustaka). Inisialisasi memeriksa DEVID 0xE5, mengatur rentang ±16 g resolusi penuh (3,9 mg/LSB), menulis register offset OFSX, OFSY, OFSZ dari `pin_map.h` (OFSZ = -114 untuk modul GY-291 yang dipakai), lalu mengaktifkan mode ukur. Nilai payload tidak masuk ke fungsi reward.

### 5.8 Payload (payload.cpp)

`payload_pack()` dan `payload_unpack()` menyusun dan membongkar paket 12 byte (Bagian 4.2), dengan pembatasan nilai (energi dibatasi pada rentang uint16, tegangan pada 2,5 sampai 5,05 V).

### 5.9 Parameter utama (params.h)

| Parameter | Nilai | Keterangan |
|---|---|---|
| `N_ARMS`, `W_SIZE`, `XI` | 4, 50, 0,5 | ruang aksi dan agen |
| `ALPHA`, `BETA` | 1,0 dan 1,0 | bobot reward (BETA dikonfirmasi Pembimbing I, 5 September 2026; D5) |
| `E_MAKS_mJ` | 93,7 | hasil Tahap 4.1, usulan menunggu keputusan |
| `ACK_SETIAP_N`, `ACK_TIMEOUT_MS` | 10, 2000 | jendela evaluasi ACK |
| `RSSI_DETECT`, `RSSI_EXIT` | -100, -105 dBm | nilai awal, belum dikalibrasi (Tahap 4.5) |
| `T_SCAN_MS`, `T_SLEEP_S` | 1200, 3 | pemindaian dan tidur IDLE (D2, D3) |
| `JEDA_ANTAR_PAKET_MS` | 500 | jeda antar paket (D7) |
| `TIMEOUT_WINDOW_MS`, `COOLDOWN_MS` | 240000, 60000 | batas window dan cooldown (D9) |
| `V_BATT_MIN`, `V_BATT_PULIH`, `GUARD_SLEEP_S` | 3,30 V, 3,50 V, 3600 s | BATTERY_GUARD |
| `ENERGI_CUPLIK_SAAT_TX`, `ENERGI_MAKS_SAMPEL` | 1, 600 | pencuplikan energi (D1) |
| `N_MIN_SIKLUS` | 10 | batas kecukupan siklus ACK per arm per skenario |

Seluruh nilai dikumpulkan di satu berkas supaya dapat dibekukan sesudah Fase Stabilisasi dan dicocokkan dengan sheet 00-Konfigurasi pada buku kerja.

## 6. Firmware gateway

### 6.1 Alur program

`setup()` menyalakan biled, LCD (bila ada), dan radio LoRa dengan parameter yang sama dengan node; bila radio gagal, layar, biled, dan serial menampilkan `FATAL`. `loop()` mengerjakan, berurutan: pembaruan biled, perintah serial, pembaruan LCD, penerapan ID window dari Sheets, beacon, penutupan jendela yang menggantung, dan penerimaan paket.

### 6.2 Beacon diam saat window aktif (D18)

Radio LoRa bersifat half-duplex, sehingga paket node yang tiba saat gateway memancar beacon hilang. Pada uji meja 1 meter, 319 paket yang diterima tidak memuat satu pun paket pada jendela sekitar 100 ms di tiap siklus 1052 ms, dan PDR hanya 0,87 (0,81 untuk Arm 4). Gateway kini tidak memancar beacon selama ada paket yang diterima dalam 2 detik terakhir (`BEACON_SENYAP_MS`). Node tidak membutuhkan beacon selama window karena memutuskan keluar dari RSSI ACK dan batas waktu. PDR sesudah perubahan 1,00 (413 dari 414 paket).

### 6.3 Unggah ke Google Sheets (unggah_sheets.cpp)

Aktif pada env `esp32dev_sheets`. Pekerjaan jaringan berjalan di satu task FreeRTOS pada core 0, sehingga `loop()` di core 1 tidak pernah menunggu jaringan. Baris DATA dan ACK dimasukkan ke antrean RAM (700 baris). Task mengelompokkan baris menjadi paket (maksimum 20 baris atau 4 detik) dan mengirimnya dengan HTTPS POST berformat JSON ke web app Apps Script. Redirect 302 diikuti secara manual. Paket yang gagal dibekukan: isi dan ID-nya tidak berubah sampai berhasil, dan server menolak ID paket yang sudah diterima, sehingga pengiriman ulang tidak menggandakan baris. Task yang sama membaca sel B1 lembar KONTROL tiap 5 detik untuk mengganti ID window tanpa laptop. Waktu NTP dipakai untuk kolom `epoch_s`. Koneksi memakai `setInsecure()` (sertifikat server tidak diverifikasi), dicatat pada komentar kode.

### 6.4 Penerima Apps Script (apps_script/Code.gs)

`doPost` memeriksa token (properti skrip `TOKEN`), menolak ID paket yang sudah tercatat (300 ID terakhir diingat), lalu menulis baris ke lembar `LOG` (kolom sama dengan CSV serial ditambah `epoch_s`, `boot`, `batch`, `buang`) dan memperbarui lembar `KONTROL`. `doGet` melayani `aksi=window` (membaca sel B1) dan `aksi=ping`. Fungsi `siapkan()` membuat kedua lembar.

### 6.5 LCD 20x4 (tampilan_lcd.cpp)

Driver sendiri untuk modul PCF8574 (tanpa pustaka), alamat dicari otomatis pada 0x20 sampai 0x27 dan 0x38 sampai 0x3F, bus I2C 100 kHz. Penulisan dibatasi satu baris per 120 ms dan hanya karakter yang berubah, supaya beacon dan penerimaan tidak terlambat. Isi layar saat operasi:

| Baris | Isi |
|---|---|
| 1 | ID window (`!BELUM-DISET!` bila belum) |
| 2 | paket diterima pada jendela berjalan dari 10, dan total paket |
| 3 | RSSI, SNR, arm, dan umur paket terakhir |
| 4 | tegangan baterai node, jumlah ACK, isi ACK terakhir, status WiFi dan antrean |

Saat boot layar menampilkan versi dan parameter radio; bila radio gagal, layar menampilkan `FATAL` beserta petunjuk daya dan pin.

### 6.6 Dua biled indikator (indikator_led.cpp)

Satu biled untuk LoRa dan satu untuk Sheets, dengan pola berbasis `millis()` tanpa jeda pada jalur penerimaan. Biled LoRa: hijau saat DATA masuk, merah saat ACK keluar, merah lambat bila ID belum diisi atau lama tanpa DATA, merah cepat bila LoRa gagal. Biled Sheets: merah bila WiFi belum tersambung atau ada kegagalan unggah, hijau saat unggahan berhasil. Rincian pola dan pemetaan pin ada pada `gateway/INDIKATOR_LED.md`.

### 6.7 Pin gateway

| Fungsi | Pin |
|---|---|
| LoRa NSS, RST, DIO0 | 5, 14, 2 |
| LoRa SCK, MISO, MOSI | 18, 19, 23 |
| I2C (LCD) SDA, SCL | 21, 22 |
| Biled LoRa merah, hijau | 26, 25 |
| Biled Sheets merah, hijau | 32, 27 |

Pin node sama untuk LoRa dan I2C (INA219 pada 0x40, ADXL345 pada 0x53).

## 7. Pustaka pihak ketiga dan perubahannya

| Pustaka | Versi | Catatan |
|---|---|---|
| LoRa (Sandeep Mistry) | ^0.8.0 | gateway memakai dari registry; node memakai salinan lokal di `node/lib/LoRa` |
| INA219_WE | 1.4.1 | dikunci persis agar kode yang sudah divalidasi tidak berubah diam-diam |

Salinan lokal pustaka LoRa pada node memuat dua perubahan kecil. Pertama, `isTransmitting()` dipindah dari private ke public. Kedua, ditambahkan `txDone()`, yang membaca flag TxDone di register IRQ dan membersihkannya. Perubahan kedua diperlukan karena tepat sesudah `endPacket(true)` register mode radio sesaat belum bernilai TX, sehingga `isTransmitting()` mengembalikan false dan loop pencuplikan energi berhenti seketika (satu cuplikan per transmisi, energi 0,3 mJ pada semua arm).

## 8. Penyimpangan terhadap Panduan HIL

| No | Penyimpangan | Letak pada kode |
|---|---|---|
| D1 | arus dibaca sesudah radio berhenti memancar, diganti dengan pencuplikan selama transmisi | `energy_meter.*`, `radio.cpp` |
| D2 | durasi pemindaian 500 ms diganti 1200 ms | `T_SCAN_MS` |
| D3 | tidur IDLE 60 detik diganti 3 detik | `T_SLEEP_S` |
| D4 | selang beacon 5000 ms diganti 1000 ms | `BEACON_SELANG_MS` (protocol.h) |
| D5 | BETA 0,02 diganti 1,0 | `BETA` |
| D6 | muatan paket ditambah energi dan tegangan | `payload.*` |
| D7 | jeda antar paket 0,5 detik | `JEDA_ANTAR_PAKET_MS` |
| D8 | 80 lintasan per skenario | rencana eksperimen |
| D9 | batas waktu window 240 detik dan cooldown 60 detik | `TIMEOUT_WINDOW_MS`, `COOLDOWN_MS` |
| D10 | lima tingkat arus pada uji kalibrasi | SOP Tahap 4.1 |
| D11 | catu daya node | (perangkat keras) |
| D12 | tiga pin SPI dituliskan eksplisit | `pin_map.h` |
| D13 | DIO0 pada GPIO2 (pin strapping), dipertahankan | `pin_map.h` |
| D14 | letak INA219 di jalur baterai | (perangkat keras) |
| D15 | kepergian kendaraan dideteksi pada evaluasi ACK | `evaluasi_reward()` |
| D16 | PDR dihitung dari rentang nomor urut | gateway `main.cpp` |
| D17 | syarat keluar BATTERY_GUARD pada 3,5 V | `fsm_tick()` |
| D18 | beacon gateway diam saat window aktif | gateway `main.cpp` |
| D19 | modul J5019 menggantikan TP4056 dan MT3608 | (perangkat keras) |

D18 dan D19 baru, dan menunggu keputusan Pembimbing I.

## 9. Program uji laboratorium (node/uji)

Program berikut bukan firmware penelitian. Masing-masing dipasang sementara lewat env tersendiri, lalu firmware node dipasang kembali.

| Program | Tujuan | Keluaran |
|---|---|---|
| `kalibrasi_ina219.cpp` | memverifikasi arus INA219 terhadap acuan (Tahap 4.1) | CSV serial: tegangan bus, tegangan shunt, arus rata-rata dan simpangan baku |
| `uji_tx_arus.cpp` | memeriksa apakah pencuplikan arus berjalan selama transmisi, termasuk jejak register LoRa | CSV serial: iterasi, durasi, jumlah cuplikan, arus |
| `uji_tx_radio.cpp` | sama, tetapi node berjalan dari baterai dan melaporkan lewat radio | kolom CSV gateway: arus rata-rata, puncak, cuplikan, durasi, tegangan terendah |
| `uji_rtc_ram.cpp` | persistensi RTC RAM lintas deep sleep (Tahap 4.2); lama tidur 5, 60, atau 3600 detik | kolom CSV gateway: nomor siklus, elemen rusak, boot dingin |

Env `uji_guard` tidak memakai program tersendiri: ia mengompilasi firmware produksi dengan ambang BATTERY_GUARD dinaikkan (4,10 V masuk, 4,00 V pulih, tidur 10 detik) untuk menguji logika guard tanpa catu daya variabel.

## 10. Alat bantu Python (tools)

| Alat | Fungsi |
|---|---|
| `prevalidasi_swucb.py` | pra-validasi SW-UCB terhadap UCB1 di Python sebelum firmware (Tahap 0): re-konvergensi, uji berpasangan, selang kepercayaan bootstrap |
| `olah_siklus.py` | mengolah log gateway per siklus ACK dan per contact window: reward per siklus, cakupan arm per skenario, tegangan bus per window; menandai arm yang datanya tipis |
| `ringkas_log.py` | meringkas log paket menjadi satu baris per ID window untuk sheet 04 buku kerja, dengan koreksi pergeseran energi satu paket dan rekonstruksi paket hilang |
| `buat_diagram.py`, `buat_gambar_jurnal.py`, `buat_gambar_slide.py` | membangkitkan diagram alur untuk naskah, artikel jurnal, dan slide |
| `diagnosa_spi_lora.py` | membaca register SX127x lewat akses GPIO sementara pada ROM loader ESP32, untuk mendiagnosis sambungan SPI tanpa memasang firmware |
| `uji_biled_gateway.py` | menguji empat warna biled lewat firmware gateway dan menyimpan pembacaan GPIO |

Alur data: serial gateway direkam ke berkas CSV, lalu `olah_siklus.py` dan `ringkas_log.py` membentuk berkas yang masuk ke buku kerja untuk analisis. Berkas log mentah disimpan di luar repositori (folder `data/` diabaikan git).

## 11. Hasil pengujian yang melandasi nilai parameter

Lihat laporan terpisah "Laporan Hasil Pengujian Laboratorium Tahap 4.1 sampai 4.4". Ringkasnya: koreksi shunt INA219 0,1104 ohm; energi per transmisi 46,7, 52,8, 67,8, dan 93,3 mJ untuk Arm 1 sampai 4 (E maksimum 93,7 mJ); RTC RAM bertahan 69 siklus tidur 5 detik dan 13 siklus tidur 60 detik tanpa kerusakan; seluruh transisi FSM terbukti pada log node.
