#pragma once

#include "core/types/note_curves.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

// Spectral envelope (formants) of one note by True Envelope (architecture spec §4.2
// estimate_envelope; implementation guide B7; Röbel & Rodet, DAFx-2005). ADR-0016 item 2 uses it to
// re-read harmonic amplitudes after a pitch shift. Design: docs/design/phase4-pitch-edit.md.

namespace anno {

struct EnvelopeParams {
    // Cepstral order = floor(order_scale * rate / (2 * highest f0 of the note)) (IRCAM rule: the
    // order must stay below rate / (2 * max f0)); dimensionless, 0.25..1.
    double order_scale = 1.0;
    int max_iterations = 50;  // True Envelope loop, 5..200
    // Stop when the envelope is within this of every harmonic peak (it never goes below a peak by
    // more than this).
    Decibels tolerance{0.2};
    int n_bins = 513;  // frequency points (43 Hz at 44.1 kHz), 0..rate/2 inclusive, 65..2049
    // Level of a frame with no measured harmonic (an unvoiced frame).
    Decibels floor{-120.0};
};

/// The smooth spectral envelope through the harmonic peaks of each frame. `harmonics` is the note's
/// HarmonicTrack (frames on curves.grid); only harmonics with amp_linear > 0 count. Per frame: the
/// peak levels (20 log10 amp) are linearly interpolated in frequency (held flat below the first and
/// above the last measured harmonic), then raised by the True Envelope iteration (cepstral
/// low-pass, A <- max(A, V)) until the envelope is within `tolerance` of every peak or
/// max_iterations is reached. Output: log_mag_db = frames x n_bins dB, freqs = n_bins values
/// 0..rate/2 (Hz), a frame without harmonics is `floor` throughout. Errors: invalid_argument (bad
/// params or rate, harmonics not on curves.grid, no harmonic frame at all). Pure; thread-safe; not
/// real-time safe. Cost: 2 FFTs of 2 * (n_bins - 1) per iteration per voiced frame.
[[nodiscard]] Result<EnvelopeTrack> estimate_envelope(const NoteCurves& curves,
                                                      const HarmonicTrack& harmonics,
                                                      Hz sample_rate, const EnvelopeParams& params);

}  // namespace anno
