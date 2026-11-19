"""SNR dan RSSI median per arm per menit dari log gateway sweep (arahan Pembimbing I 19 Nov 2026 nomor 2).

Menit dihitung dari DATA pertama (ts_ms gateway). Penanda RENDAH: median SNR satu arm 5 dB atau lebih di bawah
median arm lain pada menit yang sama (kriteria Pembimbing I). Pembanding "arm lain" = median dari median arm lain.
Juga dicetak ringkasan seluruh rekaman per arm dan baris REGPA dari log node bila diberikan.

    python tools/olah_sweep_dekat.py data/tahap43/sweep_dekat_1234_19nov_gateway.csv [log_node.txt]
"""
import collections, statistics as st, sys


def main():
    gw = sys.argv[1]
    D = []
    for x in open(gw, encoding="utf-8", errors="replace"):
        p = x.strip().split(",")
        if p[0] == "DATA" and len(p) >= 12:
            try:
                D.append((int(p[1]), int(p[4]), int(p[7]), float(p[8]), int(p[3])))
            except ValueError:
                pass
    if not D:
        print("tidak ada DATA"); return
    t0 = D[0][0]
    m = collections.defaultdict(lambda: collections.defaultdict(list))
    for ts, arm, rssi, snr, seq in D:
        m[(ts - t0) // 60000][arm].append((rssi, snr))
    print("DATA %d, seq %d-%d, lama %.1f menit" % (len(D), D[0][4], D[-1][4], (D[-1][0] - t0) / 60000))
    print("menit | per arm: SNR median / RSSI median (n)   | RENDAH")
    n_rendah = collections.Counter()
    for mn in sorted(m):
        med = {a: st.median(s for r, s in v) for a, v in m[mn].items() if a in (1, 2, 3, 4)}
        sel = []
        rendah = []
        for a in sorted(med):
            v = m[mn][a]
            sel.append("a%d %5.1f/%4.0f(%2d)" % (a, med[a], st.median(r for r, s in v), len(v)))
            lain = [med[b] for b in med if b != a]
            if lain and st.median(lain) - med[a] >= 5.0:
                rendah.append("a%d" % a); n_rendah[a] += 1
        print("%2d    | %s | %s" % (mn, "  ".join(sel), " ".join(rendah) or "-"))
    print("Ringkasan seluruh rekaman:")
    for a in (1, 2, 3, 4):
        v = [(r, s) for ts, arm, r, s, q in D if arm == a]
        if v:
            print("  arm %d: n %d, SNR median %.1f (min %.1f), RSSI median %.0f" %
                  (a, len(v), st.median(s for r, s in v), min(s for r, s in v), st.median(r for r, s in v)))
    print("Menit dengan SNR rendah per arm:", dict(n_rendah) or "tidak ada")
    if len(sys.argv) > 2:
        reg = sorted({x.strip() for x in open(sys.argv[2], encoding="utf-8", errors="replace")
                      if x.startswith("REGPA,")})
        print("REGPA unik di log node:")
        for x in reg:
            print("  " + x)


if __name__ == "__main__":
    main()
