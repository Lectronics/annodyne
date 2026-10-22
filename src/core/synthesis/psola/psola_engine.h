#pragma once

#include "core/synthesis/engine/synthesis_engine.h"
#include "core/types/frame_class_set.h"
#include "core/types/units.h"

#include <cstdint>
#include <string>
#include <string_view>

// PsolaEngine: the time-domain comparison engine for pitch edits (ROADMAP Phase 4; ADR-0016).
// Design: docs/design/phase4-pitch-edit.md (stage 6).

namespace anno {

struct PsolaEngineParams {
    // Frames of these classes are re-spaced; every other frame keeps the original samples (same
    // routing as HarmonicNoiseEngine, ADR-0017). The empty set shifts nothing.
    FrameClassSet shifted_classes{FrameClass::voiced, FrameClass::breathy};
    // Equal-power crossfade between the original and the re-spaced signal where a shifted run
    // meets an unshifted one inside a note; >= 0, finite.
    Seconds class_edge_fade{0.004};
    // |shift| above this returns not_implemented: PSOLA is not trustworthy for large shifts and
    // the caller falls back to the other engine. Finite, > 0. Provisional (listening gate #1).
    Cents max_shift{700.0};
    // Period-synchronous separation: the periodic part of a shifted run is the average of this many
    // aligned cycles on each side (Hann weights); only it is re-spaced, and the rest (noise,
    // breath, anything that does not repeat from cycle to cycle) stays where and as it was, so the
    // noise level and timing do not change. 0 re-spaces everything, noise included. 0..8.
    int separation_cycles{4};
};

/// Shifts the whole pitch curve of a note by eff.center - note.pitch_center cents with TD-PSOLA:
/// analysis marks one pitch period apart follow the note's stored pitch curve (centre + drift +
/// modulation) and are placed where the waveform best matches the previous mark's (fractional
/// samples); two-period Hann grains around the marks are overlap-added at synthesis marks one
/// shifted period apart, each synthesis mark taking the nearest analysis mark (no time stretching:
/// the timeline is untouched). Details that decide the output:
///  - The pitch curve is the note's stored one, centre + drift + modulation, interpolated linearly
///    in cents between frame centres and held beyond the first and last; the period is clamped to
///    40..2000 Hz.
///  - The first mark is the largest positive sample of one period; each next mark is one period on
///    is placed within +-0.2 period of that where the waveform of one period best matches the
///    previous mark's (normalised correlation, refined to a fraction of a sample), so every mark
///    sits on the same feature of the cycle; a peak pick jumped between features on real voices
///    and made audible hiccups. The window never widens.
///  - Source samples beyond the zone (up to three longest periods) are read from the source
///    through the time map; beyond the recording they count as zero. Nothing returns out_of_range
///    for them.
///  - Grains are read at fractional positions with Catmull-Rom interpolation, so synthesis marks
///    need not fall on samples. The overlap-add is divided by the sum of the grain windows (which
///    keeps the level across a shift); where that sum is below 1e-6 the original sample is kept.
///  - The result then has its mean over one shifted period taken out (no effect on harmonics of
///    the shifted pitch); without it a down shift leaves a DC offset.
/// Only the periodic part of a shifted frame is re-spaced (PsolaEngineParams::separation_cycles):
/// it is the average of aligned cycles; the remainder (noise, breath) stays where and as it was,
/// with a gain that keeps its power. With separation_cycles = 0 everything is re-spaced, noise
/// included, and the noise level drops by several dB. A run of shifted frames is crossfaded (equal
/// power, class_edge_fade) with the original at its inner edges; wherever the weight is 0 the
/// engine copies the read_through_map samples bit for bit.
///
/// Margin: the note's tracks cover only the note. When the first (last) frame is shifted the run
/// continues past the note with the edge frame's pitch held, for at most 0.1 s and inside the
/// zone, so the renderer's ramp (ADR-0006) is the only fade at the note's outer edge; further out,
/// and when the edge frame is not shifted, the margin is the original samples. The synthesis marks
/// of the margin are the held-pitch ones; where the source is not periodic at that pitch (a
/// neighbouring note) the result is not meaningful, which is why the renderer fades it.
///
/// Known limits (listening gate #1 judges them): shifting down by a large amount leaves gaps in
/// the grain overlap, so the level is modulated; the nearest-mark choice moves the re-spaced signal
/// by up to half a period against the original, so the crossfade at a class edge combs; the voice
/// level changes with the shift (-3 dB at +700 cents measured); whatever the cycle average does not
/// capture (fast changes) stays at the original pitch.
///
/// Supported: mono sources; effective values neutral except `center` and `onset_shift` (realised
/// by the TimeMap); a zone inside one slope-1 warp; an active note whose pitch curves and (when
/// present) noise-class track share the grid. A shift within k_neutral_snap_offset returns the
/// original samples exactly.
/// Errors, in the order they are checked: invalid_argument (bad params); not_implemented (stereo,
/// any other non-neutral edit, an inactive note, a note without pitch curves); invalid_argument (a
/// non-finite center, pitch curves or a present noise-class track that disagree with the note's
/// grid, a negative range); not_implemented (|shift| > max_shift; a zone that is not inside one
/// slope-1 warp of the time map, or whose shift is not a whole number of samples); out_of_range
/// (the range leaves the output axis). Const; thread-safe; not real-time safe (allocates).
class PsolaEngine final : public SynthesisEngine {
public:
    explicit PsolaEngine(PsolaEngineParams params = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "psola"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 2; }
    /// A fixed layout: a format tag, the class-set bits and the bit pattern of every double in hex.
    [[nodiscard]] std::string settings_key() const override;

    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override;

private:
    PsolaEngineParams params_;
};

}  // namespace anno
