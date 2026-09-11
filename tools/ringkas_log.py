"""Peringkas log paket menjadi baris contact window — SOP Subbab III.1 cara kedua.

Fase Uji Multi-Kecepatan menghasilkan sekitar 103.680 baris paket. Jumlah itu
tidak mungkin diketik manual dan terlalu berat bila seluruhnya ditempel ke sheet
03. Skrip ini meringkas satu atau beberapa berkas log sesi menjadi satu baris per
ID window, siap ditempel sebagai NILAI ke kolom G, H, dan J sheet 04-Log Window.

Dua hal yang ditangani skrip ini dan mudah keliru bila dikerjakan manual:

1. Pergeseran energi satu paket. Medan e_prev_mJ pada paket ke-n berisi energi
   paket ke-(n-1), karena energi paket berjalan baru selesai dihitung sesudah
   transmisinya tuntas (penyimpangan D6). Skrip menggesernya kembali.

2. Paket hilang. Jumlah paket yang DIKIRIM node tidak sama dengan jumlah baris
   DATA yang diterima gateway. N_tx direkonstruksi dari rentang nomor urut,
   bukan dari jumlah baris, sehingga PDR tidak menjadi 100 persen secara palsu.

Pemakaian:
    python ringkas_log.py data/2026-09-05_S7/*.csv
    python ringkas_log.py log.csv --keluaran ringkas.csv
"""

import argparse
import csv
import glob
import sys
from collections import defaultdict


class Window:
    def __init__(self, wid):
        self.id = wid
        self.seq = []          # nomor urut paket yang diterima
        self.energi = []       # energi per paket, sudah digeser
        self.arm = []
        self.rssi = []
        self.vbatt = []
        self.n_ack_total = 0   # jumlah paket sukses menurut ACK gateway
        self.n_siklus_ack = 0

    # -- N_tx direkonstruksi dari rentang nomor urut --
    @property
    def n_tx(self):
        if not self.seq:
            return 0
        return max(self.seq) - min(self.seq) + 1

    @property
    def n_diterima(self):
        return len(self.seq)

    @property
    def n_ack(self):
        # ACK adalah sumber PDR yang sah karena itulah yang dipakai node
        # menghitung reward. Bila tidak ada ACK sama sekali, jatuh kembali ke
        # jumlah baris yang diterima.
        return self.n_ack_total if self.n_siklus_ack else self.n_diterima

    @property
    def e_total(self):
        return sum(self.energi)

    @property
    def pdr(self):
        return self.n_ack / self.n_tx if self.n_tx else 0.0

    @property
    def e_per_sukses(self):
        return self.e_total / self.n_ack if self.n_ack else 0.0

    @property
    def arm_dominan(self):
        if not self.arm:
            return ""
        return max(set(self.arm), key=self.arm.count)

    @property
    def hilang(self):
        return self.n_tx - self.n_diterima


def baca(berkas, windows):
    with open(berkas, newline="", encoding="utf-8") as f:
        for baris in csv.DictReader(f):
            jenis = (baris.get("jenis") or "").strip()
            wid = (baris.get("id_window") or "").strip()
            if not wid or wid == "BELUM-DISET":
                continue

            w = windows.setdefault(wid, Window(wid))

            if jenis == "DATA":
                try:
                    w.seq.append(int(baris["seq"]))
                    w.arm.append(int(baris["arm"]))
                    w.energi.append(float(baris["e_prev_mJ"]))
                    w.rssi.append(int(baris["rssi"]))
                    w.vbatt.append(float(baris["vbatt"]))
                except (ValueError, KeyError, TypeError):
                    continue  # baris rusak, lewati
            elif jenis == "ACK":
                try:
                    w.n_ack_total += int(baris["seq"])
                    w.n_siklus_ack += 1
                except (ValueError, KeyError, TypeError):
                    pass


def geser_energi(w):
    """Kembalikan energi ke paket yang benar (lihat catatan 1 di docstring)."""
    if len(w.energi) < 2:
        w.energi = []
        return
    # Energi pada baris ke-i milik paket ke-(i-1); baris pertama dibuang karena
    # merujuk paket sebelum window dimulai.
    w.energi = w.energi[1:]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("berkas", nargs="+", help="berkas CSV log gateway")
    p.add_argument("--keluaran", default=None, help="tulis hasil ke CSV")
    p.add_argument("--min-tx", type=int, default=10,
                   help="ambang N_tx agar window dianggap sah (SOP III.10)")
    a = p.parse_args()

    daftar = []
    for pola in a.berkas:
        cocok = glob.glob(pola)
        if not cocok:
            print(f"peringatan: tidak ada berkas cocok untuk {pola}",
                  file=sys.stderr)
        daftar.extend(cocok)

    if not daftar:
        print("tidak ada berkas untuk diproses", file=sys.stderr)
        return 1

    windows = {}
    for b in daftar:
        baca(b, windows)

    for w in windows.values():
        geser_energi(w)

    baris = []
    for wid in sorted(windows):
        w = windows[wid]
        baris.append({
            "id_window": w.id,
            "n_tx": w.n_tx,
            "n_ack": w.n_ack,
            "e_total_mJ": round(w.e_total, 2),
            "pdr": round(w.pdr, 4),
            "e_per_sukses_mJ": round(w.e_per_sukses, 2),
            "arm_dominan": w.arm_dominan,
            "rssi_rata": round(sum(w.rssi) / len(w.rssi), 1) if w.rssi else "",
            "vbatt_min": round(min(w.vbatt), 2) if w.vbatt else "",
            "paket_hilang": w.hilang,
            "valid": "Ya" if w.n_tx >= a.min_tx else "Tidak",
        })

    kolom = list(baris[0].keys()) if baris else []
    if a.keluaran:
        with open(a.keluaran, "w", newline="", encoding="utf-8") as f:
            wr = csv.DictWriter(f, fieldnames=kolom)
            wr.writeheader()
            wr.writerows(baris)
        print(f"{len(baris)} window ditulis ke {a.keluaran}")
    else:
        wr = csv.DictWriter(sys.stdout, fieldnames=kolom)
        wr.writeheader()
        wr.writerows(baris)

    sah = sum(1 for b in baris if b["valid"] == "Ya")
    hilang = sum(b["paket_hilang"] for b in baris)
    print(f"\n# {len(daftar)} berkas, {len(baris)} window, {sah} sah, "
          f"{hilang} paket hilang", file=sys.stderr)
    print("# Tempelkan kolom n_tx, n_ack, dan e_total_mJ sebagai NILAI ke",
          file=sys.stderr)
    print("# kolom G, H, dan J sheet 04-Log Window (SOP III.1 cara kedua).",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
