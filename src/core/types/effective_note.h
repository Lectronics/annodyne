#pragma once

#include "core/types/edit.h"
#include "core/types/ids.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <vector>

// EffectiveNote: a note's analysed values after folding its edits (architecture spec §5.3).
// Produced by effective_state() in core/edits.
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// Gain of the noise part per frame class. FrameClass::silence has no parameter (no audio to gain);
/// breathy and rough use the voiced gain (ADR-0017).
struct NoiseGains {
    Decibels voiced;
    Decibels sibilant;
    Decibels breath;
    Decibels plosive;

    friend bool operator==(const NoiseGains&, const NoiseGains&) = default;
};

/// Gain for a frame class; 0 dB for silence. Pure; real-time safe.
[[nodiscard]] Decibels noise_gain(const NoiseGains& gains, FrameClass frame_class) noexcept;

/// Values are final: clamped to their legal ranges, and float dust within the neutral snap
/// tolerance is exactly neutral (so an edit sequence that cancels out leaves is_edited == false).
struct EffectiveNote {
    NoteId note_id;

    Cents center{};        // analysed pitch_center + folded pitch offset, absolute
    Ratio mod_scale{1.0};  // 1 = original, 0 = flat, -1 = inverted
    Ratio drift_scale{1.0};
    Ratio transition_in_scale{1.0};
    Ratio transition_out_scale{1.0};
    Cents formant_offset{};
    Decibels gain{};
    Ratio within_note_flatten{};               // 0..1
    Seconds fade_in{};                         // >= 0
    Seconds fade_out{};                        // >= 0
    FadeShape fade_shape = FadeShape::linear;  // neutral value when both fades are 0
    NoiseGains noise_gain{};
    Seconds onset_shift{};          // output position of the note start relative to its source
    Ratio length_scale{1.0};        // >= k_min_time_scale
    std::vector<TimePair> handles;  // sorted by source
    Ratio attack_time_scale{1.0};   // >= k_min_time_scale

    /// False: the renderer passes the original audio through, bit-exact (CLAUDE.md rule 5).
    bool is_edited = false;

    friend bool operator==(const EffectiveNote&, const EffectiveNote&) = default;
};

/// |offset| at or below this (in the offset's own unit: cents, dB or seconds) is snapped to 0 by
/// the fold (floating-point dust from +a +b -(a+b)).
inline constexpr double k_neutral_snap_offset = 1e-9;
/// A multiplier within this of 1 is snapped to 1 by the fold.
inline constexpr double k_neutral_snap_ratio = 1e-12;

}  // namespace anno
