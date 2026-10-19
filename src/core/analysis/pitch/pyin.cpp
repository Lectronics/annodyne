#include "core/analysis/pitch/pyin.h"

#include "core/analysis/pitch/detail/pitch_hmm.h"
#include "core/analysis/pitch/detail/pyin_observe.h"
#include "core/analysis/pitch/detail/yin.h"
#include "core/dsp/mono.h"
#include "core/dsp/resample.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace anno {

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_alternate_exclusion_cents = 75.0;
constexpr double k_a4_hz = 440.0;

Result<void> validate_params(const PitchParams& p, const PYinParams& y, Hz source_rate) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    const bool bad = !positive(p.min_f0.value()) || !positive(p.max_f0.value()) ||
                     !positive(p.hop.value()) || p.min_f0 >= p.max_f0 || p.n_alternates < 0 ||
                     !positive(y.analysis_rate.value()) || !positive(y.window.value()) ||
                     !positive(y.bin_cents) || y.beta_mean <= 0.0 || y.beta_mean >= 1.0;
    if (bad) {
        return fail(ErrorCode::invalid_argument, "pitch: invalid parameters");
    }
    if (y.window.value() * p.min_f0.value() < 2.0 - 1e-9) {
        return fail(ErrorCode::invalid_argument,
                    "pitch: the analysis window must cover two periods of min_f0");
    }
    if (source_rate.value() < 2.0 * p.max_f0.value()) {
        return fail(ErrorCode::invalid_argument,
                    "pitch: the source sample rate cannot represent max_f0");
    }
    return {};
}

/// The mono signal at the analysis rate (the source rate if that is lower).
struct AnalysisSignal {
    std::vector<double> x;
    double rate = 0.0;
};

Result<AnalysisSignal> analysis_signal(const MonoAudio& mono, double analysis_rate) {
    AnalysisSignal out;
    out.rate = std::min(mono.sample_rate.value(), analysis_rate);
    if (out.rate < mono.sample_rate.value()) {
        auto low = resample(mono.samples, mono.sample_rate, Hz{out.rate}, {});
        if (!low) {
            return Unexpected{low.error()};
        }
        out.x.assign(low->begin(), low->end());
    } else {
        out.x.assign(mono.samples.begin(), mono.samples.end());
    }
    return out;
}

/// Observations of every frame.
Result<std::vector<FrameObservation>> observe_frames(const AnalysisSignal& sig,
                                                     const PitchParams& params,
                                                     const PYinParams& pyin, std::int64_t frames,
                                                     const Bins& bins) {
    const int window = static_cast<int>(std::lround(pyin.window.value() * sig.rate));
    ObserveSetup setup{
        .tau_min = std::max(2, static_cast<int>(std::floor(sig.rate / params.max_f0.value()))),
        .tau_max = static_cast<int>(std::ceil(sig.rate / params.min_f0.value())) + 1,
        .rate = sig.rate,
        .bins = bins,
        .weights = threshold_weights(pyin.beta_mean)};
    auto yin = YinFrame::create(window, setup.tau_max);
    if (!yin) {
        return Unexpected{yin.error()};
    }
    std::vector<FrameObservation> obs(static_cast<std::size_t>(frames));
    std::vector<double> segment(idx(yin->segment_length()));
    const auto n_x = static_cast<std::int64_t>(sig.x.size());
    for (std::int64_t i = 0; i < frames; ++i) {
        const double time_samples = static_cast<double>(i) * params.hop.value() * sig.rate;
        const std::int64_t start =
            static_cast<std::int64_t>(std::llround(time_samples)) - window / 2;
        for (int k = 0; k < yin->segment_length(); ++k) {
            const std::int64_t at = start + k;
            segment[idx(k)] = (at >= 0 && at < n_x) ? sig.x[static_cast<std::size_t>(at)] : 0.0;
        }
        const YinCurves curves = yin->analyse(segment);
        obs[static_cast<std::size_t>(i)] = observe(curves.cmnd, curves.nccf, setup);
    }
    return obs;
}

/// The best path and `n_alternates` more, each kept away from the earlier ones.
std::vector<std::vector<int>> decode_paths(const std::vector<FrameObservation>& obs,
                                           const Bins& bins, const HmmCosts& costs,
                                           double bin_cents, int n_alternates) {
    const int exclusion_bins =
        static_cast<int>(std::lround(k_alternate_exclusion_cents / bin_cents));
    std::vector<std::vector<int>> excluded(obs.size());
    std::vector<std::vector<int>> paths;
    for (int p = 0; p <= n_alternates; ++p) {
        paths.push_back(viterbi(obs, bins, costs, excluded, exclusion_bins));
        for (std::size_t t = 0; t < obs.size(); ++t) {
            if (paths.back()[t] >= 0) {
                excluded[t].push_back(paths.back()[t]);
            }
        }
    }
    return paths;
}

/// The candidate nearest to the chosen bin (within one bin), or the bin's centre when the frame has
/// none.
struct Pick {
    double cents = k_nan;
    double lag = k_nan;  // in analysis-rate samples
    double mass = 0.0;
};

Pick pick(const FrameObservation& obs, const Bins& bins, int bin, double rate) {
    const Candidate* best = nullptr;
    for (const Candidate& c : obs.candidates) {
        if (std::abs(c.bin - bin) <= 1 && (best == nullptr || c.mass > best->mass)) {
            best = &c;
        }
    }
    if (best != nullptr) {
        return {.cents = best->cents, .lag = best->tau, .mass = std::min(1.0, best->mass)};
    }
    const double cents = bins.centre(bin);
    return {.cents = cents, .lag = rate / to_hz(Cents{cents}, Hz{k_a4_hz}).value(), .mass = 0.0};
}

/// YIN measures at the middle of the two compared windows: frame time + lag / 2. Resamples each
/// voiced run of measurements back onto the frame grid by linear interpolation (hold at the run's
/// ends).
void compensate_lag_centre(std::vector<double>& cents, const std::vector<double>& lag_s,
                           double hop_s) {
    const std::size_t n = cents.size();
    const auto measured_time = [&](std::size_t q) {
        return static_cast<double>(q) * hop_s + 0.5 * lag_s[q];
    };
    std::size_t i = 0;
    while (i < n) {
        if (std::isnan(cents[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && !std::isnan(cents[j])) {
            ++j;
        }
        const std::vector<double> measured(cents.begin() + static_cast<std::ptrdiff_t>(i),
                                           cents.begin() + static_cast<std::ptrdiff_t>(j));
        std::size_t q = i;  // measurement at or before the frame time (times increase with q)
        for (std::size_t k = i; k < j; ++k) {
            const double t = static_cast<double>(k) * hop_s;
            while (q + 1 < j && measured_time(q + 1) <= t) {
                ++q;
            }
            double value = measured[q - i];
            if (q + 1 < j && t > measured_time(q)) {
                const double w = (t - measured_time(q)) / (measured_time(q + 1) - measured_time(q));
                value += w * (measured[q + 1 - i] - measured[q - i]);
            }
            cents[k] = value;
        }
        i = j;
    }
}

/// Fills f0, voicing, confidence and the alternates of `track` from the decoded paths. The YIN
/// lag-centre correction is applied to the voiced runs of the best path.
void fill_track(PitchTrack& track, const std::vector<FrameObservation>& obs,
                const std::vector<std::vector<int>>& paths, const Bins& bins, double rate,
                const PitchParams& params) {
    const std::size_t n = obs.size();
    std::vector<double> cents(n, k_nan);
    std::vector<double> lag_s(n, 0.0);
    track.confidence_prob.assign(n, 0.0);
    track.voicing_prob.assign(n, 0.0);
    for (std::size_t t = 0; t < n; ++t) {
        const double voiced_mass = 1.0 - obs[t].unvoiced;
        if (paths[0][t] >= 0) {
            const Pick p = pick(obs[t], bins, paths[0][t], rate);
            cents[t] = p.cents;
            lag_s[t] = p.lag / rate;
            track.confidence_prob[t] = p.mass;
            track.voicing_prob[t] =
                std::clamp(std::max(k_voiced_probability, voiced_mass), 0.0, 1.0);
        } else {
            track.confidence_prob[t] = std::clamp(obs[t].unvoiced, 0.0, 1.0);
            track.voicing_prob[t] =
                std::clamp(std::min(k_voiced_probability - 1e-9, voiced_mass), 0.0, 1.0);
        }
    }
    compensate_lag_centre(cents, lag_s, params.hop.value());
    track.f0.resize(n);
    std::ranges::transform(cents, track.f0.begin(), [](double c) { return Cents{c}; });
    for (int a = 0; a < params.n_alternates; ++a) {
        auto& alt = track.alternates[idx(a)];
        alt.assign(n, Cents{k_nan});
        for (std::size_t t = 0; t < n; ++t) {
            const int bin = paths[idx(a) + 1][t];
            if (bin >= 0) {
                alt[t] = Cents{pick(obs[t], bins, bin, rate).cents};
            }
        }
    }
}

}  // namespace

Result<PitchTrack> PYinEstimator::estimate(const AudioSource& src,
                                           const PitchParams& params) const {
    if (auto r = validate_params(params, params_, src.sample_rate()); !r) {
        return Unexpected{r.error()};
    }
    auto mono = read_mono(src);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    PitchTrack track;
    track.grid = FrameGrid{.hop = params.hop, .start = SourceTime{}, .n_frames = 0};
    track.alternates.assign(static_cast<std::size_t>(params.n_alternates), {});
    if (mono->samples.empty()) {
        return track;
    }
    const double duration = static_cast<double>(mono->samples.size()) / src.sample_rate().value();
    const auto frames = static_cast<std::int64_t>(std::floor(duration / params.hop.value())) + 1;
    track.grid.n_frames = frames;

    auto sig = analysis_signal(*mono, params_.analysis_rate.value());
    if (!sig) {
        return Unexpected{sig.error()};
    }
    const double min_cents = to_cents(params.min_f0, Hz{k_a4_hz}).value();
    const double max_cents = to_cents(params.max_f0, Hz{k_a4_hz}).value();
    const Bins bins{
        .min_cents = min_cents,
        .step_cents = params_.bin_cents,
        .count = static_cast<int>(std::ceil((max_cents - min_cents) / params_.bin_cents)) + 1};
    auto obs = observe_frames(*sig, params, params_, frames, bins);
    if (!obs) {
        return Unexpected{obs.error()};
    }
    const HmmCosts costs =
        make_hmm_costs({.bin_cents = params_.bin_cents,
                        .max_jump_cents = params_.max_jump_cents,
                        .jump_prob = params_.jump_prob,
                        .voiced_to_unvoiced_prob = params_.voiced_to_unvoiced_prob,
                        .unvoiced_to_voiced_prob = params_.unvoiced_to_voiced_prob},
                       bins.count);
    const auto paths = decode_paths(*obs, bins, costs, params_.bin_cents, params.n_alternates);

    fill_track(track, *obs, paths, bins, sig->rate, params);
    return track;
}

}  // namespace anno
