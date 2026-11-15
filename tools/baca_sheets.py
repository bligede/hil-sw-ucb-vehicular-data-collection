"""Membaca baris terakhir lembar LOG Google Sheets gateway, tanpa kabel serial.

    python tools/baca_sheets.py --n 300 --keluar data/xxx/sheets_ekor.csv

URL web app dan token dibaca dari gateway/src/config/secrets.h (tidak dicetak).
Memakai aksi=ekor pada Code.gs (ditambahkan 15 Nov 2026); web app harus sudah
di-deploy ulang dengan versi baru supaya aksi itu dikenal.
Keluaran: CSV dengan kolom yang sama dengan lembar LOG, lalu ringkasan singkat.
"""
import argparse, csv, json, os, re, sys, urllib.parse, urllib.request

AKAR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRETS = os.path.join(AKAR, "gateway", "src", "config", "secrets.h")


def baca_secret(nama):
    with open(SECRETS, encoding="utf-8") as f:
        m = re.search(r'#define\s+%s\s+"([^"]*)"' % nama, f.read())
    if not m or not m.group(1):
        sys.exit("secrets.h tidak memuat %s" % nama)
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=200, help="jumlah baris terakhir (maks 5000)")
    ap.add_argument("--keluar", required=True, help="berkas CSV keluaran")
    a = ap.parse_args()

    url = baca_secret("SHEETS_URL")
    q = urllib.parse.urlencode({"t": baca_secret("SHEETS_TOKEN"), "aksi": "ekor", "n": a.n})
    with urllib.request.urlopen(url + "?" + q, timeout=60) as r:
        isi = json.loads(r.read().decode("utf-8"))
    if not isi.get("ok"):
        sys.exit("web app menolak: %s" % isi.get("error", isi))
    if "baris" not in isi:
        sys.exit("web app belum mengenal aksi=ekor (deploy ulang Code.gs dengan versi baru)")

    with open(a.keluar, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(isi["kolom"])
        w.writerows(isi["baris"])

    rows = isi["baris"]
    k = {nama: i for i, nama in enumerate(isi["kolom"])}
    data = [r for r in rows if r[k["jenis"]] == "DATA"]
    boots = []
    for r in rows:
        if r[k["boot"]] not in boots:
            boots.append(r[k["boot"]])
    print("total baris LOG: %s; diambil %d mulai baris %s" % (isi["total"], len(rows), isi.get("baris_awal")))
    print("DATA %d, ACK %d, boot gateway berbeda: %s" % (
        len(data), sum(1 for r in rows if r[k["jenis"]] == "ACK"), ", ".join(map(str, boots))))
    if rows:
        print("buang (terakhir): %s" % rows[-1][k["buang"]])
    print("tersimpan: %s" % a.keluar)


if __name__ == "__main__":
    main()
