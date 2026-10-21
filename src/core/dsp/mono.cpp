#include "core/dsp/mono.h"

namespace anno {

std::vector<float> mix_to_mono(const AudioBuffer& audio) {
    const auto frames = static_cast<std::size_t>(audio.frame_count().value());
    std::vector<float> mono(frames, 0.0F);
    const auto channels = audio.channel_count();
    for (int c = 0; c < channels; ++c) {
        const auto samples = audio.channel(c);
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] += samples[i];
        }
    }
    const float scale = 1.0F / static_cast<float>(channels);
    if (channels > 1) {
        for (float& v : mono) {
            v *= scale;
        }
    }
    return mono;
}

Result<MonoAudio> read_mono(const AudioSource& src) {
    if (src.frame_count().value() == 0) {
        return MonoAudio{.samples = {}, .sample_rate = src.sample_rate()};
    }
    auto audio = src.read(Samples{0}, src.frame_count());
    if (!audio) {
        return Unexpected{audio.error()};
    }
    return MonoAudio{.samples = mix_to_mono(*audio), .sample_rate = src.sample_rate()};
}

}  // namespace anno
