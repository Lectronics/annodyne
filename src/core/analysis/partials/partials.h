#pragma once

#include "core/types/frame_grid.h"
#include "core/types/note_curves.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <span>

// Harmonic (partial) analysis of one note (architecture spec §4.2 analyse_partials; implementation
// guide B5; ADR-0016). Design: docs/design/phase4-pitch-edit.md.

namespace anno {

struct PartialsParams {
    int max_harmonics = 60;  // 1..200; also capped per note by 0.45 * rate / lowest f0
    // Analysis window length in periods of the frame's f0 (dimensionless), 4..8. The window is
    // 4-term Blackman-Harris: its main lobe reaches the neighbouring harmonic at exactly four
    // periods, and its sidelobes (-92 dB) keep leakage under 0.2 cent (Hann leaks 3.8 cent).
    double window_periods = 4.0;
    double fft_pad_factor = 5.0;         // zero-padding for QIFFT (dimensionless), >= 4 (spec B5)
    Cents search_halfwidth{40.0};        // sieve window around k * f0, 10..100
    Hz a4_reference = k_a4_standard_hz;  // pitch curves are absolute cents against this A4
};

/// Harmonic tracks of the note described by `curves`, measured in `mono`: the source's mono mix,
/// unmodified, starting at SourceTime 0 (sample index = round(t * rate)); samples outside the
/// signal count as zero. Frame i is centred at frame_time(grid, i). For each voiced frame the
/// window is window_periods periods of f0(i) long (Blackman-Harris), zero-padded by fft_pad_factor,
/// and harmonic k is the largest spectral peak within search_halfwidth of k * f0(i) (at least one
/// bin each side), refined by quadratic interpolation of the log magnitude (QIFFT). Output, frames
/// x K with K = min(max_harmonics, floor(0.45 * rate / lowest f0 of the note)):
///   freq_hz     measured frequency (Hz); the nominal k * f0 where nothing was measured
///   amp_linear  linear amplitude of the sinusoid; 0 for unvoiced frames, harmonics above
///               0.45 * rate, and frames whose window leaves the signal entirely
///   phase_rad   phase of the cosine at the frame centre: x(t_i) is about sum_k amp_k *
///   cos(phase_k)
/// Errors: invalid_argument (bad params or rate, empty signal, invalid grid, drift/mod not
/// grid.n_frames long, no voiced frame in the note). Pure; thread-safe; not real-time safe
/// (allocates). Cost: one FFT per voiced frame.
[[nodiscard]] Result<HarmonicTrack> analyse_partials(std::span<const float> mono, Hz sample_rate,
                                                     const NoteCurves& curves,
                                                     const PartialsParams& params);

}  // namespace anno
