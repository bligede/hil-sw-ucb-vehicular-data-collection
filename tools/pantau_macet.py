"""Pemantau rekaman gateway: berhenti dan memberi tanda bila DATA tidak masuk lebih lama dari ambang.

Membaca berkas CSV gateway yang sedang ditulis rekam.py (tidak membuka port). Jeda normal terpanjang
pada node 1.2 adalah cooldown 60 s sesudah satu contact window, jadi ambang bawaan 90 s.
Keluar dengan kode 2 bila macet, 0 bila rekaman selesai normal.

    python tools/pantau_macet.py data/uji_gateway/gabung_02_gateway.csv --ambang 90
"""
import argparse, os, sys, time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("berkas")
    ap.add_argument("--ambang", type=float, default=90.0)
    a = ap.parse_args()

    n_data, t_data, mulai = 0, None, time.time()
    while True:
        time.sleep(5)
        if not os.path.exists(a.berkas):
            continue
        isi = open(a.berkas, encoding="utf-8", errors="replace").read()
        n = isi.count("\nDATA,")
        if n != n_data:
            n_data, t_data = n, time.time()
        if "# rekam selesai" in isi:
            print("SELESAI %s DATA=%d" % (time.strftime("%H:%M:%S"), n_data))
            return 0
        if "GATEWAY_BOOT" in isi.split("# rekam mulai", 1)[-1].split("\n", 3)[-1] and n_data > 0:
            pass  # dicatat oleh analisis; pemantau hanya mengawasi kemacetan
        if t_data and time.time() - t_data > a.ambang:
            akhir = [x for x in isi.splitlines() if x.startswith(("DATA", "ACK"))][-3:]
            print("MACET %s: tidak ada DATA baru selama %.0f s (DATA=%d)" %
                  (time.strftime("%H:%M:%S"), time.time() - t_data, n_data))
            for x in akhir:
                print("  " + x)
            return 2


if __name__ == "__main__":
    sys.exit(main())
