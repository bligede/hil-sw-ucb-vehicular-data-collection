# Gateway ke Google Sheets (tanpa laptop)

Gateway mengirim tiap baris DATA dan ACK ke Google Sheets lewat hotspot ponsel.
Sel B1 lembar KONTROL menggantikan perintah serial `W <id>`, jadi laptop tidak
perlu dibawa. Serial USB tetap mencetak semuanya bila kabelnya terpasang.

```
node --LoRa--> gateway ESP32 --WiFi hotspot--> Apps Script web app --> Google Sheets
```

## Pemasangan (sekali)

**1. Spreadsheet.** Buat spreadsheet baru. Di File > Settings, setel **Locale** ke
`United States`. Tanpa itu, unduhan CSV memakai koma sebagai pemisah desimal dan
`olah_siklus.py` tidak dapat membacanya.

**2. Skrip.** Di spreadsheet itu buka Extensions > Apps Script. Hapus isi bawaan,
tempel seluruh `Code.gs`, simpan. Pilih fungsi `siapkan` lalu Run, dan setujui
izin yang diminta. Dua lembar, `LOG` dan `KONTROL`, akan muncul.

**3. Token.** Di Apps Script buka Project Settings > Script properties > Add script
property: nama `TOKEN`, nilainya kata sandi acak yang panjang (huruf dan angka
saja). Skrip menolak semua permintaan bila properti ini belum diisi.

**4. Deploy.** Deploy > New deployment > jenis **Web app**. Isi:
- Execute as: **Me**
- Who has access: **Anyone**

"Anyone" diperlukan karena ESP32 tidak bisa login Google. Yang melindungi data
adalah TOKEN pada langkah 3. Salin URL yang berakhiran `/exec`.

**5. Kredensial di firmware.** Di `gateway/src/config/`, salin `secrets.example.h`
menjadi `secrets.h` lalu isi `WIFI_SSID`, `WIFI_PASS`, `SHEETS_URL` (dari langkah 4),
dan `SHEETS_TOKEN` (sama dengan langkah 3). `secrets.h` sudah masuk `.gitignore`.

**6. Pasang.**
```
cd gateway
pio run -e esp32dev_sheets -t upload -t monitor
```
Env `esp32dev` (tanpa WiFi) tetap ada dan tidak berubah.

## Pemakaian di lapangan

1. Nyalakan hotspot ponsel (2,4 GHz) sebelum gateway dinyalakan.
2. Sebelum tiap lintasan, ubah **KONTROL!B1** di aplikasi Google Sheets, mis. `S7_SWUCB_01`.
   Gateway membaca sel ini tiap 5 detik. ID baru dipakai begitu tidak ada jendela
   ACK yang sedang berjalan, dan pencacah dikosongkan seperti pada perintah `W`.
3. Pantau KONTROL!B3 sampai B5: waktu data terakhir masuk, jumlah baris di LOG, dan
   jumlah baris yang dibuang gateway.
4. Sesudah sesi: File > Download > CSV pada lembar LOG, lalu
   ```
   python tools/olah_siklus.py "data/sesi.csv" --metode SW-UCB
   ```
   Kolom tambahan (`epoch_s`, `boot`, `batch`, `buang`) diabaikan oleh skrip.

## Perilaku yang perlu diketahui

- **Antrean di gateway menampung 700 baris** (sekitar 60 siklus ACK). Bila hotspot
  atau sinyal seluler hilang lebih lama dari itu, baris paling baru dibuang dan
  dihitung pada kolom `buang` dan KONTROL!B5. Antrean ada di RAM: bila gateway mati
  atau reset, isi antrean hilang. Karena itu **rekam serial di laptop pada uji
  pertama** dan bandingkan jumlah barisnya dengan LOG sebelum mengandalkan jalur
  ini sendirian.
- **Pengiriman ulang aman.** Tiap paket (maksimal 20 baris) punya ID. Bila balasan
  server hilang, gateway mengirim ulang paket yang sama dan server menolak
  duplikatnya.
- **Latensi.** Gateway mengumpulkan baris selama 4 detik atau sampai 20 baris, lalu
  mengirimnya, jadi data muncul di Sheets beberapa detik sampai belasan detik sesudah
  diterima. Angka pastinya belum diukur di lapangan.
- **Jam.** Kolom `epoch_s` berisi waktu UNIX dari NTP, kosong bila gateway belum
  sempat sinkron. `ts_ms` (milidetik sejak gateway menyala) tetap dipakai untuk
  urutan antarpaket.
- **ID window kosong atau salah.** Gateway memakai isi B1 apa adanya, termasuk saat
  baru menyala. Pastikan B1 sudah diganti sebelum lintasan pertama; baris DATA di
  LOG menunjukkan ID yang benar-benar dipakai.
- **TLS tidak memverifikasi sertifikat server** (`setInsecure()`). Lihat catatan di
  `src/modules/unggah_sheets.cpp`.
- **Kuota Apps Script.** Satu paket memanggil satu eksekusi `doPost`. Bila sesi sangat
  panjang, periksa kuota akun Google Anda di dokumentasi Apps Script.
