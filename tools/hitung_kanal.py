#!/usr/bin/env python3
"""Hitungan kanal dan kadens keputusan untuk naskah dan SOP.

Semua angka yang dipakai di H-Minor 2 dan H-Minor 3 berasal dari berkas ini,
supaya dapat diperiksa ulang. Tiga kelompok hitungan:

  1. Waktu udara LoRa per arm (rumus Semtech SX1276 bagian 4.1.1.7).
  2. Pergeseran Doppler, waktu koherensi, dan jarak koherensi pada 433 MHz.
  3. Kadens keputusan: diukur dari rekaman gateway, lalu dipakai untuk
     menghitung jumlah keputusan per contact window dan jarak tempuh
     kendaraan selama satu keputusan.

Pemakaian:  python tools/hitung_kanal.py [berkas_gateway.csv]
"""

import csv
import math
import statistics as st
import sys

C = 2.99792458e8
F_HZ = 433e6
SF = 8
BW = 125e3
PREAMBLE = 8
PL_BAIT = 12          # payload_pack menyusun 12 bait
HEADER_EKSPLISIT = 1  # H = 0 pada rumus
CRC_AKTIF = 1
LDRO = 0              # SF8, mode laju rendah tidak dipakai
ARM = ((1, 5, 5), (2, 10, 5), (3, 14, 7), (4, 20, 8))   # nomor, TP dBm, CR 4/x
JEDA_PAKET_MS = 500
ACK_SETIAP_N = 10
REKAMAN = r"D:/Wahyu/TESIS/firmware/data/tahap43/dekat_idx_02_gateway.csv"

# Durasi window pada matriks skenario: 2R/v
SKENARIO = (("S1", 5, 50), ("S2", 5, 100), ("S3", 5, 150),
            ("S4", 15, 50), ("S5", 15, 100), ("S6", 15, 150),
            ("S7", 30, 50), ("S8", 30, 100), ("S9", 30, 150))


def waktu_udara_ms(cr_4x):
    """Waktu udara satu paket, milidetik. cr_4x = 5..8 untuk 4/5..4/8."""
    cr = cr_4x - 4
    ts = (2 ** SF) / BW
    t_pre = (PREAMBLE + 4.25) * ts
    n = 8 + max(math.ceil((8 * PL_BAIT - 4 * SF + 28 + 16 * CRC_AKTIF
                           - 20 * (1 - HEADER_EKSPLISIT)) / (4 * (SF - 2 * LDRO))) * (cr + 4), 0)
    return (t_pre + n * ts) * 1e3, n


def doppler(v_kmh):
    v = v_kmh / 3.6
    lam = C / F_HZ
    fd = v / lam
    return v, lam, fd


def kadens_terukur(berkas):
    baris = []
    try:
        with open(berkas, encoding="utf-8", errors="replace") as fh:
            for r in csv.DictReader(l for l in fh if not l.startswith("#")):
                if r.get("jenis") == "DATA":
                    baris.append((int(r["ts_ms"]), int(r["seq"])))
    except OSError:
        return None
    if len(baris) < 50:
        return None
    baris.sort()
    jeda = [b[0] - a[0] for a, b in zip(baris, baris[1:]) if b[1] == a[1] + 1]
    siklus = {}
    for ts, seq in baris:
        siklus.setdefault(seq // ACK_SETIAP_N, []).append(ts)
    awal = sorted((min(v), k) for k, v in siklus.items())
    d = [b[0] - a[0] for a, b in zip(awal, awal[1:]) if b[1] == a[1] + 1]
    return st.median(jeda), st.median(d), len(d)


def main():
    berkas = sys.argv[1] if len(sys.argv) > 1 else REKAMAN

    print("1. WAKTU UDARA PAKET (SF%d, BW %.0f kHz, payload %d bait, CRC aktif, preamble %d)"
          % (SF, BW / 1e3, PL_BAIT, PREAMBLE))
    t = {}
    for nomor, tp, cr in ARM:
        ms, n = waktu_udara_ms(cr)
        t[nomor] = ms
        print("   Arm %d  TP %2d dBm  CR 4/%d  %2d simbol payload  waktu udara %6.1f ms"
              % (nomor, tp, cr, n, ms))
    print("   Durasi simbol %.3f ms" % ((2 ** SF) / BW * 1e3))

    print()
    print("2. DOPPLER DAN KOHERENSI PADA %.0f MHz" % (F_HZ / 1e6))
    print("   %-10s %-9s %-9s %-11s %-11s %-11s %s"
          % ("kecepatan", "v (m/s)", "fd (Hz)", "Tc=1/fd", "Tc=.423/fd", "Tc=9/16pi.fd", "jarak koheren"))
    for v_kmh in (5, 15, 30):
        v, lam, fd = doppler(v_kmh)
        print("   %-10s %-9.2f %-9.2f %-11.1f %-11.1f %-11.1f %.2f m"
              % ("%d km/h" % v_kmh, v, fd, 1e3 / fd, 423 / fd, 9e3 / (16 * math.pi * fd), 0.423 * lam))
    print("   Panjang gelombang %.4f m" % (C / F_HZ))
    print("   Waktu udara Arm 1 sampai Arm 4: %.1f sampai %.1f ms" % (t[1], t[4]))
    for v_kmh in (5, 30):
        _, _, fd = doppler(v_kmh)
        tc = 423 / fd
        print("   Pada %2d km/h, waktu udara = %.2f sampai %.2f kali Tc(0,423/fd) = %.1f ms"
              % (v_kmh, t[1] / tc, t[4] / tc, tc))

    print()
    print("3. KADENS KEPUTUSAN")
    teori = [(ACK_SETIAP_N - 1) * (t[a[0]] + JEDA_PAKET_MS) + t[a[0]] for a in ARM]
    print("   Teori, 10 paket berurutan dengan jeda %d ms: %.2f sampai %.2f s (belum termasuk ACK)"
          % (JEDA_PAKET_MS, min(teori) / 1e3, max(teori) / 1e3))
    hasil = kadens_terukur(berkas)
    if hasil:
        jeda_m, siklus_m, n = hasil
        print("   Terukur pada %s:" % berkas.split("/")[-1])
        print("     jeda antar-paket, median        %.0f ms" % jeda_m)
        print("     satu keputusan, median          %.2f s  (%d siklus berurutan)" % (siklus_m / 1e3, n))
        t_kep = siklus_m / 1e3
    else:
        t_kep = (min(teori) + max(teori)) / 2e3
        print("   Rekaman tidak terbaca; memakai hitungan teori %.2f s" % t_kep)

    print()
    print("   %-4s %-8s %-7s %-10s %-12s %-14s %s"
          % ("skn", "v (km/h)", "R (m)", "2R/v (s)", "keputusan", "tempuh per", "W=50 setara"))
    print("   %-4s %-8s %-7s %-10s %-12s %-14s %s"
          % ("", "", "", "", "per window", "keputusan (m)", "berapa lintasan"))
    for nama, v_kmh, r in SKENARIO:
        v = v_kmh / 3.6
        dur = 2 * r / v
        kep = dur / t_kep
        tempuh = v * t_kep
        print("   %-4s %-8d %-7d %-10.1f %-12.1f %-14.1f %.1f"
              % (nama, v_kmh, r, dur, kep, tempuh, 50 / kep if kep else float("inf")))

    print()
    print("   Agen hanya di-reset saat nyala pertama, saat RTC RAM rusak, atau saat tombol BOOT")
    print("   ditahan (main.cpp). Bangun dari deep sleep tidak me-reset, sehingga window W")
    print("   berjalan terus melewati batas antar-lintasan.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
