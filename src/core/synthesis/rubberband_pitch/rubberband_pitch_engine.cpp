#include "core/synthesis/rubberband_pitch/rubberband_pitch_engine.h"

#include "core/synthesis/engine/engine_support.h"
#include "core/synthesis/engine/source_through_map.h"
#include "core/types/frame_grid.h"
#include "rubberband/RubberBandLiveShifter.h"
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
    if (!std::isfinite(p.curve_gain.value()) || !(p.curve_gain.value() > 0.0)) {
        return fail(ErrorCode::invalid_argument,
                    "RubberBandPitchEngine: curve_gain must be finite and > 0");
    }
    if (!std::isfinite(p.curve_lead.value())) {
        return fail(ErrorCode::invalid_argument,
                    "RubberBandPitchEngine: curve_lead must be finite");
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

struct SilentShifterLogger final : RubberBand::RubberBandLiveShifter::Logger {
    void log(const char* /*message*/) override {}
    void log(const char* /*message*/, double /*a*/) override {}
    void log(const char* /*message*/, double /*a*/, double /*b*/) override {}
};

/// Pitch change in cents at each of `count` samples starting at output sample `first`: the static
/// centre offset plus (mod_scale - 1) x the note's modulation and (drift_scale - 1) x its drift,
/// both read from the note's frames (linear between frame centres, zero outside the note).
std::vector<double> curve_cents(const Note& note, const EffectiveNote& eff, double offset_cents,
                                std::int64_t first, std::int64_t count, double rate_hz,
                                std::int64_t shift_samples, double gain) {
    std::vector<double> out(static_cast<std::size_t>(count), offset_cents);
    const double mod_extra = (eff.mod_scale.value() - 1.0) * gain;
    const double drift_extra = (eff.drift_scale.value() - 1.0) * gain;
    if ((mod_extra == 0.0 && drift_extra == 0.0) || note.grid.n_frames < 2 ||
        note.pitch_mod.size() != static_cast<std::size_t>(note.grid.n_frames) ||
        note.pitch_drift.size() != static_cast<std::size_t>(note.grid.n_frames)) {
        return out;
    }
    const double hop = note.grid.hop.value();
    const double grid_start = (note.grid.start - SourceTime{}).value();
    const auto last_frame = static_cast<double>(note.grid.n_frames - 1);
    for (std::int64_t i = 0; i < count; ++i) {
        const double source_s = static_cast<double>(first + i - shift_samples) / rate_hz;
        const double position = (source_s - grid_start) / hop;
        if (position < 0.0 || position > last_frame) {
            continue;
        }
        const auto k = static_cast<std::size_t>(std::min(std::floor(position), last_frame - 1.0));
        const double w = position - static_cast<double>(k);
        const double mod =
            (1.0 - w) * note.pitch_mod[k].value() + w * note.pitch_mod[k + 1].value();
        const double drift =
            (1.0 - w) * note.pitch_drift[k].value() + w * note.pitch_drift[k + 1].value();
        out[static_cast<std::size_t>(i)] += mod_extra * mod + drift_extra * drift;
    }
    return out;
}

/// `in` through the live shifter with the pitch change `cents[n]` at input sample n (the scale of
/// each block is the curve at the block's centre, advanced by `lead_samples`), the start
/// delay discarded and a tail of zeros flushed, so the result is exactly as long as `in`.
std::vector<std::vector<float>> shift_curve(const std::vector<std::vector<float>>& in,
                                            double rate_hz, const std::vector<double>& cents,
                                            PitchFormants formants, std::int64_t lead_samples) {
    namespace rb = RubberBand;
    const std::size_t channels = in.size();
    const std::size_t n = in.front().size();
    // NOLINTBEGIN(bugprone-signed-bitwise)
    rb::RubberBandLiveShifter::Options options = rb::RubberBandLiveShifter::OptionWindowShort |
                                                 rb::RubberBandLiveShifter::OptionChannelsTogether;
    if (formants == PitchFormants::preserved) {
        options = options | rb::RubberBandLiveShifter::OptionFormantPreserved;
    }
    // NOLINTEND(bugprone-signed-bitwise)
    rb::RubberBandLiveShifter shifter(static_cast<std::size_t>(std::llround(rate_hz)), channels,
                                      std::make_shared<SilentShifterLogger>(), options);
    shifter.setDebugLevel(0);
    const std::size_t block = shifter.getBlockSize();
    shifter.setPitchScale(std::exp2(cents.front() / 1200.0));
    const std::size_t delay = shifter.getStartDelay();
    const std::size_t total = n + delay;
    const std::size_t blocks = (total + block - 1) / block;
    std::vector<std::vector<float>> padded(channels, std::vector<float>(blocks * block, 0.0F));
    std::vector<std::vector<float>> out(channels, std::vector<float>(blocks * block, 0.0F));
    for (std::size_t c = 0; c < channels; ++c) {
        std::ranges::copy(in[c], padded[c].begin());
    }
    std::vector<const float*> input(channels);
    std::vector<float*> output(channels);
    for (std::size_t k = 0; k < blocks; ++k) {
        const auto wanted = static_cast<std::int64_t>(k * block + block / 2) + lead_samples;
        const auto centre = static_cast<std::size_t>(
            std::clamp<std::int64_t>(wanted, 0, static_cast<std::int64_t>(n) - 1));
        shifter.setPitchScale(std::exp2(cents[centre] / 1200.0));
        for (std::size_t c = 0; c < channels; ++c) {
            input[c] = padded[c].data() + k * block;
            output[c] = out[c].data() + k * block;
        }
        shifter.shift(input.data(), output.data());
    }
    std::vector<std::vector<float>> result(channels);
    for (std::size_t c = 0; c < channels; ++c) {
        result[c].assign(out[c].begin() + static_cast<std::ptrdiff_t>(delay),
                         out[c].begin() + static_cast<std::ptrdiff_t>(delay + n));
    }
    return result;
}

}  // namespace

std::string RubberBandPitchEngine::settings_key() const {
    return std::string{"formants="} + (params_.formants == PitchFormants::preserved ? "p" : "s") +
           ";context=" + hex64(std::bit_cast<std::uint64_t>(params_.context.value())) +
           ";max=" + hex64(std::bit_cast<std::uint64_t>(params_.max_shift.value())) +
           ";lead=" + hex64(std::bit_cast<std::uint64_t>(params_.curve_lead.value())) +
           ";gain=" + hex64(std::bit_cast<std::uint64_t>(params_.curve_gain.value()));
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
    EffectiveNote pitch_only = eff;
    pitch_only.mod_scale = Ratio{1.0};
    pitch_only.drift_scale = Ratio{1.0};
    if (!only_pitch_edited(pitch_only)) {
        return no(
            "only a pitch offset, the modulation and drift scales (and a timing shift through the "
            "time map) are realised");
    }
    if (!std::isfinite(eff.mod_scale.value()) || !std::isfinite(eff.drift_scale.value())) {
        return fail(ErrorCode::invalid_argument, "RubberBandPitchEngine: a scale is not finite");
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
    auto shift_samples = zone_shift(time_map, rate, out_range, "RubberBandPitchEngine");
    if (!shift_samples) {
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
    const bool curved = eff.mod_scale.value() != 1.0 || eff.drift_scale.value() != 1.0;
    const bool neutral = std::abs(shift_cents) <= k_neutral_snap_offset && !curved;
    std::vector<std::vector<float>> shifted;
    if (!neutral && last > first) {
        std::vector<std::vector<float>> in;
        in.reserve(static_cast<std::size_t>(ext->channel_count()));
        for (int c = 0; c < ext->channel_count(); ++c) {
            in.emplace_back(ext->channel(c).begin(), ext->channel(c).end());
        }
        shifted =
            curved
                ? shift_curve(in, rate.value(),
                              curve_cents(note, eff, shift_cents, ext_first, ext_last - ext_first,
                                          rate.value(), *shift_samples, params_.curve_gain.value()),
                              params_.formants,
                              std::llround(params_.curve_lead.value() * rate.value()))
                : shift(in, rate.value(), std::exp2(shift_cents / 1200.0), params_.formants);
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
