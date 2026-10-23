#include "core/synthesis/rubberband_pitch/rubberband_pitch_engine.h"

#include "core/synthesis/engine/engine_support.h"
#include "core/synthesis/engine/source_through_map.h"
#include "rubberband/RubberBandStretcher.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <vector>

namespace anno {

namespace {

constexpr std::size_t k_block = 2048;  // frames per study/process call

struct SilentLogger final : RubberBand::RubberBandStretcher::Logger {
    void log(const char* /*message*/) override {}
    void log(const char* /*message*/, double /*a*/) override {}
    void log(const char* /*message*/, double /*a*/, double /*b*/) override {}
};

Result<void> check_params(const RubberBandPitchParams& p) {
    if (!std::isfinite(p.context.value()) || p.context.value() < 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "RubberBandPitchEngine: context must be finite and >= 0");
    }
    if (!std::isfinite(p.max_shift.value()) || !(p.max_shift.value() > 0.0)) {
        return fail(ErrorCode::invalid_argument,
                    "RubberBandPitchEngine: max_shift must be finite and > 0");
    }
    if (p.formants != PitchFormants::preserved && p.formants != PitchFormants::shifted) {
        return fail(ErrorCode::invalid_argument, "RubberBandPitchEngine: unknown formants mode");
    }
    return {};
}

/// `in` (one vector per channel) through R3 with the pitch scale and time ratio 1, exactly as long.
std::vector<std::vector<float>> shift(const std::vector<std::vector<float>>& in, double rate_hz,
                                      double pitch_scale, PitchFormants formants) {
    namespace rb = RubberBand;
    const std::size_t n = in.front().size();
    const std::size_t channels = in.size();
    // The library's options are combined with |: upstream's API.
    // NOLINTBEGIN(bugprone-signed-bitwise)
    rb::RubberBandStretcher::Options options = rb::RubberBandStretcher::OptionProcessOffline |
                                               rb::RubberBandStretcher::OptionEngineFiner |
                                               rb::RubberBandStretcher::OptionThreadingNever |
                                               rb::RubberBandStretcher::OptionChannelsTogether;
    if (formants == PitchFormants::preserved) {
        options = options | rb::RubberBandStretcher::OptionFormantPreserved;
    }
    // NOLINTEND(bugprone-signed-bitwise)
    rb::RubberBandStretcher stretcher(static_cast<std::size_t>(std::llround(rate_hz)), channels,
                                      std::make_shared<SilentLogger>(), options, 1.0, pitch_scale);
    stretcher.setDebugLevel(0);
    stretcher.setExpectedInputDuration(n);
    std::vector<const float*> input(channels);
    const auto point_at = [&](std::size_t at) {
        for (std::size_t c = 0; c < channels; ++c) {
            input[c] = in[c].data() + at;
        }
    };
    for (std::size_t at = 0; at < n; at += k_block) {
        point_at(at);
        const std::size_t count = std::min(k_block, n - at);
        stretcher.study(input.data(), count, at + count >= n);
    }
    std::vector<std::vector<float>> out(channels);
    std::vector<std::vector<float>> chunks(channels);
    std::vector<float*> chunk_pointers(channels);
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
    for (std::size_t at = 0; at < n; at += k_block) {
        point_at(at);
        const std::size_t count = std::min(k_block, n - at);
        stretcher.process(input.data(), count, at + count >= n);
        drain();
    }
    drain();
    for (std::vector<float>& channel : out) {
        channel.resize(n, 0.0F);
    }
    return out;
}

}  // namespace

std::string RubberBandPitchEngine::settings_key() const {
    return std::string{"formants="} + (params_.formants == PitchFormants::preserved ? "p" : "s") +
           ";context=" + hex64(std::bit_cast<std::uint64_t>(params_.context.value())) +
           ";max=" + hex64(std::bit_cast<std::uint64_t>(params_.max_shift.value()));
}

Result<OutputAudio> RubberBandPitchEngine::render_note(const AudioSource& src, const Note& note,
                                                       const EffectiveNote& eff,
                                                       const TimeMap& time_map,
                                                       OutputRange out_range) const {
    if (auto ok = check_params(params_); !ok) {
        return Unexpected{ok.error()};
    }
    const auto no = [](const char* what) {
        return fail(ErrorCode::not_implemented, std::string{"RubberBandPitchEngine: "} + what);
    };
    if (!only_pitch_edited(eff)) {
        return no("only a pitch offset (and a timing shift through the time map) is realised");
    }
    if (!note.is_active) {
        return no("the note is inactive");
    }
    if (!std::isfinite(eff.center.value())) {
        return fail(ErrorCode::invalid_argument,
                    "RubberBandPitchEngine: the effective center is not finite");
    }
    const Hz rate = src.sample_rate();
    const Samples first = to_sample_index(out_range.start(), rate, Rounding::nearest);
    const Samples last = to_sample_index(out_range.end(), rate, Rounding::nearest);
    if (last.value() < first.value()) {
        return fail(ErrorCode::invalid_argument, "RubberBandPitchEngine: a negative range");
    }
    const double shift_cents = (eff.center - note.pitch_center).value();
    if (std::abs(shift_cents) > params_.max_shift.value()) {
        return no("the shift is larger than max_shift");
    }
    if (auto shift_samples = zone_shift(time_map, rate, out_range, "RubberBandPitchEngine");
        !shift_samples) {
        return Unexpected{shift_samples.error()};
    }
    const auto context =
        static_cast<std::int64_t>(std::ceil(params_.context.value() * rate.value()));
    const Samples total =
        to_sample_index(OutputTime{} + time_map.output_length(), rate, Rounding::nearest);
    const std::int64_t ext_first = std::max<std::int64_t>(first.value() - context, 0);
    const std::int64_t ext_last = std::min<std::int64_t>(last.value() + context, total.value());
    auto ext = read_through_map(src, time_map, Samples{ext_first}, Samples{ext_last - ext_first});
    if (!ext) {
        return Unexpected{ext.error()};
    }
    auto result = AudioBuffer::create(src.channel_count(), last - first, rate);
    if (!result) {
        return Unexpected{result.error()};
    }
    const auto offset = static_cast<std::ptrdiff_t>(first.value() - ext_first);
    const bool neutral = std::abs(shift_cents) <= k_neutral_snap_offset;
    std::vector<std::vector<float>> shifted;
    if (!neutral && last > first) {
        std::vector<std::vector<float>> in;
        in.reserve(static_cast<std::size_t>(ext->channel_count()));
        for (int c = 0; c < ext->channel_count(); ++c) {
            in.emplace_back(ext->channel(c).begin(), ext->channel(c).end());
        }
        shifted = shift(in, rate.value(), std::exp2(shift_cents / 1200.0), params_.formants);
    }
    for (int c = 0; c < result->channel_count(); ++c) {
        auto out = result->channel(c);
        if (neutral || last == first) {
            std::ranges::copy_n(ext->channel(c).begin() + offset, (last - first).value(),
                                out.begin());
        } else {
            std::ranges::copy_n(shifted[static_cast<std::size_t>(c)].begin() + offset,
                                (last - first).value(), out.begin());
        }
    }
    return OutputAudio{.audio = std::move(*result),
                       .start = to_time_point<OutputAxis>(first, rate)};
}

}  // namespace anno
