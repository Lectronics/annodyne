#pragma once

#include "core/synthesis/engine/synthesis_engine.h"
#include "core/types/units.h"

#include <cstdint>
#include <string>
#include <string_view>

// RubberBandPitchEngine: pitch edits by Rubber Band R3's pitch scaling (ADR-0027). A candidate for
// the pitch-edit engine decision of Phase 6/7, built to A/B against PsolaEngine and
// HarmonicNoiseEngine; reuse of tested code (CLAUDE.md rule 9). Design:
// docs/design/phase6-rubberband-pitch.md.

namespace anno {

enum class PitchFormants : std::uint8_t {
    preserved,  // the spectral envelope stays where it was (the voice keeps its vowel)
    shifted     // the envelope moves with the pitch (a resampled voice)
};

/// How a constant formant offset is made: the offline stretcher (standard window) or the live
/// shifter (short window, the path of the time-varying curves). ADR-0031 asks for an A/B by ear;
/// the objective measures slightly favour the live shifter (fewer waveform-cycle dips).
enum class FormantPath : std::uint8_t { offline, live };

struct RubberBandPitchParams {
    FormantPath formant_path = FormantPath::offline;
    PitchFormants formants = PitchFormants::preserved;
    Seconds context{0.100};   // source read beyond each end of the zone so the stretcher's start-up
                              // and tail lie outside it; >= 0, finite
    Cents max_shift{2400.0};  // |shift| above this is not_implemented; finite, > 0
    Ratio curve_gain{1.5};    // the live shifter follows a changing pitch scale with a gain below 1
                            // (about 0.67 for a 5.5 Hz vibrato, measured); the modulation and drift
                            // terms are multiplied by this to compensate; > 0, finite
    Seconds glide_time{0.040};   // the pitch offset moves between two touching notes over this long
                                 // (raised cosine, centred on the boundary); >= 0, finite
    Seconds curve_lead{-0.030};  // a time-varying pitch curve is read this far ahead of the content
                                 // (the live shifter applies a scale late); >= 0, finite
};

/// Shifts the whole zone of a note by eff.center - note.pitch_center cents with Rubber Band R3
/// (offline, one thread, channels together, time ratio 1): the zone's samples through the time map
/// plus `context` on each side go in, the middle `zone` samples come out. Everything in the zone is
/// shifted, including unvoiced parts and the margin beyond the note (the renderer fades the note's
/// outer edge, ADR-0006). A shift within k_neutral_snap_offset returns the original samples
/// exactly. Any channel count.
/// With `mod_scale` or `drift_scale` other than 1 the pitch change varies through the zone: the
/// centre offset plus (mod_scale - 1) x the note's modulation and (drift_scale - 1) x its drift
/// (cents, per analysis frame, zero outside the note), applied by Rubber Band's live shifter in
/// blocks (its pitch scale changes per block, so a 5 ms-hop curve is followed at the block rate).
/// Supported: effective values neutral except `center`, `onset_shift`, `mod_scale` and
/// `drift_scale`; an active note; a zone inside one slope-1 warp with a whole-sample shift.
/// Errors: invalid_argument (bad params, a negative range, a non-finite center); not_implemented
/// (any other edit, an inactive note, a shift above max_shift, a zone that is not slope 1); those
/// of the source read.
/// Const and thread-safe (one stretcher per call); not real-time safe (allocates).
class RubberBandPitchEngine final : public SynthesisEngine {
public:
    explicit RubberBandPitchEngine(RubberBandPitchParams params = {}) : params_{params} {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rubberband"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 3; }
    [[nodiscard]] std::string settings_key() const override;
    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override;
    /// With a touching neighbour whose offset differs, the offset glides across the boundary
    /// instead of stepping (ADR-0030): both notes follow one curve, so their zones agree where
    /// they overlap. Without one this is render_note.
    [[nodiscard]] Result<OutputAudio>
    render_note_in_context(const AudioSource& src, const Note& note, const EffectiveNote& eff,
                           const TimeMap& time_map, OutputRange out_range,
                           const TouchingNotes& context) const override;

private:
    RubberBandPitchParams params_;
};

}  // namespace anno
