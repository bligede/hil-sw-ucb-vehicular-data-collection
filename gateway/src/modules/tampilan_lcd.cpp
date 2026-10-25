#include "tampilan_lcd.h"
#include <Wire.h>
#include "../config/pin_map.h"

#define LCD_KOLOM         20
#define LCD_BARIS         4
#define LCD_I2C_HZ        100000UL   // batas PCF8574; jangan dinaikkan ke 400 kHz
#define LCD_JEDA_UPDATE   120UL      // ms antar panggilan yang boleh menulis

// Bit pada byte PCF8574
static const uint8_t BIT_RS = 0x01;
static const uint8_t BIT_EN = 0x04;
static const uint8_t BIT_BL = 0x08;

static uint8_t  s_alamat = 0;
static char     s_tampil[LCD_BARIS][LCD_KOLOM + 1];   // apa yang sedang ada di layar
static uint32_t s_t_tulis = 0;
static uint8_t  s_baris_berikut = 0;

static const uint8_t ALAMAT_BARIS[LCD_BARIS] = {0x00, 0x40, 0x14, 0x54};

// Mengirim satu byte (command atau data) sebagai dua nibble dalam SATU
// transaksi I2C: tiap nibble berupa pulsa EN naik lalu turun.
static void kirim_byte(uint8_t nilai, uint8_t rs) {
    uint8_t hi = (nilai & 0xF0) | BIT_BL | rs;
    uint8_t lo = ((nilai << 4) & 0xF0) | BIT_BL | rs;
    Wire.beginTransmission(s_alamat);
    Wire.write(hi | BIT_EN);
    Wire.write(hi);
    Wire.write(lo | BIT_EN);
    Wire.write(lo);
    Wire.endTransmission();
}

static void perintah(uint8_t c) { kirim_byte(c, 0); }

static void kirim_nibble_awal(uint8_t nibble) {
    uint8_t d = ((nibble << 4) & 0xF0) | BIT_BL;
    Wire.beginTransmission(s_alamat);
    Wire.write(d | BIT_EN);
    Wire.write(d);
    Wire.endTransmission();
}

static bool ada_perangkat(uint8_t a) {
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

bool lcd_mulai() {
    Wire.begin(I2C_SDA, I2C_SCL, LCD_I2C_HZ);

    s_alamat = 0;
    for (uint8_t a = 0x20; a <= 0x3F && !s_alamat; a++) {
        if (a > 0x27 && a < 0x38) continue;
        if (ada_perangkat(a)) s_alamat = a;
    }
    if (!s_alamat) return false;

    // Urutan inisialisasi HD44780 mode 4 bit (lembar data, Gambar 24).
    delay(50);
    kirim_nibble_awal(0x03); delay(5);
    kirim_nibble_awal(0x03); delay(5);
    kirim_nibble_awal(0x03); delayMicroseconds(200);
    kirim_nibble_awal(0x02); delayMicroseconds(200);
    perintah(0x28);                 // 4 bit, 2 baris logis (dipakai juga oleh 20x4), font 5x8
    perintah(0x08);                 // layar mati
    perintah(0x01); delay(3);       // hapus
    perintah(0x06);                 // kursor bergerak ke kanan
    perintah(0x0C);                 // layar nyala, tanpa kursor

    for (int i = 0; i < LCD_BARIS; i++) {
        memset(s_tampil[i], ' ', LCD_KOLOM);
        s_tampil[i][LCD_KOLOM] = '\0';
    }
    return true;
}

uint8_t lcd_alamat() { return s_alamat; }

// Menyalin teks ke buffer baris 20 karakter, dipotong atau diisi spasi.
static void isi_baris(char* tujuan, const char* sumber) {
    size_t i = 0;
    for (; sumber && sumber[i] && i < LCD_KOLOM; i++) tujuan[i] = sumber[i];
    for (; i < LCD_KOLOM; i++) tujuan[i] = ' ';
    tujuan[LCD_KOLOM] = '\0';
}

// Menulis hanya rentang karakter yang berbeda dari isi layar saat ini.
static void tulis_baris(uint8_t b, const char* baru) {
    int awal = -1, akhir = -1;
    for (int i = 0; i < LCD_KOLOM; i++) {
        if (baru[i] != s_tampil[b][i]) {
            if (awal < 0) awal = i;
            akhir = i;
        }
    }
    if (awal < 0) return;
    perintah(0x80 | (ALAMAT_BARIS[b] + awal));
    for (int i = awal; i <= akhir; i++) {
        kirim_byte((uint8_t)baru[i], BIT_RS);
        s_tampil[b][i] = baru[i];
    }
}

void lcd_pesan(const char* b1, const char* b2, const char* b3, const char* b4) {
    if (!s_alamat) return;
    const char* teks[LCD_BARIS] = {b1, b2, b3, b4};
    char baris[LCD_KOLOM + 1];
    for (uint8_t b = 0; b < LCD_BARIS; b++) {
        isi_baris(baris, teks[b]);
        tulis_baris(b, baris);
    }
}

void lcd_perbarui(const TampilanGateway& t) {
    if (!s_alamat) return;
    if (millis() - s_t_tulis < LCD_JEDA_UPDATE) return;
    s_t_tulis = millis();

    char buf[48];
    char baris[LCD_KOLOM + 1];
    const uint8_t b = s_baris_berikut;
    s_baris_berikut = (uint8_t)((s_baris_berikut + 1) % LCD_BARIS);

    switch (b) {
    case 0:   // ID window; tanda seru bila belum diset
        if (strcmp(t.id_window, "BELUM-DISET") == 0) snprintf(buf, sizeof buf, "!BELUM-DISET!");
        else snprintf(buf, sizeof buf, "%s", t.id_window);
        break;
    case 1:
        snprintf(buf, sizeof buf, "Pkt %u/%u Tot %lu",
                 (unsigned)t.n_diterima, (unsigned)t.n_per_jendela, (unsigned long)t.pkt_total);
        break;
    case 2:
        if (!t.ada_paket) {
            snprintf(buf, sizeof buf, "Belum ada paket");
        } else {
            unsigned umur = t.umur_paket_s > 999 ? 999 : (unsigned)t.umur_paket_s;
            snprintf(buf, sizeof buf, "R%d S%.1f A%u %us",
                     t.rssi, t.snr, (unsigned)t.arm, umur);
        }
        break;
    default:
        if (!t.ada_paket) snprintf(buf, sizeof buf, "ACK%lu %s", (unsigned long)t.ack_total, t.ringkas_sheets);
        else snprintf(buf, sizeof buf, "V%.2f ACK%lu n%u %s", t.vnode,
                      (unsigned long)t.ack_total, (unsigned)t.ack_terakhir, t.ringkas_sheets);
        break;
    }
    isi_baris(baris, buf);
    tulis_baris(b, baris);
}
