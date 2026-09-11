"""Gambar untuk artikel Smart Techno (Primakara University).

Menghasilkan Figure 2 (FSM) dan Figure 3 (blok arsitektur sistem) yang diminta
pada Catatan Perbaikan Naskah Bagian C. Label berbahasa Inggris mengikuti naskah
artikel, berbeda dari diagram tesis pada buat_diagram.py yang berbahasa Indonesia.

Nilai parameter pada gambar diambil dari node/src/config/params.h dan
protocol.h, bukan dari nilai acuan Panduan HIL yang sudah digantikan.

Pemakaian:
    python buat_gambar_jurnal.py --keluaran ../../TESIS/jurnal/gambar
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle

# Palet dijaga tetap terbaca saat dicetak hitam-putih: nilai keabuan isi
# berbeda jelas antar-jenis, sehingga peran kotak tidak bergantung pada warna.
WARNA = {
    "proses":   ("#E8EEF7", "#3B5C8F"),
    "sorot":    ("#F7E3E3", "#A32020"),
    "data":     ("#E6F0E2", "#4A6E33"),
    "terminal": ("#EDEDED", "#6E6E6E"),
}
FONT = 8.2
FONT_LABEL = 7.0


def kanvas(lebar, tinggi, xlim, ylim):
    fig, ax = plt.subplots(figsize=(lebar, tinggi))
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.axis("off")
    return fig, ax


def kotak(ax, cx, cy, w, h, teks, jenis="proses", font=FONT):
    isi, garis = WARNA[jenis]
    ax.add_patch(FancyBboxPatch(
        (cx - w / 2, cy - h / 2), w, h,
        boxstyle="round,pad=0.02,rounding_size=0.10",
        facecolor=isi, edgecolor=garis, linewidth=1.3, zorder=3))
    ax.text(cx, cy, teks, ha="center", va="center", fontsize=font,
            zorder=4, linespacing=1.42)


def wadah(ax, x0, y0, x1, y1, judul):
    ax.add_patch(Rectangle(
        (x0, y0), x1 - x0, y1 - y0, facecolor="#FCFCFC",
        edgecolor="#9A9A9A", linewidth=1.1, linestyle=(0, (5, 3)), zorder=1))
    ax.text((x0 + x1) / 2, y1 - 0.26, judul, ha="center", va="center",
            fontsize=FONT + 0.4, fontweight="bold", color="#404040", zorder=2)


def panah(ax, dari, ke, gaya="-", warna="#3A3A3A"):
    ax.add_patch(FancyArrowPatch(
        dari, ke, arrowstyle="-|>", mutation_scale=11,
        linewidth=1.15, color=warna, linestyle=gaya, zorder=2))


def jalur(ax, titik, gaya="-", warna="#3A3A3A"):
    """Panah bersiku sepanjang daftar titik; kepala panah di ruas terakhir."""
    for i in range(len(titik) - 2):
        ax.plot([titik[i][0], titik[i + 1][0]],
                [titik[i][1], titik[i + 1][1]],
                color=warna, linewidth=1.15, linestyle=gaya, zorder=2)
    panah(ax, titik[-2], titik[-1], gaya=gaya, warna=warna)


def label(ax, x, y, teks, ha="center", va="center", font=FONT_LABEL,
          warna="#2A2A2A", rotasi=0):
    ax.text(x, y, teks, ha=ha, va=va, fontsize=font, color=warna,
            rotation=rotasi, zorder=5, linespacing=1.35,
            bbox=dict(facecolor="white", edgecolor="none", pad=1.4))


def simpan(fig, folder, nama):
    os.makedirs(folder, exist_ok=True)
    jalur_berkas = os.path.join(folder, nama)
    fig.savefig(jalur_berkas, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"  {nama}")


# ===================================================================
# Figure 2 — Hybrid FSM supervisory control
# ===================================================================
def figure_fsm(folder):
    fig, ax = kanvas(9.2, 6.2, (-0.5, 10.6), (-0.55, 7.5))

    kotak(ax, 2.5, 6.3, 3.4, 0.95,
          "BATTERY_GUARD\nradio off, sleep 3600 s", "sorot")
    kotak(ax, 2.5, 4.0, 3.4, 1.10,
          "IDLE\ndeep sleep 3 s,\nbeacon scan 1.2 s", "proses")
    kotak(ax, 7.6, 4.0, 4.0, 1.10,
          "CONTACT_WINDOW\ntransmit packet, sample current\n"
          "during the transmission pulse", "proses")
    kotak(ax, 7.6, 0.95, 4.0, 1.05,
          "REWARD_EVAL\nR = α·PDR − β·E_norm,\n"
          "update SW-UCB", "data")

    # Masuk sistem
    ax.plot([-0.15], [4.0], marker="o", markersize=6, color="#3A3A3A",
            zorder=4)
    panah(ax, (-0.15, 4.0), (0.8, 4.0))
    label(ax, -0.15, 4.34, "power-on", font=FONT_LABEL - 0.3)

    # Baterai
    panah(ax, (1.9, 4.55), (1.9, 5.83))
    label(ax, 1.76, 5.19, "V_batt < 3.3 V", ha="right")
    panah(ax, (3.1, 5.83), (3.1, 4.55))
    label(ax, 3.24, 5.19, "V_batt > 3.5 V", ha="left")

    # Deteksi kontak
    panah(ax, (4.2, 4.18), (5.6, 4.18))
    label(ax, 4.9, 4.62, "beacon RSSI\n> −100 dBm")

    # Siklus evaluasi
    panah(ax, (6.6, 3.45), (6.6, 1.48))
    label(ax, 6.46, 2.66, "every 10\npackets", ha="right")
    panah(ax, (8.6, 1.48), (8.6, 3.45))
    label(ax, 8.74, 2.66, "agent\nupdated", ha="left")

    # Keluar jendela kontak
    jalur(ax, [(5.6, 3.78), (5.6, 2.35), (2.5, 2.35), (2.5, 3.45)])
    label(ax, 3.55, 1.98,
          "ACK timeout  or  RSSI < −105 dBm\nor  t > 240 s")

    label(ax, 5.05, -0.30,
          "BATTERY_GUARD is evaluated at the entry of every cycle and "
          "overrides all other transitions.\nThe 0.2 V hysteresis between the "
          "3.3 V entry and 3.5 V exit thresholds prevents oscillation under "
          "transmit load.",
          font=FONT_LABEL - 0.2, warna="#555555")

    simpan(fig, folder, "Figure2_FSM.png")


# ===================================================================
# Figure 3 — System architecture block diagram
# ===================================================================
def figure_arsitektur(folder):
    fig, ax = kanvas(11.4, 6.1, (0.0, 14.0), (0.2, 8.25))

    wadah(ax, 0.3, 0.5, 7.0, 7.7, "Sensor node (static, roadside)")
    wadah(ax, 8.0, 0.5, 13.6, 7.7, "Mobile gateway (on vehicle)")

    # --- Node: rantai logika di tengah, rantai daya di baris bawah ---
    kotak(ax, 4.0, 6.90, 2.9, 0.72, "ADXL345 accelerometer", "data")
    kotak(ax, 4.0, 5.60, 3.8, 1.20,
          "ESP32\nSW-UCB agent + hybrid FSM\nstate persisted in RTC RAM")
    kotak(ax, 4.0, 3.40, 3.8, 2.20,
          "SX1278 LoRa transceiver\n433 MHz, SF8, BW 125 kHz\n"
          "TP and CR selected\nby the agent")
    kotak(ax, 2.30, 1.15, 2.00, 1.00, "1.1 W solar\n+ TP4056 + 18650",
          "terminal")
    kotak(ax, 4.15, 1.15, 1.50, 1.00, "INA219", "sorot")
    kotak(ax, 5.85, 1.15, 1.60, 1.00, "MT3608\nboost", "terminal")

    panah(ax, (4.0, 6.54), (4.0, 6.25))
    label(ax, 4.16, 6.40, "I²C", ha="left")
    panah(ax, (4.0, 5.00), (4.0, 4.55))
    label(ax, 4.16, 4.78, "SPI", ha="left")
    panah(ax, (3.30, 1.15), (3.40, 1.15))
    panah(ax, (4.90, 1.15), (5.05, 1.15))

    # Rel 5 V dan jalur I2C sensor arus, lewat koridor kiri
    jalur(ax, [(5.85, 1.65), (5.85, 2.10), (1.25, 2.10), (1.25, 5.15),
               (2.10, 5.15)])
    label(ax, 1.25, 3.95, "5 V", rotasi=90)
    jalur(ax, [(4.15, 1.65), (4.15, 1.90), (0.75, 1.90), (0.75, 5.60),
               (2.10, 5.60)], gaya=(0, (4, 2)))
    label(ax, 0.75, 3.05, "I²C", rotasi=90)

    # --- Gateway ---
    kotak(ax, 10.8, 5.80, 3.6, 1.20,
          "ESP32\nbeacon transmitter,\nACK generator")
    kotak(ax, 10.8, 3.40, 3.6, 2.20,
          "SX1278 LoRa transceiver\n433 MHz, SF8, BW 125 kHz\nfixed TP")
    kotak(ax, 10.8, 1.15, 4.0, 1.00,
          "Laptop\nCSV logging, 5 V supply", "terminal")

    panah(ax, (10.8, 5.20), (10.8, 4.55))
    label(ax, 10.96, 4.88, "SPI", ha="left")
    jalur(ax, [(12.80, 1.15), (13.25, 1.15), (13.25, 5.80), (12.60, 5.80)])
    label(ax, 13.25, 3.40, "USB serial", rotasi=90)

    # --- Kanal LoRa ---
    panah(ax, (9.00, 4.15), (5.90, 4.15))
    label(ax, 7.45, 4.42, "beacon, every 1 s")
    panah(ax, (5.90, 3.40), (9.00, 3.40))
    label(ax, 7.45, 3.72, "data packet: arm, sequence\n"
                          "number, energy of previous")
    panah(ax, (9.00, 2.65), (5.90, 2.65))
    label(ax, 7.45, 2.96, "ACK: packets received\nover the sequence range")

    label(ax, 7.45, 7.98, "Contact window created by vehicle passage",
          font=FONT_LABEL + 0.4, warna="#555555")

    simpan(fig, folder, "Figure3_Architecture.png")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--keluaran", default="gambar_jurnal")
    a = p.parse_args()
    print(f"Gambar artikel -> {a.keluaran}")
    figure_fsm(a.keluaran)
    figure_arsitektur(a.keluaran)


if __name__ == "__main__":
    main()
