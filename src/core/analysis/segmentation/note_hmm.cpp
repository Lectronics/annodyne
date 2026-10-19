#include "core/analysis/segmentation/note_hmm.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>

namespace anno {

namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();
constexpr double k_emission_floor = 1e-3;
constexpr double k_silence_to_attack = 0.01;
constexpr double k_silence_stay = 0.99;
constexpr double k_attack_stay = 0.3;
constexpr double k_attack_to_stable = 0.65;
constexpr double k_attack_to_silence = 0.05;
constexpr double k_min_stay = 0.005;  // staying is never made impossible
constexpr int k_near_reach =
    3;  // a new note within +-1 semitone (3 bins) of the old one is likelier
constexpr int k_near_bins = 2 * k_near_reach + 1;
constexpr double k_new_near_share = 0.8;
constexpr double k_new_far_share = 0.2;

std::size_t idx(int i) {
    return static_cast<std::size_t>(i);
}

double neg_log(double p) {
    return -std::log(std::max(p, 1e-300));
}

/// State numbering: attack(b) = b, stable(b) = bins + b, silence = 2 bins.
struct Layout {
    int bins = 0;
    [[nodiscard]] static std::size_t attack(int b) { return idx(b); }
    [[nodiscard]] std::size_t stable(int b) const { return idx(bins + b); }
    [[nodiscard]] std::size_t silence() const { return idx(2 * bins); }
    [[nodiscard]] std::size_t states() const { return idx(2 * bins) + 1; }
};

/// Emission costs of frame t into `cost` (all states).
void emission(const NoteHmmInput& in, const Layout& lay, double sigma, std::size_t t,
              std::vector<double>& cost) {
    const double v = in.voicing_prob[t];
    const double flat =
        neg_log(1.0 / (4.0 * sigma));  // pitch density where f0 carries no information
    const double note_voicing = neg_log(std::max(v, k_emission_floor));
    const double s = in.smoothed_cents[t];
    for (int b = 0; b < lay.bins; ++b) {
        double c = note_voicing + flat;
        if (!std::isnan(s)) {
            const double z = (s - (in.min_cents + in.bin_cents * b)) / sigma;
            c = note_voicing + 0.5 * z * z + std::log(sigma * std::sqrt(2.0 * std::numbers::pi));
        }
        cost[Layout::attack(b)] = c;
        cost[lay.stable(b)] = c;
    }
    cost[lay.silence()] = neg_log(std::max(1.0 - v, k_emission_floor)) + flat;
}

/// Fixed -log transition costs and the per-frame note-start costs.
struct Transitions {
    double attack_stay = neg_log(k_attack_stay);
    double attack_stable = neg_log(k_attack_to_stable);
    double attack_silence = neg_log(k_attack_to_silence);
    double stable_silence = 0.0;
    double silence_stay = neg_log(k_silence_stay);
};

/// One Viterbi step: the costs `cur` of every state at frame t from `prev`, and the back pointers.
struct Step {
    const NoteHmmInput* in = nullptr;
    const SegmentParams* params = nullptr;
    const Layout* lay = nullptr;
    const Transitions* tr = nullptr;

    void run(std::size_t t, const std::vector<double>& prev, const std::vector<double>& emit,
             std::vector<double>& cur, int* back) const {
        const double p_new = in->new_note_prob[t];
        const double c_stay =
            neg_log(std::max(1.0 - p_new - params->stable_to_silence, k_min_stay));
        const double c_from_silence = neg_log(
            std::min(0.5, k_silence_to_attack * p_new / params->new_note_prob) / in->bin_count);
        const auto best_stable = static_cast<int>(
            std::min_element(prev.begin() + static_cast<std::ptrdiff_t>(lay->stable(0)),
                             prev.begin() + static_cast<std::ptrdiff_t>(lay->silence())) -
            (prev.begin() + static_cast<std::ptrdiff_t>(lay->stable(0))));
        const double c_far =
            neg_log(p_new * k_new_far_share / std::max(1, in->bin_count - k_near_bins));
        const double c_near = neg_log(p_new * k_new_near_share / k_near_bins);
        for (int b = 0; b < in->bin_count; ++b) {
            into_attack(b, prev, c_far, c_near, c_from_silence, best_stable, emit, cur, back);
            double best_s = prev[Layout::attack(b)] + tr->attack_stable;
            int from_s = static_cast<int>(Layout::attack(b));
            if (prev[lay->stable(b)] + c_stay < best_s) {
                best_s = prev[lay->stable(b)] + c_stay;
                from_s = static_cast<int>(lay->stable(b));
            }
            cur[lay->stable(b)] = best_s + emit[lay->stable(b)];
            back[lay->stable(b)] = from_s;
        }
        into_silence(prev, emit, cur, back);
    }

private:
    void into_attack(int b, const std::vector<double>& prev, double c_far, double c_near,
                     double c_from_silence, int best_stable, const std::vector<double>& emit,
                     std::vector<double>& cur, int* back) const {
        double best = prev[lay->stable(best_stable)] + c_far;
        auto from = static_cast<int>(lay->stable(best_stable));
        const int hi = std::min(in->bin_count - 1, b + k_near_reach);
        for (int a = std::max(0, b - k_near_reach); a <= hi; ++a) {
            if (prev[lay->stable(a)] + c_near < best) {
                best = prev[lay->stable(a)] + c_near;
                from = static_cast<int>(lay->stable(a));
            }
        }
        if (prev[lay->silence()] + c_from_silence < best) {
            best = prev[lay->silence()] + c_from_silence;
            from = static_cast<int>(lay->silence());
        }
        if (prev[Layout::attack(b)] + tr->attack_stay < best) {
            best = prev[Layout::attack(b)] + tr->attack_stay;
            from = static_cast<int>(Layout::attack(b));
        }
        cur[Layout::attack(b)] = best + emit[Layout::attack(b)];
        back[Layout::attack(b)] = from;
    }

    void into_silence(const std::vector<double>& prev, const std::vector<double>& emit,
                      std::vector<double>& cur, int* back) const {
        double best = prev[lay->silence()] + tr->silence_stay;
        auto from = static_cast<int>(lay->silence());
        for (int b = 0; b < in->bin_count; ++b) {
            if (prev[lay->stable(b)] + tr->stable_silence < best) {
                best = prev[lay->stable(b)] + tr->stable_silence;
                from = static_cast<int>(lay->stable(b));
            }
            if (prev[Layout::attack(b)] + tr->attack_silence < best) {
                best = prev[Layout::attack(b)] + tr->attack_silence;
                from = static_cast<int>(Layout::attack(b));
            }
        }
        cur[lay->silence()] = best + emit[lay->silence()];
        back[lay->silence()] = from;
    }
};

/// Notes from the decoded state path: a note starts where the path enters an attack state from
/// anything but that same attack state, and lasts through its attack and stable states.
std::vector<NoteRun> runs_from_path(const std::vector<std::size_t>& path, const Layout& lay) {
    std::vector<NoteRun> runs;
    for (std::size_t t = 0; t < path.size(); ++t) {
        const std::size_t s = path[t];
        if (s == lay.silence()) {
            continue;
        }
        const bool is_attack = s < idx(lay.bins);
        const int bin = is_attack ? static_cast<int>(s) : static_cast<int>(s) - lay.bins;
        const bool same_run = t > 0 && path[t - 1] != lay.silence() && !runs.empty() &&
                              runs.back().last + 1 == t && runs.back().bin == bin &&
                              (!is_attack || path[t - 1] == s);
        if (same_run) {
            runs.back().last = t;
        } else {
            runs.push_back({.first = t, .last = t, .bin = bin});
        }
    }
    return runs;
}

}  // namespace

std::vector<NoteRun> decode_note_runs(const NoteHmmInput& in, const SegmentParams& p) {
    const std::size_t frames = in.smoothed_cents.size();
    assert(frames > 0 && in.voicing_prob.size() == frames && in.new_note_prob.size() == frames);
    const Layout lay{.bins = in.bin_count};
    const std::size_t states = lay.states();
    Transitions tr;
    tr.stable_silence = neg_log(p.stable_to_silence);
    const Step step{.in = &in, .params = &p, .lay = &lay, .tr = &tr};

    std::vector<int> back(frames * states, 0);
    std::vector<double> prev(states, k_inf);
    std::vector<double> cur(states, k_inf);
    std::vector<double> emit(states);
    emission(in, lay, p.pitch_sigma_cents, 0, emit);
    prev[lay.silence()] = emit[lay.silence()];
    const double c_start = neg_log(
        k_silence_to_attack * std::min(0.5, in.new_note_prob[0] / p.new_note_prob) / in.bin_count);
    for (int b = 0; b < in.bin_count; ++b) {
        prev[Layout::attack(b)] = emit[Layout::attack(b)] + c_start;
    }
    for (std::size_t t = 1; t < frames; ++t) {
        emission(in, lay, p.pitch_sigma_cents, t, emit);
        step.run(t, prev, emit, cur, back.data() + t * states);
        prev.swap(cur);
    }

    std::vector<std::size_t> path(frames);
    auto state = static_cast<std::size_t>(std::ranges::min_element(prev) - prev.begin());
    for (std::size_t t = frames; t-- > 0;) {
        path[t] = state;
        state = static_cast<std::size_t>(back[t * states + state]);
    }
    return runs_from_path(path, lay);
}

}  // namespace anno
