"""Gambar berbahasa Indonesia untuk deck Seminar Usulan Penelitian.

Tata letaknya lanskap, berbeda dari diagram tesis pada buat_diagram.py yang
berorientasi potret dan menyempit bila ditaruh di slide 16:9. Primitif gambar
dipakai bersama buat_gambar_jurnal.py; hanya label dan lebar kotak yang
disesuaikan karena istilah Indonesia lebih panjang.

Nilai parameter dibaca dari node/src/config/params.h dan protocol.h.

Pemakaian:
    python buat_gambar_slide.py --keluaran ../../diagram
"""

import argparse
import os

from buat_gambar_jurnal import (FONT_LABEL, jalur, kanvas, kotak, label,
                                panah, simpan, wadah)


def fsm(folder):
    fig, ax = kanvas(9.6, 6.2, (-0.5, 11.1), (-0.55, 7.5))

    kotak(ax, 2.6, 6.3, 3.7, 0.95,
          "BATTERY-GUARD\nradio mati, tidur 3600 detik", "sorot")
    kotak(ax, 2.6, 4.0, 3.7, 1.10,
          "IDLE\ndeep sleep 3 detik,\npindai beacon 1,2 detik", "proses")
    kotak(ax, 7.9, 4.0, 4.2, 1.10,
          "CONTACT-WINDOW\nkirim paket, cuplik arus\nselama radio memancar",
          "proses")
    kotak(ax, 7.9, 0.95, 4.2, 1.05,
          "REWARD-EVAL\nR = α·PDR − β·E_norm,\nperbarui agen SW-UCB", "data")

    ax.plot([-0.15], [4.0], marker="o", markersize=6, color="#3A3A3A",
            zorder=4)
    panah(ax, (-0.15, 4.0), (0.75, 4.0))
    label(ax, -0.15, 4.34, "menyala", font=FONT_LABEL - 0.3)

    panah(ax, (1.95, 4.55), (1.95, 5.83))
    label(ax, 1.81, 5.19, "V_baterai < 3,3 V", ha="right")
    panah(ax, (3.25, 5.83), (3.25, 4.55))
    label(ax, 3.39, 5.19, "V_baterai > 3,5 V", ha="left")

    panah(ax, (4.45, 4.18), (5.80, 4.18))
    label(ax, 5.12, 4.62, "RSSI beacon\n> −100 dBm")

    panah(ax, (6.9, 3.45), (6.9, 1.48))
    label(ax, 6.76, 2.66, "tiap 10\npaket", ha="right")
    panah(ax, (8.9, 1.48), (8.9, 3.45))
    label(ax, 9.04, 2.66, "agen\ndiperbarui", ha="left")

    jalur(ax, [(5.80, 3.78), (5.80, 2.35), (2.60, 2.35), (2.60, 3.45)])
    label(ax, 3.7, 1.98,
          "ACK gagal  atau  RSSI < −105 dBm\natau  t > 240 detik")

    label(ax, 5.2, -0.30,
          "BATTERY-GUARD diperiksa setiap kali node bangun dan mengalahkan "
          "seluruh percabangan lain.\nHisteresis 0,2 volt antara ambang masuk "
          "3,3 volt dan ambang keluar 3,5 volt mencegah node berpindah "
          "bolak-balik saat baterai hampir habis.",
          font=FONT_LABEL - 0.2, warna="#555555")

    simpan(fig, folder, "S_mesin_keadaan.png")


def arsitektur(folder):
    fig, ax = kanvas(11.4, 6.1, (0.0, 14.2), (0.2, 8.25))

    wadah(ax, 0.3, 0.5, 7.0, 7.7, "Node sensor (statis, tepi jalan)")
    wadah(ax, 8.0, 0.5, 13.8, 7.7, "Mobile gateway (pada kendaraan)")

    kotak(ax, 4.0, 6.90, 3.1, 0.72, "ADXL345 akselerometer", "data")
    kotak(ax, 4.0, 5.60, 3.9, 1.20,
          "ESP32\nagen SW-UCB + Hybrid FSM\nkeadaan disimpan di RTC RAM")
    kotak(ax, 4.0, 3.40, 3.9, 2.20,
          "Transceiver LoRa SX1278\n433 MHz, SF8, BW 125 kHz\n"
          "TP dan CR dipilih agen")
    kotak(ax, 2.30, 1.15, 2.05, 1.00, "surya 1,1 W\n+ TP4056 + 18650",
          "terminal")
    kotak(ax, 4.20, 1.15, 1.50, 1.00, "INA219", "sorot")
    kotak(ax, 5.90, 1.15, 1.60, 1.00, "MT3608\npenaik", "terminal")

    panah(ax, (4.0, 6.54), (4.0, 6.25))
    label(ax, 4.16, 6.40, "I²C", ha="left")
    panah(ax, (4.0, 5.00), (4.0, 4.55))
    label(ax, 4.16, 4.78, "SPI", ha="left")
    panah(ax, (3.33, 1.15), (3.45, 1.15))
    panah(ax, (4.95, 1.15), (5.10, 1.15))

    jalur(ax, [(5.90, 1.65), (5.90, 2.10), (1.25, 2.10), (1.25, 5.15),
               (2.05, 5.15)])
    label(ax, 1.25, 3.95, "5 V", rotasi=90)
    jalur(ax, [(4.20, 1.65), (4.20, 1.90), (0.75, 1.90), (0.75, 5.60),
               (2.05, 5.60)], gaya=(0, (4, 2)))
    label(ax, 0.75, 3.05, "I²C", rotasi=90)

    kotak(ax, 10.9, 5.80, 3.7, 1.20,
          "ESP32\npemancar beacon,\npenghasil ACK")
    kotak(ax, 10.9, 3.40, 3.7, 2.20,
          "Transceiver LoRa SX1278\n433 MHz, SF8, BW 125 kHz\nTP tetap")
    kotak(ax, 10.9, 1.15, 4.1, 1.00,
          "Laptop\npencatatan CSV, catu 5 V", "terminal")

    panah(ax, (10.9, 5.20), (10.9, 4.55))
    label(ax, 11.06, 4.88, "SPI", ha="left")
    jalur(ax, [(12.95, 1.15), (13.45, 1.15), (13.45, 5.80), (12.75, 5.80)])
    label(ax, 13.45, 3.40, "serial USB", rotasi=90)

    panah(ax, (9.05, 4.15), (5.95, 4.15))
    label(ax, 7.50, 4.42, "beacon, tiap 1 detik")
    panah(ax, (5.95, 3.40), (9.05, 3.40))
    label(ax, 7.50, 3.72, "paket data: arm, nomor urut,\n"
                          "energi paket sebelumnya")
    panah(ax, (9.05, 2.65), (5.95, 2.65))
    label(ax, 7.50, 2.96, "ACK: jumlah paket tiba\npada rentang nomor urut")

    label(ax, 7.50, 7.98, "Jendela kontak tercipta saat kendaraan lewat",
          font=FONT_LABEL + 0.4, warna="#555555")

    simpan(fig, folder, "S_arsitektur_sistem.png")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--keluaran", default="gambar_slide")
    a = p.parse_args()
    print(f"Gambar slide -> {a.keluaran}")
    fsm(a.keluaran)
    arsitektur(a.keluaran)


if __name__ == "__main__":
    main()
