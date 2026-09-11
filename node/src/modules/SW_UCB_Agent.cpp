#include "SW_UCB_Agent.h"
#include <math.h>

SW_UCB_Agent::SW_UCB_Agent(ArmState* states, uint32_t* t_global)
    : _s(states), _t(t_global) {}

void SW_UCB_Agent::reset() {
    for (uint8_t a = 0; a < N_ARMS; a++) {
        for (uint16_t i = 0; i < W_SIZE; i++) _s[a].reward_buf[i] = 0.0f;
        _s[a].buf_head = 0;
        _s[a].n_in_window = 0;
        _s[a].mu = 0.0f;
    }
    *_t = 0;
}

float SW_UCB_Agent::score(uint8_t arm) const {
    const ArmState& s = _s[arm];

    // Arm yang belum pernah dicoba wajib dicoba lebih dahulu.
    if (s.n_in_window == 0) return INFINITY;

    // ln(min(t, W)). Pada t <= 1 logaritmanya nol atau negatif, sehingga bonus
    // eksplorasi ditiadakan dan keputusan murni mengikuti rata-rata reward.
    uint32_t eff_t = (*_t < (uint32_t)W_SIZE) ? *_t : (uint32_t)W_SIZE;
    if (eff_t < 2) return s.mu;

    float bonus = XI * sqrtf(logf((float)eff_t) / (float)s.n_in_window);
    return s.mu + bonus;
}

uint8_t SW_UCB_Agent::select_arm() {
    uint8_t best = 0;
    float best_score = -INFINITY;

    for (uint8_t a = 0; a < N_ARMS; a++) {
        float sc = score(a);
        if (sc > best_score) {
            best_score = sc;
            best = a;
        }
    }
    (*_t)++;
    return best;
}

void SW_UCB_Agent::update(uint8_t arm, float reward) {
    if (arm >= N_ARMS) return;
    ArmState& s = _s[arm];

    // Buffer melingkar: entri terlama tertimpa begitu window penuh.
    s.reward_buf[s.buf_head] = reward;
    s.buf_head = (uint16_t)((s.buf_head + 1) % W_SIZE);
    if (s.n_in_window < W_SIZE) s.n_in_window++;

    // Rata-rata dihitung ulang dari isi buffer, bukan diperbarui inkremental,
    // agar penimpaan entri lama tidak meninggalkan galat akumulatif.
    float sum = 0.0f;
    for (uint16_t i = 0; i < s.n_in_window; i++) sum += s.reward_buf[i];
    s.mu = sum / (float)s.n_in_window;
}

uint8_t SW_UCB_Agent::arm_dominan() const {
    uint8_t best = 0;
    float best_mu = -INFINITY;
    for (uint8_t a = 0; a < N_ARMS; a++) {
        if (_s[a].n_in_window > 0 && _s[a].mu > best_mu) {
            best_mu = _s[a].mu;
            best = a;
        }
    }
    return best;
}
