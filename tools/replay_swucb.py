#!/usr/bin/env python3
"""Uji replay SW-UCB: bandingkan arm yang dipilih node dengan arm yang dipilih agen acuan.

Masukan: log mentah gateway (format CSV firmware gateway) dari satu rangkaian window.
Keluaran: tabel per siklus (arm node, arm replay, margin skor) dan ringkasan kecocokan.

Cara kerja
  1. Log dipecah menjadi siklus ACK menurut baris ACK. Siklus dibedakan menjadi:
       reward   : siklus yang dievaluasi node (ada ACK dan sepuluh paket terkirim)
       terpotong: siklus yang diputus timeout_window (jeda lebih dari 30 s sesudahnya
                  dan kurang dari sepuluh paket). Node membuang siklus ini tanpa
                  reward (fsm.cpp, cabang timeout_window), tetapi select_arm() sudah
                  terpanggil untuk tiap paketnya.
       akhir    : kelompok paket terakhir tanpa ACK karena rekaman berhenti.
                  Tidak diketahui apakah node mengevaluasinya, jadi tidak dipakai.
  2. Energi satu paket tersimpan pada kolom e_prev_mJ paket berikutnya, maka energi
     paket j diambil dari baris dengan seq j+1. Reward = ALPHA*PDR - BETA*E_norm sama
     dengan fsm.cpp (PDR dari n pada ACK, E_norm = min(E_rata/E_maks, 1)).
  3. Siklus yang terjadi sebelum rekaman dimulai (seq awal lebih dari nol) direkonstruksi
     dari asumsi start dingin: banner node mencetak "RTC RAM kosong atau rusak", sehingga
     agen berangkat dari nol dan nomor seq berangkat dari nol. Reward siklus itu tidak
     tercatat, jadi diisi PDR dan energi tipikal arm yang bersangkutan dan analisis
     kepekaan menguji apakah kecocokan bergantung pada isian itu.
  4. Replay teacher-forced: untuk tiap siklus agen diberi riwayat nyata (arm dan reward
     yang benar-benar dialami node), lalu ditanya arm apa yang ia pilih.

Dua cara menggerakkan agen yang sama (kelas SWUCB di prevalidasi_swucb.py, salinan
SW_UCB_Agent.cpp):
  per_paket  : select() dipanggil per PAKET (t naik 10 per siklus), firmware 1.0
  per_siklus : select() dipanggil per SIKLUS (t naik 1 per siklus), satuan pengamatan
               agen menurut naskah Bab III, firmware 1.1

Log yang direkam dari nol (paket pertama bernomor 0 sampai 9) tidak memerlukan asumsi
start dingin, sehingga kecocokannya berlaku tanpa syarat. Log yang mulai di tengah
(seperti Tahap 4.3: paket pertama bernomor 59) hanya dapat diuji bersyarat.
"""

import argparse
import csv
import math
import os
import statistics
import sys
from collections import Counter, OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prevalidasi_swucb as pv  # noqa: E402

ACK_N = 10
JEDA_COOLDOWN_MS = 30000


# --------------------------------------------------------------------------
# Pembacaan log dan pembentukan siklus
# --------------------------------------------------------------------------
def baca_log(berkas):
    with open(berkas, newline="", encoding="utf-8", errors="replace") as f:
        baris = f.read().splitlines()
    awal = next(i for i, l in enumerate(baris) if l.startswith("jenis,"))
    ev = []
    for b in csv.DictReader(baris[awal:]):
        try:
            if (b["jenis"] or "").startswith("# NODE_RESET"):
                ev.append(("R",))
            elif b["jenis"] == "DATA":
                ev.append(("D", int(b["ts_ms"]), int(b["seq"]), int(b["arm"]),
                           float(b["e_prev_mJ"])))
            elif b["jenis"] == "ACK":
                ev.append(("A", int(b["ts_ms"]), int(b["seq"])))
        except (ValueError, KeyError, TypeError):
            continue
    return ev


def bagi_segmen(ev):
    """Pisahkan log pada baris "# NODE_RESET": tiap segmen berangkat dari agen kosong."""
    seg, cur = [], []
    for e in ev:
        if e[0] == "R":
            if cur:
                seg.append(cur)
            cur = []
        else:
            cur.append(e)
    if cur:
        seg.append(cur)
    return seg


def bentuk_kelompok(ev):
    """Kelompokkan paket menurut baris ACK: daftar (baris_paket, n_ack atau None)."""
    kel, cur = [], []
    for e in ev:
        if e[0] == "D":
            cur.append(e)
        else:
            if cur:
                kel.append((cur, e[2]))
            cur = []
    if cur:
        kel.append((cur, None))
    return kel


def bentuk_siklus(ev, e_maks):
    """Pecah log menjadi siklus node menurut nomor urut paket, bukan menurut baris ACK.

    Satu siklus node adalah sepuluh nomor urut berturut-turut (kurang dari sepuluh bila
    timeout_window memutusnya). ACK yang dipakai node adalah ACK pertama yang tercetak
    sesudah paket terakhir siklus selesai dipancarkan: ACK yang dikirim gateway ketika
    node masih memancar tidak terdengar. Gateway menutup jendela setelah 800 ms tanpa paket,
    lebih singkat daripada jeda dua paket (sekitar 1,2 detik), sehingga satu paket hilang di
    tengah siklus membuat gateway mengirim ACK dini yang tidak didengar node."""
    pk = [e for e in ev if e[0] == "D"]
    ak = [e for e in ev if e[0] == "A"]
    per_seq = {}
    for p in pk:
        per_seq.setdefault(p[2], p)
    e_dari = {p[2] - 1: p[4] for p in pk}
    seqs = sorted(per_seq)
    ts = [per_seq[x][1] for x in seqs]
    jeda_normal = [b - a for a, b in zip(ts, ts[1:]) if 0 < b - a < 2000]
    interval = statistics.median(jeda_normal) if jeda_normal else 600
    patah = {x for x, y in zip(seqs, seqs[1:]) if per_seq[y][1] - per_seq[x][1] > JEDA_COOLDOWN_MS}
    seq_terakhir = seqs[-1]
    n_awal = seqs[0] // ACK_N
    mulai = n_awal * ACK_N
    urut = n_awal
    siklus = []
    while mulai <= seq_terakhir:
        akhir = mulai + ACK_N - 1
        jenis = None
        pot = [x for x in patah if mulai <= x < akhir]
        if pot:
            akhir, jenis = min(pot), "terpotong"
        rows = [per_seq[x] for x in range(mulai, akhir + 1) if x in per_seq]
        if not rows:
            mulai, urut = akhir + 1, urut + 1
            continue
        n_ack = None
        if jenis is None:
            lanjut = [x for x in seqs if x > akhir]
            ts_lanjut = per_seq[lanjut[0]][1] if lanjut else None
            if seq_terakhir < akhir:
                akhir_log = seq_terakhir
            else:
                akhir_log = akhir
            est_selesai = rows[-1][1] + (akhir_log - rows[-1][2]) * interval   # ts paket = akhir penerimaan
            cand = [a for a in ak if a[1] > est_selesai and (ts_lanjut is None or a[1] < ts_lanjut)]
            if cand:
                jenis, n_ack = "reward", cand[0][2]
            elif ts_lanjut is None:
                jenis, akhir = "akhir", akhir_log
            else:
                jenis = "reward"                       # ACK tidak terdengar: timeout, reward -1
        e_ada = [e_dari[x] for x in range(mulai, akhir + 1) if x in e_dari]
        e_rata = sum(e_ada) / len(e_ada) if e_ada else None
        pdr = (n_ack / ACK_N) if n_ack is not None else None
        reward = None
        if jenis == "reward":
            reward = -1.0 if n_ack is None else (pdr - min(e_rata / e_maks, 1.0) if e_rata is not None else None)
        siklus.append(OrderedDict(
            urut=urut, seq_awal=mulai, seq_akhir=akhir, jenis=jenis,
            arm=rows[-1][3], arm_per_seq={r[2]: r[3] for r in rows}, n_terima=len(rows),
            n_ack=n_ack, pdr=pdr, e_rata=e_rata, n_energi=len(e_ada), reward=reward,
            n_paket_node=akhir - mulai + 1))
        mulai, urut = akhir + 1, urut + 1
    return siklus, n_awal


# --------------------------------------------------------------------------
# Replay
# --------------------------------------------------------------------------
def skor_semua(ag):
    return [ag.score(a) for a in range(pv.N_ARMS)]


def margin(skor, pilih):
    lain = [s for i, s in enumerate(skor) if i != pilih]
    if math.isinf(skor[pilih]):
        return math.inf
    return skor[pilih] - max(lain)


def putuskan(ag, n_paket, cara):
    """Satu siklus keputusan. Mengembalikan (daftar arm per paket, margin skor terkecil)."""
    if cara == "per_paket":
        keputusan, mg = [], math.inf
        for _ in range(n_paket):
            sk = skor_semua(ag)
            p = ag.select()
            keputusan.append(p)
            mg = min(mg, margin(sk, p))
        return keputusan, mg
    sk = skor_semua(ag)
    p = ag.select()
    return [p], margin(sk, p)


def replay(siklus, n_awal, e_tipikal, pdr_awal=1.0, skala_e=1.0, cara="per_paket",
           e_maks=93.7):
    """Replay teacher-forced. Mengembalikan n_awal entri rekonstruksi start dingin,
    lalu satu entri per siklus teramati."""
    ag = pv.SWUCB()
    hasil = []
    for c in range(n_awal):
        keputusan, mg = putuskan(ag, ACK_N, cara)
        arm = keputusan[-1]
        r = pdr_awal - min(e_tipikal[arm + 1] * skala_e / e_maks, 1.0)
        ag.update(arm, r)
        hasil.append(dict(urut=c, jenis="awal_dingin", arm_replay=arm + 1, arm_node=None,
                          cocok=None, margin=mg, reward=r))
    for s in siklus:
        n_paket = s["n_paket_node"]
        keputusan, mg = putuskan(ag, n_paket, cara)
        if cara == "per_paket":
            cocok = all(keputusan[j] + 1 == s["arm_per_seq"][sq]
                        for j, sq in enumerate(range(s["seq_awal"], s["seq_awal"] + n_paket))
                        if sq in s["arm_per_seq"])
        else:
            cocok = (keputusan[0] + 1 == s["arm"])
        if s["jenis"] == "reward":
            ag.update(s["arm"] - 1, s["reward"])
        hasil.append(dict(urut=s["urut"], jenis=s["jenis"], arm_replay=keputusan[-1] + 1,
                          arm_node=s["arm"], cocok=cocok, margin=mg, reward=s["reward"]))
    return hasil


def jumlah_cocok(hasil, hanya=("reward", "terpotong")):
    h = [x for x in hasil if x["jenis"] in hanya]
    return sum(1 for x in h if x["cocok"]), len(h)


# --------------------------------------------------------------------------
# Simulasi bebas (tanpa teacher forcing) untuk menguraikan penyebab selisih
# --------------------------------------------------------------------------
def simulasi_bebas(reward_arm, n_siklus, cara, reward_awal=None, lewati=0):
    """Agen berjalan sendiri dengan reward tetap per arm. reward_awal: {siklus: (indeks_arm,
    reward)} menimpa reward pada siklus tertentu bila arm yang terpilih sama. Mengembalikan
    jumlah pilihan per arm setelah `lewati` siklus awal."""
    ag = pv.SWUCB()
    hit = Counter()
    for c in range(n_siklus):
        if cara == "per_paket":
            for _ in range(ACK_N):
                p = ag.select()
        else:
            p = ag.select()
        r = reward_arm[p]
        if reward_awal and c in reward_awal and reward_awal[c][0] == p:
            r = reward_awal[c][1]
        ag.update(p, r)
        if c >= lewati:
            hit[p + 1] += 1
    return [hit[a] for a in (1, 2, 3, 4)]


def tulis_csv(berkas, siklus, h_fw, h_nk):
    peta = {x["urut"]: x for x in siklus}
    with open(berkas, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["siklus_node", "seq_awal", "jenis", "arm_node", "n_ack", "pdr", "e_rata_mJ",
                    "reward", "arm_replay_per_paket", "cocok_per_paket", "margin_per_paket",
                    "arm_replay_per_siklus", "cocok_per_siklus"])
        for hf, hn in zip(h_fw, h_nk):
            s = peta.get(hf["urut"])
            w.writerow([
                hf["urut"], s["seq_awal"] if s else hf["urut"] * ACK_N, hf["jenis"],
                hf["arm_node"] or "",
                s["n_ack"] if s and s["n_ack"] is not None else "",
                "" if not s or s["pdr"] is None else round(s["pdr"], 2),
                "" if not s or s["e_rata"] is None else round(s["e_rata"], 2),
                "" if hf["reward"] is None else round(hf["reward"], 4),
                hf["arm_replay"], "" if hf["cocok"] is None else ("Ya" if hf["cocok"] else "Tidak"),
                "" if math.isinf(hf["margin"]) else round(hf["margin"], 4),
                hn["arm_replay"], "" if hn["cocok"] is None else ("Ya" if hn["cocok"] else "Tidak")])


def analisis(ev, a, label, keluaran):
    siklus, n_awal = bentuk_siklus(ev, a.e_maks)
    print("=" * 78)
    print("SEGMEN:", label)

    e_tip = {}
    for arm in (1, 2, 3, 4):
        v = [s["e_rata"] for s in siklus if s["jenis"] == "reward" and s["arm"] == arm
             and s["e_rata"] is not None and s["n_energi"] >= 8]
        e_tip[arm] = statistics.median(v) if v else None
    print("Siklus terbaca:", dict(Counter(s["jenis"] for s in siklus)),
          "| siklus sebelum rekaman (start dingin):", n_awal)
    if n_awal:
        print("PERINGATAN: rekaman dimulai di tengah (seq pertama = %d). Riwayat %d siklus awal "
              "direkonstruksi dari asumsi start dingin, jadi kecocokan hanya berlaku bersyarat."
              % (min(r[2] for r in ev if r[0] == "D"), n_awal))
    else:
        print("Rekaman dimulai dari nomor urut 0 sampai 9: kecocokan berlaku tanpa asumsi riwayat.")
    print("Energi tipikal per arm (mJ):", {k: (round(v, 2) if v else None) for k, v in e_tip.items()})

    hasil = {}
    for cara in ("per_paket", "per_siklus"):
        h = replay(siklus, n_awal, e_tip, cara=cara, e_maks=a.e_maks)
        hasil[cara] = h
        c, n = jumlah_cocok(h)
        c_r, n_r = jumlah_cocok(h, ("reward",))
        mg = min(x["margin"] for x in h if x["jenis"] in ("reward", "terpotong"))
        print("\n[%s] cocok %d dari %d keputusan teramati (%.1f%%); siklus ber-reward %d dari %d"
              % (cara, c, n, 100.0 * c / n, c_r, n_r))
        if n_awal:
            print("   arm rekonstruksi siklus 0-%d: %s" % (n_awal - 1, "".join(str(x["arm_replay"]) for x in h[:n_awal])))
        print("   margin skor terkecil: %.4f" % mg)
        for x in [x for x in h if x["jenis"] in ("reward", "terpotong") and not x["cocok"]][:10]:
            print("   TIDAK COCOK siklus %d: node Arm %s, replay Arm %s, margin %.4f"
                  % (x["urut"], x["arm_node"], x["arm_replay"], x["margin"]))

    if n_awal:
        print("\nKepekaan terhadap isian reward siklus sebelum rekaman (cara firmware):")
        print("  PDR awal | skala energi | cocok | siklus pertama yang tidak cocok")
        total = utuh = 0
        tak_kokoh = set()
        for pdr in (0.5, 0.7, 0.9, 1.0):
            for sk in (0.8, 1.0, 1.2):
                h = replay(siklus, n_awal, e_tip, pdr_awal=pdr, skala_e=sk, cara="per_paket",
                           e_maks=a.e_maks)
                c, n = jumlah_cocok(h)
                tak = [x["urut"] for x in h if x["jenis"] in ("reward", "terpotong") and not x["cocok"]]
                tak_kokoh.update(tak)
                total += 1
                utuh += (c == n)
                print("   %.1f     |   %.1f        | %d/%d | %s" % (pdr, sk, c, n, tak[0] if tak else "-"))
        print("  Kombinasi dengan kecocokan penuh: %d dari %d" % (utuh, total))
        pasti = [x["urut"] for x in hasil["per_paket"] if x["jenis"] in ("reward", "terpotong")
                 and x["urut"] not in tak_kokoh]
        print("  Keputusan yang cocok pada semua %d kombinasi: %d dari %d (siklus pertama yang"
              " goyah: %s)" % (total, len(pasti), len(pasti) + len(tak_kokoh), min(tak_kokoh) if tak_kokoh else "-"))

    # Resolusi e_prev_mJ pada muatan adalah 0,1 mJ, jadi tiap energi paket membawa
    # pembulatan sampai 0,05 mJ. Uji: tambahkan pembulatan acak seragam pada tiap paket
    # dan hitung berapa percobaan yang tetap cocok penuh.
    import random
    n_coba = 500
    print("\nKepekaan terhadap pembulatan 0,1 mJ pada energi paket (%d percobaan):" % n_coba)
    for cara in ("per_paket", "per_siklus"):
        rng = random.Random(1)
        penuh = 0
        beda = Counter()
        for _ in range(n_coba):
            kembar = []
            for s in siklus:
                s2 = OrderedDict(s)
                if s["reward"] is not None and s["n_energi"] and s["n_ack"] is not None:
                    dE = sum(rng.uniform(-0.05, 0.05) for _ in range(s["n_energi"])) / s["n_energi"]
                    s2["reward"] = s["reward"] - dE / a.e_maks
                kembar.append(s2)
            h = replay(kembar, n_awal, e_tip, cara=cara, e_maks=a.e_maks)
            c, n = jumlah_cocok(h)
            penuh += (c == n)
            for x in h:
                if x["jenis"] in ("reward", "terpotong") and not x["cocok"]:
                    beda[x["urut"]] += 1
        sering = ", ".join("siklus %d (tidak cocok pada %d%% percobaan)" % (k, round(100.0 * v / n_coba))
                           for k, v in sorted(beda.items(), key=lambda kv: -kv[1])[:5])
        print("   [%s] cocok penuh pada %d dari %d percobaan; siklus yang paling sering tidak cocok: %s"
              % (cara, penuh, n_coba, sering or "-"))

    if n_awal:        # simulasi penguraian penyebab hanya untuk log yang mulai di tengah
        rr = {1: [], 2: [], 3: [], 4: []}
        for s in siklus:
            if s["jenis"] == "reward" and s["n_ack"] == ACK_N and s["reward"] is not None:
                rr[s["arm"]].append(s["reward"])
        r_arm = [statistics.mean(rr[k]) if rr[k] else None for k in (1, 2, 3, 4)]
        print("\nReward rata-rata per arm dari siklus utuh:",
              [round(x, 4) if x is not None else None for x in r_arm])
        r_arm[2] = 1.0 - 67.85 / a.e_maks            # Arm 3: energi hasil sweep, seperti simulasi Pak Indra
        print("   Arm 3 diganti memakai energi sweep 67,85 mJ -> %.4f" % r_arm[2])
        print("\nSimulasi bebas, jumlah pilihan Arm 1/2/3/4 pada 49 siklus sesudah siklus pertama rekaman:")
        artefak = {n_awal: (1, None), n_awal + 1: (2, None)}
        artefak = {}
        for s in siklus[:2]:
            if s["reward"] is not None:
                artefak[s["urut"]] = (s["arm"] - 1, s["reward"])
        for judul, cara, ov in (
                ("per siklus (firmware 1.1), reward tetap", "per_siklus", None),
                ("per paket (firmware 1.0), reward tetap", "per_paket", None),
                ("per paket, reward tetap + dua siklus awal bereward artefak", "per_paket", artefak)):
            tot = simulasi_bebas(r_arm, n_awal + 1 + 49, cara, reward_awal=ov, lewati=n_awal + 1)
            print("   %-62s %s" % (judul, "/".join(map(str, tot))))
        print("   (artefak:", {k: (v[0] + 1, round(v[1], 4)) for k, v in artefak.items()}, ")")

    if keluaran:
        tulis_csv(keluaran, siklus, hasil["per_paket"], hasil["per_siklus"])
        print("\nTabel per siklus ditulis ke", keluaran)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("berkas", help="log mentah gateway (CSV)")
    ap.add_argument("--e-maks", type=float, default=93.7)
    ap.add_argument("--keluaran", help="tulis tabel per siklus (CSV); akhiran _segN bila ada NODE_RESET")
    a = ap.parse_args()

    segmen = bagi_segmen(baca_log(a.berkas))
    print("Segmen (dipisah oleh baris # NODE_RESET):", len(segmen))
    for i, ev in enumerate(segmen, 1):
        keluaran = a.keluaran
        if keluaran and len(segmen) > 1:
            dasar, ekst = os.path.splitext(keluaran)
            keluaran = "%s_seg%d%s" % (dasar, i, ekst)
        pertama = [e[2] for e in ev if e[0] == "D"]
        if len(segmen) > 1 and pertama and pertama[0] >= ACK_N:
            print("=" * 78)
            print("SEGMEN %d dari %d dilewati: dimulai di tengah (seq pertama = %d), riwayat awalnya tidak "
                  "terekam." % (i, len(segmen), pertama[0]))
            continue
        analisis(ev, a, "%d dari %d" % (i, len(segmen)), keluaran)


if __name__ == "__main__":
    main()
