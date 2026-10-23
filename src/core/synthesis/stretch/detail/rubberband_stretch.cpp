#include "core/synthesis/stretch/detail/rubberband_stretch.h"

#include "rubberband/RubberBandStretcher.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace anno::stretch_detail {

namespace {

using Planar = std::vector<std::vector<float>>;  // [channel][frame]

constexpr std::size_t k_block = 2048;  // frames per study/process call

/// Source samples [from, to) of every channel, zero where the recording does not reach.
Result<Planar> read_channels(const AudioSource& src, std::int64_t from, std::int64_t to) {
    const std::int64_t frames = src.frame_count().value();
    const auto channels = static_cast<std::size_t>(src.channel_count());
    Planar out(channels, std::vector<float>(static_cast<std::size_t>(to - from), 0.0F));
    const std::int64_t lo = std::clamp<std::int64_t>(from, 0, frames);
    const std::int64_t hi = std::clamp<std::int64_t>(to, lo, frames);
    if (hi > lo) {
        auto audio = src.read(Samples{lo}, Samples{hi - lo});
        if (!audio) {
            return Unexpected{audio.error()};
        }
        for (std::size_t c = 0; c < channels; ++c) {
            std::ranges::copy(audio->channel(static_cast<int>(c)),
                              out[c].begin() + static_cast<std::ptrdiff_t>(lo - from));
        }
    }
    return out;
}

/// Linear-interpolating resample of `in` to `n_out` samples (spans too short for a stretcher).
std::vector<float> resample_linear(const std::vector<float>& in, std::size_t n_out) {
    std::vector<float> out(n_out, 0.0F);
    const double step = static_cast<double>(in.size()) / static_cast<double>(n_out);
    for (std::size_t i = 0; i < n_out; ++i) {
        const double pos = static_cast<double>(i) * step;
        const auto k = static_cast<std::size_t>(pos);
        const double w = pos - static_cast<double>(k);
        const float a = in[std::min(k, in.size() - 1)];
        const float b = in[std::min(k + 1, in.size() - 1)];
        out[i] =
            static_cast<float>((1.0 - w) * static_cast<double>(a) + w * static_cast<double>(b));
    }
    return out;
}

/// `in` through Rubber Band R3 (offline, one thread, no pitch change), cut or zero-padded to
/// exactly `n_out` frames.
Planar run_rubberband(const Planar& in, std::size_t n_out, double rate_hz) {
    namespace rb = RubberBand;
    const std::size_t n_in = in.front().size();
    const std::size_t channels = in.size();
    // The library's options are combined with |: upstream's API.
    // NOLINTBEGIN(bugprone-signed-bitwise)
    const rb::RubberBandStretcher::Options options =
        rb::RubberBandStretcher::OptionProcessOffline | rb::RubberBandStretcher::OptionEngineFiner |
        rb::RubberBandStretcher::OptionThreadingNever |
        rb::RubberBandStretcher::OptionChannelsTogether;
    // NOLINTEND(bugprone-signed-bitwise)
    rb::RubberBandStretcher stretcher(static_cast<std::size_t>(std::llround(rate_hz)), channels,
                                      options,
                                      static_cast<double>(n_out) / static_cast<double>(n_in), 1.0);
    stretcher.setDebugLevel(0);
    stretcher.setExpectedInputDuration(n_in);
    std::vector<const float*> input(channels);
    const auto point_at = [&](std::size_t at) {
        for (std::size_t c = 0; c < channels; ++c) {
            input[c] = in[c].data() + at;
        }
    };
    for (std::size_t at = 0; at < n_in; at += k_block) {
        point_at(at);
        const std::size_t count = std::min(k_block, n_in - at);
        stretcher.study(input.data(), count, at + count >= n_in);
    }
    Planar out(channels);
    std::vector<float*> chunk_pointers(channels);
    std::vector<std::vector<float>> chunks(channels);
    const auto drain = [&] {
        for (int available = stretcher.available(); available > 0;
             available = stretcher.available()) {
            const auto count = static_cast<std::size_t>(available);
            for (std::size_t c = 0; c < channels; ++c) {
                chunks[c].resize(count);
                chunk_pointers[c] = chunks[c].data();
            }
            const std::size_t got = stretcher.retrieve(chunk_pointers.data(), count);
            for (std::size_t c = 0; c < channels; ++c) {
                out[c].insert(out[c].end(), chunks[c].begin(),
                              chunks[c].begin() + static_cast<std::ptrdiff_t>(got));
            }
        }
    };
    for (std::size_t at = 0; at < n_in; at += k_block) {
        point_at(at);
        const std::size_t count = std::min(k_block, n_in - at);
        stretcher.process(input.data(), count, at + count >= n_in);
        drain();
    }
    drain();
    for (std::vector<float>& channel : out) {
        channel.resize(n_out, 0.0F);
    }
    return out;
}

/// Blends the first and last `zone` samples (equal power) into the source copy that continues the
/// neighbouring slope-1 audio: output frame i at the start is source frame i, output frame
/// n_out - 1 - d at the end is source frame n_in - 1 - d.
void anchor_edges(const Planar& source, std::size_t zone, Planar& out) {
    const std::size_t n_out = out.front().size();
    const std::size_t n_in = source.front().size();
    for (std::size_t c = 0; c < out.size(); ++c) {
        for (std::size_t d = 0; d < zone; ++d) {
            const double angle =
                0.5 * std::numbers::pi * static_cast<double>(d) / static_cast<double>(zone);
            const double copy_gain = std::cos(angle);
            const double stretch_gain = std::sin(angle);
            float& head = out[c][d];
            head = static_cast<float>(stretch_gain * static_cast<double>(head) +
                                      copy_gain * static_cast<double>(source[c][d]));
            float& tail = out[c][n_out - 1 - d];
            tail = static_cast<float>(stretch_gain * static_cast<double>(tail) +
                                      copy_gain * static_cast<double>(source[c][n_in - 1 - d]));
        }
    }
}

}  // namespace

Result<OutputAudio> rubberband_span(const AudioSource& src, const SpanGeometry& g,
                                    std::pair<std::int64_t, std::int64_t> wanted,
                                    OutputTime start) {
    const std::int64_t from = std::llround(g.src_start);
    const std::int64_t to =
        std::max(from + 1,
                 static_cast<std::int64_t>(std::llround(g.source_at(static_cast<double>(g.last)))));
    const auto n_out = static_cast<std::size_t>(g.last - g.first);
    auto source = read_channels(src, from, to);
    if (!source) {
        return Unexpected{source.error()};
    }
    Planar out;
    if (to - from < k_min_rubberband_input) {
        for (const std::vector<float>& channel : *source) {
            out.push_back(resample_linear(channel, n_out));
        }
    } else {
        out = run_rubberband(*source, n_out, g.rate_hz);
        const auto zone = std::min<std::size_t>(
            static_cast<std::size_t>(std::llround(k_rubberband_edge_zone_s * g.rate_hz)),
            std::min(n_out, source->front().size()) / 3);
        anchor_edges(*source, zone, out);
    }
    auto buffer = AudioBuffer::create(src.channel_count(), Samples{wanted.second - wanted.first},
                                      src.sample_rate());
    if (!buffer) {
        return Unexpected{buffer.error()};
    }
    for (int c = 0; c < src.channel_count(); ++c) {
        const auto& channel = out[static_cast<std::size_t>(c)];
        std::ranges::copy(channel.begin() + (wanted.first - g.first),
                          channel.begin() + (wanted.second - g.first), buffer->channel(c).begin());
    }
    return OutputAudio{.audio = std::move(*buffer), .start = start};
}

}  // namespace anno::stretch_detail
