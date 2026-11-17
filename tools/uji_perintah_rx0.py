"""Uji kirim perintah ke gateway lewat adaptor USB-serial luar (TXD adaptor -> resistor seri -> RX0 gateway),
saat gateway dicatu charger HP lewat USB papannya sendiri.

Tujuan: memastikan perintah W (ID window) dan S dapat dikirim tanpa membuka port USB papan gateway,
sebagai persiapan susunan rekaman resmi (arahan Pembimbing I 15 Nov 2026 butir 9).

Urutan: rekam 5 s -> S sebanyak --s-kali (jeda 3 s), periksa tiap balasan -> W <id> -> tunggu sampai
"# id_window=<id> berlaku" dan baris DATA ber-ID baru -> S terakhir. Semua keluaran ke satu berkas.
Lulus bila: semua S berbalas, W diterima dan berlaku, DATA ber-ID baru muncul, tidak ada GATEWAY_BOOT/rst,
ts_ms tidak turun.

    python tools/uji_perintah_rx0.py --port COM11 --id TAHAP43_UJI_W_01 --keluar data/uji_gateway/uji_w_rx0_01.csv
"""
import argparse, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rekam import buka_tanpa_reset, KEPALA  # noqa: E402


def baca(s, f, detik, buf):
    t = time.time()
    while time.time() - t < detik:
        d = s.read(512)
        if d:
            teks = d.decode("utf-8", "replace")
            f.write(teks); f.flush()
            buf.append(teks)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--id", required=True)
    ap.add_argument("--keluar", required=True)
    ap.add_argument("--s-kali", type=int, default=5)
    ap.add_argument("--tunggu-w", type=float, default=90.0)
    a = ap.parse_args()

    buf = []
    s = buka_tanpa_reset(a.port)
    with open(a.keluar, "w", encoding="utf-8", newline="") as f:
        f.write(KEPALA)
        f.write("# uji perintah lewat RX0 mulai %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
        baca(s, f, 5, buf)
        balas_s = 0
        for i in range(a.s_kali):
            sebelum = "".join(buf).count("# id_window=")
            f.write("\n# KIRIM S %d\n" % (i + 1)); f.flush()
            s.write(b"S\n")
            baca(s, f, 3, buf)
            ok = "".join(buf).count("# id_window=") > sebelum
            balas_s += ok
            print("S %d: %s" % (i + 1, "berbalas" if ok else "TIDAK berbalas"))
        f.write("\n# KIRIM W %s\n" % a.id); f.flush()
        s.write(("W %s\n" % a.id).encode())
        t = time.time()
        while time.time() - t < a.tunggu_w:
            baca(s, f, 1, buf)
            if re.search(r"^DATA,\d+,%s," % re.escape(a.id), "".join(buf), re.M):
                break
        baca(s, f, 3, buf)
        f.write("\n# KIRIM S akhir\n"); f.flush()
        s.write(b"S\n")
        baca(s, f, 3, buf)
        f.write("\n# uji selesai %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    s.close()

    isi = "".join(buf)
    diterima = ("# id_window=%s diterima" % a.id) in isi
    berlaku = ("# id_window=%s berlaku" % a.id) in isi
    data_baru = len(re.findall(r"^DATA,\d+,%s," % re.escape(a.id), isi, re.M))
    boot = isi.count("# GATEWAY_BOOT") + len(re.findall(r"^rst:", isi, re.M))
    ts = [int(m.group(1)) for m in re.finditer(r"^(?:DATA|ACK),(\d+),", isi, re.M)]
    turun = sum(1 for x, y in zip(ts, ts[1:]) if y < x)
    s_akhir = re.findall(r"^# id_window=(\S+) total=", isi, re.M)
    print("S berbalas: %d/%d" % (balas_s, a.s_kali))
    print("W diterima: %s, berlaku: %s, DATA ber-ID baru: %d" % (diterima, berlaku, data_baru))
    print("S terakhir melaporkan id_window=%s" % (s_akhir[-1] if s_akhir else "-"))
    print("GATEWAY_BOOT/rst: %d, ts_ms turun: %d dari %d" % (boot, turun, len(ts)))
    lulus = balas_s == a.s_kali and diterima and berlaku and data_baru > 0 and boot == 0 and turun == 0
    print("LULUS" if lulus else "PERIKSA")


if __name__ == "__main__":
    main()
