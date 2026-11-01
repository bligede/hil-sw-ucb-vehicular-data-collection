#!/usr/bin/env python3
"""Uji unit agen acuan (prevalidasi_swucb.py) dengan kasus yang sama persis seperti
node/uji/uji_agen.cpp. Nilai harapan dihitung tangan dari Persamaan (2.5), xi = 0,5;
lihat kepala berkas uji_agen.cpp. Toleransi 0,0001.

Pemakaian:  python tools/uji_agen.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prevalidasi_swucb as pv  # noqa: E402

INF = math.inf
TOL = 1e-4
hasil = {"lolos": 0, "gagal": 0}


def periksa(kasus, ag, harap, pilih_harap):
    skor = [ag.score(a) for a in range(pv.N_ARMS)]
    pilih = ag.select()
    ok = pilih == pilih_harap
    for s, h in zip(skor, harap):
        ok = ok and (math.isinf(s) if math.isinf(h) else abs(s - h) <= TOL)
    teks = ";".join("inf" if math.isinf(s) else "%.6f" % s for s in skor)
    print("%s %-34s t=%d I=%s pilih=%d (harap %d)"
          % ("LOLOS" if ok else "GAGAL", kasus, ag.t, teks, pilih + 1, pilih_harap + 1))
    hasil["lolos" if ok else "gagal"] += 1
    return pilih


def paksa(ag, arm, r):
    ag.select()
    if arm >= 0:
        ag.update(arm, r)


def main():
    r = [0.5, 0.4, 0.3, 0.2]

    ag = pv.SWUCB(w=5)                                   # K1, K2
    p = periksa("K1a semua N=0, pilih Arm 1", ag, [INF] * 4, 0)
    ag.update(p, r[p])
    p = periksa("K2 t=1 tanpa bonus, pilih Arm 2", ag, [0.5, INF, INF, INF], 1)
    ag.update(p, r[p])
    for _ in range(2):
        p = ag.select()
        ag.update(p, r[p])
    periksa("K1b t=4, N=1 semua, pilih Arm 1", ag, [1.332555, 1.232555, 1.132555, 1.032555], 0)

    ag = pv.SWUCB(w=5)                                   # K3
    paksa(ag, 1, 0.9)
    for _ in range(5):
        paksa(ag, 0, 0.5)
    periksa("K3 Arm 2 keluar window, N=0", ag, [0.901178, INF, INF, INF], 1)

    ag = pv.SWUCB(w=5)                                   # K4
    for a, rr in ((0, -1.0), (1, 0.4), (2, 0.3), (3, 0.2), (0, 0.5), (1, 0.4), (2, 0.3)):
        paksa(ag, a, rr)
    periksa("K4 artefak -1 keluar window", ag, [1.397061, 1.297061, 0.934318, 1.097061], 0)

    ag = pv.SWUCB(w=5)                                   # K5
    for a, rr in ((0, 0.5), (1, 0.4), (2, 0.3), (3, 0.2), (-1, 0.0), (0, 0.5)):
        paksa(ag, a, rr)
    periksa("K5 siklus tanpa reward geser t", ag, [1.397061, 1.297061, 1.197061, 1.097061], 0)

    ag = pv.SWUCB()                                      # K6, W = 50
    paksa(ag, 1, -0.442)
    for _ in range(49):
        paksa(ag, 0, 0.49)
    periksa("K6a W=50, t=50, Arm 2 masih di window", ag, [0.689796, 0.956575, INF, INF], 2)
    ag.update(0, 0.49)
    periksa("K6b W=50, t=51, Arm 2 keluar window", ag, [0.687788, INF, INF, INF], 1)

    ag = pv.UCB1()                                       # K7
    for a, rr in ((0, 0.5), (1, 0.4), (2, 0.3), (3, 0.2), (0, 0.3)):
        paksa(ag, a, rr)
    periksa("K7 UCB1 t=5, pilih Arm 2", ag, [1.034318, 1.297061, 1.197061, 1.097061], 1)

    print("# HASIL lolos=%d gagal=%d" % (hasil["lolos"], hasil["gagal"]))
    return 1 if hasil["gagal"] else 0


if __name__ == "__main__":
    sys.exit(main())
