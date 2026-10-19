#pragma once

#include "core/synthesis/engine/synthesis_engine.h"

// PassthroughEngine: the "nothing to synthesise" engine (ROADMAP Phase 2).
// Design: docs/design/phase2-render-skeleton.md.

namespace anno {

/// Returns the source samples of the requested range unchanged. Requires an identity TimeMap and
/// ignores `eff` by design.
class PassthroughEngine final : public SynthesisEngine {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "passthrough"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    [[nodiscard]] std::string settings_key() const override { return {}; }

    /// Errors: not_implemented if !time_map.is_identity(); out_of_range if the range leaves the
    /// recording or starts before the origin. Samples are bit-identical to the source.
    /// Const; thread-safe; not real-time safe.
    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override;
};

}  // namespace anno
