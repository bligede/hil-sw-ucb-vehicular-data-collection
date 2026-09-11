# Kendali Transmisi Otonom LoRa Berbasis Sliding-Window UCB

Firmware Hardware-in-the-Loop untuk penelitian tesis *Arsitektur Edge-AI untuk
Kendali Transmisi Otonom LoRa pada Pengumpulan Data Kendaraan*.

Program Studi Magister Teknik Elektro, Pascasarjana Universitas Udayana.

> **English summary.** Firmware for a Hardware-in-the-Loop study on autonomous
> LoRa transmission control at the edge. A Sliding-Window UCB agent runs on an
> ESP32 roadside node and selects transmit power and coding rate on its own,
> using a reward built from measured Packet Delivery Ratio and **physically
> measured** transmission energy from an INA219 current sensor — not datasheet
> estimates. A mobile gateway on a vehicle collects the data opportunistically,
> which makes the radio channel non-stationary by construction.

---

## Ringkas

Node sensor statis di tepi jalan mengirim data ke gateway yang dibawa kendaraan
lewat. Karena gateway bergerak, kanal radionya non-stasioner: jarak berubah tiap
detik dan jendela kontak hanya berlangsung beberapa belas sampai beberapa ratus
detik.

Agen Sliding-Window UCB berjalan lokal di ESP32 dan memilih sendiri kombinasi
Transmit Power dan Coding Rate. Reward-nya menyeimbangkan keandalan dan energi:

```
R = alpha * PDR  -  beta * E_norm
```

`PDR` berupa rasio [0, 1] dari jendela ACK sepuluh paket. `E_norm` adalah energi
per transmisi yang dinormalisasi terhadap energi arm termahal, keduanya diukur
sensor arus INA219. Karena kedua komponen berada pada skala yang sama, bobot
setara `alpha = beta = 1,0` menghasilkan keterpisahan reward antar-arm yang
memadai baik pada jarak dekat maupun jauh. Nilai `beta = 1,0` dikonfirmasi
Pembimbing I pada 5 September 2026, dengan syarat analisis sensitivitas selisih
reward dijalankan sesudah `E_MAKS_mJ` diisi hasil kalibrasi INA219 di Tahap 4.1.

## Perangkat keras

| Peran | Komponen |
| --- | --- |
| Node sensor statis | ESP32 DevKit v1, LoRa SX1278 (Ra-02) 433 MHz, INA219, ADXL345, 18650, TP4056 berproteksi, MT3608, panel surya 1,1 W |
| Mobile gateway | ESP32 DevKit v1, LoRa SX1278, daya USB dari laptop |

Spreading Factor dikunci pada SF8 dan Bandwidth pada 125 kHz sebagai variabel
kontrol, karena gateway berbiaya rendah bersifat single-channel dan tidak dapat
mendemodulasi beberapa SF secara paralel. Optimasi karena itu dialihkan ke TP
dan CR.

### Ruang aksi

| Arm | TP | CR | Peran |
| --- | --- | --- | --- |
| 1 | 5 dBm | 4/5 | jarak sangat dekat, mencegah pemborosan energi |
| 2 | 10 dBm | 4/5 | jarak moderat, LoS stabil |
| 3 | 14 dBm | 4/7 | mengimbangi redaman saat menjauh |
| 4 | 20 dBm | 4/8 | worst-case, NLoS atau kecepatan tinggi |

## Struktur

```
node/                   firmware node sensor statis
  src/config/           pin_map.h, params.h, protocol.h
  src/modules/          SW_UCB_Agent, energy_meter, radio, sensor, fsm, payload
gateway/                firmware mobile gateway
tools/
  prevalidasi_swucb.py  validasi SW-UCB vs UCB1 sebelum firmware ditulis
  ringkas_log.py        peringkas log paket menjadi baris contact window
```

## Membangun

Butuh [PlatformIO](https://platformio.org/). Node dan gateway adalah dua proyek
terpisah dan harus dibuka di jendela berbeda.

```bash
cd node     && pio run -t upload -t monitor
cd gateway  && pio run -t upload -t monitor
```

Sebelum menulis firmware, jalankan pra-validasi algoritmanya:

```bash
python tools/prevalidasi_swucb.py
```

Skrip itu menjalankan SW-UCB dan UCB1 pada dataset non-stasioner yang sama dan
mengukur kecepatan re-konvergensi keduanya sesudah distribusi reward berganti.
Implementasinya sengaja menyalin struktur `SW_UCB_Agent.cpp` persis; bila
keduanya berbeda, hasil validasinya tidak mewakili perilaku firmware.

## Empat keputusan rancangan yang menentukan

**Energi dicuplik selama transmisi, bukan sesudahnya.** Membaca INA219 sesudah
`endPacket()` selesai merekam arus mode siaga, karena radio sudah kembali diam.
Nilainya bukan hanya terlalu kecil, tetapi hampir seragam untuk keempat arm
sebab arus siaga tidak bergantung pada TP maupun CR — komponen energi pada
reward berubah menjadi derau. Firmware ini memakai transmisi asinkron dan
mencuplik arus dalam gelung selama radio memancar, lalu mengintegrasikan daya
terhadap waktu. Mode ADC dipakai pada konversi tunggal 12-bit (532 us), bukan
rata-rata 128 cuplikan (68,1 ms) yang lebih panjang daripada sebagian waktu
udara. Baris `TX` mencetak jumlah cuplikan per transmisi sebagai pemeriksaan.

**Energi ikut dikirim di dalam paket.** INA219 mengukur di sisi node, sedangkan
pencatatan berlangsung di sisi gateway. Tanpa medan energi pada muatan, nilai
terukur hanya dipakai internal oleh agen lalu hilang sebelum sampai ke berkas
log. Muatan 12 byte membawa nomor arm, nomor urut, energi paket sebelumnya,
tegangan baterai, dan percepatan tiga sumbu.

**Parameter deteksi diturunkan dari dua syarat, bukan dipilih.** Durasi
pemindaian harus melampaui selang pancaran beacon agar beacon pasti tertangkap;
dan jumlah durasi tidur ditambah dua kali durasi pemindaian tidak boleh
melampaui jendela kontak terpendek pada matriks skenario. Melanggar syarat
pertama membatasi peluang deteksi pada rasio durasi pindai terhadap selang
beacon, berapa pun panjang jendela kontaknya.

**Kepergian kendaraan dideteksi lewat ACK, bukan pemindaian ulang.** Memindai
sesudah setiap paket menghabiskan waktu berkali lipat dibandingkan siklus
transmisinya sendiri, sehingga laju paket runtuh. RSSI paket ACK dan kegagalan
menerima ACK sudah cukup, dan tersedia tanpa biaya tambahan.

**Jendela ACK ditutup oleh rentang nomor urut, bukan oleh jumlah penerimaan.**
Node menghitung paket yang dikirim, sedangkan gateway hanya melihat yang tiba.
Bila keduanya dipakai bersamaan, ACK hanya terkirim ketika kesepuluh paket
sampai utuh, sehingga PDR yang dilaporkan selalu bernilai 1,0 dan kehilangan
paket tidak pernah terlihat. Karena nomor urut sudah dibawa pada muatan paket,
gateway dapat menutup jendela berdasarkan rentangnya lalu melaporkan berapa
yang benar-benar tiba. Tersedia pula penutupan paksa sesudah 800 ms diam, agar
kehilangan paket terakhir tidak membuang sembilan paket yang berhasil sampai.

## Status

Firmware belum pernah dijalankan pada perangkat keras. Kalibrasi INA219 dan
ambang RSSI belum dilakukan, sehingga `E_MAKS_mJ` dan `RSSI_DETECT` di
`node/src/config/params.h` masih berupa nilai awal. Node mencetak peringatan
saat boot selama keduanya belum diisi hasil pengukuran.
