#include "core/analysis/onsets/onset_stages.h"

#include "core/dsp/mono.h"
#include "core/dsp/stft.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_fmax_nyquist_share = 0.45;
constexpr int k_min_bands = 4;
constexpr double k_energy_floor_db = -100.0;
constexpr double k_level_frame_s = 0.020;

Unexpected bad(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"onset stages: "} + what);
}

bool positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

// ---- P1 helpers (the Phase 3 filterbank, moved unchanged) ------------------------------------

struct Filter {
    std::size_t first = 0;
    std::size_t centre_bin = 0;
    std::vector<double> weights;  // for bins first, first + 1, ...
};

/// Log-spaced triangular filters over FFT bins (centres on distinct bins), unit sum each.
std::vector<Filter> log_filterbank(const BandParams& p, double bin_hz, int n_bins, double rate) {
    const double fmax = std::min(p.f_max.value(), k_fmax_nyquist_share * rate);
    std::vector<int> centres;
    for (int k = 0;; ++k) {
        const double f =
            p.f_min.value() * std::pow(2.0, static_cast<double>(k) / p.bands_per_octave);
        if (f > fmax) {
            break;
        }
        const int bin = std::clamp(static_cast<int>(std::lround(f / bin_hz)), 1, n_bins - 2);
        if (centres.empty() || bin > centres.back()) {
            centres.push_back(bin);
        }
    }
    if (static_cast<int>(centres.size()) <
        k_min_bands) {  // very coarse spectrum: fall back to a linear split
        centres.clear();
        for (int b = 1; b < n_bins - 1; b += std::max(1, (n_bins - 2) / 16)) {
            centres.push_back(b);
        }
    }
    std::vector<Filter> bank;
    for (std::size_t i = 0; i < centres.size(); ++i) {
        const int left = i == 0 ? centres[i] - 1 : centres[i - 1];
        const int right = i + 1 == centres.size() ? centres[i] + 1 : centres[i + 1];
        Filter f{.first = static_cast<std::size_t>(std::max(0, left)),
                 .centre_bin = static_cast<std::size_t>(centres[i]),
                 .weights = {}};
        double sum = 0.0;
        for (int b = std::max(0, left); b <= std::min(n_bins - 1, right); ++b) {
            const double w = b <= centres[i]
                                 ? static_cast<double>(b - left) / std::max(1, centres[i] - left)
                                 : static_cast<double>(right - b) / std::max(1, right - centres[i]);
            f.weights.push_back(std::max(0.0, w));
            sum += std::max(0.0, w);
        }
        if (sum > 0.0) {
            for (double& w : f.weights) {
                w /= sum;
            }
            bank.push_back(std::move(f));
        }
    }
    return bank;
}

int nearest_pow2(double n) {
    const int exponent = std::max(8, static_cast<int>(std::lround(std::log2(std::max(1.0, n)))));
    return static_cast<int>(std::pow(2.0, exponent));
}

// ---- L helpers (the Phase 3 refinement, moved unchanged) --------------------------------------

struct Refined {
    std::int64_t sample = 0;
    bool rose = false;
};

/// Short-window energy in dB at sample `n` (window [n - w/2, n + w/2)).
double energy_db(std::span<const float> x, std::int64_t n, std::int64_t w) {
    const auto size = static_cast<std::int64_t>(x.size());
    const std::int64_t lo = std::max<std::int64_t>(0, n - w / 2);
    const std::int64_t hi = std::min<std::int64_t>(size, n + (w + 1) / 2);
    if (hi <= lo) {
        return k_energy_floor_db;
    }
    double sum = 0.0;
    for (std::int64_t i = lo; i < hi; ++i) {
        sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    }
    return std::max(k_energy_floor_db,
                    10.0 * std::log10(sum / static_cast<double>(hi - lo) + 1e-10));
}

/// The sample where the energy rises through the halfway point (in dB) between the quietest point
/// before `centre` and the loudest point after it, searched within +-`radius` samples; `centre`
/// itself if there is no clear rise. Empty if the signal is clearly louder before `centre` than
/// after it: the end of a note makes spectral flux too, but it is not an onset.
std::optional<Refined> refine_to_energy_rise(std::span<const float> x, std::int64_t centre,
                                             std::int64_t radius, std::int64_t w,
                                             const LocaliseParams& p) {
    const std::int64_t lo = std::max<std::int64_t>(0, centre - radius);
    const std::int64_t hi =
        std::min<std::int64_t>(static_cast<std::int64_t>(x.size()) - 1, centre + radius);
    double floor_db = 1e9;
    double top_db = -1e9;
    double start_db = -1e9;  // loudest point in the first quarter of the region
    double end_db = -1e9;    // loudest point in the last quarter
    for (std::int64_t n = lo; n <= hi; ++n) {
        const double e = energy_db(x, n, w);
        if (n <= centre) {
            floor_db = std::min(floor_db, e);
        }
        top_db = std::max(top_db, e);
        if (n - lo <= (hi - lo) / 4) {
            start_db = std::max(start_db, e);
        }
        if (hi - n <= (hi - lo) / 4) {
            end_db = std::max(end_db, e);
        }
    }
    if (start_db - end_db >= p.end_drop.value()) {
        return std::nullopt;
    }
    if (top_db - floor_db < p.min_rise.value()) {
        return Refined{.sample = centre, .rose = false};
    }
    const double halfway = floor_db + 0.5 * (top_db - floor_db);
    for (std::int64_t n = lo; n <= hi; ++n) {
        if (energy_db(x, n, w) >= halfway) {
            return Refined{.sample = n, .rose = true};
        }
    }
    return Refined{.sample = centre, .rose = false};
}

}  // namespace

Result<void> validate(const BandSpectrogram& spec) {
    if (spec.n_frames < 0 || spec.n_bands < 0 ||
        spec.centres.size() != static_cast<std::size_t>(spec.n_bands) ||
        spec.data.size() != static_cast<std::size_t>(spec.n_frames * spec.n_bands)) {
        return bad("BandSpectrogram sizes disagree");
    }
    for (std::size_t i = 1; i < spec.centres.size(); ++i) {
        if (!(spec.centres[i].value() > spec.centres[i - 1].value())) {
            return bad("BandSpectrogram centres are not strictly increasing");
        }
    }
    if (!std::ranges::all_of(spec.data, [](double v) { return std::isfinite(v) && v >= 0.0; })) {
        return bad("BandSpectrogram holds a negative or non-finite value");
    }
    return {};
}

/// The level in dB per `mode`, empty for silence (no non-zero sample or no active frame).
std::optional<double> measure_level_db(std::span<const float> mono, Hz rate,
                                       const LevelNormParams& params) {
    if (params.mode == LevelNorm::peak) {
        double peak = 0.0;
        for (const float v : mono) {
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }
        return peak > 0.0 ? std::optional{20.0 * std::log10(peak)} : std::nullopt;
    }
    const auto frame = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(k_level_frame_s * rate.value())));
    std::vector<double> powers;
    for (std::size_t i = 0; i + frame <= mono.size(); i += frame) {
        double sum = 0.0;
        for (std::size_t k = 0; k < frame; ++k) {
            sum += static_cast<double>(mono[i + k]) * static_cast<double>(mono[i + k]);
        }
        powers.push_back(sum / static_cast<double>(frame));
    }
    const double loudest = powers.empty() ? 0.0 : *std::ranges::max_element(powers);
    if (!(loudest > 0.0)) {
        return std::nullopt;
    }
    const double threshold = loudest * std::pow(10.0, params.active_floor.value() / 10.0);
    double sum = 0.0;
    std::size_t count = 0;
    for (const double p : powers) {
        if (p >= threshold) {
            sum += p;
            ++count;
        }
    }
    return 10.0 * std::log10(sum / static_cast<double>(count));
}

Result<NormalisedSignal> normalise_level(std::span<const float> mono, Hz rate,
                                         const LevelNormParams& params) {
    if (!positive(rate.value()) || !std::isfinite(params.target.value()) ||
        !std::isfinite(params.active_floor.value()) || !std::isfinite(params.max_gain.value()) ||
        params.max_gain.value() < 0.0 ||
        !std::ranges::all_of(mono, [](float v) { return std::isfinite(v); })) {
        return fail(ErrorCode::invalid_argument,
                    "normalise_level: non-finite sample or parameter, or max_gain < 0");
    }
    NormalisedSignal out{.samples = std::vector<float>(mono.begin(), mono.end()),
                         .gain = Decibels{0.0}};
    const std::optional<double> level_db = measure_level_db(mono, rate, params);
    if (!level_db) {
        return out;
    }
    const double gain_db = std::min(params.target.value() - *level_db, params.max_gain.value());
    const double gain = std::pow(10.0, gain_db / 20.0);
    for (float& v : out.samples) {
        v = static_cast<float>(static_cast<double>(v) * gain);
    }
    out.gain = Decibels{gain_db};
    return out;
}

Result<BandSpectrogram> band_spectrogram(std::span<const float> mono, Hz rate,
                                         const BandParams& params) {
    if (!positive(rate.value()) || !positive(params.window.value()) ||
        !positive(params.hop.value()) || !positive(params.f_min.value()) ||
        !std::isfinite(params.f_max.value()) || params.f_min.value() >= params.f_max.value() ||
        params.bands_per_octave < 1) {
        return bad("invalid band parameters");
    }
    const int hop_samples =
        std::max(1, static_cast<int>(std::lround(params.hop.value() * rate.value())));
    BandSpectrogram out{.hop = Seconds{static_cast<double>(hop_samples) / rate.value()},
                        .n_frames = 0,
                        .n_bands = 0,
                        .centres = {},
                        .data = {}};
    const int n_fft = nearest_pow2(params.window.value() * rate.value());
    if (mono.size() < static_cast<std::size_t>(n_fft)) {
        return out;  // shorter than the window: no meaningful spectral change
    }
    auto spec = stft(mono, rate,
                     {.n_fft = n_fft,
                      .hop_samples = std::min(hop_samples, n_fft),
                      .window = WindowKind::hann_periodic});
    if (!spec) {
        return Unexpected{spec.error()};
    }
    const double bin_hz = rate.value() / n_fft;
    const std::vector<Filter> bank =
        log_filterbank(params, bin_hz, static_cast<int>(spec->n_bins), rate.value());
    out.n_frames = spec->n_frames;
    out.n_bands = static_cast<std::int64_t>(bank.size());
    for (const Filter& f : bank) {
        out.centres.emplace_back(static_cast<double>(f.centre_bin) * bin_hz);
    }
    const auto frames = static_cast<std::size_t>(spec->n_frames);
    const std::size_t bands = bank.size();
    out.data.resize(frames * bands);
    for (std::size_t f = 0; f < frames; ++f) {
        const auto frame = static_cast<std::int64_t>(f);
        for (std::size_t b = 0; b < bands; ++b) {
            double s = 0.0;
            const auto bin_first = static_cast<std::int64_t>(bank[b].first);
            for (std::size_t k = 0; k < bank[b].weights.size(); ++k) {
                s += bank[b].weights[k] *
                     std::abs(spec->at(frame, bin_first + static_cast<std::int64_t>(k)));
            }
            out.data[f * bands + b] = s;
        }
    }
    return out;
}

Result<BandSpectrogram> adaptive_whiten(const BandSpectrogram& spec, const WhitenParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!positive(params.relax.value()) || !positive(params.floor_magnitude)) {
        return bad("adaptive_whiten: relax and floor_magnitude must be positive");
    }
    const double decay = std::exp(-spec.hop.value() / params.relax.value());
    BandSpectrogram out = spec;
    const auto bands = static_cast<std::size_t>(spec.n_bands);
    std::vector<double> peak(bands, 0.0);
    const auto frames = static_cast<std::size_t>(spec.n_frames);
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t b = 0; b < bands; ++b) {
            const double x = spec.data[f * bands + b];
            peak[b] = f == 0 ? std::max(x, params.floor_magnitude)
                             : std::max({x, decay * peak[b], params.floor_magnitude});
            out.data[f * bands + b] = x / peak[b];
        }
    }
    return out;
}

Result<BandSpectrogram> log_compress(const BandSpectrogram& spec, const LogParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!positive(params.log_gain)) {
        return bad("log_compress: log_gain must be positive");
    }
    BandSpectrogram out = spec;
    for (double& v : out.data) {
        v = std::log1p(params.log_gain * v);
    }
    return out;
}

Result<Activation> superflux(const BandSpectrogram& spec, const FluxParams& params) {
    if (auto r = validate(spec); !r) {
        return Unexpected{r.error()};
    }
    if (!positive(params.lag.value()) || params.max_filter_bins < 1 ||
        params.max_filter_bins % 2 == 0) {
        return bad("superflux: lag must be positive and max_filter_bins odd and >= 1");
    }
    Activation out{.values = {}, .hop = spec.hop};
    const auto frames = static_cast<std::size_t>(spec.n_frames);
    const auto bands = static_cast<std::size_t>(spec.n_bands);
    if (frames == 0 || bands == 0) {
        return out;
    }
    const auto lag =
        static_cast<std::size_t>(std::max(1L, std::lround(params.lag.value() / out.hop.value())));
    const auto radius = static_cast<std::ptrdiff_t>((params.max_filter_bins - 1) / 2);
    out.values.assign(frames, 0.0);
    for (std::size_t f = lag; f < frames; ++f) {
        double sum = 0.0;
        for (std::size_t b = 0; b < bands; ++b) {
            const auto lo = static_cast<std::size_t>(
                std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(b) - radius));
            const auto hi = std::min(bands - 1, b + static_cast<std::size_t>(radius));
            double reference = 0.0;
            for (std::size_t k = lo; k <= hi; ++k) {
                reference = std::max(reference, spec.data[(f - lag) * bands + k]);
            }
            sum += std::max(0.0, spec.data[f * bands + b] - reference);
        }
        out.values[f] = sum / static_cast<double>(bands);
    }
    return out;
}

Result<PeakList> pick_peaks_windowed(const Activation& activation,
                                     const WindowedPeakParams& params) {
    const double hop = activation.hop.value();
    if (!positive(hop) || params.pre_max.value() < 0.0 || params.post_max.value() < 0.0 ||
        params.pre_avg.value() < 0.0 || params.post_avg.value() < 0.0 ||
        params.min_gap.value() < 0.0 || !std::isfinite(params.delta_activation) ||
        params.delta_activation < 0.0) {
        return bad("pick_peaks_windowed: invalid parameters");
    }
    const auto count = [&](Seconds s) {
        return static_cast<std::int64_t>(std::llround(s.value() / hop));
    };
    const std::int64_t pre_max = count(params.pre_max);
    const std::int64_t post_max = count(params.post_max);
    const std::int64_t pre_avg = count(params.pre_avg);
    const std::int64_t post_avg = count(params.post_avg);
    const std::int64_t gap = count(params.min_gap);
    const auto& a = activation.values;
    const auto n = static_cast<std::int64_t>(a.size());
    PeakList out{.hop = activation.hop, .peaks = {}};
    std::int64_t last = -1;
    bool have_last = false;
    for (std::int64_t i = 0; i < n; ++i) {
        const double v = a[static_cast<std::size_t>(i)];
        if (!(v > 0.0)) {
            continue;
        }
        bool is_max = true;
        for (std::int64_t k = std::max<std::int64_t>(0, i - pre_max); k < i && is_max; ++k) {
            is_max = a[static_cast<std::size_t>(k)] < v;
        }
        for (std::int64_t k = i + 1; k <= std::min(n - 1, i + post_max) && is_max; ++k) {
            is_max = a[static_cast<std::size_t>(k)] <= v;
        }
        if (!is_max) {
            continue;
        }
        double sum = 0.0;
        for (std::int64_t k = std::max<std::int64_t>(0, i - pre_avg);
             k <= std::min(n - 1, i + post_avg); ++k) {
            sum += a[static_cast<std::size_t>(k)];
        }
        const double mean = sum / static_cast<double>(pre_avg + post_avg + 1);
        const double threshold = mean + params.delta_activation;
        if (v < threshold) {
            continue;
        }
        if (have_last && i - last <= gap) {
            continue;
        }
        out.peaks.push_back(
            {.frame = static_cast<std::size_t>(i), .strength = v / std::max(threshold, 1e-9)});
        last = i;
        have_last = true;
    }
    return out;
}

Result<std::vector<LocalisedOnset>> localise(std::span<const float> mono, Hz rate,
                                             const PeakList& peaks, const LocaliseParams& params) {
    if (!positive(rate.value()) || !positive(peaks.hop.value()) ||
        !positive(params.search.value()) || !positive(params.energy_window.value()) ||
        !(params.min_rise.value() >= 0.0) || !(params.end_drop.value() >= 0.0)) {
        return bad("localise: invalid parameters");
    }
    const double sr = rate.value();
    const auto window = std::max<std::int64_t>(2, std::llround(params.energy_window.value() * sr));
    const auto radius = static_cast<std::int64_t>(std::llround(params.search.value() * sr));
    std::vector<LocalisedOnset> out;
    for (const ActivationPeak& p : peaks.peaks) {
        const auto centre = static_cast<std::int64_t>(
            std::llround(static_cast<double>(p.frame) * peaks.hop.value() * sr));
        if (std::cmp_greater(centre, mono.size())) {
            return bad("localise: a peak lies beyond the signal");
        }
        const std::optional<Refined> refined =
            refine_to_energy_rise(mono, centre, radius, window, params);
        if (!refined) {
            continue;
        }
        out.push_back(
            {.sample = refined->sample,
             .time = SourceTime{} + Seconds{static_cast<double>(refined->sample) / sr},
             .strength = p.strength,
             .outcome = refined->rose ? LocaliseOutcome::rose : LocaliseOutcome::unchanged});
    }
    return out;
}

Result<OnsetList> detect_transients(const AudioSource& src, const TransientParams& params) {
    auto mono = read_mono(src);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    std::vector<float> samples = std::move(mono->samples);
    const Hz rate = mono->sample_rate;
    if (params.level) {
        auto normalised = normalise_level(samples, rate, *params.level);
        if (!normalised) {
            return Unexpected{normalised.error()};
        }
        samples = std::move(normalised->samples);
    }
    auto bands = band_spectrogram(samples, rate, params.bands);
    if (!bands) {
        return Unexpected{bands.error()};
    }
    if (params.whiten) {
        bands = adaptive_whiten(*bands, *params.whiten);
        if (!bands) {
            return Unexpected{bands.error()};
        }
    }
    auto logged = log_compress(*bands, params.log);
    if (!logged) {
        return Unexpected{logged.error()};
    }
    auto activation = superflux(*logged, params.flux);
    if (!activation) {
        return Unexpected{activation.error()};
    }
    auto peaks = pick_peaks_windowed(*activation, params.peaks);
    if (!peaks) {
        return Unexpected{peaks.error()};
    }
    auto onsets = localise(samples, rate, *peaks, params.localise);
    if (!onsets) {
        return Unexpected{onsets.error()};
    }
    std::ranges::stable_sort(*onsets, {}, [](const LocalisedOnset& o) { return o.time; });
    OnsetList list;
    for (const LocalisedOnset& o : *onsets) {
        list.times.push_back(o.time);
        list.strength.push_back(o.strength);
        list.kind.push_back(OnsetKind::attack);
    }
    return list;
}

}  // namespace anno
