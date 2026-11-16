"""Uji tanpa-reset gateway 1.4: port serial dibuka dan ditutup berulang saat siklus node berjalan.

Arahan Pembimbing I 15 Nov 2026 butir 7 (A.2). Port dibuka dengan cara yang sama dengan rekam.py
(buka_tanpa_reset: DTR dan RTS rendah sebelum open). Urutan:
  buka 1 -> kirim "W <id>" -> rekam BUKA_S -> tutup -> jeda TUTUP_S
  buka 2..N -> rekam BUKA_S -> tutup -> jeda TUTUP_S
  buka terakhir -> rekam AKHIR_S -> kirim "S" -> rekam 3 s -> tutup
Semua keluaran ditulis ke satu berkas dengan penanda "# PORT ..." dan cap waktu laptop.
Lulus bila: tidak ada "# GATEWAY_BOOT" atau "rst:" sesudah pembukaan pertama, ts_ms gateway terus naik
melintasi setiap tutup-buka, dan paket DATA masuk sebelum dan sesudah tiap tutup-buka.

    python tools/uji_buka_port.py --port COM11 --id TAHAP43_UJI_PORT_01 --keluar data/uji_gateway/uji_port_01.csv
"""
import argparse, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rekam import buka_tanpa_reset, KEPALA  # noqa: E402


def kini():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def rekam_selama(s, f, detik):
    t = time.time()
    while time.time() - t < detik:
        d = s.read(512)
        if d:
            f.write(d.decode("utf-8", "replace"))
            f.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--id", required=True)
    ap.add_argument("--keluar", required=True)
    ap.add_argument("--kali", type=int, default=4, help="jumlah tutup-buka")
    ap.add_argument("--buka-s", type=float, default=8.0)
    ap.add_argument("--tutup-s", type=float, default=5.0)
    ap.add_argument("--akhir-s", type=float, default=90.0)
    a = ap.parse_args()

    with open(a.keluar, "w", encoding="utf-8", newline="") as f:
        f.write(KEPALA)
        for i in range(a.kali + 1):
            s = buka_tanpa_reset(a.port)
            f.write("\n# PORT BUKA %d %s\n" % (i + 1, kini())); f.flush()
            print("buka %d %s" % (i + 1, kini()))
            if i == 0:
                time.sleep(0.3)
                s.write(("W %s\n" % a.id).encode())
            if i < a.kali:
                rekam_selama(s, f, a.buka_s)
            else:
                rekam_selama(s, f, a.akhir_s)
                s.write(b"S\n")
                rekam_selama(s, f, 3)
            s.close()
            f.write("\n# PORT TUTUP %d %s\n" % (i + 1, kini())); f.flush()
            print("tutup %d %s" % (i + 1, kini()))
            if i < a.kali:
                time.sleep(a.tutup_s)

    isi = open(a.keluar, encoding="utf-8", errors="replace").read()
    pertama = isi.find("# PORT BUKA 1")
    sesudah = isi[pertama:]
    boot = sesudah.count("# GATEWAY_BOOT") + len(re.findall(r"^rst:", sesudah, re.M))
    ts = [int(m.group(1)) for m in re.finditer(r"^(?:DATA|ACK),(\d+),", sesudah, re.M)]
    turun = sum(1 for x, y in zip(ts, ts[1:]) if y < x)
    blok = re.split(r"# PORT TUTUP \d+", sesudah)
    data_per_blok = [b.count("\nDATA,") for b in blok[:-1]]
    print("GATEWAY_BOOT/rst sesudah buka 1: %d" % boot)
    print("ts_ms turun: %d dari %d baris" % (turun, len(ts)))
    print("DATA per pembukaan: %s" % data_per_blok)
    print("TOLAK: %d, RADIO_REINIT: %d" % (sesudah.count("# TOLAK"), sesudah.count("RADIO_REINIT")))
    print("LULUS" if boot == 0 and turun == 0 and all(data_per_blok) else "PERIKSA")


if __name__ == "__main__":
    main()
