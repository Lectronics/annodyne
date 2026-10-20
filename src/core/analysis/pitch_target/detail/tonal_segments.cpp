#include "core/analysis/pitch_target/detail/tonal_segments.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

double slope_of(const Seg& s, double hop_s) {
    const double t = static_cast<double>(s.last - s.first) * hop_s;
    return t > 0.0 ? (s.end_cents - s.start_cents) / t : 0.0;
}

void split_range(const std::vector<double>& p, std::size_t a, std::size_t b, double threshold,
                 std::vector<std::size_t>& cuts) {
    if (b <= a + 1) {
        return;
    }
    const double slope = (p[b] - p[a]) / static_cast<double>(b - a);
    double worst = 0.0;
    std::size_t at = a;
    for (std::size_t k = a + 1; k < b; ++k) {
        const double line = p[a] + slope * static_cast<double>(k - a);
        const double d = std::abs(p[k] - line);
        if (d > worst) {
            worst = d;
            at = k;
        }
    }
    if (worst > threshold) {
        split_range(p, a, at, threshold, cuts);
        cuts.push_back(at);
        split_range(p, at, b, threshold, cuts);
    }
}

}  // namespace

std::vector<double> smooth_curve(const std::vector<double>& curve,
                                 const std::vector<double>& weight, double min_weight,
                                 std::size_t window_frames) {
    const std::size_t n = curve.size();
    const std::size_t half = std::max<std::size_t>(1, window_frames / 2);
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > half ? i - half : 0;
        const std::size_t hi = std::min(n - 1, i + half);
        double sum = 0.0;
        double count = 0.0;
        double all_sum = 0.0;
        for (std::size_t k = lo; k <= hi; ++k) {
            all_sum += curve[k];
            if (weight[k] >= min_weight) {
                sum += curve[k];
                count += 1.0;
            }
        }
        out[i] = count > 0.0 ? sum / count : all_sum / static_cast<double>(hi - lo + 1);
    }
    return out;
}

std::vector<Seg> split_curve(const std::vector<double>& curve, Seconds hop,
                             const SegmentingParams& params) {
    const std::size_t n = curve.size();
    std::vector<std::size_t> cuts;
    cuts.push_back(0);
    split_range(curve, 0, n - 1, params.turn_threshold.value(), cuts);
    cuts.push_back(n - 1);
    std::vector<Seg> segs;
    for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
        const std::size_t first = cuts[k];
        const std::size_t last = cuts[k + 1];
        segs.push_back({.first = first,
                        .last = last,
                        .start_cents = curve[first],
                        .end_cents = curve[last],
                        .moving = false});
    }
    if (n == 1) {
        segs = {{.first = 0,
                 .last = 0,
                 .start_cents = curve[0],
                 .end_cents = curve[0],
                 .moving = false}};
    }
    // Join neighbours that agree: similar slopes, or two flat stretches at about the same pitch.
    const double hop_s = hop.value();
    std::vector<Seg> merged;
    for (const Seg& s : segs) {
        if (!merged.empty()) {
            Seg& m = merged.back();
            const bool similar_slope =
                std::abs(slope_of(m, hop_s) - slope_of(s, hop_s)) < params.slope_merge_cents_per_s;
            const bool flat_pair =
                std::abs(m.end_cents - m.start_cents) < params.plateau_merge.value() &&
                std::abs(s.end_cents - s.start_cents) < params.plateau_merge.value() &&
                std::abs(s.end_cents - m.start_cents) < params.plateau_merge.value();
            // Never merge across a step that is large on its own.
            const bool bridge_ok =
                std::abs(s.end_cents - m.start_cents) <=
                params.turn_threshold.value() * 1.5 + std::abs(m.end_cents - m.start_cents);
            if ((similar_slope && bridge_ok && m.last - m.first > 0) || flat_pair) {
                m.last = s.last;
                m.end_cents = s.end_cents;
                continue;
            }
        }
        merged.push_back(s);
    }
    // Segments share boundary frames; make them disjoint (the next one starts after the last).
    for (std::size_t k = 1; k < merged.size(); ++k) {
        if (merged[k].first <= merged[k - 1].last && merged[k].first < merged[k].last) {
            merged[k].first = merged[k - 1].last + 1;
        }
    }
    return merged;
}

void label_segments(std::vector<Seg>& segs, Seconds hop, std::size_t note_frames,
                    const SegmentingParams& params) {
    const double hop_s = hop.value();
    const double note_s = static_cast<double>(note_frames) * hop_s;
    // The transient zones are fixed in time (research: ~120 ms) but a short note keeps a middle:
    // each zone is at most `zone_max_share` of the note.
    const double zone = std::min(params.transient_window.value(), params.zone_max_share * note_s);
    for (Seg& s : segs) {
        const double t = std::max(hop_s, static_cast<double>(s.last - s.first + 1) * hop_s);
        const double delta = std::abs(s.end_cents - s.start_cents);
        const double middle = (static_cast<double>(s.first + s.last) / 2.0) * hop_s;
        const bool in_zone = middle < zone || middle > note_s - zone;
        if (in_zone) {
            s.moving = delta >= params.turn_threshold.value();
        } else {
            const double t_eff = std::min(t, params.glissando_max_duration.value());
            const bool glide = delta >= params.min_glide_extent.value() &&
                               delta > params.glissando_g * 100.0 / t_eff;
            const bool step =
                delta >= params.step_min_extent.value() && t <= params.transient_window.value();
            s.moving = glide || step;
        }
    }
}

}  // namespace anno
