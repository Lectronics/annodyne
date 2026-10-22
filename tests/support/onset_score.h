#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Onset scoring as the research's run_all.py does it (docs/design/onset_test/run_all.py `score`):
// for each truth onset in order, the nearest unused detection with |d - t| <= tolerance (inclusive)
// is its match. Greedy, not an optimal assignment. Offset = detection - truth (positive = late).
// FP = detections - TP; FN = truths - TP. Times are seconds.

namespace anno::test {

struct OnsetScore {
    int tp = 0;
    int fp = 0;
    int fn = 0;
    std::vector<double> offsets_s;  // of the matched detections, in truth order

    [[nodiscard]] double median_offset_s() const {
        if (offsets_s.empty()) {
            return 0.0;
        }
        auto v = offsets_s;
        std::ranges::sort(v);
        const std::size_t mid = v.size() / 2;
        return v.size() % 2 == 1 ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
    }
    [[nodiscard]] double f_measure() const {
        const int denominator = 2 * tp + fp + fn;
        return denominator == 0 ? 1.0 : 2.0 * tp / denominator;
    }
};

[[nodiscard]] inline OnsetScore score_onsets(std::vector<double> detections,
                                             const std::vector<double>& truth, double tolerance_s) {
    std::ranges::sort(detections);
    std::vector<bool> used(detections.size(), false);
    OnsetScore score;
    for (const double t : truth) {
        int best = -1;
        for (std::size_t j = 0; j < detections.size(); ++j) {
            if (used[j] || std::abs(detections[j] - t) > tolerance_s) {
                continue;
            }
            if (best < 0 || std::abs(detections[j] - t) <
                                std::abs(detections[static_cast<std::size_t>(best)] - t)) {
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            used[static_cast<std::size_t>(best)] = true;
            score.offsets_s.push_back(detections[static_cast<std::size_t>(best)] - t);
            ++score.tp;
        }
    }
    score.fp = static_cast<int>(detections.size()) - score.tp;
    score.fn = static_cast<int>(truth.size()) - score.tp;
    return score;
}

/// Pooled over several signals: the sums of TP, FP, FN.
inline void accumulate(OnsetScore& total, const OnsetScore& one) {
    total.tp += one.tp;
    total.fp += one.fp;
    total.fn += one.fn;
    total.offsets_s.insert(total.offsets_s.end(), one.offsets_s.begin(), one.offsets_s.end());
}

}  // namespace anno::test
