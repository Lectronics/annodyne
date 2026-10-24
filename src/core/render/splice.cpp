#include "core/render/splice.h"

#include "core/dsp/crossfade.h"

#include <algorithm>
#include <vector>

namespace anno {

namespace {

std::int64_t index_of(OutputTime t, Hz rate) {
    return to_sample_index(t, rate, Rounding::nearest).value();
}
std::int64_t index_of(SourceTime t, Hz rate) {
    return to_sample_index(t, rate, Rounding::nearest).value();
}

/// Output samples [lo, hi) of the cut at `join`: the source continued from `source_first`
/// crossfaded into the audio already in the block.
Result<void> splice_cut(const AudioSource& source, std::int64_t begin, AudioBuffer& block,
                        std::int64_t join, std::int64_t source_first, std::int64_t fade,
                        const SpliceParams& params) {
    const std::int64_t end = begin + block.frame_count().value();
    const std::int64_t lo = std::max(join, begin);
    const std::int64_t hi =
        std::min({join + fade, end, source.frame_count().value() - (source_first - join)});
    if (hi <= lo) {
        return {};
    }
    auto left = source.read(Samples{source_first + (lo - join)}, Samples{hi - lo});
    if (!left) {
        return Unexpected{left.error()};
    }
    for (int c = 0; c < block.channel_count(); ++c) {
        auto dest = block.channel(c).subspan(static_cast<std::size_t>(lo - begin),
                                             static_cast<std::size_t>(hi - lo));
        const std::vector<float> right(dest.begin(), dest.end());
        crossfade(left->channel(c), right, dest, params.shape, lo - join, fade);
    }
    return {};
}

/// Fades `block` samples [lo, hi) of every channel against zero; `to_zero` fades the audio out.
void fade_against_zero(AudioBuffer& block, std::int64_t begin, std::int64_t lo, std::int64_t hi,
                       std::int64_t ramp_begin, std::int64_t ramp_length, bool to_zero,
                       const SpliceParams& params) {
    const std::int64_t clipped_lo = std::max(lo, begin);
    const std::int64_t clipped_hi = std::min(hi, begin + block.frame_count().value());
    if (clipped_hi <= clipped_lo) {
        return;
    }
    const std::vector<float> zeros(static_cast<std::size_t>(clipped_hi - clipped_lo), 0.0F);
    for (int c = 0; c < block.channel_count(); ++c) {
        auto dest = block.channel(c).subspan(static_cast<std::size_t>(clipped_lo - begin),
                                             static_cast<std::size_t>(clipped_hi - clipped_lo));
        const std::vector<float> audio(dest.begin(), dest.end());
        if (to_zero) {
            crossfade(audio, zeros, dest, params.shape, clipped_lo - ramp_begin, ramp_length);
        } else {
            crossfade(zeros, audio, dest, params.shape, clipped_lo - ramp_begin, ramp_length);
        }
    }
}

/// The silence region of the span's gap that holds the insert, in samples; none if not found.
struct Room {
    std::int64_t left = 0;   // samples of silence before the insert point
    std::int64_t right = 0;  // after it
};

Room room_of(const TimeSpan& span, const SegmentTimeline* timeline, Hz rate) {
    if (timeline == nullptr || !span.segment) {
        return {};
    }
    for (const Gap& gap : timeline->gaps()) {
        if (!(gap.id == *span.segment)) {
            continue;
        }
        const std::int64_t at = index_of(span.source_start, rate);
        for (const GapRegion& r : gap.regions) {
            const std::int64_t a = index_of(r.start, rate);
            const std::int64_t b = index_of(r.end, rate);
            if (r.kind == GapRegionKind::silence && at >= a && at <= b) {
                return {.left = at - a, .right = b - at};
            }
        }
    }
    return {};
}

}  // namespace

Result<void> apply_splices(const AudioSource& source, const TimeMap& map,
                           const SegmentTimeline* timeline, std::int64_t begin, AudioBuffer& block,
                           const SpliceParams& params) {
    const Hz rate = source.sample_rate();
    if (block.sample_rate() != rate || block.channel_count() != source.channel_count()) {
        return fail(ErrorCode::internal, "apply_splices: the block does not match the source");
    }
    const std::int64_t fade_max = to_samples(params.splice_fade, rate, Rounding::nearest).value();
    if (fade_max < 1) {
        return {};  // a splice_fade of 0 keeps the hard joins (tests; the click detector flags it)
    }
    for (const TimeSpan& span : map.spans()) {
        if (span.kind == SpanKind::cut) {
            const std::int64_t first = index_of(span.source_start, rate);
            const std::int64_t length = index_of(span.source_end, rate) - first;
            const std::int64_t fade = std::min(fade_max, std::max<std::int64_t>(1, length));
            if (auto r = splice_cut(source, begin, block, index_of(span.output_start, rate), first,
                                    fade, params);
                !r) {
                return r;
            }
        } else if (span.kind == SpanKind::insert) {
            const Room room = room_of(span, timeline, rate);
            const std::int64_t from = index_of(span.output_start, rate);
            const std::int64_t to = index_of(span.output_end, rate);
            const std::int64_t before = std::min(fade_max, room.left);
            const std::int64_t after = std::min(fade_max, room.right);
            if (before > 0) {
                fade_against_zero(block, begin, from - before, from, from - before, before, true,
                                  params);
            }
            if (after > 0) {
                fade_against_zero(block, begin, to, to + after, to, after, false, params);
            }
        }
    }
    return {};
}

}  // namespace anno
