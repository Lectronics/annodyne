#pragma once

#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The onset pipeline as testable stages (ROADMAP Phase 5; design:
// docs/design/phase5-transient-pipeline.md; research: docs/design/onset-detection-research.md):
//   P0 normalise_level -> P1 band_spectrogram -> [P2 adaptive_whiten] -> P3 log_compress
//   -> D superflux -> K pick_peaks_windowed -> L localise,
// composed by detect_transients. detect_onsets (onsets.h) is built from the same stages.
//
// Conventions. STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a sine of amplitude A
// has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a unit-sum weighted
// MEAN of magnitudes. The activation is the MEAN over bands of the positive log difference (natural
// log). Every function is pure, thread-safe and not real-time safe (it allocates); inputs are never
// written. Times are on the source axis, frame i centred at i * hop from the start of the analysed
// signal.

namespace anno {

/// One value per STFT frame; frame i is centred at i * hop. `hop` is the ACTUAL hop (whole samples
/// over the rate).
struct Activation {
    std::vector<double> values;
    Seconds hop;
};

/// A picked frame and its strength: the detector's output at the frame over its threshold, >= 1 for
/// a picked peak (both pickers).
struct ActivationPeak {
    std::size_t frame = 0;
    double strength = 0.0;
};

/// Frames x bands of non-negative values, frame-major.
struct BandSpectrogram {
    Seconds hop;                // the ACTUAL hop
    std::int64_t n_frames = 0;  // 0 when the signal is shorter than the window
    std::int64_t n_bands = 0;
    std::vector<Hz> centres;   // strictly increasing, one per band
    std::vector<double> data;  // n_frames * n_bands
};

/// Errors: invalid_argument if the counts are negative, centres.size() != n_bands, data.size() !=
/// n_frames * n_bands, the centres are not strictly increasing, or a value is negative or not
/// finite.
[[nodiscard]] Result<void> validate(const BandSpectrogram& spec);

// ---- P0 -------------------------------------------------------------------------------------

enum class LevelNorm { peak, rms_active };

struct LevelNormParams {
    LevelNorm mode = LevelNorm::rms_active;
    Decibels target{-23.0};  // peak mode: the peak in dBFS; rms mode: the RMS of the active frames
    Decibels active_floor{-60.0};  // 20 ms frames more than this below the loudest do not count
    Decibels max_gain{
        40.0};  // the largest gain applied: a very quiet file is not amplified without bound
};

struct NormalisedSignal {
    std::vector<float> samples;  // a scaled COPY
    Decibels gain{0.0};          // what was applied
};

/// A copy of `mono` scaled so a fixed detection threshold means the same at any recording level.
/// rms_active: the RMS is sqrt(mean of the squares) over the 20 ms frames within active_floor of
/// the loudest frame. Silence (or no active frame) is returned unchanged with gain 0 dB. The gain
/// is limited to max_gain (so a quiet file ends below the target). Errors: invalid_argument for a
/// non-finite sample, rate or parameter, or max_gain < 0. The source is never touched.
[[nodiscard]] Result<NormalisedSignal> normalise_level(std::span<const float> mono, Hz rate,
                                                       const LevelNormParams& params);

// ---- P1 -------------------------------------------------------------------------------------

struct BandParams {
    Seconds window{
        0.043};          // Hann window; n_fft = the power of two nearest to window * rate (>= 256)
    Seconds hop{0.005};  // rounded to whole samples; the result carries the actual hop
    int bands_per_octave = 24;  // log-spaced triangular filters, unit sum, centres on distinct bins
    Hz f_min{30.0};
    Hz f_max{16000.0};  // also limited to 0.45 of the rate; fewer than 4 bands fall back to a
                        // linear split
};

/// STFT magnitudes reduced to log-spaced bands (the Phase 3 / SuperFlux filterbank). A signal
/// shorter than n_fft, or an empty one, gives 0 frames (and the actual hop). Errors:
/// invalid_argument for a non-positive or non-finite window, hop, rate or f_min, f_min >= f_max,
/// bands_per_octave < 1.
[[nodiscard]] Result<BandSpectrogram> band_spectrogram(std::span<const float> mono, Hz rate,
                                                       const BandParams& params);

// ---- P2 -------------------------------------------------------------------------------------

struct WhitenParams {
    Seconds relax{0.5};             // the running maximum decays by exp(-hop / relax) per frame
    double floor_magnitude = 1e-4;  // on the STFT scale; provisional
};

/// Adaptive whitening of LINEAR band values (before P3): per band p(n) = max(x(n), decay * p(n-1),
/// floor), y(n) = x(n) / p(n); y is in (0, 1]. p starts at max(x(0), floor), so frame 0 reads 1 in
/// every band above the floor (the first `lag` activation frames ignore it). The decaying-maximum
/// form of Stowell & Plumbley / aubio; the research quotes Bock 2012 ("m = 10, r = 0.005") and that
/// form was NOT checked: provisional. Errors: invalid_argument for relax <= 0, floor <= 0, an
/// invalid spectrogram.
[[nodiscard]] Result<BandSpectrogram> adaptive_whiten(const BandSpectrogram& spec,
                                                      const WhitenParams& params);

// ---- P3 -------------------------------------------------------------------------------------

struct LogParams {
    double log_gain = 1000.0;  // log(1 + log_gain * x), x in the units of the input (see the note
                               // on the whitened path)
};

/// log(1 + log_gain * x): monotone, 0 -> 0. Errors: invalid_argument for log_gain <= 0 or not
/// finite, an invalid spectrogram.
[[nodiscard]] Result<BandSpectrogram> log_compress(const BandSpectrogram& spec,
                                                   const LogParams& params);

// ---- D --------------------------------------------------------------------------------------

struct FluxParams {
    Seconds lag{0.015};       // mu = max(1, round(lag / hop)) frames
    int max_filter_bins = 3;  // odd; 1 = plain spectral flux
};

/// SuperFlux (Bock & Widmer 2013): sum over bands of H(x(n,b) - max over |b'-b| <= (w-1)/2 of
/// x(n-mu, b')), divided by the number of bands; the first mu frames are 0. Errors:
/// invalid_argument for lag <= 0 or not finite, max_filter_bins even or < 1, an invalid
/// spectrogram.
[[nodiscard]] Result<Activation> superflux(const BandSpectrogram& spec, const FluxParams& params);

// ---- K --------------------------------------------------------------------------------------

struct WindowedPeakParams {
    Seconds pre_max{0.030};
    Seconds post_max{0.030};
    Seconds pre_avg{0.100};
    Seconds post_avg{0.070};
    Seconds min_gap{0.030};
    // An increment above the local mean, in activation units. Provisional and arbitrary (it is NOT
    // Phase 3's min_activation, which floors a threshold). Chosen on the bench of
    // tests/support/onset_bench.h (pooled F plateau 0.02-0.05 at log_gain 1, lag 15 ms) with the
    // signals it is scored on: optimistic.
    double delta_activation = 0.05;
};

struct PeakList {
    Seconds hop;
    std::vector<ActivationPeak> peaks;  // sorted by frame
};

/// The research's rule (Bock 2012; section 2.4), offline. With counts = round(seconds / hop), frame
/// n is a peak when (1) act(n) > 0, (2) act(n) is the maximum over [n - pre_max, n + post_max]
/// clamped to the signal, the FIRST frame winning a tie, (3) act(n) >= mean(act over [n - pre_avg,
/// n + post_avg]) + delta_activation, the mean taking 0 for frames outside the signal and dividing
/// by the full window width, and (4) n - previous kept peak > min_gap (first wins; the reference
/// compares with the previous raw detection). strength = act / max(mean + delta_activation, 1e-9).
/// Errors: invalid_argument for a negative window, a non-positive hop, a negative delta.
[[nodiscard]] Result<PeakList> pick_peaks_windowed(const Activation& activation,
                                                   const WindowedPeakParams& params);

// ---- L --------------------------------------------------------------------------------------

struct LocaliseParams {
    Seconds search{0.025};         // +- around the peak
    Seconds energy_window{0.002};  // short-window energy
    Decibels min_rise{6.0};        // a smaller rise leaves the frame time
    Decibels end_drop{
        3.0};  // louder at the start of the region than at its end by this: a note end
};

enum class LocaliseOutcome { rose, unchanged };

struct LocalisedOnset {
    std::int64_t sample = 0;
    SourceTime time;  // SourceTime{} + Seconds{sample / rate}
    double strength = 0.0;
    LocaliseOutcome outcome =
        LocaliseOutcome::unchanged;  // rose: the short-window energy really rose
};

/// Moves each peak to the sample where the short-window energy (energy_window, in dB) rises through
/// the halfway point between the quietest point before the peak and the loudest after it, searched
/// within +-search; a rise under min_rise leaves the frame's own sample (outcome unchanged). A peak
/// where the signal is louder at the start of the region than at its end by end_drop is a note END
/// (spectral flux responds to offsets) and is dropped. Sorted as the peaks. Errors:
/// invalid_argument for a non-positive search or window, a negative min_rise or end_drop, a peak
/// beyond the signal.
[[nodiscard]] Result<std::vector<LocalisedOnset>>
localise(std::span<const float> mono, Hz rate, const PeakList& peaks, const LocaliseParams& params);

// ---- composed -------------------------------------------------------------------------------

struct TransientParams {
    std::optional<LevelNormParams> level;  // P0 when set
    BandParams bands;
    std::optional<WhitenParams> whiten;  // P2 when set
    LogParams log{.log_gain = 1.0};      // the bench optimum (Phase 3's detect_onsets uses 1000); a
                                         // whitened path needs its own value
    FluxParams flux;
    WindowedPeakParams peaks;
    LocaliseParams localise;
};

/// P0 (if set) -> P1 -> P2 (if set) -> P3 -> D -> K -> L on the mono mix of `src`. Output: kind
/// attack, strength from K, sorted by time. An empty or too short source gives an empty list.
/// Errors: those of the stages.
[[nodiscard]] Result<OnsetList> detect_transients(const AudioSource& src,
                                                  const TransientParams& params);

}  // namespace anno
