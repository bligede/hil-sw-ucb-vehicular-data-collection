"""Pengolah log gateway per siklus ACK: cakupan arm, ekspor siklus dan contact window.

Menjawab tiga butir catatan Pembimbing I (18 September 2026):
  A.1  tegangan bus mentah tersedia per contact window (vbatt_awal, vbatt_min, vbatt_akhir)
  A.2  jumlah siklus ACK per arm untuk tiap skenario, dengan tanda arm yang datanya tipis
  B.1  berkas serah terima per siklus ACK dan per contact window

Satuan pengamatan agen adalah SATU SIKLUS ACK (sepuluh paket, satu reward, satu arm),
bukan satu contact window. Satu contact window memuat banyak siklus dan arm-nya dapat
berganti antar siklus, sehingga data per arm hanya dapat dibangun dari tingkat siklus.

Asumsi pemulihan siklus dari log gateway:
  - Siklus dibatasi oleh baris ACK. Baris DATA sesudah ACK terakhir tanpa ACK berikutnya
    dianggap siklus tanpa ACK, dan mendapat reward penalti sebesar REWARD_TIMEOUT.
  - N_tx per siklus tetap sepuluh (ACK_SETIAP_N). Blok nomor urut dihitung dari paket
    pertama yang diterima pada window itu; bila paket pertama window hilang, blok
    bergeser satu dua nomor dan energinya sedikit meleset.
  - Energi paket ke-n dibawa oleh paket ke-(n+1) (medan e_prev_mJ). Baris pertama tiap
    siklus dibuang dari perhitungan energi, karena e_prev-nya milik paket terakhir siklus
    sebelumnya yang dapat memakai arm lain. Energi siklus adalah rata-rata e_prev dari baris
    sisanya, dan kolom n_energi menyatakan jumlahnya. Bila tidak ada satu pun, dipakai median
    energi siklus lain dengan arm yang sama pada window itu; bila tetap tidak ada, reward
    dikosongkan.
  - Arm siklus adalah arm pada paket terakhir yang diterima, sama dengan yang menerima
    update reward di node (rtc_arm_sekarang). Kolom arm_beragam menandai siklus yang
    memuat lebih dari satu arm.
  - Siklus yang seluruh sepuluh paketnya hilang tidak meninggalkan baris apa pun. Jumlahnya
    dilaporkan pada kolom blok_hilang di berkas per window.

Pemakaian:
    python olah_siklus.py "data/2026-09-05_S7/*.csv" --metode SW-UCB --e-maks 113.3
    python olah_siklus.py "data/*/*.csv" --min-siklus 10 --siklus siklus.csv --window window.csv
"""

import argparse
import csv
import glob
import sys
from collections import defaultdict, OrderedDict

ACK_SETIAP_N = 10
REWARD_TIMEOUT = -1.0
ARM = {1: (5, "4/5"), 2: (10, "4/5"), 3: (14, "4/7"), 4: (20, "4/8")}
SKEN = {
    "S1": (5, 50), "S2": (5, 100), "S3": (5, 150),
    "S4": (15, 50), "S5": (15, 100), "S6": (15, 150),
    "S7": (30, 50), "S8": (30, 100), "S9": (30, 150),
}


def baca(berkas, windows):
    """Kumpulkan kejadian per id_window menurut urutan baris pada berkas."""
    with open(berkas, newline="", encoding="utf-8") as f:
        for b in csv.DictReader(f):
            jenis = (b.get("jenis") or "").strip()
            wid = (b.get("id_window") or "").strip()
            if not wid or wid == "BELUM-DISET":
                continue
            ev = windows.setdefault(wid, [])
            try:
                if jenis == "DATA":
                    ev.append(("D", dict(
                        seq=int(b["seq"]), arm=int(b["arm"]),
                        e_prev=float(b["e_prev_mJ"]), vbatt=float(b["vbatt"]),
                        rssi=int(b["rssi"]))))
                elif jenis == "ACK":
                    ev.append(("A", int(b["seq"])))
            except (ValueError, KeyError, TypeError):
                continue


def bentuk_siklus(wid, ev, a):
    """Ubah kejadian satu window menjadi daftar siklus ACK."""
    kelompok, cur = [], []
    for tipe, isi in ev:
        if tipe == "D":
            cur.append(isi)
        else:
            kelompok.append((cur, isi))
            cur = []
    if cur:
        kelompok.append((cur, None))
    kelompok = [(r, n) for r, n in kelompok if r]
    if not kelompok:
        return [], 0

    b0 = min(r["seq"] for rows, _ in kelompok for r in rows)

    siklus, blok_lalu, hilang = [], None, 0
    for k, (rows, n_ack) in enumerate(kelompok, 1):
        blok = (min(r["seq"] for r in rows) - b0) // ACK_SETIAP_N
        if blok_lalu is not None and blok > blok_lalu + 1:
            hilang += blok - blok_lalu - 1
        blok_lalu = blok
        urut = sorted(rows, key=lambda r: r["seq"])
        e_paket = [r["e_prev"] for r in urut[1:]]
        arm = urut[-1]["arm"]
        pdr = (n_ack / ACK_SETIAP_N) if n_ack is not None else 0.0
        e_rata = sum(e_paket) / len(e_paket) if e_paket else None
        reward = None
        siklus.append(OrderedDict(
            id_window=wid, siklus=k, arm=arm,
            tp_dBm=ARM.get(arm, ("", ""))[0], cr=ARM.get(arm, ("", ""))[1],
            n_tx=ACK_SETIAP_N, n_terima_gateway=len(rows),
            n_ack=n_ack if n_ack is not None else 0,
            ada_ack="Ya" if n_ack is not None else "Tidak",
            pdr=round(pdr, 3),
            e_rata_mJ=round(e_rata, 2) if e_rata is not None else "",
            n_energi=len(e_paket),
            reward="",
            vbatt_awal=round(rows[0]["vbatt"], 2),
            vbatt_min=round(min(r["vbatt"] for r in rows), 2),
            rssi_rata=round(sum(r["rssi"] for r in rows) / len(rows), 1),
            arm_beragam="Ya" if len({r["arm"] for r in rows}) > 1 else "Tidak",
        ))

    for s_ in siklus:
        if s_["e_rata_mJ"] == "":
            lain = sorted(x["e_rata_mJ"] for x in siklus if x["arm"] == s_["arm"] and x["e_rata_mJ"] != "")
            if lain:
                s_["e_rata_mJ"] = lain[len(lain) // 2]
    for s_ in siklus:
        if s_["ada_ack"] == "Tidak":
            r_ = REWARD_TIMEOUT
        elif s_["e_rata_mJ"] == "" or a.e_maks is None:
            r_ = None
        else:
            r_ = a.alpha * s_["pdr"] - a.beta * min(s_["e_rata_mJ"] / a.e_maks, 1.0)
        s_["reward"] = round(r_, 4) if r_ is not None else ""
    return siklus, hilang


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("berkas", nargs="+", help="berkas CSV log gateway (boleh pola)")
    p.add_argument("--metode", default="SW-UCB",
                   help="SW-UCB, UCB1, Statis-Worst, Statis-Opt, Sweep (satu nilai per pemanggilan)")
    p.add_argument("--e-maks", type=float, default=None, help="E_maks (mJ) untuk menghitung reward")
    p.add_argument("--alpha", type=float, default=1.0)
    p.add_argument("--beta", type=float, default=1.0)
    p.add_argument("--min-siklus", type=int, default=10,
                   help="batas kecukupan siklus ACK per arm per skenario (ditetapkan sebelum pengambilan data)")
    p.add_argument("--siklus", help="tulis berkas per siklus ACK")
    p.add_argument("--window", help="tulis berkas per contact window")
    p.add_argument("--cakupan", help="tulis laporan cakupan arm per skenario")
    a = p.parse_args()

    daftar = []
    for pola in a.berkas:
        cocok = sorted(glob.glob(pola))
        if not cocok:
            print(f"peringatan: tidak ada berkas cocok untuk {pola}", file=sys.stderr)
        daftar += cocok
    if not daftar:
        print("tidak ada berkas untuk diproses", file=sys.stderr)
        return 1

    windows = OrderedDict()
    for b in daftar:
        baca(b, windows)

    semua_siklus, baris_window = [], []
    cakupan = defaultdict(lambda: {"window": 0, "siklus": {1: 0, 2: 0, 3: 0, 4: 0}, "paket": {1: 0, 2: 0, 3: 0, 4: 0}})
    for wid, ev in windows.items():
        sk, hilang = bentuk_siklus(wid, ev, a)
        if not sk:
            continue
        kode = wid.split("-")[0]
        kec, jrk = SKEN.get(kode, ("", ""))
        semua_siklus += sk

        per_arm = {i: sum(1 for s in sk if s["arm"] == i) for i in (1, 2, 3, 4)}
        reward = [s["reward"] for s in sk if s["reward"] != ""]
        vb = [ev_[1]["vbatt"] for ev_ in ev if ev_[0] == "D"]
        n_ack = sum(s["n_ack"] for s in sk)
        n_tx = sum(s["n_tx"] for s in sk)
        e_total = sum(s["e_rata_mJ"] * ACK_SETIAP_N for s in sk if s["e_rata_mJ"] != "")
        baris_window.append(OrderedDict(
            id_window=wid, skenario=kode, kecepatan_kmj=kec, jarak_m=jrk, metode=a.metode,
            n_siklus=len(sk), siklus_tanpa_ack=sum(1 for s in sk if s["ada_ack"] == "Tidak"),
            blok_hilang=hilang,
            arm_dominan=max(per_arm, key=per_arm.get),
            siklus_arm1=per_arm[1], siklus_arm2=per_arm[2], siklus_arm3=per_arm[3], siklus_arm4=per_arm[4],
            n_tx=n_tx, n_ack=n_ack, pdr=round(n_ack / n_tx, 4) if n_tx else "",
            e_total_mJ=round(e_total, 1),
            reward_rata=round(sum(reward) / len(reward), 4) if reward else "",
            vbatt_awal=round(vb[0], 2), vbatt_min=round(min(vb), 2), vbatt_akhir=round(vb[-1], 2),
            valid="Ya" if n_tx >= ACK_SETIAP_N else "Tidak",
        ))
        c = cakupan[kode]
        c["window"] += 1
        for s in sk:
            c["siklus"][s["arm"]] = c["siklus"].get(s["arm"], 0) + 1
            c["paket"][s["arm"]] = c["paket"].get(s["arm"], 0) + s["n_terima_gateway"]

    def tulis(nama, baris):
        if nama and baris:
            with open(nama, "w", newline="", encoding="utf-8") as f:
                w = csv.DictWriter(f, fieldnames=list(baris[0].keys()))
                w.writeheader()
                w.writerows(baris)
            print(f"{len(baris)} baris ditulis ke {nama}")

    tulis(a.siklus, semua_siklus)
    tulis(a.window, baris_window)

    print(f"\nCakupan arm per skenario, metode {a.metode}, batas {a.min_siklus} siklus ACK per arm")
    print(f"{'Skenario':9s}{'window':>7s}{'siklus':>8s}   {'arm1':>6s}{'arm2':>6s}{'arm3':>6s}{'arm4':>6s}   status")
    laporan, ada_tipis = [], False
    for kode in sorted(cakupan):
        c = cakupan[kode]
        tot = sum(c["siklus"].values())
        tipis = [i for i in (1, 2, 3, 4) if c["siklus"][i] < a.min_siklus]
        ada_tipis |= bool(tipis)
        status = "cukup" if not tipis else "TIPIS: arm " + ",".join(map(str, tipis))
        print(f"{kode:9s}{c['window']:7d}{tot:8d}   {c['siklus'][1]:6d}{c['siklus'][2]:6d}{c['siklus'][3]:6d}{c['siklus'][4]:6d}   {status}")
        laporan.append(OrderedDict(
            skenario=kode, metode=a.metode, window=c["window"], siklus=tot,
            arm1=c["siklus"][1], arm2=c["siklus"][2], arm3=c["siklus"][3], arm4=c["siklus"][4],
            paket_arm1=c["paket"][1], paket_arm2=c["paket"][2], paket_arm3=c["paket"][3], paket_arm4=c["paket"][4],
            batas=a.min_siklus, status=status))
    tulis(a.cakupan, laporan)
    print(f"\n# {len(daftar)} berkas, {len(baris_window)} window, {len(semua_siklus)} siklus ACK",
          file=sys.stderr)
    if ada_tipis:
        print("# Ada skenario dengan arm yang datanya tipis. Laporkan ke Pembimbing I.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
