#!/usr/bin/env python3
"""Perekam serial gateway dan node (catatan Pembimbing I, 1 Nov 2026, A.2).

Port dibuka dengan DTR dan RTS nonaktif sebelum port terbuka, sehingga membuka
port tidak memicu reset gateway. Karena gateway tidak menyala ulang, kepala CSV
tidak dicetak gateway; perekam menuliskannya sendiri di awal berkas. Bila gateway
tetap ter-reset (sambungan USB terputus, kabel bergeser), baris "# GATEWAY_BOOT"
akan muncul di tengah rekaman dan skenario harus diulang menurut SOP.

Log node direkam dari adaptor USB-serial (mis. FT232RL, logika 3,3 V) yang hanya
menyambung GND dan TX0 node, tanpa jalur 5 V, supaya node tetap dicatu baterai.
Baris IDX dan UPD dipakai oleh tools/replay_indeks.py.

Pemakaian:
  python tools/rekam.py --gateway COM11 --node COM7 --detik 900 --awalan data/tahap43/rekam_01
  python tools/rekam.py --gateway COM11 --id S1_SWUCB_01        (kirim ID window)
Berhenti dengan Ctrl+C.
"""

import argparse
import sys
import threading
import time

import serial

KEPALA = "jenis,ts_ms,id_window,seq,arm,e_prev_mJ,vbatt,rssi,snr,ax,ay,az\n"


def buka_tanpa_reset(port, baud=115200):
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.2
    s.dsrdtr = False
    s.rtscts = False
    s.dtr = False      # diterapkan saat open(), sebelum garis sempat aktif
    s.rts = False
    s.open()
    return s


def rekam(s, berkas, kepala, berhenti, catatan):
    with open(berkas, "w", encoding="utf-8", newline="") as f:
        if kepala:
            f.write(kepala)
        f.write("# rekam mulai %s port=%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), s.port))
        f.flush()
        n = 0
        while not berhenti.is_set():
            d = s.read(512)
            if d:
                teks = d.decode("utf-8", "replace")
                f.write(teks)
                f.flush()
                n += teks.count("\n")
                if "# GATEWAY_BOOT" in teks:
                    catatan.append("PERINGATAN: gateway menyala ulang di tengah rekaman (%s)" % berkas)
                    print("\a!!! GATEWAY_BOOT terekam: skenario ini harus diulang (SOP)")
        f.write("# rekam selesai %s baris=%d\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gateway", help="port gateway, mis. COM11")
    ap.add_argument("--node", help="port adaptor serial node, mis. COM7")
    ap.add_argument("--id", help="ID window yang dikirim ke gateway sesudah port terbuka")
    ap.add_argument("--detik", type=float, default=0, help="lama rekaman; 0 = sampai Ctrl+C")
    ap.add_argument("--awalan", default="rekam", help="awalan nama berkas keluaran")
    ap.add_argument("--w-uji", type=float, default=0,
                    help="uji A.2: detik ke berapa perintah 'W <id>_W' dikirim di tengah rekaman; 0 = tidak")
    a = ap.parse_args()
    if not a.gateway and not a.node:
        ap.error("sebutkan --gateway dan/atau --node")

    berhenti = threading.Event()
    catatan, thread_rekam, port = [], [], []
    if a.gateway:
        g = buka_tanpa_reset(a.gateway)
        port.append(g)
        thread_rekam.append(threading.Thread(target=rekam, args=(g, a.awalan + "_gateway.csv", KEPALA,
                                                         berhenti, catatan), daemon=True))
    if a.node:
        n = buka_tanpa_reset(a.node)
        port.append(n)
        thread_rekam.append(threading.Thread(target=rekam, args=(n, a.awalan + "_node.txt", "",
                                                         berhenti, catatan), daemon=True))
    for u in thread_rekam:
        u.start()
    if a.gateway and a.id:
        time.sleep(0.5)
        port[0].write(("W %s\n" % a.id).encode())
    print("Merekam ke %s_*.  Ctrl+C untuk berhenti." % a.awalan)
    t0 = time.time()
    w_terkirim = False
    try:
        while not a.detik or time.time() - t0 < a.detik:
            time.sleep(0.5)
            if a.gateway and a.w_uji and not w_terkirim and time.time() - t0 >= a.w_uji:
                port[0].write(("W %s_W\n" % (a.id or "UJI")).encode())
                w_terkirim = True
                print("W uji dikirim pada %.0f s" % (time.time() - t0))
    except KeyboardInterrupt:
        pass
    berhenti.set()
    for u in thread_rekam:
        u.join(timeout=2)
    for s in port:
        s.close()
    for c in catatan:
        print(c)
    return 1 if catatan else 0


if __name__ == "__main__":
    sys.exit(main())
