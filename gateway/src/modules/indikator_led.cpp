#include "indikator_led.h"
#include "../config/pin_map.h"

namespace {

enum class Warna : uint8_t { Mati, Merah, Hijau };

// ESP32 klasik: GPIO6..11 untuk flash, GPIO1/3 untuk serial, GPIO34..39
// hanya input. Pin radio dan LCD yang sedang dipakai juga tidak boleh diambil.
constexpr bool pin_boleh(int pin) {
    return pin == -1 ||
           (pin >= 0 && pin <= 33 && pin != 1 && pin != 3 &&
            !(pin >= 6 && pin <= 11) && pin != 20 && pin != 24 &&
            !(pin >= 28 && pin <= 31) &&
            pin != LORA_NSS && pin != LORA_RESET && pin != LORA_DIO0 &&
            pin != LORA_SCK && pin != LORA_MISO && pin != LORA_MOSI &&
            pin != I2C_SDA && pin != I2C_SCL);
}

constexpr bool pasangan_boleh(int merah, int hijau) {
    return (merah == -1 && hijau == -1) ||
           (merah >= 0 && hijau >= 0 && merah != hijau &&
            pin_boleh(merah) && pin_boleh(hijau));
}

constexpr bool pin_berbeda(int a, int b) {
    return a == -1 || b == -1 || a != b;
}

static_assert(pasangan_boleh(LED_LORA_MERAH, LED_LORA_HIJAU),
              "GPIO biled LoRa tidak valid/bertabrakan. Set dua pin atau -1/-1.");
static_assert(pasangan_boleh(LED_SHEETS_MERAH, LED_SHEETS_HIJAU),
              "GPIO biled Sheets tidak valid/bertabrakan. Set dua pin atau -1/-1.");
static_assert(pin_berbeda(LED_LORA_MERAH, LED_SHEETS_MERAH) &&
              pin_berbeda(LED_LORA_MERAH, LED_SHEETS_HIJAU) &&
              pin_berbeda(LED_LORA_HIJAU, LED_SHEETS_MERAH) &&
              pin_berbeda(LED_LORA_HIJAU, LED_SHEETS_HIJAU),
              "Dua biled harus memakai empat GPIO berbeda.");
static_assert((LED_LORA_AKTIF_LOW == 0 || LED_LORA_AKTIF_LOW == 1) &&
              (LED_SHEETS_AKTIF_LOW == 0 || LED_SHEETS_AKTIF_LOW == 1) &&
              (LED_LORA_DUA_KAKI == 0 || LED_LORA_DUA_KAKI == 1) &&
              (LED_SHEETS_DUA_KAKI == 0 || LED_SHEETS_DUA_KAKI == 1),
              "Opsi polaritas/dua kaki biled harus 0 atau 1.");

struct Biled {
    int merah;
    int hijau;
    bool aktif_low;
    bool dua_kaki;
    Warna warna;

    bool tersedia() const { return merah >= 0 && hijau >= 0; }
    int level_merah() const { return tersedia() ? digitalRead(merah) : -1; }
    int level_hijau() const { return tersedia() ? digitalRead(hijau) : -1; }

    void mulai() {
        if (!tersedia()) return;
        const uint8_t mati = !dua_kaki && aktif_low ? HIGH : LOW;
        digitalWrite(merah, mati);
        digitalWrite(hijau, mati);
        pinMode(merah, OUTPUT);
        pinMode(hijau, OUTPUT);
        warna = Warna::Mati;
    }

    void tampil(Warna baru) {
        if (!tersedia() || warna == baru) return;
        // Matikan kedua sisi sebelum berganti warna: tidak ada keadaan dua
        // warna ON sekaligus, termasuk ketika membalik biled antiparalel.
        const uint8_t mati = !dua_kaki && aktif_low ? HIGH : LOW;
        digitalWrite(merah, mati);
        digitalWrite(hijau, mati);
        if (dua_kaki) {
            if (baru == Warna::Merah) digitalWrite(merah, HIGH);
            if (baru == Warna::Hijau) digitalWrite(hijau, HIGH);
        } else {
            const uint8_t nyala = aktif_low ? LOW : HIGH;
            if (baru == Warna::Merah) digitalWrite(merah, nyala);
            if (baru == Warna::Hijau) digitalWrite(hijau, nyala);
        }
        warna = baru;
    }
};

Biled radio{LED_LORA_MERAH, LED_LORA_HIJAU,
            LED_LORA_AKTIF_LOW != 0, LED_LORA_DUA_KAKI != 0, Warna::Mati};
Biled cloud{LED_SHEETS_MERAH, LED_SHEETS_HIJAU,
            LED_SHEETS_AKTIF_LOW != 0, LED_SHEETS_DUA_KAKI != 0, Warna::Mati};
bool radio_siap = false;
bool radio_gagal = false;
bool ada_data = false;
bool ada_ack = false;
bool sedang_uji = false;
bool ada_sukses = false;
bool ada_gagal = false;
uint32_t t_data = 0, t_ack = 0, t_uji = 0, t_sukses = 0, t_gagal = 0;
uint32_t durasi_uji = 1200, langkah_uji = 150;
uint32_t terkirim_lama = 0, gagal_lama = 0, buang_lama = 0;

Warna kedip(Warna warna, uint32_t now, uint32_t periode, uint32_t nyala) {
    return now % periode < nyala ? warna : Warna::Mati;
}

Warna pola_radio(bool window_siap, uint32_t now) {
    if (radio_gagal) return kedip(Warna::Merah, now, 250, 125);
    if (!radio_siap) return kedip(Warna::Merah, now, 1000, 200);
    if (ada_ack && now - t_ack < 180) return Warna::Merah;
    if (ada_data && now - t_data < 140) return Warna::Hijau;
    if (!window_siap || (ada_data && now - t_data >= 15000)) {
        return kedip(Warna::Merah, now, 1000, 200);
    }
    return kedip(Warna::Hijau, now, 1500, 80);
}

Warna pola_cloud(const StatusUnggahSheets& s, uint32_t now) {
    if (!s.aktif) return Warna::Mati;
    if (!s.siap) return Warna::Merah;
    if (!s.wifi) return kedip(Warna::Merah, now, 1000, 300);
    if (s.http < 0 || s.http >= 400 || (ada_gagal && now - t_gagal < 5000)) {
        const uint32_t fase = now % 1200;
        return fase < 120 || (fase >= 240 && fase < 360) ? Warna::Merah : Warna::Mati;
    }
    if (ada_sukses && now - t_sukses < 350) return Warna::Hijau;
    if (s.antre > 0 || s.pending > 0) {
        return now % 400 < 200 ? Warna::Merah : Warna::Hijau;
    }
    if (s.terkirim > 0) return Warna::Hijau;
    return kedip(Warna::Hijau, now, 1000, 120);
}

const char* jenis(const Biled& led) {
    if (!led.tersedia()) return "OFF";
    if (led.dua_kaki) return "2k";
    return led.aktif_low ? "3k-3V3" : "3k-GND";
}

const char* warna_nama(Warna warna) {
    switch (warna) {
        case Warna::Merah: return "merah";
        case Warna::Hijau: return "hijau";
        default: return "mati";
    }
}

}  // namespace

void indikator_mulai() {
    radio.mulai();
    cloud.mulai();
    radio.tampil(Warna::Merah);
    cloud.tampil(Warna::Merah);
    char st[180];
    indikator_status(st, sizeof st);
    Serial.printf("# %s\n", st);
    if (!radio.tersedia() || !cloud.tersedia()) {
        Serial.println("# biled dengan pin=-1 nonaktif; isi GPIO sesuai wiring di config/pin_map.h");
    }
}

void indikator_radio_siap() { radio_siap = true; }

void indikator_radio_gagal() {
    radio_gagal = true;
    sedang_uji = false;
    radio.tampil(Warna::Merah);
}

void indikator_data() {
    ada_data = true;
    t_data = millis();
    if (!sedang_uji && !radio_gagal) radio.tampil(Warna::Hijau);
}

void indikator_ack() {
    ada_ack = true;
    t_ack = millis();
    if (!sedang_uji && !radio_gagal) radio.tampil(Warna::Merah);
}

void indikator_uji(uint32_t durasi_ms) {
    if (durasi_ms > 60000) durasi_ms = 60000;
    durasi_uji = durasi_ms;
    langkah_uji = durasi_ms == 1200 ? 150 : max(1UL, (unsigned long)durasi_ms / 4);
    t_uji = millis();
    sedang_uji = durasi_ms > 0 && !radio_gagal;
}

void indikator_perbarui(bool window_siap, const StatusUnggahSheets& sheets) {
    const uint32_t now = millis();
    if (sheets.terkirim != terkirim_lama) {
        ada_sukses = true;
        t_sukses = now;
        terkirim_lama = sheets.terkirim;
    }
    if (sheets.gagal != gagal_lama || sheets.buang != buang_lama) {
        ada_gagal = true;
        t_gagal = now;
        gagal_lama = sheets.gagal;
        buang_lama = sheets.buang;
    }
    if (sedang_uji) {
        const uint32_t umur = now - t_uji;
        if (umur < durasi_uji) {
            const unsigned langkah = (umur / langkah_uji) % 4;
            radio.tampil(langkah == 0 ? Warna::Merah :
                         langkah == 1 ? Warna::Hijau : Warna::Mati);
            cloud.tampil(langkah == 2 ? Warna::Merah :
                         langkah == 3 ? Warna::Hijau : Warna::Mati);
            return;
        }
        sedang_uji = false;
    }
    radio.tampil(pola_radio(window_siap, now));
    cloud.tampil(pola_cloud(sheets, now));
}

void indikator_status(char* keluar, size_t n) {
    snprintf(keluar, n, "biled: LoRa=%d/%d %s [%s GPIO=%d/%d] Sheets=%d/%d %s [%s GPIO=%d/%d]",
             radio.merah, radio.hijau, jenis(radio), warna_nama(radio.warna),
             radio.level_merah(), radio.level_hijau(),
             cloud.merah, cloud.hijau, jenis(cloud), warna_nama(cloud.warna),
             cloud.level_merah(), cloud.level_hijau());
}
