#pragma once

#include "core/synthesis/engine/synthesis_engine.h"

// PassthroughEngine: the "nothing to synthesise" engine (ROADMAP Phase 2).
// Design: docs/design/phase2-render-skeleton.md.

namespace anno {

/// Returns the original audio of the requested output range: the source samples as played through the
/// TimeMap (read_through_map: shifted slope-1 warps, zeros in inserts) and ignores `eff` by design.
class PassthroughEngine final : public SynthesisEngine {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "passthrough"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    [[nodiscard]] std::string settings_key() const override { return {}; }

    /// Errors: those of read_through_map (not_implemented for a warp that is not sample-aligned
    /// slope 1; out_of_range if the range leaves the output; invalid_argument if the map does not fit
    /// the source). Samples are bit-identical to the source samples the map names.
    /// Const; thread-safe; not real-time safe.
    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override;
};

}  // namespace anno
