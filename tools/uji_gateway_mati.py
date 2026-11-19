"""Uji: apakah node pulih sesudah gateway mati sementara di tengah window (pengganti uji_node_macet.py).

Sama dengan tools/uji_node_macet.py (1 Nov 2026), tetapi setiap reset gateway yang disengaja ditandai di log
gateway dan log node (arahan Pembimbing I 19 Nov 2026):
  # RESET_SENGAJA mulai cara=EN_rendah_via_RTS lama_s=30 <waktu>   tepat sebelum EN ditahan rendah
  # RESET_SENGAJA selesai <waktu>                                    tepat sesudah EN dilepas
Baris GATEWAY_BOOT atau rst: di antara kedua penanda itu adalah akibat uji, bukan kegagalan gateway.
Syarat: garis RTS adaptor gateway tersambung ke EN (USB papan gateway). Dengan CP2102 yang hanya RXD/TXD/GND,
EN tidak dapat dikendalikan dan uji ini tidak dapat dijalankan.

    python tools/uji_gateway_mati.py --gateway COM11 --node COM5 --awalan data/tahap43/uji_gateway_mati_01
"""
import argparse, os, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rekam import buka_tanpa_reset, rekam, KEPALA  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gateway", required=True)
    ap.add_argument("--node", required=True)
    ap.add_argument("--awalan", required=True)
    ap.add_argument("--mati-s", type=float, default=30.0)
    ap.add_argument("--sesudah-s", type=float, default=300.0)
    a = ap.parse_args()

    g = buka_tanpa_reset(a.gateway)
    n = buka_tanpa_reset(a.node)
    henti = threading.Event()
    cat, pg, pn = [], [], []
    tg = threading.Thread(target=rekam, args=(g, a.awalan + "_gateway.csv", KEPALA, henti, cat, pg), daemon=True)
    tn = threading.Thread(target=rekam, args=(n, a.awalan + "_node.txt", "", henti, cat, pn), daemon=True)
    tg.start(); tn.start()
    t0 = time.time()

    def jumlah(pola):
        try:
            return open(a.awalan + "_gateway.csv", encoding="utf-8", errors="replace").read().count(pola)
        except OSError:
            return 0

    while jumlah("\nDATA,") < 15 and time.time() - t0 < 360:
        time.sleep(1)
    print("paket sebelum gateway dimatikan:", jumlah("\nDATA,"))

    teks = "RESET_SENGAJA mulai cara=EN_rendah_via_RTS lama_s=%.0f" % a.mati_s
    pg.append(teks); pn.append(teks)
    time.sleep(0.6)                       # beri waktu perekam menulis penanda sebelum gateway mati
    g.rts = True                          # EN gateway rendah: gateway mati
    time.sleep(a.mati_s)
    g.rts = False                         # gateway menyala lagi
    pg.append("RESET_SENGAJA selesai"); pn.append("RESET_SENGAJA selesai")
    sebelum = jumlah("\nDATA,")
    time.sleep(a.sesudah_s)
    print("paket dalam %.0f s sesudah gateway menyala: %d" % (a.sesudah_s, jumlah("\nDATA,") - sebelum))
    henti.set()
    time.sleep(1)
    g.close(); n.close()


if __name__ == "__main__":
    main()
