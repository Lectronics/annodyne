#include "core/analysis/loudness/loudness.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_lufs_offset = -0.691;
constexpr double k_tiny = 1e-20;

Result<void> validate_inputs(const MonoAudio& plain, const MonoAudio& weighted,
                             const LoudnessParams& p) {
    if (plain.sample_rate != weighted.sample_rate ||
        plain.samples.size() != weighted.samples.size()) {
        return fail(ErrorCode::invalid_argument,
                    "measure_loudness: the signals differ in rate or length");
    }
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(p.amp_window.value()) || !positive(p.block.value()) ||
        !std::isfinite(p.absolute_gate.value()) || !std::isfinite(p.amp_floor.value()) ||
        !positive(plain.sample_rate.value())) {
        return fail(ErrorCode::invalid_argument, "measure_loudness: invalid parameters");
    }
    return {};
}

double mean_square(const std::vector<float>& x, std::int64_t lo, std::int64_t hi) {
    double sum = 0.0;
    for (std::int64_t i = lo; i < hi; ++i) {
        sum += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
    }
    return hi > lo ? sum / static_cast<double>(hi - lo) : 0.0;
}

double to_lufs(double ms) {
    return k_lufs_offset + 10.0 * std::log10(ms + k_tiny);
}

/// BS.1770 absolute-gated loudness of x[lo, hi) in blocks of at most `block` samples.
double gated_loudness(const std::vector<float>& x, std::int64_t lo, std::int64_t hi,
                      std::int64_t block, double gate) {
    double energy = 0.0;
    std::int64_t kept = 0;
    const std::int64_t blocks = std::max<std::int64_t>(1, (hi - lo + block - 1) / block);
    for (std::int64_t b = 0; b < blocks; ++b) {
        const std::int64_t from = lo + b * (hi - lo) / blocks;
        const std::int64_t to = lo + (b + 1) * (hi - lo) / blocks;
        const double ms = mean_square(x, from, to);
        if (to_lufs(ms) >= gate) {
            energy += ms * static_cast<double>(to - from);
            kept += to - from;
        }
    }
    return kept == 0 ? gate : to_lufs(energy / static_cast<double>(kept));
}

}  // namespace

Result<NoteLoudness> measure_loudness(const MonoAudio& plain, const MonoAudio& k_weighted,
                                      const NoteSpan& span, const FrameGrid& grid,
                                      const LoudnessParams& params) {
    if (auto r = validate_inputs(plain, k_weighted, params); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = validate(grid); !r) {
        return Unexpected{r.error()};
    }
    const double rate = plain.sample_rate.value();
    const auto size = static_cast<std::int64_t>(plain.samples.size());
    const auto sample_at = [&](SourceTime t) {
        return static_cast<std::int64_t>(std::llround((t - SourceTime{}).value() * rate));
    };
    const std::int64_t attack_end = sample_at(span.attack_end);
    const std::int64_t end = sample_at(span.source_end);
    if (attack_end < 0 || end > size || end <= attack_end) {
        return fail(ErrorCode::invalid_argument,
                    "measure_loudness: the note lies outside the signal (or is empty)");
    }
    NoteLoudness out;
    const auto block = static_cast<std::int64_t>(std::llround(params.block.value() * rate));
    out.loudness = LoudnessUnits{gated_loudness(k_weighted.samples, attack_end, end,
                                                std::max<std::int64_t>(1, block),
                                                params.absolute_gate.value())};
    const std::int64_t half =
        std::max<std::int64_t>(1, std::llround(params.amp_window.value() * rate / 2.0));
    out.amp.reserve(static_cast<std::size_t>(grid.n_frames));
    for (std::int64_t i = 0; i < grid.n_frames; ++i) {
        const std::int64_t centre = sample_at(frame_time(grid, i));
        const std::int64_t lo = std::clamp<std::int64_t>(centre - half, 0, size);
        const std::int64_t hi = std::clamp<std::int64_t>(centre + half, 0, size);
        const double db = 10.0 * std::log10(mean_square(plain.samples, lo, hi) + k_tiny);
        out.amp.emplace_back(std::max(db, params.amp_floor.value()));
    }
    return out;
}

}  // namespace anno
