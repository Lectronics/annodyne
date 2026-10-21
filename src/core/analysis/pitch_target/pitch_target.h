#pragma once

#include "core/analysis/perceived_pitch/perceived_pitch.h"
#include "core/tuning/style_preset.h"
#include "core/types/note.h"
#include "core/types/pitch_target.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <optional>
#include <span>

// The pitch a note is judged by: level and moving segments, kind, target, approach/release labels
// and flags (ADR-0013). Design: docs/design/note-pitch-target.md (sections 3, 11, 12, 13).

namespace anno {

struct ReliabilityParams {
    Seconds consonant_zone{0.150};  // frames this soon after a consonant onset count less
    double min_reliability = 0.2;   // frames below this are ignored for segments and the target
    Seconds creak_min_stretch{0.030};
    Cents creak_tolerance{50.0};     // half/double the note's median f0 within this = creak
    double unpitched_voicing = 0.3;  // median voicing probability below this: no pitch
};

struct SegmentingParams {
    Seconds segment_smoothing{0.050};  // moving mean of the curve before it is split
    Cents turn_threshold{35.0};        // a straight line may deviate this much inside a segment
    double slope_merge_cents_per_s = 150.0;
    Cents plateau_merge{30.0};  // adjacent level segments this close are one
    Cents min_glide_extent{40.0};
    double glissando_g =
        0.32;  // moving if |delta| > g * 100 / T (T seconds); owner decision 2026-10-20
    Seconds transient_window{
        0.120};  // note-end zones where any change >= turn_threshold is a transient
    // The glissando threshold uses T up to this long: a slow drift over a long note is a sag, not a
    // glide (g * 100 / T would otherwise fall to 32 cents for a 1 s note).
    Seconds glissando_max_duration{0.400};
    // A change this large that is faster than transient_window is a step between plateaus (too
    // fast to hear as a glide), which separates two cores (kind `multi`).
    Cents step_min_extent{60.0};
    double zone_max_share = 0.2;  // a transient zone covers at most this share of a short note
};

/// Take-level flags (design note §3 step 7, §12 S3/S4).
struct TakeParams {
    Seconds phrase_gap{0.35};    // silence this long between notes ends a phrase
    Seconds run_max_note{0.25};  // a run is notes at most this long ...
    int run_min_notes = 3;       // ... at least this many in a row
    Seconds run_max_gap{0.05};   // ... with at most this much between them
    int off_grid_min_count = 3;  // same pitch class off the grid by a similar amount
    Cents off_grid_spread{15.0};
    Cents off_grid_min_offset{15.0};  // after the take's own offset is removed
    Cents peak_tolerance{20.0};       // the phrase's highest note sharp by up to this
    Cents peak_min_offset{5.0};
    int peak_min_phrase_notes = 3;
};

struct OrnamentParams {
    Cents min_extent{80.0};            // distance between the two levels of a trill / mordent
    double flat_top_share = 0.35;      // share of each plateau's frames that are stationary
    double still_cents_per_s = 400.0;  // |slope| below this is stationary (a vibrato never is)
    Seconds min_plateau{0.040};        // a stretch on one level counts from this long
    bool detect_gamaka = false;        // style `gamaka`: dense turning points without a level part
    int gamaka_min_segments = 4;       // ... at least this many segments and no level core
    int min_transitions = 2;           // A-B-A is a mordent, A-B-A-B a trill
};

struct TargetParams {
    // gamma 0: the stability factor is off (on the Vocadito notes it lowered the share of targets
    // within -10/+15 cents from 0.60 to 0.51); the loudness term of the weights stays.
    StabilityWeightParams weights{
        .rate_scale_cents_per_s = 600.0, .gamma = 0.0, .amp_range = Decibels{30.0}};
    double end_bias = 0.0;    // weight rises by this much over the core (0.3 in the research; off:
                              // it lowered the same share)
    Seconds min_core{0.080};  // shorter cores lower the confidence
    Seconds core_floor{0.040};  // below this a level stretch is no core: the note is a glide
    Seconds short_note{0.120};
    Seconds glide_min_duration{0.250};  // shorter moving notes are judged as a whole
    Cents multi_min_separation{60.0};
    Cents overshoot_min{25.0};  // past the target by this much and back = overshoot
    Cents preparation_max{60.0};
    Cents preparation_min{20.0};  // a dip at least this deep, opposite to the next interval
    Seconds preparation_window{0.080};
    Cents ambiguous_low{40.0};        // a target this far (low..high) from the nearest semitone
    Cents ambiguous_high{60.0};       // is between two grid lines: `ambiguous_intent`
    Cents register_break_min{300.0};  // a step this large between two plateaus is a register break
    Seconds register_break_max_step{0.120};
    Seconds legato_gap{0.020};  // legato/preparation need the neighbour this close
    Cents legato_match{50.0};   // approach starts within this of the previous note = legato
};

struct PitchTargetParams {
    ReliabilityParams reliability;
    SegmentingParams segmenting;
    TargetParams target;
    OrnamentParams ornament;
    TakeParams take;
    Cents tuning_offset{0.0};  // grid offset from A4 = 440 Hz (cents); 0 until Phase 8 estimates it
};

/// Errors: invalid_argument if any parameter of any group is not finite or out of its range
/// (durations and thresholds positive, probabilities in 0..1, counts >= 1, weights.gamma >= 0,
/// preparation_min < preparation_max, zone_max_share <= 0.5, peak_min_offset < peak_tolerance).
/// Pure; thread-safe.
[[nodiscard]] Result<void> validate(const PitchTargetParams& params);

/// Parameters for a style preset: the glissando constant and gamaka detection come from
/// style_defaults(); everything else keeps its default. Pure.
[[nodiscard]] PitchTargetParams pitch_target_params_for(StylePreset preset);

/// What the target is computed from besides the note's own curves.
struct NoteEvidence {
    const PitchTrack* pitch = nullptr;       // voicing and confidence; its frames cover the note
    const OnsetList* onsets = nullptr;       // consonant onsets lower the weight of nearby frames
    std::span<const double> disagreement{};  // per note frame, 0..1 (Phase 13 hook); empty = none
};

/// The note's neighbours (pitch_center of the previous / next note, if any) and the silent time
/// between them; labels such as legato and preparation need a neighbour that touches.
struct NoteContext {
    std::optional<Cents> previous_pitch;
    std::optional<Cents> next_pitch;
    Seconds gap_before{1.0};
    Seconds gap_after{1.0};
};

/// Decides how the note's pitch is judged and returns the target (design note §3). The curve is
/// pitch_center + pitch_drift + pitch_mod on note.grid (absolute cents, A4 = 6900); voicing comes
/// from `evidence.pitch` at the same frames. A note with too little evidence is a valid result
/// (`unpitched`, or `whole` with `short_note` and low confidence), never an error. The note's own
/// pitch_target is ignored. Errors: invalid_argument (bad params, the note's curves not one per
/// grid frame, no evidence pitch track). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<PitchTarget> estimate_pitch_target(const Note& note,
                                                        const NoteEvidence& evidence,
                                                        const NoteContext& context,
                                                        const PitchTargetParams& params);

}  // namespace anno
