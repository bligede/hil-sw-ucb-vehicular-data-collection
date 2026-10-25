# Dua biled indikator gateway

Firmware gateway 1.1 menggunakan satu biled untuk LoRa dan satu biled untuk
Google Sheets. Pola berjalan dengan millis(); tidak ada jeda tambahan pada
jalur penerimaan paket. GPIO diperbarui hanya saat warna berubah.

## Sesuaikan dengan kabel aktual

Isi src/config/pin_map.h sesudah mengidentifikasi GPIO dan jenis biled.
Nilai -1/-1 menonaktifkan satu biled, sehingga firmware tidak menggerakkan
GPIO yang belum diketahui. Kedua pin satu biled harus diisi bersamaan.

Pemetaan kabel yang dikonfirmasi pengguna:

~~~cpp
#define LED_LORA_MERAH       26
#define LED_LORA_HIJAU       25
#define LED_SHEETS_MERAH     32
#define LED_SHEETS_HIJAU     27
~~~

- Biled **3 kaki, common cathode ke GND**: DUA_KAKI=0, AKTIF_LOW=0.
- Biled **3 kaki, common anode ke 3,3 V**: DUA_KAKI=0, AKTIF_LOW=1.
- Biled **2 kaki, LED antiparalel**: DUA_KAKI=1. Pin MERAH HIGH dan pin
  HIJAU LOW menyalakan merah; polaritas sebaliknya menyalakan hijau. Kedua
  pin LOW mematikan LED. Jika warna terbalik, tukar pemetaan dua pin.

Opsi DUA_KAKI dan AKTIF_LOW tersedia terpisah untuk LoRa dan Sheets.
Pengguna mengonfirmasi kedua biled 3 kaki, common GND, dan sudah memiliki
resistor. Konfigurasi aktif keduanya adalah DUA_KAKI=0 dan AKTIF_LOW=0.
Gunakan resistor pembatas arus atau modul yang sudah memiliki resistor.
Untuk tiga kaki, masing-masing jalur warna memerlukan pembatas arus.
Matikan daya ketika memperbaiki kabel. Kaki bersama bukan ke 5 V.

Firmware menolak pin flash, input-only, serial, pin yang dipakai LoRa/LCD,
dan GPIO yang dipakai dua biled sekaligus. GPIO0/12/15 memengaruhi boot;
pilih GPIO25/26/32/33 jika merancang wiring baru. Bila pin lain sudah dipakai,
periksa dampak rangkaiannya saat reset.

Rujukan boot ESP32: [Espressif — Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

## Arti pola

Saat boot normal, dua warna tiap biled diuji bergantian selama 1,2 detik.
Setelah itu pola berikut berlaku.

| Biled | Pola | Arti |
| --- | --- | --- |
| LoRa | Hijau pendek tiap 1,5 detik | Radio siap, menunggu DATA. |
| LoRa | Hijau saat paket masuk | DATA diterima gateway; nyala sekitar 140 ms. |
| LoRa | Merah saat ACK keluar | ACK dikirim gateway; nyala sekitar 180 ms. Belum membuktikan ACK diterima node. |
| LoRa | Merah lambat | ID window belum diisi atau sudah 15 detik tanpa DATA sejak penerimaan terakhir; lihat LCD untuk membedakannya. |
| LoRa | Merah cepat, 4 kali/detik | LoRa gagal inisialisasi. |
| Sheets | Mati | Build tanpa UNGGAH_SHEETS, atau GPIO biled belum diisi. |
| Sheets | Merah terus | Kredensial belum siap atau antrean unggah gagal dibuat. |
| Sheets | Merah sekali/detik | Wi-Fi belum tersambung. |
| Sheets | Merah dua kedip berulang | HTTP bermasalah atau terjadi kegagalan POST/baris dibuang; kejadian penghitung memberi peringatan selama 5 detik ketika Wi-Fi tersambung dan uji warna selesai. |
| Sheets | Merah–hijau bergantian | Ada baris dalam antrean atau batch yang belum mendapat konfirmasi unggah. |
| Sheets | Hijau pendek sekali/detik | Wi-Fi siap, belum ada POST DATA/ACK yang terkonfirmasi. |
| Sheets | Hijau setelah unggahan berhasil | Penghitung baris terunggah bertambah; nyala sekitar 350 ms. |
| Sheets | Hijau terus | Pernah ada unggahan berhasil dan saat ini antrean serta batch tertunda kosong, dengan Wi-Fi tersambung tanpa peringatan aktif. |

Peringatan merah lebih diutamakan daripada animasi unggahan sukses.
Kedipan merah LoRa untuk ACK merupakan aktivitas normal. Node yang tidur atau
berada di luar contact window juga bisa membuat indikator LoRa merah lambat.
LED menunjukkan keadaan operasional; verifikasi kelengkapan data tetap memakai
penghitung S, log node/gateway, serta tab LOG.

## Uji dan pemasangan firmware

1. Set GPIO serta polaritas sesuai kabel, lalu build environment gateway.
2. Pasang firmware ke **port gateway aktual**; pemasangan mereset gateway dan
   menghapus antrean RAM. Selesaikan/arsipkan sesi yang sedang berjalan dahulu.
3. Amati animasi boot. Kedua warna setiap biled harus terlihat.
4. Kirim L pada Serial Monitor 115200 baud untuk mengulang animasi cepat.
   Kirim L 20 untuk uji lambat: LED1 merah 5 detik, LED1 hijau 5 detik,
   LED2 merah 5 detik, LED2 hijau 5 detik. L 0 mengakhiri uji; durasi maksimum
   60 detik. Radio/jaringan tetap berjalan dan pencacah tidak dikosongkan.
5. Kirim S untuk melihat konfigurasi, warna yang diperintahkan, serta level
   GPIO merah/hijau yang dibaca. Pada common GND: merah=1/0, hijau=0/1,
   mati=0/0. Level GPIO bukan pengukuran arus atau bukti warna fisik.
6. Dengan ID window diisi, hidupkan node: LoRa berkedip hijau untuk DATA dan
   merah untuk ACK; Sheets mengikuti proses unggah.

~~~powershell
Set-Location "D:\Wahyu\TESIS\firmware\gateway"
pio run -e esp32dev_sheets
pio run -e esp32dev_sheets -t upload --upload-port COM_AKTUAL
pio device monitor --port COM_AKTUAL --baud 115200
~~~

Untuk uji empat fase sambil menyimpan pembacaan GPIO, jalankan alat berikut
dengan Python yang memiliki pySerial. Tutup monitor lain sebelum memakai port.
Alat menjaga serial terbuka sepanjang uji, memeriksa balasan perintah L,
meminta S pada setiap fase, lalu mengembalikan LED ke status normal.

~~~powershell
Set-Location "D:\Wahyu\TESIS\firmware"
python tools/uji_biled_gateway.py --port COM11 --detik 40
~~~

Bukti terakhir disimpan pada gateway/.pio/uji_biled_GPIO.json. Untuk wiring
common GND yang dikonfirmasi, hasil yang diharapkan adalah:

| Fase | GPIO26/25 (LED1 merah/hijau) | GPIO32/27 (LED2 merah/hijau) |
| --- | --- | --- |
| LED1 merah | 1/0 | 0/0 |
| LED1 hijau | 0/1 | 0/0 |
| LED2 merah | 0/0 | 1/0 |
| LED2 hijau | 0/0 | 0/1 |

Jika keempat pola GPIO sudah benar tetapi warna tertentu tidak terlihat,
periksa kabel warna tersebut, resistor, kaki bersama yang benar dan LED
fisiknya. Pembacaan HIGH/LOW tidak menggantikan pengukuran tegangan/arus
atau pengamatan warna. Jangan menukar pemetaan warna berdasarkan status
normal LED1 merah/LED2 hijau saja: status tersebut dapat terjadi saat ID
belum diisi dan Wi-Fi sudah tersambung.

Perubahan ini tidak mengubah payload, parameter radio, aturan ACK, maupun
baris CSV DATA/ACK. Informasi biled dicetak sebagai baris diagnostik #.
