#pragma once

#include "core/dsp/filter.h"
#include "support/synth_signals.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Probe scenes for the voicing decision: C++ ports of docs/design/probes/make_probes.py and
// sweep.py (docs/design/phase3-voicing-review.md section 3). Peak-normalised sources scaled by dB,
// 44.1 kHz, seeded noise. Each region carries the expected answer of review section 4.

namespace anno::test {

inline constexpr double k_probe_rate = 44100.0;

enum class Expect { voiced_note, unvoiced, voiced_not_note, voiced, either };

struct ProbeRegion {
    double from_s;
    double to_s;
    Expect expect;
    const char* what;
};

struct Probe {
    std::vector<float> samples;
    std::vector<ProbeRegion> regions;
};

namespace probe_detail {

inline double db(double x) {
    return std::pow(10.0, x / 20.0);
}

inline std::size_t count_of(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * k_probe_rate));
}

inline std::vector<double> to_double(const std::vector<float>& x) {
    return {x.begin(), x.end()};
}

inline void normalise_peak(std::vector<double>& x) {
    double peak = 0.0;
    for (const double v : x) {
        peak = std::max(peak, std::abs(v));
    }
    for (double& v : x) {
        v /= peak;
    }
}

/// Partials 1/k (k <= 15 by default) of a constant or gliding f0, peak 1.
template <class F0Hz>
std::vector<double> harmonic_f(const F0Hz& f0_hz, double seconds, std::vector<double> amps = {}) {
    if (amps.empty()) {
        for (int k = 1; k <= 15; ++k) {
            amps.push_back(1.0 / k);
        }
    }
    std::vector<double> x = to_double(make_harmonic(f0_hz, seconds, k_probe_rate, amps));
    normalise_peak(x);
    return x;
}

inline std::vector<double> harmonic(double f0, double seconds, std::vector<double> amps = {}) {
    return harmonic_f([f0](double) { return f0; }, seconds, std::move(amps));
}

inline std::vector<double> white(double seconds, std::uint64_t seed) {
    return to_double(make_noise(seconds, k_probe_rate, 1.0, seed));
}

/// Band noise (zero-phase 2nd-order high-pass and low-pass), peak 1.
inline std::vector<double> band_noise(double seconds, double lo_hz, double hi_hz,
                                      std::uint64_t seed) {
    std::vector<double> x = white(seconds, seed);
    x = zero_phase_highpass(x, Hz{k_probe_rate}, Hz{lo_hz}).value();
    x = zero_phase_lowpass(x, Hz{k_probe_rate}, Hz{hi_hz}).value();
    normalise_peak(x);
    return x;
}

/// Pink noise (Kellet's filter on white noise), peak 1.
inline std::vector<double> pink(double seconds, std::uint64_t seed) {
    const std::vector<double> w = white(seconds, seed);
    std::vector<double> x(w.size());
    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        b0 = 0.99765 * b0 + w[i] * 0.0990460;
        b1 = 0.96300 * b1 + w[i] * 0.2965164;
        b2 = 0.57000 * b2 + w[i] * 1.0526913;
        x[i] = b0 + b1 + b2 + w[i] * 0.1848;
    }
    normalise_peak(x);
    return x;
}

/// Linear attack and release ramps, 1 in between.
inline std::vector<double> envelope(std::size_t n, double attack_s = 0.03,
                                    double release_s = 0.05) {
    std::vector<double> e(n, 1.0);
    const std::size_t a = count_of(attack_s);
    const std::size_t r = count_of(release_s);
    for (std::size_t i = 0; i < a && i < n; ++i) {
        e[i] = static_cast<double>(i) / static_cast<double>(a - 1);
    }
    for (std::size_t i = 0; i < r && i < n; ++i) {
        e[n - 1 - i] = std::min(e[n - 1 - i], static_cast<double>(i) / static_cast<double>(r - 1));
    }
    return e;
}

inline void place(std::vector<double>& buf, double start_s, const std::vector<double>& sig) {
    const std::size_t first = count_of(start_s);
    for (std::size_t i = 0; i < sig.size() && first + i < buf.size(); ++i) {
        buf[first + i] += sig[i];
    }
}

inline void add_scaled(std::vector<double>& buf, const std::vector<double>& sig, double gain) {
    for (std::size_t i = 0; i < buf.size() && i < sig.size(); ++i) {
        buf[i] += gain * sig[i];
    }
}

inline std::vector<double> scaled(std::vector<double> x, double gain) {
    for (double& v : x) {
        v *= gain;
    }
    return x;
}

inline std::vector<double> times(std::vector<double> x, const std::vector<double>& y) {
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) {
        x[i] *= y[i];
    }
    return x;
}

inline double rms_of(const std::vector<double>& x) {
    double s = 0.0;
    for (const double v : x) {
        s += v * v;
    }
    return std::sqrt(s / static_cast<double>(x.size()));
}

/// `x` scaled to RMS `level_db` dBFS.
inline std::vector<double> at_rms_db(std::vector<double> x, double level_db) {
    const double gain = db(level_db) / rms_of(x);
    return scaled(std::move(x), gain);
}

inline Probe finish(const std::vector<double>& buf, std::vector<ProbeRegion> regions) {
    Probe p;
    p.samples.assign(buf.begin(), buf.end());
    p.regions = std::move(regions);
    return p;
}

}  // namespace probe_detail

/// p1: a note, then a same-pitch exponential tail (-40 dB/s) down to -92 dB under -70 dB pink.
inline Probe probe_reverb_tail() {
    using namespace probe_detail;
    std::vector<double> buf(count_of(4.0), 0.0);
    place(buf, 0.5,
          scaled(times(harmonic(220.0, 1.0), envelope(count_of(1.0), 0.03, 0.01)), db(-12)));
    std::vector<double> tail = harmonic(220.0, 2.0);
    for (std::size_t i = 0; i < tail.size(); ++i) {
        const double t = static_cast<double>(i) / k_probe_rate;
        tail[i] *= db(-12) * std::exp(-t * std::numbers::ln10 * 80.0 / 20.0 / 2.0);
    }
    place(buf, 1.5, tail);
    add_scaled(buf, pink(4.0, 1), db(-70));
    return finish(
        buf,
        {{.from_s = 0.55, .to_s = 1.45, .expect = Expect::voiced_note, .what = "sung note A3"},
         {.from_s = 1.5,
          .to_s = 2.0,
          .expect = Expect::either,
          .what = "tail -12..-32 dB (audible decay)"},
         {.from_s = 2.5, .to_s = 3.5, .expect = Expect::unvoiced, .what = "tail below -52 dB"}});
}

/// p2: two notes over 50 Hz mains hum (-55 dBFS) and room noise.
inline Probe probe_hum() {
    using namespace probe_detail;
    std::vector<double> buf(count_of(4.0), 0.0);
    add_scaled(buf, harmonic(50.0, 4.0, {1, 0.6, 0.4, 0.3, 0.2}), db(-55));
    add_scaled(buf, pink(4.0, 2), db(-65));
    place(buf, 0.5, scaled(times(harmonic(330.0, 0.8), envelope(count_of(0.8))), db(-14)));
    place(buf, 2.8, scaled(times(harmonic(294.0, 0.8), envelope(count_of(0.8))), db(-14)));
    return finish(buf,
                  {{.from_s = 0.55, .to_s = 1.25, .expect = Expect::voiced_note, .what = "E4"},
                   {.from_s = 1.5,
                    .to_s = 2.6,
                    .expect = Expect::unvoiced,
                    .what = "50 Hz hum at -55 dBFS between notes"},
                   {.from_s = 2.85, .to_s = 3.55, .expect = Expect::voiced_note, .what = "D4"}});
}

/// p3: inhale, a note, a sibilant, a voiced 'z', and a breathy E4 (HNR about 3 dB).
inline Probe probe_breath_sibilant() {
    using namespace probe_detail;
    std::vector<double> buf(count_of(5.0), 0.0);
    add_scaled(buf, pink(5.0, 3), db(-70));
    std::vector<double> hann(count_of(0.45));
    for (std::size_t i = 0; i < hann.size(); ++i) {
        hann[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                       static_cast<double>(hann.size() - 1));
    }
    place(buf, 0.3, scaled(times(band_noise(0.45, 300, 4000, 4), hann), db(-30)));
    place(buf, 1.0, scaled(times(harmonic(262.0, 0.7), envelope(count_of(0.7))), db(-14)));
    place(buf, 2.0,
          scaled(times(band_noise(0.18, 4000, 11000, 5), envelope(count_of(0.18), 0.01, 0.02)),
                 db(-24)));
    std::vector<double> z = scaled(harmonic(140.0, 0.15, {1, 0.3, 0.1}), db(-36));
    add_scaled(z, band_noise(0.15, 3500, 9000, 6), db(-30));
    place(buf, 2.6, times(z, envelope(z.size(), 0.01, 0.02)));
    std::vector<double> breathy = scaled(harmonic(330.0, 0.8), db(-24));
    add_scaled(breathy, band_noise(0.8, 200, 8000, 7), db(-27));
    place(buf, 3.2, times(breathy, envelope(breathy.size())));
    return finish(
        buf, {{.from_s = 0.35,
               .to_s = 0.7,
               .expect = Expect::unvoiced,
               .what = "inhale (band noise -30 dB)"},
              {.from_s = 1.05, .to_s = 1.65, .expect = Expect::voiced_note, .what = "C4"},
              {.from_s = 2.02, .to_s = 2.16, .expect = Expect::unvoiced, .what = "sibilant 's'"},
              {.from_s = 2.62,
               .to_s = 2.73,
               .expect = Expect::voiced_not_note,
               .what = "voiced 'z', 150 ms"},
              {.from_s = 3.25,
               .to_s = 3.95,
               .expect = Expect::voiced_note,
               .what = "breathy E4 (HNR about 3 dB)"}});
}

/// p4: two notes over headphone bleed of an A minor triad at -48 dBFS.
inline Probe probe_bleed() {
    using namespace probe_detail;
    std::vector<double> buf(count_of(4.0), 0.0);
    std::vector<double> chord(count_of(4.0), 0.0);
    for (const double f : {220.0, 262.0, 330.0}) {
        add_scaled(chord, harmonic(f, 4.0, {1, 0.5, 0.25}), 1.0 / 3.0);
    }
    add_scaled(buf, chord, db(-48));
    add_scaled(buf, pink(4.0, 8), db(-68));
    place(buf, 0.4, scaled(times(harmonic(440.0, 0.9), envelope(count_of(0.9))), db(-14)));
    place(buf, 2.7, scaled(times(harmonic(392.0, 0.9), envelope(count_of(0.9))), db(-14)));
    return finish(buf,
                  {{.from_s = 0.45, .to_s = 1.25, .expect = Expect::voiced_note, .what = "A4"},
                   {.from_s = 1.5,
                    .to_s = 2.5,
                    .expect = Expect::unvoiced,
                    .what = "backing chord bleed at -48 dBFS"},
                   {.from_s = 2.75, .to_s = 3.55, .expect = Expect::voiced_note, .what = "G4"}});
}

/// p5: A3 after a 250-cent scoop with a fall-off, a 70 ms grace note, then C4.
inline Probe probe_contours() {
    using namespace probe_detail;
    std::vector<double> buf(count_of(3.5), 0.0);
    add_scaled(buf, pink(3.5, 9), db(-70));
    const auto cents = [](double t) {
        double c = -250.0 * std::exp(-t / 0.05);
        if (t > 0.75) {
            c -= 400.0 * std::pow((t - 0.75) / 0.15, 2.0);
        }
        return c;
    };
    std::vector<double> amps;
    for (int k = 1; k < 12; ++k) {
        amps.push_back(1.0 / k);
    }
    const std::vector<double> a3 =
        harmonic_f([&](double t) { return 220.0 * std::pow(2.0, cents(t) / 1200.0); }, 0.9, amps);
    place(buf, 0.3, scaled(times(a3, envelope(count_of(0.9), 0.02, 0.04)), db(-14)));
    place(buf, 1.6,
          scaled(times(harmonic(247.0, 0.07), envelope(count_of(0.07), 0.01, 0.01)), db(-16)));
    place(buf, 1.68, scaled(times(harmonic(262.0, 0.6), envelope(count_of(0.6))), db(-14)));
    return finish(
        buf,
        {{.from_s = 0.45,
          .to_s = 1.0,
          .expect = Expect::voiced_note,
          .what = "A3 after a 250-cent scoop (target 5700)"},
         {.from_s = 1.06, .to_s = 1.18, .expect = Expect::voiced, .what = "fall-off (release)"},
         {.from_s = 1.6, .to_s = 1.67, .expect = Expect::either, .what = "70 ms grace note B3"},
         {.from_s = 1.75, .to_s = 2.2, .expect = Expect::voiced_note, .what = "C4"}});
}

/// Sweep (a): E4 at -30 dBFS RMS with 200-8000 Hz breath noise at the given HNR; the note body is
/// 0.35-1.65 s.
inline std::vector<float> breathy_tone(double hnr_db) {
    using namespace probe_detail;
    const std::vector<double> tone = at_rms_db(harmonic(330.0, 1.5), -30.0);
    const std::vector<double> noise = at_rms_db(band_noise(1.5, 200, 8000, 10), -30.0 - hnr_db);
    std::vector<double> buf(count_of(2.0), 0.0);
    place(buf, 0.25, tone);
    place(buf, 0.25, noise);
    return {buf.begin(), buf.end()};
}

/// Sweep (b): 50 Hz hum (5 harmonics) at `level_db` RMS over -75 dBFS pink noise, 2 s.
inline std::vector<float> hum_alone(double level_db) {
    using namespace probe_detail;
    std::vector<double> buf = at_rms_db(harmonic(50.0, 2.0, {1, 0.6, 0.4, 0.3, 0.2}), level_db);
    add_scaled(buf, pink(2.0, 11), db(-75));
    return {buf.begin(), buf.end()};
}

/// Sweep (c): an A3 tone alone at `level_db` RMS, 0.25-1.25 s of 1.5 s.
inline std::vector<float> lone_tone(double level_db) {
    using namespace probe_detail;
    std::vector<double> buf(count_of(1.5), 0.0);
    place(buf, 0.25, at_rms_db(harmonic(220.0, 1.0), level_db));
    return {buf.begin(), buf.end()};
}

}  // namespace anno::test
