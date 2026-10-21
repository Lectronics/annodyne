#pragma once

#include "core/types/audio_buffer.h"
#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/units.h"

#include <vector>

// Mono mixes for analysis (design: docs/design/phase3-dsp.md). The source is never modified.

namespace anno {

/// A mono signal with its rate: what every analysis stage reads. Value type.
struct MonoAudio {
    std::vector<float> samples;
    Hz sample_rate;
};

/// Mean of the channels as float (antiphase channels cancel, which is correct for a mono mix).
/// Pure; thread-safe; not real-time safe (allocates).
[[nodiscard]] std::vector<float> mix_to_mono(const AudioBuffer& audio);

/// The whole source as a mono mix. Errors: those of AudioSource::read. An empty source gives an
/// empty signal (not an error). Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<MonoAudio> read_mono(const AudioSource& src);

}  // namespace anno
