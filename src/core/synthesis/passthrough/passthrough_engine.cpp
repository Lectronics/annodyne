#include "core/synthesis/passthrough/passthrough_engine.h"

#include "core/synthesis/engine/source_through_map.h"

namespace anno {

Result<OutputAudio> PassthroughEngine::render_note(const AudioSource& src, const Note& /*note*/,
                                                   const EffectiveNote& /*eff*/,
                                                   const TimeMap& time_map,
                                                   OutputRange out_range) const {
    const Hz rate = src.sample_rate();
    const Samples first = to_sample_index(out_range.start(), rate, Rounding::nearest);
    const Samples last = to_sample_index(out_range.end(), rate, Rounding::nearest);
    auto audio = read_through_map(src, time_map, first, last - first);
    if (!audio) {
        return Unexpected{audio.error()};
    }
    return OutputAudio{.audio = std::move(*audio), .start = to_time_point<OutputAxis>(first, rate)};
}

}  // namespace anno
