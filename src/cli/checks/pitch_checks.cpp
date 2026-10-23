#include "cli/checks/pitch_checks.h"

#include "core/dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>
#include <vector>

namespace anno::cli {

namespace {

constexpr std::size_t k_centroid_fft = 8192;
constexpr std::size_t k_centroid_min_samples = 512;
constexpr double k_centroid_floor_hz = 1000.0;

double seconds_of(SourceTime t) {
    return (t - SourceTime{}).value();
}

/// Frame range [first, last) of the note's grid that lies inside the note less the edge margin.
std::pair<std::int64_t, std::int64_t> inner_frames(const Note& note, Seconds margin) {
    const double lo = seconds_of(note.source_start) + margin.value();
    const double hi = seconds_of(note.source_end) - margin.value();
    const double start = seconds_of(note.grid.start);
    const double hop = note.grid.hop.value();
    const auto first =
        std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil((lo - start) / hop)));
    const auto last = std::min<std::int64_t>(
        note.grid.n_frames, static_cast<std::int64_t>(std::floor((hi - start) / hop)) + 1);
    return {first, last};
}

double median(std::vector<double> v) {
    std::ranges::sort(v);
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}  // namespace

const Note* matching_note(const AnalysisResult& analysis, const Note& note) {
    const Note* best = nullptr;
    double best_overlap = 0.0;
    for (const Note& candidate : analysis.notes) {
        const double overlap =
            std::min(seconds_of(candidate.source_end), seconds_of(note.source_end)) -
            std::max(seconds_of(candidate.source_start), seconds_of(note.source_start));
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best = &candidate;
        }
    }
    return best;
}

std::optional<Cents> median_pitch_shift(const AnalysisResult& original,
                                        const AnalysisResult& edited, const Note& note,
                                        const PitchCheckParams& params) {
    if (!original.pitch || !edited.pitch || original.pitch->grid != edited.pitch->grid) {
        return std::nullopt;
    }
    const PitchTrack& a = *original.pitch;
    const PitchTrack& b = *edited.pitch;
    const double start = seconds_of(a.grid.start);
    const double hop = a.grid.hop.value();
    const double lo = seconds_of(note.source_start) + params.edge_margin.value();
    const double hi = seconds_of(note.source_end) - params.edge_margin.value();
    std::vector<double> shifts;
    for (std::int64_t i = 0; i < a.grid.n_frames; ++i) {
        const double t = start + static_cast<double>(i) * hop;
        const auto k = static_cast<std::size_t>(i);
        if (t >= lo && t <= hi && is_voiced(a, k) && is_voiced(b, k)) {
            shifts.push_back(b.f0[k].value() - a.f0[k].value());
        }
    }
    if (std::cmp_less(shifts.size(), params.min_voiced_frames)) {
        return std::nullopt;
    }
    return Cents{median(std::move(shifts))};
}

std::optional<VibratoStats> vibrato_of(const Note& note, const PitchCheckParams& params) {
    const auto [first, last] = inner_frames(note, params.edge_margin);
    std::vector<double> curve;
    for (std::int64_t i = first; i < last && std::cmp_less(i, note.pitch_mod.size()); ++i) {
        const double v = note.pitch_mod[static_cast<std::size_t>(i)].value();
        if (std::isfinite(v)) {
            curve.push_back(v);
        }
    }
    if (std::cmp_less(curve.size(), params.min_voiced_frames)) {
        return std::nullopt;
    }
    double mean = 0.0;
    for (const double v : curve) {
        mean += v;
    }
    mean /= static_cast<double>(curve.size());
    int crossings = 0;
    for (std::size_t i = 1; i < curve.size(); ++i) {
        crossings += (curve[i - 1] - mean) * (curve[i] - mean) < 0.0 ? 1 : 0;
    }
    if (crossings == 0) {
        return std::nullopt;
    }
    const auto [lo, hi] = std::ranges::minmax(curve);
    const double seconds = static_cast<double>(curve.size()) * note.grid.hop.value();
    return VibratoStats{.rate = Hz{0.5 * crossings / seconds}, .extent = Cents{0.5 * (hi - lo)}};
}

std::optional<Hz> first_formant(const Note& note, Hz lo, Hz hi) {
    const EnvelopeTrack& env = note.envelope;
    if (env.log_mag_db.rows == 0 || env.freqs.size() < 3) {
        return std::nullopt;
    }
    const std::size_t cols = env.freqs.size();
    const auto row = static_cast<std::size_t>(env.log_mag_db.rows / 2);
    const auto at = [&](std::size_t b) {
        return static_cast<double>(env.log_mag_db.data[row * cols + b]);
    };
    std::size_t best = 0;
    for (std::size_t b = 1; b + 1 < cols; ++b) {
        const double f = env.freqs[b].value();
        if (f >= lo.value() && f <= hi.value() && (best == 0 || at(b) > at(best))) {
            best = b;
        }
    }
    if (best == 0 || env.freqs[best - 1].value() < lo.value() ||
        env.freqs[best + 1].value() > hi.value()) {
        return std::nullopt;
    }
    const double y0 = at(best - 1);
    const double y1 = at(best);
    const double y2 = at(best + 1);
    const double denom = y0 - 2.0 * y1 + y2;
    const double shift = denom < 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
    const double bin_hz = env.freqs[best + 1].value() - env.freqs[best].value();
    return Hz{env.freqs[best].value() + shift * bin_hz};
}

Hz spectral_centroid(std::span<const float> samples, Hz sample_rate) {
    if (samples.size() < k_centroid_min_samples) {
        return Hz{0.0};
    }
    const std::size_t n = std::min(k_centroid_fft, samples.size());
    const std::size_t begin = (samples.size() - n) / 2;
    auto fft = RealFft::create(n);
    if (!fft) {
        return Hz{0.0};
    }
    std::vector<double> frame(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                               static_cast<double>(n)));
        frame[i] = w * static_cast<double>(samples[begin + i]);
    }
    std::vector<std::complex<double>> bins(n / 2 + 1);
    fft->forward(frame, bins);
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t b = 0; b < bins.size(); ++b) {
        const double f = sample_rate.value() * static_cast<double>(b) / static_cast<double>(n);
        if (f >= k_centroid_floor_hz) {
            const double m = std::abs(bins[b]);
            weighted += f * m;
            total += m;
        }
    }
    return Hz{total > 0.0 ? weighted / total : 0.0};
}

}  // namespace anno::cli
