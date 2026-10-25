// secrets.example.h - contoh kredensial untuk build esp32dev_sheets.
//
// Salin berkas ini menjadi secrets.h di folder yang sama, lalu isi nilainya.
// secrets.h masuk .gitignore dan TIDAK boleh di-commit: isinya kata sandi
// hotspot dan kunci akses web app.

#pragma once

// Hotspot ponsel. Gunakan jaringan 2,4 GHz; ESP32 tidak mendukung 5 GHz.
#define WIFI_SSID   "GANTI_SSID_HOTSPOT"
#define WIFI_PASS   "GANTI_PASSWORD_HOTSPOT"

// URL web app hasil Deploy di Apps Script, berakhiran /exec.
#define SHEETS_URL  "GANTI_URL_WEB_APP"

// Harus sama persis dengan Script property TOKEN di Apps Script.
// Hindari tanda petik dan garis miring terbalik di dalam nilainya.
#define SHEETS_TOKEN "GANTI_TOKEN"
