"""Pra-validasi SW-UCB terhadap UCB1 — SOP Tahap 0 (Panduan HIL Subbab 0.1-0.3).

Dijalankan sebelum satu baris firmware ditulis. Memperbaiki kekeliruan formula
di Python jauh lebih murah daripada menelusurinya di ESP32.

Implementasi SW-UCB di bawah SENGAJA menyalin struktur modules/SW_UCB_Agent.cpp
(buffer melingkar, rata-rata dihitung ulang dari isi buffer, bonus eksplorasi
memakai ln(min(t, W))). Bila keduanya berbeda, hasil validasi ini tidak mewakili
perilaku firmware.

Kriteria lolos (Panduan HIL Subbab 0.3):
  1. Sesudah distribusi berganti, SW-UCB kembali ke arm optimal dalam <= 30 iterasi
  2. UCB1 membutuhkan iterasi lebih banyak daripada SW-UCB

Keluaran: ringkasan angka ke layar, grafik konvergensi ke berkas PNG, dan
angka re-konvergensi UCB1 yang wajib disimpan sebagai data rujukan Subbab 4.8.5
naskah, lalu dilaporkan ke Pembimbing I bersama nilai E maksimum Tahap 4.1.

Pemakaian:
    python prevalidasi_swucb.py
    python prevalidasi_swucb.py --ulangan 200 --keluaran hasil.png
"""

import argparse
import math
import random

# Nilai HARUS sama dengan node/src/config/params.h
N_ARMS = 4
W_SIZE = 50
XI = 0.5

N_ITER = 500
CHANGE_AT = 250
AMBANG_REKONV = 30      # N_RECONV pada params.h / Hipotesis Minor 1
DOMINAN = 0.8           # arm optimal terpilih pada >80% keputusan berturut-turut
JENDELA_DOMINAN = 20    # panjang jendela geser untuk menilai dominasi

# Fase 1 (iterasi 0-249): Arm 0 terbaik. Fase 2 (250-499): Arm 3 terbaik.
MEAN_FASE1 = [0.8, 0.5, 0.4, 0.2]
MEAN_FASE2 = [0.2, 0.4, 0.5, 0.8]
STD = 0.1


def reward(arm, t, rng):
    mean = MEAN_FASE1[arm] if t < CHANGE_AT else MEAN_FASE2[arm]
    return rng.gauss(mean, STD)


def arm_optimal(t):
    m = MEAN_FASE1 if t < CHANGE_AT else MEAN_FASE2
    return m.index(max(m))


class SWUCB:
    """Menyalin modules/SW_UCB_Agent.cpp."""

    nama = "SW-UCB"

    def __init__(self):
        self.buf = [[0.0] * W_SIZE for _ in range(N_ARMS)]
        self.head = [0] * N_ARMS
        self.n = [0] * N_ARMS
        self.mu = [0.0] * N_ARMS
        self.t = 0

    def score(self, a):
        if self.n[a] == 0:
            return math.inf
        eff_t = min(self.t, W_SIZE)
        if eff_t < 2:
            return self.mu[a]
        return self.mu[a] + XI * math.sqrt(math.log(eff_t) / self.n[a])

    def select(self):
        best, best_s = 0, -math.inf
        for a in range(N_ARMS):
            s = self.score(a)
            if s > best_s:
                best_s, best = s, a
        self.t += 1
        return best

    def update(self, a, r):
        self.buf[a][self.head[a]] = r
        self.head[a] = (self.head[a] + 1) % W_SIZE
        if self.n[a] < W_SIZE:
            self.n[a] += 1
        self.mu[a] = sum(self.buf[a][: self.n[a]]) / self.n[a]


class UCB1:
    """Pembanding: rata-rata kumulatif seluruh riwayat, tanpa sliding window."""

    nama = "UCB1"

    def __init__(self):
        self.sum = [0.0] * N_ARMS
        self.n = [0] * N_ARMS
        self.t = 0

    def score(self, a):
        if self.n[a] == 0:
            return math.inf
        mu = self.sum[a] / self.n[a]
        if self.t < 2:
            return mu
        return mu + XI * math.sqrt(math.log(self.t) / self.n[a])

    def select(self):
        best, best_s = 0, -math.inf
        for a in range(N_ARMS):
            s = self.score(a)
            if s > best_s:
                best_s, best = s, a
        self.t += 1
        return best

    def update(self, a, r):
        self.sum[a] += r
        self.n[a] += 1


def jalankan(agen, seed):
    rng = random.Random(seed)
    dipilih = []
    for t in range(N_ITER):
        a = agen.select()
        agen.update(a, reward(a, t, rng))
        dipilih.append(a)
    return dipilih


def waktu_rekonvergensi(dipilih):
    """Iterasi sesudah CHANGE_AT sampai arm optimal baru mendominasi.

    Mengembalikan None bila tidak pernah tercapai sampai akhir percobaan.
    """
    target = arm_optimal(CHANGE_AT)
    for i in range(CHANGE_AT, N_ITER - JENDELA_DOMINAN):
        jendela = dipilih[i : i + JENDELA_DOMINAN]
        if jendela.count(target) / JENDELA_DOMINAN > DOMINAN:
            return i - CHANGE_AT
    return None


def ringkas(nama, hasil):
    sah = [x for x in hasil if x is not None]
    gagal = len(hasil) - len(sah)
    if not sah:
        return None, f"  {nama:8s}: tidak pernah re-konvergen ({gagal} ulangan)"
    rata = sum(sah) / len(sah)
    teks = (
        f"  {nama:8s}: rata {rata:6.1f} iterasi   "
        f"min {min(sah):3d}   maks {max(sah):3d}   "
        f"gagal {gagal}/{len(hasil)}"
    )
    return rata, teks


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ulangan", type=int, default=100)
    p.add_argument("--keluaran", default="konvergensi_tahap0.png")
    p.add_argument("--seed", type=int, default=1)
    a = p.parse_args()

    print("Pra-validasi SW-UCB vs UCB1 — SOP Tahap 0")
    print(f"W={W_SIZE} xi={XI} iterasi={N_ITER} ganti_di={CHANGE_AT} "
          f"ulangan={a.ulangan}\n")

    sw, ucb = [], []
    jejak_sw, jejak_ucb = None, None
    for i in range(a.ulangan):
        ds = jalankan(SWUCB(), a.seed + i)
        du = jalankan(UCB1(), a.seed + i)
        sw.append(waktu_rekonvergensi(ds))
        ucb.append(waktu_rekonvergensi(du))
        if i == 0:
            jejak_sw, jejak_ucb = ds, du

    print("Waktu re-konvergensi sesudah distribusi berganti:")
    rata_sw, teks_sw = ringkas("SW-UCB", sw)
    rata_ucb, teks_ucb = ringkas("UCB1", ucb)
    print(teks_sw)
    print(teks_ucb)
    print()

    # ---- kriteria lolos ----
    k1 = rata_sw is not None and rata_sw <= AMBANG_REKONV
    k2 = rata_sw is not None and (rata_ucb is None or rata_sw < rata_ucb)

    print("Kriteria lolos (Panduan HIL 0.3):")
    print(f"  [{'v' if k1 else 'x'}] SW-UCB re-konvergen <= {AMBANG_REKONV} iterasi")
    print(f"  [{'v' if k2 else 'x'}] SW-UCB lebih cepat daripada UCB1")
    print()

    if k1 and k2:
        print("LOLOS. Simpan angka UCB1 di atas sebagai rujukan Subbab 4.8.5,")
        print("lalu laporkan ke Pembimbing I bersama E maksimum Tahap 4.1.")
    else:
        print("GAGAL. Perbaiki formula sebelum firmware C++ ditulis.")
        print("Periksa: bonus eksplorasi, pengelolaan buffer melingkar,")
        print("dan perhitungan ulang rata-rata sesudah entri lama tertimpa.")

    # ---- grafik ----
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig = plt.figure(figsize=(11, 8.5))
        gs = fig.add_gridspec(3, 1, height_ratios=[1, 1, 1.3], hspace=0.45)

        # Dua panel atas: SATU contoh lintasan, hanya sebagai ilustrasi
        # perilaku. Satu lintasan tidak membuktikan apa pun karena
        # variasinya besar; buktinya ada pada panel ketiga.
        ax0 = fig.add_subplot(gs[0])
        ax1 = fig.add_subplot(gs[1], sharex=ax0)
        for sumbu, jejak, judul in (
            (ax0, jejak_sw, "SW-UCB"),
            (ax1, jejak_ucb, "UCB1"),
        ):
            sumbu.plot(jejak, ".", markersize=2)
            sumbu.axvline(CHANGE_AT, color="red", linestyle="--",
                          label="distribusi berganti")
            sumbu.set_ylabel("arm terpilih")
            sumbu.set_yticks(range(N_ARMS))
            sumbu.set_yticklabels([f"Arm {i+1}" for i in range(N_ARMS)])
            sumbu.set_title(f"{judul} — satu contoh lintasan (bukan bukti)",
                            fontsize=10)
            sumbu.legend(loc="center right", fontsize=8)
        ax1.set_xlabel("iterasi")

        # Panel ketiga: sebaran waktu re-konvergensi seluruh ulangan.
        # Inilah yang menjadi bukti, karena memakai semua percobaan.
        ax2 = fig.add_subplot(gs[2])
        sw_ok = [v for v in sw if v is not None]
        ucb_ok = [v for v in ucb if v is not None]
        batas = max(max(sw_ok, default=0), max(ucb_ok, default=0))
        bins = range(0, batas + 6, 5)
        ax2.hist([sw_ok, ucb_ok], bins=bins,
                 label=[f"SW-UCB (rata {sum(sw_ok)/len(sw_ok):.1f})",
                        f"UCB1 (rata {sum(ucb_ok)/len(ucb_ok):.1f})"],
                 color=["#4472C4", "#ED7D31"])
        ax2.axvline(AMBANG_REKONV, color="red", linestyle="--",
                    label=f"batas H-Minor 1 ({AMBANG_REKONV} transmisi)")
        ax2.set_xlabel("waktu re-konvergensi (iterasi)")
        ax2.set_ylabel("jumlah ulangan")
        ax2.set_title(f"Sebaran waktu re-konvergensi, {len(sw_ok)} ulangan "
                      f"— inilah buktinya", fontsize=10)
        ax2.legend(fontsize=8)

        fig.savefig(a.keluaran, dpi=150, bbox_inches="tight")
        print(f"\nGrafik disimpan: {a.keluaran}")
    except ImportError:
        print("\nmatplotlib tidak terpasang; grafik dilewati.")
        print("Pasang dengan: pip install matplotlib")


if __name__ == "__main__":
    main()
