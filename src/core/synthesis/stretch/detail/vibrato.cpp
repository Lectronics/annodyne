#include "core/synthesis/stretch/detail/vibrato.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace anno::stretch_detail {

namespace {

constexpr double k_two_pi = 2.0 * std::numbers::pi;

double wrap_to_pi(double x) {
    return x - k_two_pi * std::round(x / k_two_pi);
}

double hann(double x) {  // x in [0, 1]
    return 0.5 * (1.0 - std::cos(k_two_pi * x));
}

/// Hann-windowed single-frequency DFT of the points (seconds from the run's first frame), mean
/// removed: v(t) ~ 2 Re(z e^{i 2 pi f t}). The frame positions are used as they are (frames of
/// several notes need not be evenly spaced).
std::complex<double> dft_at(const std::vector<double>& t_s, const std::vector<double>& v,
                            double f_hz) {
    const double span = t_s.back() - t_s.front();
    std::complex<double> sum{0.0, 0.0};
    double weight = 0.0;
    for (std::size_t k = 0; k < t_s.size(); ++k) {
        const double w = hann((t_s[k] - t_s.front()) / span);
        sum += w * v[k] * std::polar(1.0, -k_two_pi * f_hz * t_s[k]);
        weight += w;
    }
    return weight > 0.0 ? sum / weight : std::complex<double>{};
}

}  // namespace

double Demod::fast_at(double t) const {
    const auto upper = std::ranges::upper_bound(t_s, t);
    if (upper == t_s.begin()) {
        return v.front() + mean;
    }
    if (upper == t_s.end()) {
        return v.back() + mean;
    }
    const auto k = static_cast<std::size_t>(upper - t_s.begin());
    const double w = (t - t_s[k - 1]) / (t_s[k] - t_s[k - 1]);
    return (1.0 - w) * v[k - 1] + w * v[k] + mean;
}

std::pair<double, double> Demod::amplitude_and_phase(double t) const {
    const double window = k_demod_cycles / f_hz;
    const double lo = t_s.front() + 0.5 * window;
    const double hi = t_s.back() - 0.5 * window;
    const double centre = hi > lo ? std::clamp(t, lo, hi) : 0.5 * (t_s.front() + t_s.back());
    std::complex<double> sum{0.0, 0.0};
    double weight = 0.0;
    const auto first = std::ranges::lower_bound(t_s, centre - 0.5 * window);
    for (auto it = first; it != t_s.end() && *it < centre + 0.5 * window; ++it) {
        const auto k = static_cast<std::size_t>(it - t_s.begin());
        const double w = hann((*it - (centre - 0.5 * window)) / window);
        sum += w * v[k] * std::polar(1.0, -k_two_pi * f_hz * *it);
        weight += w;
    }
    const std::complex<double> z = weight > 0.0 ? sum / weight : std::complex<double>{};
    return {2.0 * std::abs(z), std::arg(z) + k_two_pi * f_hz * t};
}

std::optional<VibratoClock> VibratoClock::make(const VibratoSource& source,
                                               const SpanGeometry& geometry, Run run,
                                               bool join_begin, bool join_end) {
    (void)join_begin;  // the clock starts on the source's phase: nothing to close at the begin
    constexpr std::size_t k_min_frames = 8;
    if (source.size() < k_min_frames || run.end <= run.begin) {
        return std::nullopt;
    }
    Demod d;
    d.t_s.reserve(source.size());
    d.v.reserve(source.size());
    double mean = 0.0;
    for (const grain::CentsPoint& p : source) {
        d.t_s.push_back(p.position / geometry.rate_hz);
        d.v.push_back(p.cents);
        mean += p.cents;
    }
    mean /= static_cast<double>(d.v.size());
    for (double& x : d.v) {
        x -= mean;
    }
    d.mean = mean;
    const double span_s = d.t_s.back() - d.t_s.front();
    if (!(span_s > 0.0)) {
        return std::nullopt;
    }
    // The rate: the peak of the windowed DFT magnitude over 3..9 Hz, refined by a parabola.
    const auto steps = static_cast<int>(
        std::lround((k_vibrato_max_hz - k_vibrato_min_hz) / k_vibrato_scan_step_hz));
    std::vector<double> magnitude(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        magnitude[static_cast<std::size_t>(i)] =
            std::abs(dft_at(d.t_s, d.v, k_vibrato_min_hz + i * k_vibrato_scan_step_hz));
    }
    const auto peak_it = std::ranges::max_element(magnitude);
    const auto peak = static_cast<int>(peak_it - magnitude.begin());
    if (peak == 0 || peak == steps) {
        return std::nullopt;  // the peak is on the edge of the scan: not a vibrato in range
    }
    const double left = magnitude[static_cast<std::size_t>(peak) - 1];
    const double mid = magnitude[static_cast<std::size_t>(peak)];
    const double right = magnitude[static_cast<std::size_t>(peak) + 1];
    const double curve = left - 2.0 * mid + right;
    const double offset = curve < 0.0 ? 0.5 * (left - right) / curve : 0.0;
    d.f_hz = k_vibrato_min_hz + (peak + offset) * k_vibrato_scan_step_hz;
    const double depth = 2.0 * mid;
    if (depth < k_vibrato_min_depth_cents || d.f_hz * span_s < k_vibrato_min_cycles) {
        return std::nullopt;
    }

    VibratoClock clock;
    clock.geometry_ = geometry;
    clock.run_begin_ = static_cast<double>(run.begin);
    const double tau_begin_s = geometry.source_at(clock.run_begin_) / geometry.rate_hz;
    const double tau_end_s = geometry.source_at(static_cast<double>(run.end)) / geometry.rate_hz;
    clock.phase_begin_rad_ = d.amplitude_and_phase(tau_begin_s).second;
    const double nominal_step = k_two_pi * d.f_hz / geometry.rate_hz;
    double eps = 0.0;
    if (join_end) {
        // close the phase at the end on a whole cycle of the source's phase there
        const double advance = nominal_step * static_cast<double>(run.end - run.begin);
        const double phase_end = d.amplitude_and_phase(tau_end_s).second;
        eps = wrap_to_pi(phase_end - clock.phase_begin_rad_ - advance) / advance;
    }
    clock.phase_step_rad_ = nominal_step * (1.0 + eps);
    clock.info_ = {.rate_hz = d.f_hz, .depth_cents = depth, .rate_change_hz = d.f_hz * eps};
    clock.demod_ = std::move(d);
    return clock;
}

double VibratoClock::cents(double out_position) const {
    const double tau_s = geometry_.source_at(out_position) / geometry_.rate_hz;
    const auto [amplitude, source_phase] = demod_.amplitude_and_phase(tau_s);
    const double regenerated =
        amplitude * std::cos(phase_begin_rad_ + phase_step_rad_ * (out_position - run_begin_));
    // the source's fast component with its sinusoid swapped for the regenerated one
    return demod_.fast_at(tau_s) - amplitude * std::cos(source_phase) + regenerated;
}

double OutputPitch::period_samples(double out_position) const {
    double period = curve->at(geometry->source_at(out_position));
    if (clock != nullptr) {
        period *= std::exp2(-clock->cents(out_position) / 1200.0);
        period =
            std::clamp(period, grain::k_min_period_s * rate_hz, grain::k_max_period_s * rate_hz);
    }
    return period;
}

}  // namespace anno::stretch_detail
