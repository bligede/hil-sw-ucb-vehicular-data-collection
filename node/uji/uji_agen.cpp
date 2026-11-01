// uji_agen.cpp - uji unit agen SW-UCB dan UCB1 dengan riwayat pendek yang
// dihitung tangan (catatan Pembimbing I, 1 Nov 2026, butir A.1).
//
// Bukan firmware node. Tidak memakai radio maupun sensor: cukup papan ESP32 dan
// kabel USB. Kasus yang sama persis dijalankan di tools/uji_agen.py terhadap
// kelas SWUCB dan UCB1 pada tools/prevalidasi_swucb.py.
//
// Nilai harapan dihitung langsung dari Persamaan (2.5), xi = 0,5:
//   bonus(c, N) = sqrt(0,5 * ln c / N),  c = min(t, W) pada SW-UCB, c = t pada UCB1
//   bonus(4,1) = 0,832555   bonus(5,1) = 0,897061   bonus(5,2) = 0,634318
//   bonus(5,5) = 0,401178   bonus(50,1) = 1,398575  bonus(50,49) = 0,199796
//   bonus(50,50) = 0,197788
// Toleransi 0,0001, sama dengan toleransi replay berbasis indeks.
//
// Env: uji_agen (SW-UCB) dan uji_agen_ucb1 (MODE_UCB1).

#include <Arduino.h>
#include <math.h>
#include "../src/modules/SW_UCB_Agent.h"

static AgentState st;
static uint32_t n_lolos = 0, n_gagal = 0;
static const float TOL = 0.0001f;
static const float INF = INFINITY;

static void periksa(const char* kasus, SW_UCB_Agent& ag, uint8_t pilih,
                    const float harap[N_ARMS], uint8_t pilih_harap) {
    bool ok = (pilih == pilih_harap);
    for (uint8_t a = 0; a < N_ARMS; a++) {
        const float s = ag.skor_terakhir(a);
        if (isinf(harap[a])) ok = ok && isinf(s);
        else ok = ok && !isinf(s) && fabsf(s - harap[a]) <= TOL;
    }
    Serial.printf("%s %-34s t=%lu I=%.6f;%.6f;%.6f;%.6f pilih=%u (harap %u)\n",
                  ok ? "LOLOS" : "GAGAL", kasus, (unsigned long)ag.t(),
                  ag.skor_terakhir(0), ag.skor_terakhir(1),
                  ag.skor_terakhir(2), ag.skor_terakhir(3),
                  (unsigned)(pilih + 1), (unsigned)(pilih_harap + 1));
    if (ok) n_lolos++; else n_gagal++;
}

// Satu keputusan dengan arm dipaksa: select_arm() dipanggil seperti di firmware
// (menaikkan t), lalu reward dicatat untuk arm paksaan. arm < 0: tanpa reward
// (siklus diputus timeout_window).
static void paksa(SW_UCB_Agent& ag, int arm, float r) {
    ag.select_arm();
    if (arm >= 0) ag.update((uint8_t)arm, r);
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println();
    Serial.printf("# uji_agen, mode %s, xi=%.2f\n", MODE_SW_UCB ? "SW-UCB" : "UCB1", XI);

#if MODE_SW_UCB
    {   // K1 dan K2: inisialisasi dari state kosong, W = 5
        SW_UCB_Agent ag(&st, 5); ag.reset();
        const float r[N_ARMS] = {0.5f, 0.4f, 0.3f, 0.2f};
        uint8_t p = ag.select_arm();
        const float h1[N_ARMS] = {INF, INF, INF, INF};
        periksa("K1a semua N=0, pilih Arm 1", ag, p, h1, 0);
        ag.update(p, r[p]);
        p = ag.select_arm();
        const float h2[N_ARMS] = {0.5f, INF, INF, INF};
        periksa("K2 t=1 tanpa bonus, pilih Arm 2", ag, p, h2, 1);
        ag.update(p, r[p]);
        p = ag.select_arm(); ag.update(p, r[p]);   // keputusan 3: Arm 3
        p = ag.select_arm(); ag.update(p, r[p]);   // keputusan 4: Arm 4
        p = ag.select_arm();
        const float h3[N_ARMS] = {1.332555f, 1.232555f, 1.132555f, 1.032555f};
        periksa("K1b t=4, N=1 semua, pilih Arm 1", ag, p, h3, 0);
    }
    {   // K3: arm keluar dari window, W = 5
        SW_UCB_Agent ag(&st, 5); ag.reset();
        paksa(ag, 1, 0.9f);
        for (int i = 0; i < 5; i++) paksa(ag, 0, 0.5f);
        uint8_t p = ag.select_arm();
        const float h[N_ARMS] = {0.901178f, INF, INF, INF};
        periksa("K3 Arm 2 keluar window, N=0", ag, p, h, 1);
    }
    {   // K4: reward artefak terlupakan, W = 5
        SW_UCB_Agent ag(&st, 5); ag.reset();
        paksa(ag, 0, -1.0f); paksa(ag, 1, 0.4f); paksa(ag, 2, 0.3f);
        paksa(ag, 3, 0.2f);  paksa(ag, 0, 0.5f); paksa(ag, 1, 0.4f);
        paksa(ag, 2, 0.3f);
        uint8_t p = ag.select_arm();
        const float h[N_ARMS] = {1.397061f, 1.297061f, 0.934318f, 1.097061f};
        periksa("K4 artefak -1 keluar window", ag, p, h, 0);
    }
    {   // K5: siklus terpotong menaikkan t tanpa entri, W = 5
        SW_UCB_Agent ag(&st, 5); ag.reset();
        paksa(ag, 0, 0.5f); paksa(ag, 1, 0.4f); paksa(ag, 2, 0.3f);
        paksa(ag, 3, 0.2f); paksa(ag, -1, 0.0f); paksa(ag, 0, 0.5f);
        uint8_t p = ag.select_arm();
        const float h[N_ARMS] = {1.397061f, 1.297061f, 1.197061f, 1.097061f};
        periksa("K5 siklus tanpa reward geser t", ag, p, h, 0);
    }
    {   // K6: W = 50 produksi, pola rekaman ulang TAHAP43_5M_02
        SW_UCB_Agent ag(&st); ag.reset();
        paksa(ag, 1, -0.442f);
        for (int i = 0; i < 49; i++) paksa(ag, 0, 0.49f);
        uint8_t p = ag.select_arm();
        const float h1[N_ARMS] = {0.689796f, 0.956575f, INF, INF};
        periksa("K6a W=50, t=50, Arm 2 masih di window", ag, p, h1, 2);
        ag.update(0, 0.49f);                        // keputusan 51 dipaksa Arm 1
        p = ag.select_arm();
        const float h2[N_ARMS] = {0.687788f, INF, INF, INF};
        periksa("K6b W=50, t=51, Arm 2 keluar window", ag, p, h2, 1);
    }
#else
    {   // K7: UCB1, xi di dalam akar, c = t
        SW_UCB_Agent ag(&st); ag.reset();
        paksa(ag, 0, 0.5f); paksa(ag, 1, 0.4f); paksa(ag, 2, 0.3f);
        paksa(ag, 3, 0.2f); paksa(ag, 0, 0.3f);
        uint8_t p = ag.select_arm();
        const float h[N_ARMS] = {1.034318f, 1.297061f, 1.197061f, 1.097061f};
        periksa("K7 UCB1 t=5, pilih Arm 2", ag, p, h, 1);
    }
#endif
    Serial.printf("# HASIL lolos=%lu gagal=%lu\n", (unsigned long)n_lolos, (unsigned long)n_gagal);
}

void loop() { delay(1000); }
