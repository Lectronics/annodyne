#include "core/analysis/pitch_target/detail/reliability.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_octave_cents = 1200.0;

double median_of(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::ranges::sort(v);
    return v[v.size() / 2];
}

/// Index into the evidence pitch track of note frame `i` (clamped), by time.
std::size_t track_index(const Note& note, const PitchTrack& p, std::size_t i) {
    const double t =
        (note.grid.start - p.grid.start).value() + static_cast<double>(i) * note.grid.hop.value();
    const long k = std::lround(t / p.grid.hop.value());
    const auto last = static_cast<long>(p.f0.size()) - 1;
    return static_cast<std::size_t>(std::clamp(k, 0L, last));
}

/// Frames soon after a consonant onset count less: a ramp from 0 at the onset to 1 at `zone`.
void apply_consonant_factor(const Note& note, const OnsetList& onsets, double zone,
                            std::vector<double>& weight) {
    for (std::size_t k = 0; k < onsets.times.size(); ++k) {
        if (onsets.kind[k] != OnsetKind::consonant) {
            continue;
        }
        const double onset = (onsets.times[k] - note.grid.start).value();
        for (std::size_t i = 0; i < weight.size(); ++i) {
            const double after = static_cast<double>(i) * note.grid.hop.value() - onset;
            if (after >= 0.0 && after < zone) {
                weight[i] *= after / zone;
            }
        }
    }
}

/// Zeroes the stretches (at least `min_frames`) whose f0 sits an octave from the note's median
/// (creak / period doubling); true if there was one.
bool zero_creak_stretches(const std::vector<double>& curve, std::size_t min_frames,
                          double tolerance, std::vector<double>& weight) {
    const double median = median_of(curve);
    bool found = false;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= curve.size(); ++i) {
        const bool at_octave =
            i < curve.size() && std::abs(std::abs(curve[i] - median) - k_octave_cents) <= tolerance;
        if (at_octave && (i == 0 || !(std::abs(std::abs(curve[i - 1] - median) - k_octave_cents) <=
                                      tolerance))) {
            begin = i;
        }
        if (!at_octave && i > 0 &&
            std::abs(std::abs(curve[i - 1] - median) - k_octave_cents) <= tolerance &&
            i - begin >= min_frames) {
            found = true;
            std::fill(weight.begin() + static_cast<std::ptrdiff_t>(begin),
                      weight.begin() + static_cast<std::ptrdiff_t>(i), 0.0);
        }
    }
    return found;
}

}  // namespace

Reliability frame_reliability(const Note& note, const NoteEvidence& evidence,
                              const std::vector<double>& curve_cents,
                              const ReliabilityParams& params) {
    const std::size_t n = curve_cents.size();
    const PitchTrack& pitch = *evidence.pitch;
    Reliability out;
    out.weight.assign(n, 0.0);
    std::vector<double> voicing(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        voicing[i] = pitch.f0.empty() ? 0.0 : pitch.voicing_prob[track_index(note, pitch, i)];
        out.weight[i] = voicing[i];
    }
    out.median_voicing = median_of(voicing);
    if (evidence.onsets != nullptr) {
        apply_consonant_factor(note, *evidence.onsets, params.consonant_zone.value(), out.weight);
    }
    const auto min_frames = static_cast<std::size_t>(
        std::max(1L, std::lround(params.creak_min_stretch.value() / note.grid.hop.value())));
    out.creak =
        zero_creak_stretches(curve_cents, min_frames, params.creak_tolerance.value(), out.weight);
    for (std::size_t i = 0; i < n && i < evidence.disagreement.size(); ++i) {
        out.weight[i] *= std::clamp(1.0 - evidence.disagreement[i], 0.0, 1.0);
    }
    return out;
}

}  // namespace anno
