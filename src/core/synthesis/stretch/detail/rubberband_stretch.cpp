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
Planar run_rubberband(const Planar& in, std::size_t n_out, double rate_hz, std::size_t head,
                      std::size_t tail, std::size_t ramp) {
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
    // Slope 1 for the context and for `ramp` samples inside each end of the span: there the output
    // is nearly the source, so blending it into the source copy cannot cancel or add.
    const std::size_t span_in = n_in - head - tail;
    const std::size_t span_out = n_out - head - tail;
    stretcher.setKeyFrameMap({{0, 0},
                              {head + ramp, head + ramp},
                              {head + span_in - ramp, head + span_out - ramp},
                              {head + span_in, head + span_out},
                              {n_in, n_out}});
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

/// Gain of the stretched audio that keeps the level of a fade between two signals correlated by
/// `rho` (0..1) when the source copy has gain `a`: a^2 + b^2 + 2 rho a b = 1. rho = 0 is the
/// equal-power fade, rho = 1 the linear one (the sum of the gains is 1).
double stretch_gain_for(double a, double rho) {
    return -rho * a + std::sqrt(std::max(0.0, 1.0 - a * a * (1.0 - rho * rho)));
}

/// Normalised correlation of a[0..n) and b[0..n), floored at 0 and capped at 1.
double coherence(const std::vector<float>& a, const std::vector<float>& b, std::size_t a_at,
                 std::size_t b_at, std::size_t n) {
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto x = static_cast<double>(a[a_at + i]);
        const auto y = static_cast<double>(b[b_at + i]);
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    return std::clamp(ab / std::sqrt(aa * bb + 1e-30), 0.0, 1.0);
}

/// Blends the first and last `zone` samples into the source copy that continues the neighbouring
/// slope-1 audio: output frame i at the start is source frame i, output frame n_out - 1 - d at the
/// end is source frame n_in - 1 - d. The gains follow the correlation of the two signals over the
/// zone (so a stretched note that is nearly the source is not dipped or bumped by an equal-power
/// fade); at the edge sample the copy has gain 1.
void anchor_edges(const Planar& source, std::size_t zone, Planar& out) {
    const std::size_t n_out = out.front().size();
    const std::size_t n_in = source.front().size();
    for (std::size_t c = 0; c < out.size(); ++c) {
        const double rho_head = coherence(out[c], source[c], 0, 0, zone);
        const double rho_tail = coherence(out[c], source[c], n_out - zone, n_in - zone, zone);
        for (std::size_t d = 0; d < zone; ++d) {
            const double a = std::cos(0.5 * std::numbers::pi * static_cast<double>(d) /
                                      static_cast<double>(zone));
            float& head = out[c][d];
            head = static_cast<float>(stretch_gain_for(a, rho_head) * static_cast<double>(head) +
                                      a * static_cast<double>(source[c][d]));
            float& tail = out[c][n_out - 1 - d];
            tail = static_cast<float>(stretch_gain_for(a, rho_tail) * static_cast<double>(tail) +
                                      a * static_cast<double>(source[c][n_in - 1 - d]));
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
    const auto n_span = static_cast<std::size_t>(to - from);
    const auto context = static_cast<std::size_t>(std::llround(k_rubberband_context_s * g.rate_hz));
    // The span with `context` samples of the recording on each side (zero beyond its ends).
    auto source = read_channels(src, from - static_cast<std::int64_t>(context),
                                to + static_cast<std::int64_t>(context));
    if (!source) {
        return Unexpected{source.error()};
    }
    Planar span_source;
    for (const std::vector<float>& channel : *source) {
        span_source.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(context),
                                 channel.begin() + static_cast<std::ptrdiff_t>(context + n_span));
    }
    Planar out;
    if (to - from < k_min_rubberband_input) {
        for (const std::vector<float>& channel : span_source) {
            out.push_back(resample_linear(channel, n_out));
        }
    } else {
        const auto ramp = std::min<std::size_t>(
            static_cast<std::size_t>(std::llround(k_rubberband_edge_zone_s * g.rate_hz)),
            std::min(n_out, n_span) / 4);
        const Planar all =
            run_rubberband(*source, n_out + 2 * context, g.rate_hz, context, context, ramp);
        for (const std::vector<float>& channel : all) {
            out.emplace_back(channel.begin() + static_cast<std::ptrdiff_t>(context),
                             channel.begin() + static_cast<std::ptrdiff_t>(context + n_out));
        }
        anchor_edges(span_source, ramp, out);
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
