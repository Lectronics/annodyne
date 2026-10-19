#include "core/synthesis/passthrough/passthrough_engine.h"

namespace anno {

Result<OutputAudio> PassthroughEngine::render_note(const AudioSource& src, const Note& /*note*/,
                                                   const EffectiveNote& /*eff*/,
                                                   const TimeMap& time_map,
                                                   OutputRange out_range) const {
    if (!time_map.is_identity()) {
        return fail(ErrorCode::not_implemented,
                    "passthrough engine cannot render a non-identity TimeMap (Phase 6)");
    }
    const Hz rate = src.sample_rate();
    const Samples first = to_sample_index(out_range.start(), rate, Rounding::nearest);
    const Samples last = to_sample_index(out_range.end(), rate, Rounding::nearest);
    auto audio = src.read(first, last - first);
    if (!audio) {
        return Unexpected{audio.error()};
    }
    return OutputAudio{.audio = std::move(*audio), .start = to_time_point<OutputAxis>(first, rate)};
}

}  // namespace anno
