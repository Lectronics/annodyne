#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// SynthesisEngine: renders one edited note (architecture spec §4.5, ADR-0002, ADR-0006).
// Design: docs/design/phase2-render-skeleton.md.

namespace anno {

/// A touching neighbour of an edited note (its gap to this note is at most k_touching_gap): the
/// pitch offset it ends up with and whether an engine renders it (a pitch-edited note) or the base
/// audio plays there (an unedited, timing-only or level-only note; offset 0 then).
struct NeighbourOffset {
    Cents offset;
    bool rendered = false;

    friend bool operator==(const NeighbourOffset&, const NeighbourOffset&) = default;
};

/// The notes touching an edited note, so a pitch engine can glide between offsets instead of
/// stepping at the boundary (ADR-0030). Empty: no touching neighbour on that side.
struct TouchingNotes {
    std::optional<NeighbourOffset> previous;
    std::optional<NeighbourOffset> next;

    friend bool operator==(const TouchingNotes&, const TouchingNotes&) = default;
};

/// Notes closer than this (end to start, seconds) count as touching.
inline constexpr Seconds k_touching_gap{0.030};

class SynthesisEngine {
public:
    SynthesisEngine() = default;
    SynthesisEngine(const SynthesisEngine&) = delete;
    SynthesisEngine& operator=(const SynthesisEngine&) = delete;
    virtual ~SynthesisEngine();

    /// Stable engine name, e.g. "passthrough". Part of the render cache key.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// Bump whenever the engine's output for the same inputs changes. Part of the cache key.
    [[nodiscard]] virtual std::uint32_t version() const noexcept = 0;
    /// Bytes that change when the engine's settings change; may be empty. Part of the cache key.
    [[nodiscard]] virtual std::string settings_key() const = 0;

    /// The edited audio of `note` over `out_range` (output axis). The renderer always passes the
    /// whole blend zone of the note (its span widened by the crossfade length, clamped to the
    /// recording) and slices the result itself, so an engine needs no sub-range consistency. The
    /// edit is applied as if the note extended over the whole range. The result's range equals
    /// `out_range` to the sample, with the source's channel count and sample rate.
    /// `time_map` is the track's map (ADR-0008); `out_range` is on its output axis. Over a slope-1
    /// part of the map an engine must give the samples read_through_map gives (the "original").
    /// Errors: not_implemented if the engine cannot realise `eff` or `time_map`; invalid_argument
    /// for a bad range; out_of_range from the source.
    /// Const and thread-safe; not real-time safe (allocates).
    [[nodiscard]] virtual Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                          const EffectiveNote& eff,
                                                          const TimeMap& time_map,
                                                          OutputRange out_range) const = 0;

    /// render_note with the touching neighbours of `note` (ADR-0030). The default ignores the
    /// context; an engine that can follow a pitch curve through a boundary overrides it. The
    /// renderer always calls this one. Same contract and errors as render_note.
    [[nodiscard]] virtual Result<OutputAudio>
    render_note_in_context(const AudioSource& src, const Note& note, const EffectiveNote& eff,
                           const TimeMap& time_map, OutputRange out_range,
                           const TouchingNotes& /*context*/) const {
        return render_note(src, note, eff, time_map, out_range);
    }
};

}  // namespace anno
