#pragma once

#include "core/dsp/filter.h"
#include "support/synth_signals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <string>
#include <vector>

// The research's onset test signals (docs/design/onset-detection-research.md section 9;
// docs/design/onset_test/gen.py) in C++: synthetic singing-like material with known onset times.
// Deterministic. Fixed at 44.1 kHz, like gen.py. The vowel is gen.py's make_vowel (impulse train
// with vibrato -> one-pole tilt -> four formant resonators), without jitter, shimmer or noise. The
// noise of the consonants and of family F comes from the seeded generators of synth_signals.h and
// the repo's zero-phase high-pass, not numpy, so the signals are the same in kind and not sample
// for sample.

namespace anno::test::onset_bench {

inline constexpr double k_rate = 44100.0;

inline double cos_ramp(std::size_t i, std::size_t n) {  // 0 -> 1 over n samples
    return 0.5 - 0.5 * std::cos(std::numbers::pi * static_cast<double>(i) /
                                static_cast<double>(std::max<std::size_t>(n, 1)));
}

/// gen.py / psola_sim.py make_vowel: formants (700,80) (1100,90) (2500,120) (3500,150), tilt 0.95,
/// peak-normalised to 1. `vibrato_cents` is the amplitude of a sine at `vibrato_hz`.
inline std::vector<double> make_vowel(double f0_hz, double vibrato_hz, double vibrato_cents,
                                      double seconds) {
    const auto n = static_cast<std::size_t>(seconds * k_rate);
    std::vector<double> cycles(n);
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / k_rate;
        const double cents = vibrato_cents * std::sin(2.0 * std::numbers::pi * vibrato_hz * t);
        acc += f0_hz * std::exp2(cents / 1200.0);
        cycles[i] = acc / k_rate;
    }
    std::vector<double> source(n + 4, 0.0);
    for (int k = 1; static_cast<double>(k) < cycles.back(); ++k) {
        const auto it = std::ranges::lower_bound(cycles, static_cast<double>(k));
        if (it == cycles.begin() || it == cycles.end()) {
            continue;
        }
        const auto j = static_cast<std::size_t>(it - cycles.begin());
        const double w = (static_cast<double>(k) - cycles[j - 1]) / (cycles[j] - cycles[j - 1]);
        const double t = (static_cast<double>(j - 1) + w) / k_rate;  // seconds of the cycle start
        const double pos = t * k_rate;
        const auto i0 = static_cast<std::size_t>(std::floor(pos));
        const double fr = pos - static_cast<double>(i0);
        source[i0] += 1.0 - fr;
        source[i0 + 1] += fr;
    }
    source.resize(n);
    std::vector<double> y(n);
    double prev = 0.0;
    for (std::size_t i = 0; i < n; ++i) {  // lfilter([1], [1, -0.95])
        prev = source[i] + 0.95 * prev;
        y[i] = prev;
    }
    for (const auto& [freq, bandwidth] :
         {std::pair{700.0, 80.0}, {1100.0, 90.0}, {2500.0, 120.0}, {3500.0, 150.0}}) {
        const double r = std::exp(-std::numbers::pi * bandwidth / k_rate);
        const double a1 = -2.0 * r * std::cos(2.0 * std::numbers::pi * freq / k_rate);
        const double a2 = r * r;
        const double b0 = 1.0 + a1 + a2;
        double y1 = 0.0;
        double y2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double out = b0 * y[i] - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = out;
            y[i] = out;
        }
    }
    double peak = 0.0;
    for (const double v : y) {
        peak = std::max(peak, std::abs(v));
    }
    for (double& v : y) {
        v /= peak;
    }
    return y;
}

/// gen.py `note`: a vowel with a cosine attack and a 40 ms cosine release.
inline std::vector<double> make_note(double f0_hz, double seconds, double attack_s,
                                     double vibrato_cents = 50.0, double vibrato_hz = 5.5) {
    auto y = make_vowel(f0_hz, vibrato_hz, vibrato_cents, seconds);
    const auto attack = static_cast<std::size_t>(attack_s * k_rate);
    const auto release = static_cast<std::size_t>(0.04 * k_rate);
    for (std::size_t i = 0; i < attack && i < y.size(); ++i) {
        y[i] *= cos_ramp(i, attack);
    }
    for (std::size_t i = 0; i < release && i < y.size(); ++i) {
        y[y.size() - 1 - i] *= cos_ramp(i, release);
    }
    return y;
}

inline void place(std::vector<double>& sig, const std::vector<double>& y, double t_s) {
    const auto at = static_cast<std::size_t>(std::llround(t_s * k_rate));
    for (std::size_t i = 0; i < y.size() && at + i < sig.size(); ++i) {
        sig[at + i] += y[i];
    }
}

inline constexpr std::array<double, 16> k_pitches = {220.0, 247.0, 262.0, 294.0, 330.0, 294.0,
                                                     262.0, 220.0, 196.0, 220.0, 247.0, 262.0,
                                                     294.0, 330.0, 349.0, 392.0};

/// The k-th note's pitch (gen.py cycles through its 16 pitches).
inline double pitch_of(int k) {
    return k_pitches.at(static_cast<std::size_t>(k) % k_pitches.size());
}

/// One test signal: samples (float, peak 0.5), the truth onset times (seconds), and for family E
/// the consonant starts too (`truth` is the vowel start there, as in gen.py).
struct Family {
    std::string name;
    std::vector<float> samples;
    std::vector<double> truth;
    std::vector<double> consonant_truth;
};

inline Family finish(std::string name, const std::vector<double>& sig, std::vector<double> truth,
                     std::vector<double> consonant_truth = {}) {
    double peak = 0.0;
    for (const double v : sig) {
        peak = std::max(peak, std::abs(v));
    }
    Family f{.name = std::move(name),
             .samples = {},
             .truth = std::move(truth),
             .consonant_truth = std::move(consonant_truth)};
    f.samples.reserve(sig.size());
    for (const double v : sig) {
        f.samples.push_back(static_cast<float>(v / peak * 0.5));
    }
    return f;
}

inline std::vector<double> sequence(int n, double dur, double gap, double attack,
                                    std::vector<double>& truth, double vibrato_cents = 50.0) {
    const double lead = 0.2;
    std::vector<double> sig(static_cast<std::size_t>((lead + n * (dur + gap) + 0.3) * k_rate), 0.0);
    for (int k = 0; k < n; ++k) {
        const double t = lead + k * (dur + gap);
        place(sig, make_note(pitch_of(k), dur, attack, vibrato_cents), t);
        truth.push_back(t);
    }
    return sig;
}

inline Family family_a() {
    std::vector<double> truth;
    auto sig = sequence(8, 0.45, 0.15, 0.005, truth);
    return finish("A_sharp", sig, std::move(truth));
}
inline Family family_a0() {
    std::vector<double> truth;
    auto sig = sequence(8, 0.45, 0.15, 0.005, truth, 0.0);
    return finish("A0_sharp_novib", sig, std::move(truth));
}
inline Family family_b() {
    std::vector<double> truth;
    auto sig = sequence(8, 0.45, 0.15, 0.080, truth);
    return finish("B_soft80ms", sig, std::move(truth));
}
inline Family family_c() {
    const double dur = 0.5;
    const double xf = 0.02;
    const double lead = 0.2;
    const int n = 8;
    std::vector<double> sig(static_cast<std::size_t>((lead + n * dur + 0.3) * k_rate), 0.0);
    std::vector<double> truth;
    const auto nx = static_cast<std::size_t>(xf * k_rate);
    for (int k = 0; k < n; ++k) {
        const double t = lead + k * dur;
        auto y = make_vowel(pitch_of(k), 5.5, 50.0, dur + xf);
        const std::size_t first = k > 0 ? nx : static_cast<std::size_t>(0.005 * k_rate);
        for (std::size_t i = 0; i < first; ++i) {
            y[i] *= cos_ramp(i, first);
        }
        const std::size_t last = k < n - 1 ? nx : static_cast<std::size_t>(0.04 * k_rate);
        for (std::size_t i = 0; i < last; ++i) {
            y[y.size() - 1 - i] *= cos_ramp(i, last);
        }
        place(sig, y, t - (k > 0 ? xf : 0.0));
        truth.push_back(t);
    }
    return finish("C_legato", sig, std::move(truth));
}
inline Family family_d() {
    auto y = make_vowel(220.0, 6.0, 100.0, 4.0);
    const auto attack = static_cast<std::size_t>(0.005 * k_rate);
    const auto release = static_cast<std::size_t>(0.04 * k_rate);
    for (std::size_t i = 0; i < y.size(); ++i) {
        const double t = static_cast<double>(i) / k_rate;
        y[i] *= 1.0 + 0.15 * std::sin(2.0 * std::numbers::pi * 6.0 * t);
    }
    for (std::size_t i = 0; i < attack; ++i) {
        y[i] *= cos_ramp(i, attack);
    }
    for (std::size_t i = 0; i < release; ++i) {
        y[y.size() - 1 - i] *= cos_ramp(i, release);
    }
    std::vector<double> sig(static_cast<std::size_t>(4.5 * k_rate), 0.0);
    place(sig, y, 0.2);
    return finish("D_vib_trem", sig, {0.2});
}

/// High-passed seeded noise scaled to the given peak.
inline std::vector<double> noise_hp(std::size_t n, double cutoff_hz, double level,
                                    std::uint64_t seed) {
    const auto raw = make_noise(static_cast<double>(n) / k_rate, k_rate, 1.0, seed);
    const std::vector<double> in(raw.begin(), raw.end());
    auto z = zero_phase_highpass(in, Hz{k_rate}, Hz{cutoff_hz}).value();
    double peak = 0.0;
    for (const double v : z) {
        peak = std::max(peak, std::abs(v));
    }
    for (double& v : z) {
        v = v / peak * level;
    }
    return z;
}

inline Family family_e() {
    const int n = 8;
    const double lead = 0.2;
    const double spacing = 0.7;
    std::vector<double> sig(static_cast<std::size_t>((lead + n * spacing + 0.5) * k_rate), 0.0);
    std::vector<double> truth_c;
    std::vector<double> truth_v;
    for (int k = 0; k < n; ++k) {
        const double t0 = lead + k * spacing;
        double tv = 0.0;
        if (k % 2 == 0) {  // plosive: a 6 ms burst then 60 ms aspiration
            auto burst = noise_hp(static_cast<std::size_t>(0.006 * k_rate), 2000.0, 0.5,
                                  std::uint64_t{10} + static_cast<std::uint64_t>(k));
            for (std::size_t i = 0; i < burst.size(); ++i) {
                burst[i] *= std::exp(-static_cast<double>(i) / (0.002 * k_rate));
            }
            auto asp = noise_hp(static_cast<std::size_t>(0.060 * k_rate), 1500.0, 0.06,
                                std::uint64_t{30} + static_cast<std::uint64_t>(k));
            for (std::size_t i = 0; i < asp.size(); ++i) {
                asp[i] *= cos_ramp(i, asp.size());
            }
            place(sig, burst, t0);
            place(sig, asp, t0 + 0.006);
            tv = t0 + 0.066;
        } else {  // fricative: 150 ms of noise with 15 ms ramps
            auto fr = noise_hp(static_cast<std::size_t>(0.150 * k_rate), 4000.0, 0.25,
                               std::uint64_t{50} + static_cast<std::uint64_t>(k));
            const auto ramp = static_cast<std::size_t>(0.015 * k_rate);
            for (std::size_t i = 0; i < ramp; ++i) {
                fr[i] *= cos_ramp(i, ramp);
                fr[fr.size() - 1 - i] *= cos_ramp(i, ramp);
            }
            place(sig, fr, t0);
            tv = t0 + 0.150;
        }
        auto vowel = make_note(pitch_of(k), 0.4, 0.010);
        for (double& v : vowel) {
            v *= 0.5;
        }
        place(sig, vowel, tv);
        truth_c.push_back(t0);
        truth_v.push_back(tv);
    }
    return finish("E_consonants", sig, std::move(truth_v), std::move(truth_c));
}

inline Family family_f() {
    std::vector<double> truth;
    auto sig = sequence(8, 0.45, 0.15, 0.005, truth);
    std::vector<float> noisy;
    noisy.reserve(sig.size());
    std::ranges::transform(sig, std::back_inserter(noisy),
                           [](double v) { return static_cast<float>(v); });
    add_white_noise(noisy, 30.0, 11);
    return finish("F_sharp_noise30dB", std::vector<double>(noisy.begin(), noisy.end()),
                  std::move(truth));
}
inline Family family_g() {
    std::vector<double> truth;
    auto sig = sequence(16, 0.11, 0.015, 0.005, truth);
    return finish("G_fast_8perSec", sig, std::move(truth));
}

/// The families of the research's pooled level test (A, A0, B, D, F, G).
inline std::vector<Family> pooled_families() {
    return {family_a(), family_a0(), family_b(), family_d(), family_f(), family_g()};
}

/// A copy attenuated by `db` (negative = quieter).
inline std::vector<float> attenuated(const std::vector<float>& x, double db) {
    const double g = std::pow(10.0, db / 20.0);
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = static_cast<float>(static_cast<double>(x[i]) * g);
    }
    return y;
}

}  // namespace anno::test::onset_bench
