#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <cstdint>
#include <span>
#include <vector>

// The level of an edited note: gain, fades and within-note flattening as one gain curve applied to
// the note's zone after the pitch engine (ADR-0028; design: docs/design/phase6-level.md).

namespace anno {

/// A flattening correction never exceeds this, up or down (a loud passage next to a near-silent
/// frame must not be boosted into noise).
inline constexpr Decibels k_max_flatten_db{12.0};
/// Frames this far below the note's mean level are not corrected (silence between phonemes).
inline constexpr Decibels k_flatten_floor_below_mean_db{30.0};
/// Level in dB at the start of a `log` fade (silence below it).
inline constexpr Decibels k_log_fade_floor_db{-60.0};

/// The zone a gain curve is built for: absolute output sample indices.
struct LevelZone {
    std::int64_t first = 0;       // first sample of the zone
    std::int64_t count = 0;       // samples in the zone
    std::int64_t note_first = 0;  // the note's first output sample
    std::int64_t note_last = 0;   // one past the note's last output sample
    Hz rate{44100.0};
    std::int64_t shift_samples = 0;  // output sample n plays source sample n - shift_samples
};

/// True if gain, flatten, a fade or the sibilant, breath or plosive noise gain is non-neutral. (The
/// voiced noise gain is the engine's.)
[[nodiscard]] bool has_level_edit(const EffectiveNote& eff) noexcept;

/// `eff` with the level fields neutral (what the pitch engine is shown).
[[nodiscard]] EffectiveNote without_level(EffectiveNote eff);

/// Linear gain for each sample of `zone`:
///  gain = 10^(eff.gain/20) (0 at or below -120 dB, a mute) x fade x flatten.
///  - fade: from the note's first sample, `fade_in` seconds long, 0 -> 1 by `fade_shape` (linear:
///  x;
///    equal_power: sin(pi x / 2); log: 60 dB over the fade); from `fade_out` seconds before its
///    last sample, 1 -> 0 mirrored. If the two fades together exceed the note both shrink in
///    proportion. Samples before the note hold the gain of its first sample and samples after it
///    that of its last (the renderer's ramp (ADR-0006) is the fade at the zone's outer edge).
///  - flatten: dB correction (mean - level) x within_note_flatten per sample, where level is the
///    note's `amp` (dBFS per frame, linear in dB between frame centres) and mean its mean over the
///    frames above mean - k_flatten_floor_below_mean_db; clamped to +-k_max_flatten_db; frames
///    below the floor are left alone. A note without amp frames is not flattened.
///  - class gains: the noise gain (eff.noise_gain) of the frames whose class is sibilant, breath or
///    plosive (note.noise.frame_class on note.noise.grid), linear in dB between frame centres, so a
///    class change is a ramp of one hop. Those frames are noise, so a gain on the frame is a gain
///    on the noise. Other classes are not touched.
/// Errors: invalid_argument (count < 0, rate not > 0, note_last < note_first, a non-finite gain,
/// amp and grid disagree). Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] Result<std::vector<float>> level_gain(const Note& note, const EffectiveNote& eff,
                                                    const LevelZone& zone);

/// Multiplies every channel of `audio` by `gain` (one entry per frame).
/// Errors: invalid_argument if gain.size() != audio.frame_count().
[[nodiscard]] Result<void> apply_gain(AudioBuffer& audio, std::span<const float> gain);

}  // namespace anno
