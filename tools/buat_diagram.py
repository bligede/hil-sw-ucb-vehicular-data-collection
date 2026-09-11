"""Pembangkit diagram alur untuk dokumentasi tesis.

Menghasilkan berkas PNG beresolusi tinggi yang dapat langsung disisipkan ke
naskah Word. Tata letaknya ditulis eksplisit, bukan diatur otomatis, agar
hasilnya sama persis setiap kali dijalankan ulang.

Pemakaian:
    python buat_diagram.py --keluaran ../../diagram
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon, FancyArrowPatch

# Palet: proses, keputusan, terminal, sorotan, catatan
WARNA = {
    "proses":   ("#DCE9F7", "#4472C4"),
    "keputusan": ("#FCE4CE", "#ED7D31"),
    "terminal": ("#E4E4E4", "#7F7F7F"),
    "sorot":    ("#FBDDDD", "#C00000"),
    "data":     ("#DFF0DC", "#548235"),
}
FONT = 9.0


def kotak(ax, x, y, w, h, teks, jenis="proses", font=FONT):
    isi, garis = WARNA[jenis]
    ax.add_patch(FancyBboxPatch(
        (x - w / 2, y - h / 2), w, h,
        boxstyle="round,pad=0.02,rounding_size=0.12",
        facecolor=isi, edgecolor=garis, linewidth=1.4, zorder=2))
    ax.text(x, y, teks, ha="center", va="center", fontsize=font,
            zorder=3, linespacing=1.45)
    return (x, y, w, h)


def belah(ax, x, y, w, h, teks, font=FONT - 0.5):
    isi, garis = WARNA["keputusan"]
    ax.add_patch(Polygon(
        [(x, y + h / 2), (x + w / 2, y), (x, y - h / 2), (x - w / 2, y)],
        closed=True, facecolor=isi, edgecolor=garis, linewidth=1.4, zorder=2))
    ax.text(x, y, teks, ha="center", va="center", fontsize=font,
            zorder=3, linespacing=1.4)
    return (x, y, w, h)


def panah(ax, dari, ke, label="", sisi="kanan", gaya="-", warna="#404040"):
    ax.add_patch(FancyArrowPatch(
        dari, ke, arrowstyle="-|>", mutation_scale=13,
        linewidth=1.2, color=warna, linestyle=gaya,
        connectionstyle="arc3,rad=0", zorder=1))
    if label:
        mx, my = (dari[0] + ke[0]) / 2, (dari[1] + ke[1]) / 2
        dx = 0.16 if sisi == "kanan" else -0.16
        ax.text(mx + dx, my, label, ha="left" if sisi == "kanan" else "right",
                va="center", fontsize=FONT - 1.4, color=warna, zorder=3,
                bbox=dict(facecolor="white", edgecolor="none", pad=1.2))


def siku(ax, dari, ke, lewat_x=None, lewat_y=None, label="", warna="#404040"):
    """Panah bersiku, untuk jalur balik dan cabang samping."""
    x0, y0 = dari
    x1, y1 = ke
    if lewat_x is not None:
        titik = [(x0, y0), (lewat_x, y0), (lewat_x, y1), (x1, y1)]
    else:
        titik = [(x0, y0), (x0, lewat_y), (x1, lewat_y), (x1, y1)]
    for i in range(len(titik) - 2):
        ax.plot([titik[i][0], titik[i + 1][0]], [titik[i][1], titik[i + 1][1]],
                color=warna, linewidth=1.2, zorder=1)
    ax.add_patch(FancyArrowPatch(
        titik[-2], titik[-1], arrowstyle="-|>", mutation_scale=13,
        linewidth=1.2, color=warna, zorder=1))
    if label:
        ax.text(titik[1][0], (titik[1][1] + titik[2][1]) / 2, label,
                ha="center", va="center", fontsize=FONT - 1.4, color=warna,
                rotation=90, zorder=3,
                bbox=dict(facecolor="white", edgecolor="none", pad=1.2))


def kanvas(judul, lebar, tinggi, xlim, ylim):
    fig, ax = plt.subplots(figsize=(lebar, tinggi))
    ax.set_xlim(*xlim); ax.set_ylim(*ylim)
    ax.axis("off")
    ax.set_title(judul, fontsize=12.5, fontweight="bold", pad=14)
    return fig, ax


def simpan(fig, folder, nama):
    jalur = os.path.join(folder, nama)
    fig.savefig(jalur, dpi=200, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"  {nama}")


# ===================================================================
# 1. Mesin keadaan node
# ===================================================================
def diagram_fsm(folder):
    fig, ax = kanvas("Gambar A. Mesin Keadaan (FSM) Node Sensor",
                     10.5, 12, (0, 11.6), (-0.7, 15.5))

    kotak(ax, 5, 14.6, 3.0, 0.7, "Node menyala / bangun", "terminal")
    kotak(ax, 5, 13.4, 4.4, 0.8,
          "Baca tegangan baterai\nlewat INA219", "proses")

    belah(ax, 5, 11.9, 3.6, 1.5, "Sedang di dalam\nBATTERY-GUARD?")
    belah(ax, 2.0, 9.9, 3.2, 1.4, "Tegangan\nlebih dari 3,5 V?")
    belah(ax, 8.0, 9.9, 3.2, 1.4, "Tegangan\nkurang dari 3,3 V?")

    kotak(ax, 2.0, 7.9, 3.0, 0.9,
          "BATTERY-GUARD\nTidur 1 jam", "sorot")
    kotak(ax, 8.0, 7.9, 3.0, 0.9,
          "Masuk BATTERY-GUARD\nTandai di RTC RAM", "sorot")

    kotak(ax, 5, 6.4, 3.4, 0.8, "STATE IDLE\nPindai beacon 1,2 detik", "proses")
    belah(ax, 5, 4.7, 3.4, 1.4, "RSSI melebihi\nambang deteksi?")
    kotak(ax, 1.5, 4.7, 2.4, 0.8, "Tidur 3 detik", "proses")

    kotak(ax, 5, 3.1, 4.6, 0.9,
          "STATE CONTACT-WINDOW\nKirim satu paket, catat energi", "proses")
    belah(ax, 5, 1.5, 3.6, 1.4, "Sudah sepuluh paket\natau batas waktu?")
    kotak(ax, 8.6, 1.5, 2.6, 0.9, "STATE\nREWARD-EVAL", "proses")

    panah(ax, (5, 14.25), (5, 13.8))
    panah(ax, (5, 13.0), (5, 12.65))
    siku(ax, (3.2, 11.9), (2.0, 10.6), lewat_x=2.0, label="ya")
    siku(ax, (6.8, 11.9), (8.0, 10.6), lewat_x=8.0, label="tidak")

    panah(ax, (2.0, 9.2), (2.0, 8.35), "tidak", "kanan")
    panah(ax, (8.0, 9.2), (8.0, 8.35), "ya", "kanan")
    siku(ax, (3.6, 9.9), (5.0, 6.8), lewat_x=4.3, label="ya, pulih")
    siku(ax, (6.4, 9.9), (5.0, 6.8), lewat_x=5.7, label="tidak")

    panah(ax, (5, 6.0), (5, 5.4))
    panah(ax, (3.3, 4.7), (2.7, 4.7), "tidak", "kanan")
    panah(ax, (5, 4.0), (5, 3.55), "ya", "kanan")
    panah(ax, (5, 2.65), (5, 2.2))
    panah(ax, (6.8, 1.5), (7.3, 1.5), "ya", "kanan")

    siku(ax, (1.5, 4.3), (5.0, 6.8), lewat_x=0.55)
    siku(ax, (3.2, 1.5), (4.2, 2.65), lewat_x=2.3, label="belum")
    siku(ax, (8.6, 1.05), (5.8, 2.65), lewat_x=10.8)

    ax.text(5.6, -0.45,
            "Keluar dari CONTACT-WINDOW terjadi saat ACK gagal, RSSI ACK di bawah ambang,\n"
            "atau batas waktu 240 detik terlampaui yang diikuti jeda 60 detik.",
            ha="center", va="center", fontsize=FONT - 1, style="italic",
            color="#555555")

    simpan(fig, folder, "A_mesin_keadaan_node.png")


# ===================================================================
# 2. Satu siklus transmisi
# ===================================================================
def diagram_siklus(folder):
    fig, ax = kanvas("Gambar B. Satu Siklus Transmisi dan Evaluasi Reward",
                     9.5, 14.5, (0, 10), (-2.1, 17.3))

    y = 16.4
    langkah = [
        ("Agen SW-UCB memilih arm\nberdasarkan skor tertinggi", "proses"),
        ("Terapkan Transmit Power\ndan Coding Rate arm terpilih", "proses"),
        ("Baca percepatan dari ADXL345", "data"),
        ("Susun muatan 12 byte\narm, nomor urut, energi paket lalu,\n"
         "tegangan, tiga sumbu percepatan", "data"),
        ("Baca tegangan bus sekali\nsebagai acuan seluruh cuplikan", "proses"),
        ("Mulai transmisi ASINKRON\nendPacket(true)", "sorot"),
        ("Selama radio memancar:\ncuplik arus INA219 berulang\ndan integrasikan dayanya", "sorot"),
        ("Transmisi selesai\nEnergi paket ini tersimpan", "proses"),
        ("Naikkan nomor urut\ndan pencacah paket", "proses"),
    ]
    posisi = []
    for teks, jenis in langkah:
        tinggi = 0.95 if teks.count("\n") >= 2 else 0.78
        posisi.append(kotak(ax, 5, y, 5.6, tinggi, teks, jenis))
        y -= 1.45

    y_belah = 3.30
    belah(ax, 5, y_belah, 3.8, 1.5, "Sudah genap\nsepuluh paket?")

    y_ack = 1.75
    kotak(ax, 5, y_ack, 5.6, 0.78,
          "Tunggu ACK dari gateway\nbatas 2 detik", "proses")
    y_rew = 0.15
    kotak(ax, 5, y_rew, 5.6, 0.95,
          "Hitung PDR = diterima / 10\nHitung E_norm = E_rata / E_maks\n"
          "Reward = alfa x PDR  -  beta x E_norm", "data")
    y_upd = -1.35
    kotak(ax, 5, y_upd, 5.6, 0.78,
          "Perbarui sliding window agen\nSimpan keadaan ke RTC RAM", "proses")

    for i in range(len(posisi) - 1):
        panah(ax, (5, posisi[i][1] - posisi[i][3] / 2),
              (5, posisi[i + 1][1] + posisi[i + 1][3] / 2))
    panah(ax, (5, posisi[-1][1] - posisi[-1][3] / 2), (5, y_belah + 0.75))

    panah(ax, (5, y_belah - 0.75), (5, y_ack + 0.39), "ya", "kanan")
    panah(ax, (5, y_ack - 0.39), (5, y_rew + 0.475))
    panah(ax, (5, y_rew - 0.475), (5, y_upd + 0.39))

    atas = posisi[1][1] + posisi[1][3] / 2      # kembali ke langkah kedua
    siku(ax, (6.9, y_belah), (5.0, atas), lewat_x=9.4, label="belum")
    siku(ax, (5.0, y_upd - 0.39), (5.0, atas), lewat_x=0.5)

    simpan(fig, folder, "B_siklus_transmisi.png")


# ===================================================================
# 3. Alur data dari sensor sampai kesimpulan
# ===================================================================
def diagram_data(folder):
    fig, ax = kanvas("Gambar C. Alur Data dari Sensor sampai Uji Hipotesis",
                     13.5, 9.4, (0, 15), (-0.8, 11))

    ax.text(2.2, 10.4, "NODE SENSOR", fontsize=10.5, fontweight="bold",
            ha="center", color="#4472C4")
    ax.text(7.5, 10.4, "MOBILE GATEWAY", fontsize=10.5, fontweight="bold",
            ha="center", color="#ED7D31")
    ax.text(12.6, 10.4, "PENGOLAHAN", fontsize=10.5, fontweight="bold",
            ha="center", color="#548235")
    for x in (4.9, 10.1):
        ax.plot([x, x], [0.4, 10.0], color="#CCCCCC", linewidth=1.0,
                linestyle="--", zorder=0)

    kotak(ax, 2.2, 9.3, 3.6, 0.7, "ADXL345 — getaran jalan", "data")
    kotak(ax, 2.2, 8.2, 3.6, 0.7, "INA219 — arus dan tegangan", "data")
    kotak(ax, 2.2, 6.9, 3.8, 0.9,
          "Muatan 12 byte\narm, seq, energi, V, ax ay az", "proses")
    kotak(ax, 2.2, 5.5, 3.4, 0.7, "Transmisi LoRa 433 MHz", "proses")
    kotak(ax, 2.2, 3.6, 3.8, 0.9,
          "Agen SW-UCB\nmemakai reward untuk\nmemilih arm berikutnya", "sorot")
    kotak(ax, 2.2, 2.0, 3.4, 0.7, "Keadaan di RTC RAM", "proses")

    kotak(ax, 7.5, 5.5, 3.6, 0.7, "Terima paket, baca RSSI", "proses")
    kotak(ax, 7.5, 4.2, 3.8, 0.9,
          "Tutup jendela tiap rentang\nsepuluh nomor urut", "sorot")
    kotak(ax, 7.5, 2.8, 3.6, 0.8,
          "Kirim ACK berisi jumlah\nyang benar-benar tiba", "proses")
    kotak(ax, 7.5, 7.4, 3.8, 0.9,
          "Cetak baris CSV ke serial\nDATA dan ACK", "data")
    kotak(ax, 7.5, 8.9, 3.4, 0.7, "Berkas log per sesi", "data")

    kotak(ax, 12.6, 8.9, 4.0, 0.8,
          "ringkas_log.py\nsatu baris per contact window", "proses")
    kotak(ax, 12.6, 7.3, 4.0, 0.9,
          "Sheet 04 Log Window\nPDR, E per sukses, Reward", "data")
    kotak(ax, 12.6, 5.7, 4.0, 0.8,
          "Sheet 06, 07, 08\nenergi, konvergensi, re-konvergensi", "data")
    kotak(ax, 12.6, 4.1, 4.0, 0.8,
          "Sheet 13 Statistik\nnilai p dan ukuran efek", "data")
    kotak(ax, 12.6, 2.5, 4.0, 0.8,
          "Sheet 09 dan 10\nmatriks validitas, uji hipotesis", "sorot")
    kotak(ax, 12.6, 1.0, 3.4, 0.7, "Bab Hasil naskah", "terminal")

    panah(ax, (2.2, 8.95), (2.2, 8.55))
    panah(ax, (2.2, 7.85), (2.2, 7.35))
    panah(ax, (2.2, 6.45), (2.2, 5.85))
    panah(ax, (3.9, 5.5), (5.7, 5.5))
    panah(ax, (7.5, 5.15), (7.5, 4.65))
    panah(ax, (7.5, 3.75), (7.5, 3.2))
    panah(ax, (5.7, 2.8), (3.9, 3.4), warna="#C00000")
    ax.text(4.8, 2.5, "ACK", ha="center", fontsize=FONT - 1.4, color="#C00000")
    panah(ax, (2.2, 3.15), (2.2, 2.35))
    siku(ax, (2.2, 1.65), (2.2, 7.35), lewat_x=0.35)
    panah(ax, (7.5, 5.85), (7.5, 6.95))
    panah(ax, (7.5, 7.85), (7.5, 8.55))
    panah(ax, (9.2, 8.9), (10.6, 8.9))
    for a, b in ((8.5, 7.75), (6.85, 6.1), (5.3, 4.5), (3.7, 2.9), (2.1, 1.35)):
        panah(ax, (12.6, a), (12.6, b))

    ax.text(7.5, -0.45,
            "Energi diukur di sisi node, sedangkan pencatatan berlangsung di sisi gateway. "
            "Karena itu\nenergi paket sebelumnya ikut dibawa di dalam muatan; tanpa itu angkanya "
            "tidak pernah sampai ke berkas log.",
            ha="center", va="center", fontsize=FONT - 1, style="italic",
            color="#555555")

    simpan(fig, folder, "C_alur_data.png")


# ===================================================================
# 4. Alur penyusunan program
# ===================================================================
def diagram_penyusunan(folder):
    fig, ax = kanvas("Gambar D. Alur Penyusunan Program dan Gerbang Kelulusan",
                     11.5, 11, (0, 13), (0, 13.5))

    tahap = [
        (11.9, "Tahap 0 — Pra-validasi di Python", "proses",
         "SW-UCB re-konvergen <= 30 iterasi\ndan lebih cepat dari UCB1"),
        (10.0, "Tahap 1 — Lingkungan pengembangan", "proses",
         "Dua project terkompilasi\npin_map.h dipakai bersama"),
        (8.1, "Tahap 2 — Firmware node (7 modul)", "proses",
         "Tiap modul lolos uji satuan\nrasio energi Arm 4 : Arm 1 >= 2,0"),
        (6.2, "Tahap 3 — Firmware gateway", "proses",
         "Beacon terpancar, ACK terkirim\nbaris CSV lengkap"),
        (4.3, "Tahap 4 — Kalibrasi laboratorium", "proses",
         "E maksimum ditetapkan\nthreshold RSSI dikalibrasi"),
        (2.4, "Tahap 5 — Persiapan deployment", "proses",
         "Enclosure lolos uji rendam\npenanda jarak terukur"),
    ]
    for y, teks, jenis, gerbang in tahap:
        kotak(ax, 3.4, y, 5.4, 0.85, teks, jenis)
        kotak(ax, 9.6, y, 5.2, 1.0, gerbang, "terminal", FONT - 1)
        panah(ax, (6.1, y), (7.0, y))

    for i in range(len(tahap) - 1):
        panah(ax, (3.4, tahap[i][0] - 0.43), (3.4, tahap[i + 1][0] + 0.43))

    kotak(ax, 3.4, 0.9, 5.4, 0.8,
          "Fase lapangan\nStabilisasi, Baseline, Konvergensi, Multi-Kecepatan",
          "sorot", FONT - 0.5)
    panah(ax, (3.4, 1.97), (3.4, 1.3))

    kotak(ax, 3.4, 12.9, 4.4, 0.6, "Mulai", "terminal")
    panah(ax, (3.4, 12.6), (3.4, 12.33))

    ax.text(9.6, 12.9, "GERBANG KELULUSAN", fontsize=9.5, fontweight="bold",
            ha="center", color="#7F7F7F")
    ax.text(6.5, 0.15,
            "Gerbang yang gagal menghentikan tahap berikutnya. Tahap 4 sekaligus membuktikan "
            "pengukuran energi\nbenar-benar berlangsung selama transmisi, bukan sesudahnya.",
            ha="center", va="center", fontsize=FONT - 1, style="italic",
            color="#555555")

    simpan(fig, folder, "D_alur_penyusunan_program.png")


# ===================================================================
# 5. Jendela ACK dan perhitungan PDR
# ===================================================================
def diagram_ack(folder):
    fig, ax = kanvas("Gambar E. Penutupan Jendela ACK dan Perhitungan PDR",
                     11, 8.9, (0, 13), (-0.9, 10))

    ax.text(3.1, 9.4, "NODE — menghitung yang DIKIRIM",
            fontsize=10, fontweight="bold", ha="center", color="#4472C4")
    ax.text(9.7, 9.4, "GATEWAY — melihat yang TIBA",
            fontsize=10, fontweight="bold", ha="center", color="#ED7D31")
    ax.plot([6.4, 6.4], [0.4, 9.0], color="#CCCCCC", linewidth=1.0,
            linestyle="--", zorder=0)

    kotak(ax, 3.1, 8.3, 4.6, 0.7, "Kirim paket, nomor urut naik", "proses")
    kotak(ax, 3.1, 7.0, 4.6, 0.7, "Pencacah paket dikirim naik", "proses")
    belah(ax, 3.1, 5.4, 3.6, 1.5, "Sudah sepuluh\npaket dikirim?")
    kotak(ax, 3.1, 3.5, 4.6, 0.7, "Tunggu ACK, batas 2 detik", "proses")
    belah(ax, 3.1, 1.8, 3.6, 1.5, "ACK diterima?")

    kotak(ax, 9.7, 8.3, 4.6, 0.8,
          "Paket tiba\nCatat nomor urut pertama", "proses")
    belah(ax, 9.7, 6.6, 4.2, 1.5,
          "Selisih nomor urut\nsudah mencapai sembilan?")
    belah(ax, 9.7, 4.5, 4.2, 1.5, "Diam lebih dari\n800 milidetik?")
    kotak(ax, 9.7, 2.6, 4.8, 0.9,
          "Kirim ACK berisi jumlah\nyang BENAR-BENAR tiba", "sorot")

    panah(ax, (3.1, 7.95), (3.1, 7.35))
    panah(ax, (3.1, 6.65), (3.1, 6.15))
    panah(ax, (3.1, 4.65), (3.1, 3.85), "ya", "kanan")
    siku(ax, (1.3, 5.4), (3.1, 8.65), lewat_x=0.5, label="belum")
    panah(ax, (3.1, 3.15), (3.1, 2.55))

    panah(ax, (5.4, 8.3), (7.4, 8.3))
    ax.text(6.4, 8.62, "paket data", ha="center", fontsize=FONT - 1.4,
            color="#404040")
    panah(ax, (9.7, 7.95), (9.7, 7.35))
    panah(ax, (9.7, 5.85), (9.7, 5.25), "belum", "kanan")
    siku(ax, (11.8, 6.6), (9.7, 3.05), lewat_x=12.6, label="sudah")
    panah(ax, (9.7, 3.75), (9.7, 3.05), "ya", "kanan")
    siku(ax, (7.6, 4.5), (9.7, 8.7), lewat_x=6.9, label="belum")

    panah(ax, (7.3, 2.6), (4.9, 1.9), warna="#C00000")
    ax.text(6.1, 1.95, "ACK", ha="center", fontsize=FONT - 1.4, color="#C00000")

    kotak(ax, 1.3, 0.6, 2.6, 0.8,
          "Reward = -1,0\nkeluar jendela", "sorot", FONT - 1)
    kotak(ax, 4.9, 0.6, 2.8, 0.8,
          "PDR = tiba / 10\nhitung reward", "data", FONT - 1)
    panah(ax, (1.3, 1.8), (1.3, 1.05), "tidak", "kiri")
    panah(ax, (4.9, 1.8), (4.9, 1.05), "ya", "kanan")

    ax.text(6.5, -0.6,
            "Jendela ditutup oleh RENTANG nomor urut, bukan oleh jumlah penerimaan. "
            "Bila jumlah penerimaan yang dipakai,\nACK hanya terkirim saat kesepuluh paket "
            "tiba utuh, sehingga PDR selalu bernilai 1,00 dan kehilangan tidak terlihat.",
            ha="center", va="center", fontsize=FONT - 1, style="italic",
            color="#555555")

    simpan(fig, folder, "E_jendela_ack_pdr.png")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--keluaran", default="../../diagram")
    a = p.parse_args()
    os.makedirs(a.keluaran, exist_ok=True)
    print("Membuat diagram:")
    diagram_fsm(a.keluaran)
    diagram_siklus(a.keluaran)
    diagram_data(a.keluaran)
    diagram_penyusunan(a.keluaran)
    diagram_ack(a.keluaran)
    print(f"\nSeluruh diagram tersimpan di {os.path.abspath(a.keluaran)}")


if __name__ == "__main__":
    main()
