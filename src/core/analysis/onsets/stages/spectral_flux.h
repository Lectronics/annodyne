#pragma once

#include "core/analysis/onsets/stages/band_spectrogram.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Stages P2 (adaptive whitening), P3 (log compression) and D (SuperFlux).
// Stage conventions (all stages): STFT magnitudes are core/dsp's (periodic Hann, no 1/N scaling: a
// sine of amplitude A has |X| about A * n_fft / 4) of a float signal in [-1, 1]. A band value is a
// unit-sum weighted MEAN of magnitudes. The activation is the MEAN over bands of the positive log
// difference (natural log). Every function is pure, thread-safe and not real-time safe (it
// allocates); inputs are never written. Times are on the source axis, frame i centred at i * hop
// from the start of the analysed signal. Design: docs/design/phase5-transient-pipeline.md.

namespace anno {

/// One value per STFT frame; frame i is centred at i * hop. `hop` is the ACTUAL hop (whole samples
/// over the rate).
struct Activation {
    std::vector<double> values;
    Seconds hop;
};

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

struct LogParams {
    double log_gain = 1000.0;  // log(1 + log_gain * x), x in the units of the input (see the note
                               // on the whitened path)
};

/// log(1 + log_gain * x): monotone, 0 -> 0. Errors: invalid_argument for log_gain <= 0 or not
/// finite, an invalid spectrogram.
[[nodiscard]] Result<BandSpectrogram> log_compress(const BandSpectrogram& spec,
                                                   const LogParams& params);

struct FluxParams {
    Seconds lag{0.015};       // mu = max(1, round(lag / hop)) frames
    int max_filter_bins = 3;  // odd; 1 = plain spectral flux
};

/// SuperFlux (Bock & Widmer 2013): sum over bands of H(x(n,b) - max over |b'-b| <= (w-1)/2 of
/// x(n-mu, b')), divided by the number of bands; the first mu frames are 0. Errors:
/// invalid_argument for lag <= 0 or not finite, max_filter_bins even or < 1, an invalid
/// spectrogram.
[[nodiscard]] Result<Activation> superflux(const BandSpectrogram& spec, const FluxParams& params);

}  // namespace anno
