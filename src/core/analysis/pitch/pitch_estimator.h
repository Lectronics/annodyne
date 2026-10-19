#pragma once

#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstdint>
#include <string_view>

// Pitch (f0) tracking (architecture spec §4.2 detect_pitch; implementation guide B1).
// Design: docs/design/phase3-pitch-onsets.md.

namespace anno {

/// Generic parameters of any estimator.
struct PitchParams {
    Hz min_f0{50.0};
    Hz max_f0{1600.0};
    Seconds hop{0.005};  // PitchTrack grid hop; frame i is analysed around time i * hop
    int n_alternates = 2;
};

/// Interface so a neural estimator (RMVPE / PESTO / CREPE, Phase 13) can replace the DSP one.
class PitchEstimator {
public:
    PitchEstimator() = default;
    PitchEstimator(const PitchEstimator&) = delete;
    PitchEstimator& operator=(const PitchEstimator&) = delete;
    virtual ~PitchEstimator();

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// Bump whenever the output for the same input changes.
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;

    /// PitchTrack on the source axis: grid.start = SourceTime{}, grid.hop = params.hop, n_frames =
    /// floor(duration / hop) + 1 (0 for an empty source). f0 is absolute cents (A4 = 6900), NaN
    /// when unvoiced; `alternates` holds params.n_alternates other paths (octave candidates) of the
    /// same length. Stereo sources are mixed to mono. Errors: invalid_argument (min_f0/max_f0/hop
    /// not finite and positive, min_f0 >= max_f0, n_alternates < 0, window shorter than two periods
    /// of min_f0, source rate below 2 x max_f0); anything the source read returns. Pure;
    /// thread-safe; not real-time safe.
    [[nodiscard]] virtual Result<PitchTrack> estimate(const AudioSource& src,
                                                      const PitchParams& params) const = 0;
};

/// Spec entry point: runs the default (pYIN) estimator with its default parameters.
[[nodiscard]] Result<PitchTrack> detect_pitch(const AudioSource& src, const PitchParams& params);

}  // namespace anno
