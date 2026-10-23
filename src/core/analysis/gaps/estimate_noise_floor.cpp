#include "core/analysis/gaps/estimate_noise_floor.h"

#include "core/dsp/fft.h"
#include "core/dsp/mono.h"
#include "core/dsp/window.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <optional>

namespace anno {

namespace {

constexpr double k_max_band_fraction = 0.45;  // of the sample rate: the anti-alias guard band
constexpr double k_tiny_power = 1e-12;        // keeps log10 finite; equals k_level_floor_db
constexpr int k_fft_zero_pad = 4;             // FFT length = this x the window length (rounded up)

Unexpected invalid(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"estimate_noise_floor: "} + what);
}

bool finite_positive(double v) {
    return std::isfinite(v) && v > 0.0;
}

Result<void> check_params(const NoiseFloorParams& p) {
    if (!finite_positive(p.window.value()) || !finite_positive(p.spectrum_window.value())) {
        return invalid("window and spectrum_window must be finite and > 0");
    }
    if (std::isnan(p.percentile_prob) || p.percentile_prob <= 0.0 || p.percentile_prob >= 1.0) {
        return invalid("percentile_prob must be in (0, 1)");
    }
    if (p.bands < 1 || p.max_spectrum_frames < 1) {
        return invalid("bands and max_spectrum_frames must be >= 1");
    }
    if (!finite_positive(p.band_low.value()) || !std::isfinite(p.band_high.value()) ||
        !(p.band_low.value() < p.band_high.value())) {
        return invalid("0 < band_low < band_high required");
    }
    if (!std::isfinite(p.spectrum_margin.value()) || p.spectrum_margin.value() < 0.0 ||
        !std::isfinite(p.note_guard.value()) || p.note_guard.value() < 0.0 ||
        !std::isfinite(p.min_support.value()) || p.min_support.value() < 0.0) {
        return invalid("spectrum_margin, note_guard and min_support must be finite and >= 0");
    }
    return {};
}

Result<void> check_activity(const ActivityTrack& a) {
    if (auto r = validate(a.grid); !r) {
        return Unexpected{r.error()};
    }
    const auto n = static_cast<std::size_t>(a.grid.n_frames);
    if (a.level.size() != n || a.activity.size() != n) {
        return invalid("the activity track does not match its grid");
    }
    if (!std::ranges::all_of(a.level, [](Decibels d) { return std::isfinite(d.value()); })) {
        return invalid("the activity levels must be finite");
    }
    return {};
}

struct Band {
    double low_hz = 0.0;
    double high_hz = 0.0;
    double centre_hz = 0.0;  // geometric mean of the edges
};

/// Log-spaced bands on the nominal edges; those above 0.45 x the rate are dropped.
std::vector<Band> make_bands(const NoiseFloorParams& p, double rate_hz) {
    const double ratio = std::pow(p.band_high.value() / p.band_low.value(), 1.0 / p.bands);
    std::vector<Band> bands;
    for (int j = 0; j < p.bands; ++j) {
        const double low = p.band_low.value() * std::pow(ratio, j);
        const double high = p.band_low.value() * std::pow(ratio, j + 1);
        if (high > k_max_band_fraction * rate_hz) {
            break;
        }
        bands.push_back({.low_hz = low, .high_hz = high, .centre_hz = std::sqrt(low * high)});
    }
    return bands;
}

/// True for each grid frame that lies inside an active note or within `guard` of one.
std::vector<char> note_mask(const FrameGrid& grid, std::span<const Note> notes, Seconds guard) {
    std::vector<char> inside(static_cast<std::size_t>(grid.n_frames), 0);
    const double hop = grid.hop.value();
    for (const Note& note : notes) {
        if (!note.is_active) {
            continue;
        }
        const double from = (note.source_start - guard - grid.start).value() / hop;
        const double to = (note.source_end + guard - grid.start).value() / hop;
        const auto first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(from)));
        const auto last =
            std::min<std::int64_t>(grid.n_frames - 1, static_cast<std::int64_t>(std::floor(to)));
        for (std::int64_t i = first; i <= last; ++i) {
            inside[static_cast<std::size_t>(i)] = 1;
        }
    }
    return inside;
}

/// The percentile of the outside-note levels within `half` frames of each frame; empty where the
/// window holds fewer than `min_frames` of them.
std::vector<std::optional<double>> windowed_percentile(const ActivityTrack& activity,
                                                       const std::vector<char>& inside,
                                                       std::int64_t half, double prob,
                                                       std::size_t min_frames) {
    const auto n = static_cast<std::int64_t>(activity.level.size());
    std::vector<std::optional<double>> out(static_cast<std::size_t>(n));
    std::vector<double> candidates;
    for (std::int64_t i = 0; i < n; ++i) {
        candidates.clear();
        for (std::int64_t j = std::max<std::int64_t>(0, i - half); j <= std::min(n - 1, i + half);
             ++j) {
            if (inside[static_cast<std::size_t>(j)] == 0) {
                candidates.push_back(activity.level[static_cast<std::size_t>(j)].value());
            }
        }
        if (candidates.empty() || candidates.size() < min_frames) {
            continue;
        }
        const auto k =
            static_cast<std::size_t>(std::floor(prob * static_cast<double>(candidates.size() - 1)));
        std::ranges::nth_element(candidates, candidates.begin() + static_cast<std::ptrdiff_t>(k));
        out[static_cast<std::size_t>(i)] = candidates[k];
    }
    return out;
}

/// Fills the empty estimates from the nearest estimate on either side (fewest frames away, the
/// earlier one on a tie), else `fallback`.
std::vector<double> fill_nearest(const std::vector<std::optional<double>>& estimate,
                                 double fallback) {
    const auto n = static_cast<std::int64_t>(estimate.size());
    std::vector<std::int64_t> left(estimate.size(), -1);
    std::vector<std::int64_t> right(estimate.size(), -1);
    for (std::int64_t i = 0, last = -1; i < n; ++i) {
        last = estimate[static_cast<std::size_t>(i)] ? i : last;
        left[static_cast<std::size_t>(i)] = last;
    }
    for (std::int64_t i = n - 1, next = -1; i >= 0; --i) {
        next = estimate[static_cast<std::size_t>(i)] ? i : next;
        right[static_cast<std::size_t>(i)] = next;
    }
    std::vector<double> out(estimate.size(), fallback);
    for (std::int64_t i = 0; i < n; ++i) {
        const auto at = static_cast<std::size_t>(i);
        const std::int64_t l = left[at];
        const std::int64_t r = right[at];
        std::int64_t pick = l;
        if (l < 0 || (r >= 0 && r - i < i - l)) {
            pick = r;
        }
        if (pick >= 0) {
            out[at] = *estimate[static_cast<std::size_t>(pick)];
        }
    }
    return out;
}

/// The frames that feed the spectrum: outside every note, at most `margin` above the curve, evenly
/// strided down to `max_frames`.
std::vector<std::int64_t> floor_frames(const ActivityTrack& activity,
                                       const std::vector<char>& inside,
                                       const std::vector<Decibels>& curve, double margin_db,
                                       int max_frames) {
    std::vector<std::int64_t> all;
    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (inside[i] == 0 && activity.level[i].value() <= curve[i].value() + margin_db) {
            all.push_back(static_cast<std::int64_t>(i));
        }
    }
    const auto stride =
        std::max<std::size_t>(1, (all.size() + static_cast<std::size_t>(max_frames) - 1) /
                                     static_cast<std::size_t>(max_frames));
    std::vector<std::int64_t> picked;
    for (std::size_t k = 0; k < all.size(); k += stride) {
        picked.push_back(all[k]);
    }
    return picked;
}

std::size_t next_power_of_two(std::size_t v) {
    std::size_t n = 1;
    while (n < v) {
        n *= 2;
    }
    return n;
}

/// Mean power per band over the given frames, RMS dBFS (see the header).
Result<std::vector<Decibels>> band_levels(const MonoAudio& mono, const FrameGrid& grid,
                                          const std::vector<std::int64_t>& frames,
                                          const std::vector<Band>& bands, Seconds window,
                                          Decibels fallback) {
    if (frames.empty()) {
        return std::vector<Decibels>(bands.size(), fallback);
    }
    const double rate = mono.sample_rate.value();
    const auto length =
        std::max<std::size_t>(2, static_cast<std::size_t>(std::llround(window.value() * rate)));
    const std::size_t n_fft = k_fft_zero_pad * next_power_of_two(length);
    auto fft = RealFft::create(n_fft);
    if (!fft) {
        return Unexpected{fft.error()};
    }
    const std::vector<double> w = make_window(WindowKind::hann_periodic, static_cast<int>(length));
    double window_power = 0.0;
    for (const double v : w) {
        window_power += v * v;
    }
    std::vector<double> power_sum(bands.size(), 0.0);
    std::vector<double> frame(n_fft);
    std::vector<std::complex<double>> spectrum(n_fft / 2 + 1);
    const auto total = static_cast<std::int64_t>(mono.samples.size());
    for (const std::int64_t index : frames) {
        const std::int64_t centre =
            std::llround((frame_time(grid, index) - SourceTime{}).value() * rate);
        const std::int64_t first = centre - static_cast<std::int64_t>(length / 2);
        std::ranges::fill(frame, 0.0);
        for (std::size_t n = 0; n < length; ++n) {
            const std::int64_t at = first + static_cast<std::int64_t>(n);
            if (at >= 0 && at < total) {
                frame[n] = w[n] * static_cast<double>(mono.samples[static_cast<std::size_t>(at)]);
            }
        }
        fft->forward(frame, spectrum);
        for (std::size_t b = 0; b < bands.size(); ++b) {
            const double bin_hz = rate / static_cast<double>(n_fft);
            const auto lo = static_cast<std::size_t>(std::ceil(bands[b].low_hz / bin_hz));
            const auto hi = static_cast<std::size_t>(std::ceil(bands[b].high_hz / bin_hz));
            double sum = 0.0;
            for (std::size_t k = lo; k < std::min(hi, spectrum.size()); ++k) {
                sum += std::norm(spectrum[k]);
            }
            power_sum[b] += 2.0 * sum / (static_cast<double>(n_fft) * window_power);
        }
    }
    std::vector<Decibels> out;
    out.reserve(bands.size());
    for (const double sum : power_sum) {
        const double mean = sum / static_cast<double>(frames.size());
        out.emplace_back(
            std::max(k_level_floor_db.value(), 10.0 * std::log10(mean + k_tiny_power)));
    }
    return out;
}

}  // namespace

Result<NoiseFloor> estimate_noise_floor(const AudioSource& source, const ActivityTrack& activity,
                                        std::span<const Note> notes,
                                        const NoiseFloorParams& params) {
    if (auto r = check_params(params); !r) {
        return Unexpected{r.error()};
    }
    auto mono = read_mono(source);
    if (!mono) {
        return Unexpected{mono.error()};
    }
    if (mono->samples.empty()) {
        return fail(ErrorCode::out_of_range, "estimate_noise_floor: the recording is empty");
    }
    if (auto r = check_activity(activity); !r) {
        return Unexpected{r.error()};
    }
    const std::vector<Band> bands = make_bands(params, mono->sample_rate.value());
    if (bands.empty()) {
        return invalid("every band lies above 0.45 x the sample rate");
    }

    const std::vector<char> inside = note_mask(activity.grid, notes, params.note_guard);
    const auto half = static_cast<std::int64_t>(
        std::floor(0.5 * params.window.value() / activity.grid.hop.value()));
    const auto min_frames =
        static_cast<std::size_t>(std::ceil(params.min_support.value() / activity.grid.hop.value()));
    const std::vector<double> estimate = fill_nearest(
        windowed_percentile(activity, inside, half, params.percentile_prob, min_frames),
        activity.noise_floor.value());

    NoiseFloor out;
    out.grid = activity.grid;
    out.curve_dbfs.reserve(estimate.size());
    for (const double v : estimate) {
        out.curve_dbfs.emplace_back(std::max(v, k_level_floor_db.value()));
    }
    const std::vector<std::int64_t> frames =
        floor_frames(activity, inside, out.curve_dbfs, params.spectrum_margin.value(),
                     params.max_spectrum_frames);
    auto levels = band_levels(*mono, activity.grid, frames, bands, params.spectrum_window,
                              activity.noise_floor);
    if (!levels) {
        return Unexpected{levels.error()};
    }
    out.band_dbfs = std::move(*levels);
    for (const Band& b : bands) {
        out.band_centre.emplace_back(b.centre_hz);
    }
    if (auto r = validate(out); !r) {
        return Unexpected{r.error()};
    }
    return out;
}

}  // namespace anno
