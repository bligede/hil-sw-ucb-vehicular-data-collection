"""Kapasitas sel dari log pengosongan (program kalibrasi_ina219 + perekam log_kal, satu baris per detik).

Format baris log: <waktu ISO>,detik,vbus_V,vshunt_mV,i_mA_rata,i_mA_sd,i_mA_min,i_mA_maks,n
Arus diintegrasikan terhadap cap waktu laptop. Celah lebih dari 5 s (misalnya USB tercabut) dilaporkan dan
diisi dengan rata-rata arus di kedua ujungnya (trapesium). Kapasitas dihitung sampai tegangan bus pertama kali
turun di bawah batas (bawaan 3,00 V, rata-rata bergulir 10 s supaya derau tidak memotong terlalu dini) dan sampai
data terakhir. Faktor koreksi arus dari kalibrasi (Fluke/INA219) dapat diberikan.

    python tools/olah_kapasitas.py data/kapasitas_sel/kapasitas_selb1.txt --batas 3.0 --faktor 1.0104
"""
import argparse, datetime as dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("berkas")
    ap.add_argument("--batas", type=float, default=3.0)
    ap.add_argument("--faktor", type=float, default=1.0, help="koreksi arus: I_acuan = faktor x I_INA219")
    ap.add_argument("--min-arus", type=float, default=20.0, help="baris dengan arus di bawah ini dianggap belum/tidak dibebani")
    a = ap.parse_args()

    rows = []
    for x in open(a.berkas, encoding="utf-8", errors="replace"):
        p = x.strip().split(",")
        if len(p) == 9 and p[1].isdigit():
            try:
                t = dt.datetime.fromisoformat(p[0])
                rows.append((t, float(p[2]), float(p[4]) * a.faktor))
            except ValueError:
                pass
    rows = [r for r in rows if r[2] >= a.min_arus]
    if len(rows) < 2:
        print("data beban tidak cukup"); return
    mah = wh = 0.0; celah = []; mah_b = wh_b = None; t_b = None
    v_buf = []
    for (t0, v0, i0), (t1, v1, i1) in zip(rows, rows[1:]):
        d = (t1 - t0).total_seconds()
        if d > 5:
            celah.append((t0.strftime("%H:%M:%S"), d))
        mah += (i0 + i1) / 2 * d / 3600
        wh += (v0 * i0 + v1 * i1) / 2 * d / 3600 / 1000
        v_buf.append(v1); v_buf = v_buf[-10:]
        if mah_b is None and len(v_buf) == 10 and sum(v_buf) / 10 < a.batas:
            mah_b, wh_b, t_b = mah, wh, t1
    lama = (rows[-1][0] - rows[0][0]).total_seconds() / 3600
    print("beban mulai %s, data terakhir %s (%.2f jam), arus awal %.1f mA, akhir %.1f mA"
          % (rows[0][0], rows[-1][0], lama, rows[0][2], rows[-1][2]))
    print("tegangan bus awal %.3f V, akhir %.3f V" % (rows[0][1], rows[-1][1]))
    if mah_b is not None:
        print("sampai %.2f V (tercapai %s): %.0f mAh, %.2f Wh" % (a.batas, t_b, mah_b, wh_b))
    else:
        print("batas %.2f V tidak tercapai" % a.batas)
    print("sampai data terakhir: %.0f mAh, %.2f Wh" % (mah, wh))
    print("celah > 5 s:", celah if celah else "tidak ada")


if __name__ == "__main__":
    main()
