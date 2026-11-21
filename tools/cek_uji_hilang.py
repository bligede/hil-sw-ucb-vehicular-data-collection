#!/usr/bin/env python3
"""Pemeriksa uji penyisipan kehilangan paket (catatan Pembimbing I 1 Nov 2026, B.3).

Membaca log node (env uji_hilang, node-1.3) dan log gateway (gateway-1.5) hasil
tools/rekam.py, memetakan tiap siklus ACK lewat penanda siklus seq_awal (nomor urut
paket berindeks 0), lalu memeriksa kriteria Pembimbing I per kasus:

  LULUS bila pada setiap siklus kasus itu node mendengar ACK (tidak ada "ACK timeout")
  dan jumlah pada ACK sama dengan jumlah paket yang tercatat di gateway.

Untuk tiap siklus dibandingkan tiga angka: n yang diterima node (baris ACK_TERIMA,
cadangan dari REWARD pdr=), n pada baris ACK gateway untuk seq_awal itu (kolom ke-4,
seq_awal di kolom ke-5), dan jumlah baris DATA gateway bernomor urut seq_awal..seq_awal+9
sejak ACK siklus sebelumnya. Jumlah harapan (10 dikurangi paket yang dilewati) dicetak
sebagai keterangan saja: paket yang hilang karena kanal tidak menggagalkan kriteria.

Siklus yang terpotong (CONTACT_WINDOW keluar: timeout_window, RESET AGEN, atau akhir
rekaman sebelum evaluasi ACK) dikecualikan dan dilaporkan terpisah.

Pemakaian:
  python tools/cek_uji_hilang.py --awalan data/uji_hilang/UJI_HILANG_01
  python tools/cek_uji_hilang.py --node X_node.txt --gateway X_gateway.csv
Kode keluar 0 bila semua kasus yang teramati LULUS.
"""

import argparse
import csv
import re
import sys

RE_KASUS = re.compile(r"UJI_HILANG kasus=(\d+) lewati=\{([\d,]*)\}")
RE_TX = re.compile(r"\bTX seq=(\d+) arm=(\d+) .*\bk=(\d+)")
RE_LEWATI = re.compile(r"\bLEWATI seq=(\d+) indeks=(\d+)")
RE_ACK_TERIMA = re.compile(r"ACK_TERIMA n=(\d+) penanda=(\d+)")
RE_ACK_TOLAK = re.compile(r"ACK_TOLAK (.*)")
RE_REWARD = re.compile(r"REWARD pdr=([\d.]+)")

N_SIKLUS = 10


class SiklusNode:
    def __init__(self, kasus, lewati):
        self.kasus = kasus
        self.lewati = lewati
        self.seq_awal = None
        self.arm = None
        self.n_tx = 0           # paket dipancarkan
        self.n_lewati = 0       # paket sengaja dilewati
        self.ack_node = None    # n dari ACK_TERIMA
        self.pdr_node = None    # n dari REWARD pdr=
        self.timeout = False
        self.tolak = []         # isi baris ACK_TOLAK
        self.selesai = False
        self.potong = None      # sebab terpotong

    def n_node(self):
        if self.ack_node is not None:
            return self.ack_node
        if self.pdr_node is not None:
            return int(round(self.pdr_node * N_SIKLUS))
        return None


def baca_node(berkas):
    with open(berkas, "rb") as f:
        teks = f.read().decode("utf-8", "replace").replace("\x00", "")
    siklus = []
    cur = None
    kasus_tunda = None   # (kasus, lewati) dari baris UJI_HILANG, berlaku untuk paket berikutnya

    def tutup(sebab):
        nonlocal cur
        if cur is not None and not cur.selesai:
            cur.potong = sebab
        cur = None

    for baris in teks.splitlines():
        m = RE_KASUS.search(baris)
        if m:
            lew = tuple(int(x) for x in m.group(2).split(",") if x)
            kasus_tunda = (int(m.group(1)), lew)
            continue
        m_tx = RE_TX.search(baris)
        m_lw = RE_LEWATI.search(baris)
        if m_tx or m_lw:
            seq = int((m_tx or m_lw).group(1))
            k = int(m_tx.group(3)) if m_tx else int(m_lw.group(2))
            if k == 0 or cur is None or cur.selesai:
                tutup("siklus baru sebelum evaluasi ACK")
                kasus, lew = kasus_tunda if kasus_tunda else (None, ())
                kasus_tunda = None
                cur = SiklusNode(kasus, lew)
                siklus.append(cur)
            if cur.seq_awal is None:
                cur.seq_awal = (seq - k) & 0xFFFF
            if m_tx:
                cur.n_tx += 1
                cur.arm = int(m_tx.group(2))
            else:
                cur.n_lewati += 1
            continue
        if cur is None:
            if "RESET AGEN" in baris:
                kasus_tunda = None
            continue
        m = RE_ACK_TERIMA.search(baris)
        if m:
            cur.ack_node = int(m.group(1))
            continue
        m = RE_ACK_TOLAK.search(baris)
        if m:
            cur.tolak.append(m.group(1).strip())
            continue
        m = RE_REWARD.search(baris)
        if m:
            cur.pdr_node = float(m.group(1))
            cur.selesai = True
            continue
        if "ACK timeout" in baris:
            cur.timeout = True
            cur.selesai = True
            continue
        if "timeout_window" in baris:
            tutup("timeout_window")
            continue
        if "RESET AGEN" in baris:
            tutup("RESET AGEN")
            kasus_tunda = None
            continue
    tutup("akhir rekaman")
    return siklus


def baca_gateway(berkas):
    """Kembalikan daftar siklus gateway berurutan: (seq_awal, n_ack, n_data, seq_data)."""
    with open(berkas, "rb") as f:
        teks = f.read().decode("utf-8", "replace").replace("\x00", "")
    hasil, data, tolak = [], [], 0
    for baris in teks.splitlines():
        if baris.startswith("# TOLAK"):
            tolak += 1
            continue
        if not (baris.startswith("DATA,") or baris.startswith("ACK,")):
            continue
        try:
            kol = next(csv.reader([baris]))
        except csv.Error:
            continue
        if len(kol) < 5:
            continue
        try:
            if kol[0] == "DATA":
                data.append(int(kol[3]))
            else:
                n_ack = int(kol[3])
                if not kol[4].strip():
                    # ACK gateway lama tanpa penanda siklus: tidak dapat dipetakan.
                    data = []
                    continue
                awal = int(kol[4])
                milik = [s for s in data if ((s - awal) & 0xFFFF) < N_SIKLUS]
                hasil.append(dict(seq_awal=awal, n_ack=n_ack, n_data=len(milik), seq=milik,
                                  dipakai=False))
                data = []
        except ValueError:
            continue
    return hasil, tolak


def cocokkan(sn, gw, mulai):
    """Siklus gateway berikutnya (sejak indeks mulai) dengan seq_awal yang sama."""
    for i in range(mulai, len(gw)):
        if not gw[i]["dipakai"] and gw[i]["seq_awal"] == sn.seq_awal:
            gw[i]["dipakai"] = True
            return gw[i], i + 1
    return None, mulai


def fmt(x):
    return "-" if x is None else str(x)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--awalan", help="awalan berkas rekam.py: <awalan>_node.txt dan <awalan>_gateway.csv")
    ap.add_argument("--node", help="log node")
    ap.add_argument("--gateway", help="log gateway (CSV)")
    a = ap.parse_args()
    node = a.node or (a.awalan + "_node.txt" if a.awalan else None)
    gwy = a.gateway or (a.awalan + "_gateway.csv" if a.awalan else None)
    if not node or not gwy:
        ap.error("sebutkan --awalan, atau --node dan --gateway")

    siklus = baca_node(node)
    gw, n_tolak_gw = baca_gateway(gwy)

    sah = [s for s in siklus if s.potong is None]
    potong = [s for s in siklus if s.potong is not None]

    print("Log node   : %s (%d siklus, %d terpotong)" % (node, len(siklus), len(potong)))
    print("Log gateway: %s (%d ACK berpenanda, %d baris # TOLAK)" % (gwy, len(gw), n_tolak_gw))
    if not any(s.kasus is not None for s in siklus):
        print("PERINGATAN: tidak ada baris UJI_HILANG; log ini bukan dari env uji_hilang.")
    print()
    print("%-6s %-9s %-7s %-4s %-5s %-6s %-8s %-7s %-7s %-6s %s" % (
        "kasus", "lewati", "seq_aw", "arm", "harap", "dengar", "ACK_node", "ACK_gw", "DATA_gw",
        "hasil", "catatan"))

    per_kasus = {}
    j = 0
    for s in sah:
        g, j = cocokkan(s, gw, j)
        n_node = s.n_node()
        dengar = (not s.timeout) and n_node is not None
        n_ack_gw = g["n_ack"] if g else None
        n_data_gw = g["n_data"] if g else None
        lulus = dengar and g is not None and n_node == n_ack_gw == n_data_gw
        catatan = []
        if g is None:
            catatan.append("tidak ada ACK gateway berpenanda %s" % s.seq_awal)
        if s.tolak:
            catatan.append("ACK_TOLAK x%d (%s)" % (len(s.tolak), s.tolak[0]))
        if s.ack_node is None and s.pdr_node is not None:
            catatan.append("n node dari REWARD pdr")
        harap = N_SIKLUS - len(s.lewati)
        if n_data_gw is not None and n_data_gw != harap:
            catatan.append("kanal kehilangan %d paket di luar pola" % (harap - n_data_gw))
        kunci = (s.kasus, s.lewati)
        per_kasus.setdefault(kunci, []).append(lulus)
        print("%-6s %-9s %-7s %-4s %-5d %-6s %-8s %-7s %-7s %-6s %s" % (
            fmt(s.kasus), "{%s}" % ",".join(map(str, s.lewati)), fmt(s.seq_awal), fmt(s.arm), harap,
            "ya" if dengar else "TIDAK", fmt(n_node), fmt(n_ack_gw), fmt(n_data_gw),
            "LULUS" if lulus else "GAGAL", "; ".join(catatan)))

    if potong:
        print()
        print("Siklus terpotong (dikecualikan dari kriteria):")
        for s in potong:
            print("  kasus=%s lewati={%s} seq_awal=%s paket=%d sebab=%s" % (
                fmt(s.kasus), ",".join(map(str, s.lewati)), fmt(s.seq_awal),
                s.n_tx + s.n_lewati, s.potong))

    sisa_gw = [g for g in gw if not g["dipakai"]]
    if sisa_gw:
        print()
        print("ACK gateway tanpa pasangan siklus node sah: %d (seq_awal %s)" % (
            len(sisa_gw), ", ".join(str(g["seq_awal"]) for g in sisa_gw[:10])))

    print()
    print("Ringkasan per kasus:")
    semua_lulus = bool(per_kasus)
    for (kasus, lew), hasil in sorted(per_kasus.items(), key=lambda x: (x[0][0] is None, x[0][0] or 0)):
        ok = all(hasil)
        semua_lulus &= ok
        print("  kasus %-4s lewati=%-9s siklus=%-3d lulus=%-3d -> %s" % (
            fmt(kasus), "{%s}" % ",".join(map(str, lew)), len(hasil), sum(hasil),
            "LULUS" if ok else "GAGAL"))
    if not per_kasus:
        print("  tidak ada siklus sah")
    print()
    print("HASIL: %s" % ("SEMUA KASUS LULUS" if semua_lulus else "ADA KASUS GAGAL atau tidak ada data"))
    return 0 if semua_lulus else 1


if __name__ == "__main__":
    sys.exit(main())
