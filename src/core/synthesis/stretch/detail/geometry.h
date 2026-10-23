#pragma once

#include <cstdint>

// The sample geometry of one stretched warp span (ADR-0023): which output samples it covers and how
// output positions map to source positions. Raw doubles are allowed here (ADR-0003): kernel code in
// samples.

namespace anno::stretch_detail {

struct SpanGeometry {
    double rate_hz = 0.0;
    double slope = 1.0;      // output length / source length
    double out_start = 0.0;  // output_start * rate, fractional output samples
    double src_start = 0.0;  // source_start * rate, fractional source samples
    std::int64_t first = 0;  // first output sample of the span (round-to-nearest of out_start)
    std::int64_t last = 0;   // one past the last output sample of the span

    /// Source position (fractional samples) played at output position `out_pos`.
    [[nodiscard]] double source_at(double out_pos) const {
        return src_start + (out_pos - out_start) / slope;
    }
    /// Output position (fractional samples) at which source position `src_pos` is played.
    [[nodiscard]] double output_at(double src_pos) const {
        return out_start + (src_pos - src_start) * slope;
    }
};

}  // namespace anno::stretch_detail
