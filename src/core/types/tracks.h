#pragma once

#include "core/types/frame_grid.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Frame-based and event analysis tracks (architecture spec §3). Plain data; the producing
// analysis stages arrive in later phases. Units are in the type or the field name.
// Design: docs/design/phase1-data-model.md.

namespace anno {

enum class FrameClass { voiced, sibilant, breath, plosive, silence };
enum class OnsetKind { attack, consonant, pitch_change, phoneme };

/// Estimated fundamental per frame.
struct PitchTrack {
    FrameGrid grid;
    std::vector<Cents> f0;                       // absolute (A4 = 6900); NaN when unvoiced
    std::vector<double> confidence_prob;         // 0..1
    std::vector<double> voicing_prob;            // 0..1
    std::vector<std::vector<Cents>> alternates;  // 2nd/3rd best paths, each grid.n_frames long
};

/// Detected onsets, parallel arrays of equal length.
struct OnsetList {
    std::vector<SourceTime> times;
    std::vector<double> strength;  // dimensionless detector output, >= 0; scale set by the detector
    std::vector<OnsetKind> kind;
};

/// Harmonics of one note: matrices are frames x K (harmonic k = column k - 1).
struct HarmonicTrack {
    FrameGrid grid;
    FrameMatrix freq_hz;     // measured frequency of each harmonic
    FrameMatrix amp_linear;  // linear amplitude
    FrameMatrix phase_rad;   // radians, for phase-coherent resynthesis
};

/// Noise residual of one note.
struct NoiseTrack {
    FrameGrid grid;
    FrameMatrix band_energy_linear;  // frames x bands
    std::vector<FrameClass> frame_class;
};

/// Smooth spectral envelope of one note.
struct EnvelopeTrack {
    FrameGrid grid;
    FrameMatrix log_mag_db;  // frames x freqs.size(), magnitude in dB
    std::vector<Hz> freqs;
};

/// Each validate checks the grid and that every array/matrix has grid.n_frames rows/entries (an
/// empty track, n_frames == 0 with empty arrays, is valid; NoiseTrack::frame_class may also be
/// empty when the band matrix has rows), and probabilities lie in 0..1.
/// Errors: invalid_argument. Pure; thread-safe.
[[nodiscard]] Result<void> validate(const PitchTrack& track);
[[nodiscard]] Result<void> validate(const OnsetList& onsets);
[[nodiscard]] Result<void> validate(const HarmonicTrack& track);
[[nodiscard]] Result<void> validate(const NoiseTrack& track);
[[nodiscard]] Result<void> validate(const EnvelopeTrack& track);

}  // namespace anno
