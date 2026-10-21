// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/analysis/pitch/c4dm_pyin.h"

#include "core/dsp/mono.h"
#include "core/dsp/resample.h"

#include <algorithm>
#include <c4dm_pyin/MonoPitchHMM.h>
#include <c4dm_pyin/Yin.h>
#include <c4dm_pyin/YinUtil.h>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace anno {

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_a4_hz = 440.0;
constexpr double k_unvoiced_margin =
    1e-9;  // keeps an unvoiced frame's voicing below k_voiced_probability
constexpr double k_a4_cents = 6900.0;
constexpr double k_reference_rate_hz = 44100.0;  // the plugin's usual rate ...
constexpr double k_reference_block = 2048.0;     // ... and its block size
constexpr double k_min_analysis_rate_hz = 8000.0;
constexpr int k_max_threshold_distribution = 7;
constexpr double k_cents_per_octave = 1200.0;
constexpr double k_midi_a4 = 69.0;
constexpr double k_subharmonic_reach =
    0.97;                             // the deeper dip is searched within +-3 % of 2x / 3x the lag
constexpr int k_max_guard_steps = 4;  // lag / 3 or / 2 repeated: reaches 1/24 of a lag at most
constexpr double k_demoted_mass = 1e-3;  // what remains of a candidate whose mass moved

Result<void> validate_params(const PitchParams& p, const C4dmPYinParams& y, Hz source_rate) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    const bool bad =
        !positive(p.min_f0.value()) || !positive(p.max_f0.value()) || !positive(p.hop.value()) ||
        p.min_f0 >= p.max_f0 || p.n_alternates < 0 || y.threshold_distribution < 0 ||
        y.threshold_distribution > k_max_threshold_distribution || !std::isfinite(y.low_amp_rms) ||
        y.low_amp_rms < 0.0 || y.fixed_lag_frames < 0 || y.long_source_lag_frames < 1 ||
        !positive(y.full_viterbi_max_duration.value()) || !std::isfinite(y.down_margin3) ||
        !std::isfinite(y.down_margin2) || !std::isfinite(y.max_analysis_rate.value()) ||
        y.max_analysis_rate.value() < k_min_analysis_rate_hz;
    if (bad) {
        return fail(ErrorCode::invalid_argument, "pitch: invalid parameters");
    }
    const double analysis_rate = std::min(source_rate.value(), y.max_analysis_rate.value());
    if (analysis_rate < 2.0 * p.max_f0.value()) {
        return fail(ErrorCode::invalid_argument,
                    "pitch: the source sample rate cannot represent max_f0");
    }
    return {};
}

/// The plugin's MIDI-note pitch scale (A4 = 69) of a frequency.
double midi_of(double hz) {
    return k_cents_per_octave / 100.0 * std::log2(hz / k_a4_hz) + k_midi_a4;
}

double cents_of(double hz) {
    return k_a4_cents + k_cents_per_octave * std::log2(hz / k_a4_hz);
}

/// Block size (even) that keeps the plugin's 2048 samples at 44.1 kHz in time.
std::size_t block_size_for(double rate) {
    const auto n =
        static_cast<std::size_t>(std::llround(k_reference_block * rate / k_reference_rate_hz));
    return n + (n % 2);
}

/// What the plugin's process() does with one block: YIN candidates, the low-amplitude scaling, and
/// the pitch/probability pairs (pitch in MIDI notes) the HMM sees.
struct FrameCandidates {
    std::vector<std::pair<double, double>> pairs;  // (MIDI pitch, mass after the level scaling)
    double periodic_mass = 0.0;                    // summed mass before it: the periodicity alone
};

/// The deepest local minimum of `cmnd` within +-3 % of `centre` (a lag); -1 if there is none.
int deepest_near(const std::vector<double>& cmnd, double centre) {
    const auto size = static_cast<int>(cmnd.size());
    const int lo = std::max(2, static_cast<int>(std::floor(centre * k_subharmonic_reach)));
    const int hi =
        std::min(size - 1, static_cast<int>(std::ceil(centre * (2.0 - k_subharmonic_reach))));
    int best = -1;
    for (int tau = lo; tau <= hi; ++tau) {
        const auto t = static_cast<std::size_t>(tau);
        const bool dip = tau + 1 < size && cmnd[t] <= cmnd[t - 1] && cmnd[t] <= cmnd[t + 1];
        if (dip && (best < 0 || cmnd[t] < cmnd[static_cast<std::size_t>(best)])) {
            best = tau;
        }
    }
    return best;
}

/// The sub-octave guard: moves the strongest candidate's mass to a dip at 1/3 or 1/2 of its lag
/// that is nearly as deep (see C4dmPYinParams::down_margin3), and repeats from the new strongest
/// candidate: a perfectly periodic signal dips at every multiple of its period, so the strongest
/// candidate can sit several periods out.
void guard_sub_octave(std::vector<double>& peak, const std::vector<double>& cmnd,
                      const C4dmPYinParams& y) {
    for (int step = 0; step < k_max_guard_steps; ++step) {
        const auto top = static_cast<std::size_t>(std::ranges::max_element(peak) - peak.begin());
        if (peak[top] <= 0.0) {
            return;
        }
        bool moved = false;
        for (const auto [divisor, margin] :
             {std::pair{3.0, y.down_margin3}, {2.0, y.down_margin2}}) {
            if (margin < 0.0) {
                continue;
            }
            const int best = deepest_near(cmnd, static_cast<double>(top) / divisor);
            if (best >= 0 && cmnd[static_cast<std::size_t>(best)] <= cmnd[top] + margin) {
                peak[static_cast<std::size_t>(best)] += peak[top];
                peak[top] *= k_demoted_mass;
                moved = true;
                break;
            }
        }
        if (!moved) {
            return;
        }
    }
}

/// The plugin's Yin::processProbabilisticYin on the vendored YinUtil, with the sub-octave guard in
/// between: per-lag peak probabilities from the CMND function, each candidate's frequency by
/// parabolic interpolation. With the guard off it is the plugin's result exactly.
std::vector<std::pair<double, double>> yin_candidates(YinUtil& util, std::vector<double>& cmnd,
                                                      const double* block, double rate,
                                                      const C4dmPYinParams& y) {
    util.fastDifference(block, cmnd.data());
    util.cumulativeDifference(cmnd.data());
    std::vector<double> peak = util.yinProb(cmnd.data(), y.threshold_distribution);
    guard_sub_octave(peak, cmnd, y);
    std::vector<std::pair<double, double>> out;
    for (std::size_t i = 0; i < peak.size(); ++i) {
        if (peak[i] > 0.0) {
            out.emplace_back(rate / util.parabolicInterpolation(cmnd.data(), static_cast<int>(i)),
                             peak[i]);
        }
    }
    return out;
}

FrameCandidates frame_candidates(YinUtil& util, std::vector<double>& cmnd, const double* block,
                                 std::size_t size, double rate, const PitchParams& params,
                                 const C4dmPYinParams& y) {
    double sum = 0.0;
    for (std::size_t i = 0; i < size; ++i) {
        sum += block[i] * block[i];
    }
    const double rms = std::sqrt(sum / static_cast<double>(size));
    double factor = 1.0;
    if (y.low_amp_rms > 0.0 && rms < y.low_amp_rms) {
        factor = (rms + 0.01 * y.low_amp_rms) / (1.01 * y.low_amp_rms);
    }
    FrameCandidates result;
    for (const auto& [hz, probability] : yin_candidates(util, cmnd, block, rate, y)) {
        if (hz >= params.min_f0.value() && hz <= params.max_f0.value()) {
            result.pairs.emplace_back(midi_of(hz), probability * factor);
            result.periodic_mass += probability;
        }
    }
    return result;
}

/// The HMM smoothing lag: the explicit one, else a bounded one for a long source, else 0 (full
/// Viterbi).
std::size_t lag_for(const C4dmPYinParams& y, double duration_s) {
    if (y.fixed_lag_frames > 0) {
        return static_cast<std::size_t>(y.fixed_lag_frames);
    }
    return duration_s > y.full_viterbi_max_duration.value()
               ? static_cast<std::size_t>(y.long_source_lag_frames)
               : 0;
}

/// What the HMM saw and decided, per frame.
struct Decoded {
    std::vector<int> states;  // HMM state of every frame, or k_undecoded
    std::vector<std::vector<std::pair<double, double>>> candidates;
    std::vector<double> periodic_mass;
};

constexpr int k_undecoded = -1;

/// The plugin's process() for every frame, then its getRemainingFeatures(): the HMM state of every
/// frame (fixed-lag mode settles the oldest frame of the window at each step).
Decoded decode_frames(MonoPitchHMM& hmm, const std::vector<double>& x, double rate,
                      std::size_t frames, std::size_t lag, const PitchParams& params,
                      const C4dmPYinParams& y) {
    const std::size_t block = block_size_for(rate);
    YinUtil util(static_cast<int>(block / 2));
    std::vector<double> cmnd(block / 2);
    Decoded d;
    d.states.assign(frames, k_undecoded);
    d.candidates.reserve(frames);
    d.periodic_mass.reserve(frames);
    std::vector<double> buffer(block);
    const auto n_x = static_cast<std::int64_t>(x.size());
    const auto quarter = static_cast<std::int64_t>(block / 4);
    for (std::size_t i = 0; i < frames; ++i) {
        const std::int64_t start =
            std::llround(static_cast<double>(i) * params.hop.value() * rate) - quarter;
        for (std::size_t k = 0; k < block; ++k) {
            const std::int64_t at = start + static_cast<std::int64_t>(k);
            buffer[k] = (at >= 0 && at < n_x) ? x[static_cast<std::size_t>(at)] : 0.0;
        }
        FrameCandidates fc = frame_candidates(util, cmnd, buffer.data(), block, rate, params, y);
        d.candidates.push_back(std::move(fc.pairs));
        d.periodic_mass.push_back(fc.periodic_mass);
        const std::vector<double> obs = hmm.calculateObsProb(d.candidates.back());
        if (i == 0) {
            hmm.initialise(obs);
        } else {
            hmm.process(obs);
        }
        if (lag > 0 && i >= lag) {
            d.states[i - lag + 1] = hmm.track().front();
        }
    }
    const std::vector<int> path = hmm.track();
    for (std::size_t k = 0; k < path.size(); ++k) {
        d.states[frames - path.size() + k] = path[k];
    }
    if (d.states[0] == k_undecoded && frames > 1) {
        d.states[0] = d.states[1];  // the plugin never decodes frame 0 in fixed-lag mode
    }
    return d;
}

/// f0, voicing and confidence from the decoded states. Voicing and confidence describe the
/// periodicity; the level enters the decision (through the scaled mass the HMM saw), not the
/// number.
void fill_track(PitchTrack& track, MonoPitchHMM& hmm, const Decoded& d) {
    const std::size_t frames = d.states.size();
    track.f0.assign(frames, Cents{k_nan});
    track.voicing_prob.assign(frames, 0.0);
    track.confidence_prob.assign(frames, 0.0);
    for (auto& alternate : track.alternates) {
        alternate.assign(frames, Cents{k_nan});
    }
    for (std::size_t i = 0; i < frames; ++i) {
        if (d.states[i] == k_undecoded) {
            continue;  // a single-frame source in fixed-lag mode
        }
        const double mass = std::clamp(d.periodic_mass[i], 0.0, 1.0);
        const double freq = hmm.nearestFreq(d.states[i], d.candidates[i]);
        if (freq > 0.0) {
            track.f0[i] = Cents{cents_of(freq)};
            track.voicing_prob[i] = std::max(k_voiced_probability, mass);
            track.confidence_prob[i] = mass;
        } else {
            track.voicing_prob[i] = std::min(k_voiced_probability - k_unvoiced_margin, mass);
            track.confidence_prob[i] = 1.0 - mass;
        }
    }
}

}  // namespace

Result<PitchTrack> C4dmPYinEstimator::estimate(const AudioSource& src,
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
    const auto frames = static_cast<std::size_t>(std::floor(duration / params.hop.value())) + 1;
    track.grid.n_frames = static_cast<std::int64_t>(frames);

    const double rate = std::min(mono->sample_rate.value(), params_.max_analysis_rate.value());
    std::vector<double> x(mono->samples.begin(), mono->samples.end());
    if (rate < mono->sample_rate.value()) {
        auto low = resample(mono->samples, mono->sample_rate, Hz{rate}, {});
        if (!low) {
            return Unexpected{low.error()};
        }
        x.assign(low->begin(), low->end());
    }
    const std::size_t lag = lag_for(params_, duration);
    MonoPitchHMM hmm(static_cast<int>(lag));
    const Decoded decoded = decode_frames(hmm, x, rate, frames, lag, params, params_);
    fill_track(track, hmm, decoded);
    return track;
}

}  // namespace anno
