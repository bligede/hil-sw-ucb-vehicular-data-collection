#!/usr/bin/env python3
"""Replay berbasis indeks untuk log serial node firmware 1.2 (catatan Pembimbing I,
1 Nov 2026, butir A.1).

Node mencetak dua jenis baris:
  IDX,t=<t>,I=<I1>;<I2>;<I3>;<I4>,N=<N1>;<N2>;<N3>;<N4>,pilih=<arm>   setiap keputusan
  UPD,t=<t>,arm=<arm>,r=<reward>                                      setiap reward
Baris "RESET AGEN" memulai agen acuan dari nol.

Agen acuan (prevalidasi_swucb.SWUCB atau UCB1) diberi urutan keputusan dan reward
yang sama. Pada setiap IDX, keempat indeks agen acuan dibandingkan dengan indeks
node (toleransi 0,0001, inf harus sama-sama inf), begitu pula N dan arm terpilih.
Arm pada UPD dipakai apa adanya, sehingga keputusan node yang tidak cocok tidak
mengubah riwayat agen acuan.

Pemakaian:  python tools/replay_indeks.py log_node.txt [--ucb1] [--keluaran tabel.csv]
"""

import argparse
import csv
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prevalidasi_swucb as pv  # noqa: E402

TOL = 1e-4
RE_IDX = re.compile(r"IDX,t=(\d+),I=([^,]+),N=([^,]+),pilih=(\d+)")
RE_UPD = re.compile(r"UPD,t=(\d+),arm=(\d+),r=(-?[\d.]+)")


def angka(x):
    x = x.strip().lower()
    return math.inf if x in ("inf", "+inf") else float(x)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("berkas")
    ap.add_argument("--ucb1", action="store_true", help="log dari build MODE_UCB1")
    ap.add_argument("--keluaran", help="tulis tabel per keputusan (CSV)")
    a = ap.parse_args()

    baru = pv.UCB1 if a.ucb1 else pv.SWUCB
    ag = baru()
    baris_tabel = []
    n_kep = n_ok = n_reset = 0
    selisih_maks = 0.0
    with open(a.berkas, encoding="utf-8", errors="replace") as f:
        for no, l in enumerate(f, 1):
            if "RESET AGEN" in l:
                ag = baru()
                n_reset += 1
                continue
            m = RE_IDX.search(l)
            if m:
                t_node = int(m.group(1))
                i_node = [angka(x) for x in m.group(2).split(";")]
                n_node = [int(x) for x in m.group(3).split(";")]
                pilih_node = int(m.group(4))
                i_ref = [ag.score(k) for k in range(pv.N_ARMS)]
                pilih_ref = ag.select() + 1
                ok = pilih_ref == pilih_node and ag.t == t_node
                for x, y in zip(i_node, i_ref):
                    if math.isinf(x) or math.isinf(y):
                        ok = ok and math.isinf(x) and math.isinf(y)
                    else:
                        d = abs(x - y)
                        selisih_maks = max(selisih_maks, d)
                        ok = ok and d <= TOL
                if hasattr(ag, "entri"):
                    n_ref = [sum(1 for t_e, k, _ in ag.entri if ag.t - 1 - t_e < ag.w and k == j)
                             for j in range(pv.N_ARMS)]
                    ok = ok and n_ref == n_node
                n_kep += 1
                n_ok += ok
                baris_tabel.append([no, t_node, pilih_node, pilih_ref, "Ya" if ok else "Tidak"]
                                   + ["inf" if math.isinf(x) else "%.6f" % x for x in i_node]
                                   + ["inf" if math.isinf(y) else "%.6f" % y for y in i_ref])
                if not ok:
                    print("TIDAK COCOK baris %d t=%d: node Arm %d %s, acuan Arm %d %s"
                          % (no, t_node, pilih_node, m.group(2), pilih_ref,
                             ";".join("inf" if math.isinf(y) else "%.6f" % y for y in i_ref)))
                continue
            m = RE_UPD.search(l)
            if m:
                ag.update(int(m.group(2)) - 1, float(m.group(3)))

    print("Reset agen terbaca: %d" % n_reset)
    print("Keputusan cocok (indeks, N, arm): %d dari %d; selisih indeks terbesar %.2e"
          % (n_ok, n_kep, selisih_maks))
    if a.keluaran:
        with open(a.keluaran, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["baris_log", "t", "arm_node", "arm_acuan", "cocok",
                        "I1_node", "I2_node", "I3_node", "I4_node",
                        "I1_acuan", "I2_acuan", "I3_acuan", "I4_acuan"])
            w.writerows(baris_tabel)
        print("Tabel ditulis ke", a.keluaran)
    return 0 if n_kep and n_ok == n_kep else 1


if __name__ == "__main__":
    sys.exit(main())
