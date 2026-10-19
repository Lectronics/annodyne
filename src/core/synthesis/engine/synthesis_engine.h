#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"
#include "core/types/result.h"
#include "core/types/time_map.h"
#include "core/types/time_range.h"

#include <cstdint>
#include <string>
#include <string_view>

// SynthesisEngine: renders one edited note (architecture spec §4.5, ADR-0002, ADR-0006).
// Design: docs/design/phase2-render-skeleton.md.

namespace anno {

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
    /// Errors: not_implemented if the engine cannot realise `eff` or `time_map`; invalid_argument
    /// for a bad range; out_of_range from the source.
    /// Const and thread-safe; not real-time safe (allocates).
    [[nodiscard]] virtual Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                          const EffectiveNote& eff,
                                                          const TimeMap& time_map,
                                                          OutputRange out_range) const = 0;
};

}  // namespace anno
