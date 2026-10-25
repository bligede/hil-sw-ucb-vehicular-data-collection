// unggah_sheets.cpp - lihat unggah_sheets.h untuk gambaran umum.
//
// Rancangan:
//   * loop() memanggil sheets_antre_*, yang menyalin baris ke antrean FreeRTOS
//     tanpa menunggu apa pun.
//   * task_unggah (core 0) menarik baris dari antrean, mengelompokkannya per
//     paket (BATCH_MAKS baris, atau FLUSH_MS sesudah baris pertama), lalu
//     mengirimnya dengan HTTPS POST ke web app Apps Script.
//   * Sebuah paket yang gagal dikirim DIBEKUKAN: isi dan ID-nya tidak berubah
//     sampai berhasil. Server mengingat ID paket yang sudah diterima, sehingga
//     pengiriman ulang (misalnya karena balasan hilang di jalan) tidak
//     menggandakan baris di Sheets.
//   * Task yang sama membaca sel B1 lembar KONTROL tiap POLL_WINDOW_MS untuk
//     mengganti id_window tanpa laptop dan kabel serial.
//
// Catatan keamanan: koneksi memakai setInsecure(), artinya sertifikat server
// tidak diverifikasi. Token tetap terenkripsi di jalan, tetapi pihak yang
// berada di tengah jalur hotspot dapat menyamar sebagai server. Untuk data
// penelitian ini risikonya diterima; ganti dengan setCACertBundle bila
// gateway dipakai di jaringan yang tidak dipercaya.

#ifdef UNGGAH_SHEETS

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_system.h>
#include "../config/secrets.h"
#include "unggah_sheets.h"

#define ANTREAN_MAKS     700        // sekitar 48 KB heap
#define BATCH_MAKS       20
#define FLUSH_MS         4000UL
#define POLL_WINDOW_MS   5000UL
#define WIFI_COBA_MS     15000UL
#define HTTP_CONNECT_MS  8000
#define HTTP_TIMEOUT_MS  12000
#define JEDA_ULANG_MS    2500UL
#define STACK_TASK       12288
#define EPOCH_MINIMUM    1700000000UL   // di bawah ini jam belum tersinkron

struct Baris {
    uint8_t  jenis;       // 0 = DATA, 1 = ACK
    uint8_t  arm;
    uint16_t seq;         // pada ACK: jumlah paket diterima
    int16_t  ax, ay, az;
    int16_t  rssi;
    uint32_t ts_ms;
    uint32_t epoch;       // 0 bila NTP belum tersinkron
    float    e_prev;
    float    vbatt;
    float    snr;
    char     id[32];
};

static QueueHandle_t g_antrean = nullptr;
static char          g_boot[9];
static uint32_t      g_no_paket = 0;

static volatile uint32_t g_terkirim = 0;
static volatile uint32_t g_gagal    = 0;
static volatile uint32_t g_buang    = 0;
static volatile int      g_kode     = 0;
static volatile bool     g_wifi     = false;
static volatile bool     g_siap     = false;   // kredensial sudah diisi
static volatile uint16_t g_pending  = 0;       // batch menunggu konfirmasi POST

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static char         g_id_baru[32];
static bool         g_id_siap = false;

static char g_buf[4608];   // badan POST; di segmen data, bukan di stack task


static uint32_t epoch_kini() {
    time_t t = time(nullptr);
    return (t > (time_t)EPOCH_MINIMUM) ? (uint32_t)t : 0;
}

static void salin_id(char* tujuan, const char* sumber) {
    // Karakter yang bisa merusak JSON diganti agar tidak perlu escape.
    size_t i = 0;
    for (; sumber[i] && i < 31; i++) {
        char c = sumber[i];
        tujuan[i] = (c == '"' || c == '\\' || (uint8_t)c < 0x20) ? '_' : c;
    }
    tujuan[i] = '\0';
}

static void antre(const Baris& b) {
    if (!g_antrean) return;
    if (xQueueSend(g_antrean, &b, 0) != pdTRUE) g_buang++;
}

void sheets_antre_data(uint32_t ts_ms, const char* id_window, uint16_t seq,
                       uint8_t arm, float e_prev_mJ, float vbatt, int rssi,
                       float snr, int16_t ax, int16_t ay, int16_t az) {
    Baris b = {};
    b.jenis = 0; b.arm = arm; b.seq = seq;
    b.ax = ax; b.ay = ay; b.az = az; b.rssi = (int16_t)rssi;
    b.ts_ms = ts_ms; b.epoch = epoch_kini();
    b.e_prev = e_prev_mJ; b.vbatt = vbatt; b.snr = snr;
    salin_id(b.id, id_window);
    antre(b);
}

void sheets_antre_ack(uint32_t ts_ms, const char* id_window, uint8_t n_diterima) {
    Baris b = {};
    b.jenis = 1; b.seq = n_diterima;
    b.ts_ms = ts_ms; b.epoch = epoch_kini();
    salin_id(b.id, id_window);
    antre(b);
}

bool sheets_window_baru(char* keluar, size_t n) {
    bool ada;
    portENTER_CRITICAL(&g_mux);
    ada = g_id_siap;
    if (ada) {
        strlcpy(keluar, g_id_baru, n);
        g_id_siap = false;
    }
    portEXIT_CRITICAL(&g_mux);
    return ada;
}

void sheets_status(char* keluar, size_t n) {
    snprintf(keluar, n, "sheets: kredensial=%s wifi=%s ntp=%s antre=%u terkirim=%lu gagal=%lu buang=%lu http=%d",
             g_siap ? "ok" : "BELUM-DIISI", g_wifi ? "ya" : "tidak",
             epoch_kini() ? "ya" : "belum",
             g_antrean ? (unsigned)uxQueueMessagesWaiting(g_antrean) : 0u,
             (unsigned long)g_terkirim, (unsigned long)g_gagal,
             (unsigned long)g_buang, (int)g_kode);
}

void sheets_ringkas(char* keluar, size_t n) {
    if (!g_siap) { snprintf(keluar, n, "W?"); return; }
    snprintf(keluar, n, "W%cQ%u", g_wifi ? '+' : '-',
             g_antrean ? (unsigned)uxQueueMessagesWaiting(g_antrean) : 0u);
}

StatusUnggahSheets sheets_keadaan() {
    StatusUnggahSheets s;
    s.aktif = true;
    s.siap = g_siap && g_antrean != nullptr;
    s.wifi = g_wifi;
    s.antre = g_antrean ? (uint16_t)uxQueueMessagesWaiting(g_antrean) : 0;
    s.pending = g_pending;
    s.terkirim = g_terkirim;
    s.gagal = g_gagal;
    s.buang = g_buang;
    s.http = g_kode;
    return s;
}

// ---------------------------------------------------------------------------
// HTTP. Apps Script menjawab POST dan GET dengan redirect 302 ke
// script.googleusercontent.com; balasan sebenarnya ada di alamat tujuan dan
// diambil dengan GET. Redirect diikuti manual agar jenis permintaan jelas.

static bool http_lakukan(const char* url_awal, const char* body, String& balasan) {
    String url = url_awal;
    for (int hop = 0; hop < 4; hop++) {
        WiFiClientSecure klien;
        klien.setInsecure();
        HTTPClient http;
        http.setReuse(false);
        http.setConnectTimeout(HTTP_CONNECT_MS);
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char* hdr[] = {"Location"};
        http.collectHeaders(hdr, 1);
        if (!http.begin(klien, url)) return false;

        int kode;
        if (hop == 0 && body) {
            http.addHeader("Content-Type", "text/plain");
            kode = http.POST((uint8_t*)body, strlen(body));
        } else {
            kode = http.GET();
        }
        g_kode = kode;

        if (kode == 301 || kode == 302 || kode == 303 || kode == 307 || kode == 308) {
            url = http.header("Location");
            http.end();
            if (url.length() == 0) return false;
            continue;
        }
        if (kode == 200) {
            balasan = http.getString();
            http.end();
            return true;
        }
        http.end();
        return false;
    }
    return false;
}

// Menyusun badan POST dari paling banyak n_ingin baris. Mengembalikan jumlah
// baris yang muat; sisanya menunggu paket berikutnya.
static int bangun_body(const Baris* b, int n_ingin, const char* id_paket) {
    int pos = snprintf(g_buf, sizeof g_buf,
                       "{\"t\":\"%s\",\"b\":\"%s\",\"boot\":\"%s\",\"buang\":%lu,\"r\":[",
                       SHEETS_TOKEN, id_paket, g_boot, (unsigned long)g_buang);
    int muat = 0;
    for (int i = 0; i < n_ingin; i++) {
        char baris[220];
        int m;
        const char* koma = muat ? "," : "";
        if (b[i].jenis == 0) {
            m = snprintf(baris, sizeof baris,
                         "%s[\"DATA\",%lu,%lu,\"%s\",%u,%u,%.1f,%.2f,%d,%.1f,%d,%d,%d]",
                         koma, (unsigned long)b[i].ts_ms, (unsigned long)b[i].epoch, b[i].id,
                         (unsigned)b[i].seq, (unsigned)b[i].arm, b[i].e_prev, b[i].vbatt,
                         (int)b[i].rssi, b[i].snr, (int)b[i].ax, (int)b[i].ay, (int)b[i].az);
        } else {
            m = snprintf(baris, sizeof baris,
                         "%s[\"ACK\",%lu,%lu,\"%s\",%u,null,null,null,null,null,null,null,null]",
                         koma, (unsigned long)b[i].ts_ms, (unsigned long)b[i].epoch, b[i].id,
                         (unsigned)b[i].seq);
        }
        if (m < 0 || m >= (int)sizeof baris) break;
        if (pos + m + 4 >= (int)sizeof g_buf) break;
        memcpy(g_buf + pos, baris, m);
        pos += m;
        muat++;
    }
    snprintf(g_buf + pos, sizeof g_buf - pos, "]}");
    return muat;
}

static bool kredensial_siap() {
    return strncmp(WIFI_SSID, "GANTI", 5) != 0 && strncmp(SHEETS_URL, "GANTI", 5) != 0 &&
           strncmp(SHEETS_TOKEN, "GANTI", 5) != 0;
}

static void baca_window(String& id_terakhir) {
    String url = String(SHEETS_URL) + "?aksi=window&t=" + SHEETS_TOKEN;
    String r;
    if (!http_lakukan(url.c_str(), nullptr, r)) return;
    const char* kunci = "\"id_window\":\"";
    int i = r.indexOf(kunci);
    if (i < 0) return;
    i += strlen(kunci);
    int j = r.indexOf('"', i);
    if (j < 0) return;
    String id = r.substring(i, j);
    id.trim();
    if (id.length() == 0 || id == id_terakhir) return;
    id_terakhir = id;
    portENTER_CRITICAL(&g_mux);
    strlcpy(g_id_baru, id.c_str(), sizeof g_id_baru);
    g_id_siap = true;
    portEXIT_CRITICAL(&g_mux);
}

static void task_unggah(void*) {
    static Baris pending[BATCH_MAKS];
    int      n_pending = 0;
    int      muat_beku = 0;
    bool     beku = false;            // paket sudah pernah dicoba kirim
    char     id_paket[24] = "";
    uint32_t t_awal = 0, t_poll = 0, t_wifi = 0;
    bool     ntp_diminta = false;
    String   id_sel_terakhir;

    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    t_wifi = millis();

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            g_wifi = false;
            if (millis() - t_wifi > WIFI_COBA_MS) {
                WiFi.disconnect();
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                t_wifi = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        g_wifi = true;
        if (!ntp_diminta) {
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            ntp_diminta = true;
        }

        if (!beku) {
            while (n_pending < BATCH_MAKS &&
                   xQueueReceive(g_antrean, &pending[n_pending], 0) == pdTRUE) {
                if (n_pending == 0) t_awal = millis();
                n_pending++;
            }
            g_pending = (uint16_t)n_pending;
        }

        bool saatnya = beku || n_pending >= BATCH_MAKS ||
                       (n_pending > 0 && millis() - t_awal >= FLUSH_MS);
        if (saatnya && n_pending > 0) {
            if (!beku) {
                snprintf(id_paket, sizeof id_paket, "%s-%lu", g_boot, (unsigned long)++g_no_paket);
                beku = true;
                muat_beku = 0;
            }
            int muat = bangun_body(pending, muat_beku ? muat_beku : n_pending, id_paket);
            if (muat_beku == 0) muat_beku = muat;
            String r;
            if (http_lakukan(SHEETS_URL, g_buf, r) && r.indexOf("\"ok\":true") >= 0) {
                g_terkirim += muat;
                n_pending -= muat;
                g_pending = (uint16_t)n_pending;
                if (n_pending > 0) memmove(pending, pending + muat, n_pending * sizeof(Baris));
                beku = false;
                muat_beku = 0;
                t_awal = millis();
            } else {
                g_gagal++;
                vTaskDelay(pdMS_TO_TICKS(JEDA_ULANG_MS));
            }
            continue;
        }

        if (millis() - t_poll >= POLL_WINDOW_MS) {
            baca_window(id_sel_terakhir);
            t_poll = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void sheets_mulai() {
    snprintf(g_boot, sizeof g_boot, "%08lX", (unsigned long)esp_random());
    g_siap = kredensial_siap();
    g_antrean = xQueueCreate(ANTREAN_MAKS, sizeof(Baris));
    if (!g_antrean) {
        Serial.println("# PERINGATAN: antrean Sheets gagal dibuat (heap kurang), unggah nonaktif");
        return;
    }
    if (!g_siap) {
        Serial.println("# PERINGATAN: secrets.h belum diisi, unggah Sheets tidak berjalan");
        vQueueDelete(g_antrean);     // tanpa task pengosong, antrean hanya akan penuh
        g_antrean = nullptr;
        return;
    }
    xTaskCreatePinnedToCore(task_unggah, "unggah", STACK_TASK, nullptr, 1, nullptr, 0);
}

#endif  // UNGGAH_SHEETS
