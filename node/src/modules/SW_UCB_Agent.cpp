#include "SW_UCB_Agent.h"
#include <math.h>

SW_UCB_Agent::SW_UCB_Agent(AgentState* s, uint16_t w)
    : _s(s), _w(w > W_SIZE ? W_SIZE : w) {
    for (uint8_t a = 0; a < N_ARMS; a++) { _skor[a] = 0.0f; _n[a] = 0; }
}

void SW_UCB_Agent::reset() {
    _s->t = 0;
#if MODE_SW_UCB
    for (uint16_t i = 0; i < W_SIZE; i++) {
        _s->t_entri[i] = 0;
        _s->reward[i] = 0.0f;
        _s->arm[i] = 0;
    }
    _s->head = 0;
    _s->isi = 0;
#else
    for (uint8_t a = 0; a < N_ARMS; a++) {
        _s->n_total[a] = 0;
        _s->mu_cumulative[a] = 0.0f;
    }
#endif
    for (uint8_t a = 0; a < N_ARMS; a++) { _skor[a] = 0.0f; _n[a] = 0; }
}

void SW_UCB_Agent::statistik(uint32_t n[N_ARMS], float jumlah[N_ARMS]) const {
    for (uint8_t a = 0; a < N_ARMS; a++) { n[a] = 0; jumlah[a] = 0.0f; }
#if MODE_SW_UCB
    // Entri masuk window bila nomor keputusannya termasuk W keputusan terakhir:
    // t - t_entri < W. Selisih tak bertanda aman karena t_entri <= t.
    for (uint16_t i = 0; i < _s->isi; i++) {
        if (_s->t - _s->t_entri[i] < _w && _s->arm[i] < N_ARMS) {
            n[_s->arm[i]]++;
            jumlah[_s->arm[i]] += _s->reward[i];
        }
    }
#else
    for (uint8_t a = 0; a < N_ARMS; a++) {
        n[a] = _s->n_total[a];
        jumlah[a] = _s->mu_cumulative[a] * (float)_s->n_total[a];
    }
#endif
}

float SW_UCB_Agent::skor_dari(uint32_t n, float jumlah) const {
    // Aturan N = 0: arm wajib dipilih.
    if (n == 0) return INFINITY;
    const float mu = jumlah / (float)n;

#if MODE_SW_UCB
    // ln(min(t, W)). Pada t < 2 logaritmanya nol atau negatif, sehingga bonus
    // eksplorasi ditiadakan.
    const uint32_t c = (_s->t < (uint32_t)_w) ? _s->t : (uint32_t)_w;
#else
    // UCB1: t tidak dibatasi W.
    const uint32_t c = _s->t;
#endif
    if (c < 2) return mu;
    // xi di DALAM akar, Persamaan (2.5).
    return mu + sqrtf(XI * logf((float)c) / (float)n);
}

float SW_UCB_Agent::score(uint8_t arm) const {
    uint32_t n[N_ARMS];
    float jumlah[N_ARMS];
    statistik(n, jumlah);
    return skor_dari(n[arm], jumlah[arm]);
}

uint8_t SW_UCB_Agent::select_arm() {
    uint32_t n[N_ARMS];
    float jumlah[N_ARMS];
    statistik(n, jumlah);

    // Lebih besar secara ketat: seri dimenangkan arm berindeks terkecil, juga
    // antara beberapa arm dengan N = 0.
    uint8_t best = 0;
    float best_score = -INFINITY;
    for (uint8_t a = 0; a < N_ARMS; a++) {
        _n[a] = n[a];
        _skor[a] = skor_dari(n[a], jumlah[a]);
        if (_skor[a] > best_score) {
            best_score = _skor[a];
            best = a;
        }
    }
    _s->t++;
    return best;
}

void SW_UCB_Agent::update(uint8_t arm, float reward) {
    if (arm >= N_ARMS) return;
#if MODE_SW_UCB
    _s->t_entri[_s->head] = _s->t;     // nomor keputusan yang sedang berjalan
    _s->reward[_s->head] = reward;
    _s->arm[_s->head] = arm;
    _s->head = (uint16_t)((_s->head + 1) % W_SIZE);
    if (_s->isi < W_SIZE) _s->isi++;
#else
    // UCB1, Persamaan (2.4) naskah: rata-rata diperbarui inkremental.
    _s->n_total[arm]++;
    _s->mu_cumulative[arm] += (reward - _s->mu_cumulative[arm]) / (float)_s->n_total[arm];
#endif
}

float SW_UCB_Agent::mu(uint8_t arm) const {
    uint32_t n[N_ARMS];
    float jumlah[N_ARMS];
    statistik(n, jumlah);
    return n[arm] ? jumlah[arm] / (float)n[arm] : 0.0f;
}

uint32_t SW_UCB_Agent::count(uint8_t arm) const {
    uint32_t n[N_ARMS];
    float jumlah[N_ARMS];
    statistik(n, jumlah);
    return n[arm];
}

uint8_t SW_UCB_Agent::arm_dominan() const {
    uint32_t n[N_ARMS];
    float jumlah[N_ARMS];
    statistik(n, jumlah);
    uint8_t best = 0;
    float best_mu = -INFINITY;
    for (uint8_t a = 0; a < N_ARMS; a++) {
        if (n[a] > 0 && jumlah[a] / (float)n[a] > best_mu) {
            best_mu = jumlah[a] / (float)n[a];
            best = a;
        }
    }
    return best;
}
