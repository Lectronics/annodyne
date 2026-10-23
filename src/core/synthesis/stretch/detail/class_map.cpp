#include "core/synthesis/stretch/detail/class_map.h"

#include <algorithm>
#include <cmath>

namespace anno::stretch_detail {

namespace {

double raw_seconds(SourceTime t) {
    return (t - SourceTime{}).value();
}

/// The active note holding source time `t_s`, or nullptr. Notes are sorted by start (mono).
const Note* note_at(const AnalysisResult& analysis, double t_s) {
    const auto it =
        std::ranges::upper_bound(analysis.notes, t_s, std::ranges::less{},
                                 [](const Note& n) { return raw_seconds(n.source_start); });
    for (auto back = it; back != analysis.notes.begin();) {
        --back;
        if (!back->is_active) {
            continue;
        }
        return t_s < raw_seconds(back->source_end) ? &*back : nullptr;
    }
    return nullptr;
}

std::int64_t frame_index(const Note& note, double t_s) {
    const double hop = note.grid.hop.value();
    const double rel = (t_s - raw_seconds(note.grid.start)) / hop;
    return std::clamp<std::int64_t>(std::llround(rel), 0, note.grid.n_frames - 1);
}

double cents_at(const Note& note, std::int64_t i) {
    const auto k = static_cast<std::size_t>(i);
    return note.pitch_center.value() + note.pitch_drift[k].value() + note.pitch_mod[k].value();
}

}  // namespace

Result<std::vector<std::uint8_t>> voiced_indicator(const AnalysisResult& analysis,
                                                   const SpanGeometry& geometry,
                                                   const StretchParams& params) {
    for (const Note& n : analysis.notes) {
        if (n.is_active && !n.noise.frame_class.empty() &&
            n.noise.frame_class.size() != static_cast<std::size_t>(n.grid.n_frames)) {
            return fail(ErrorCode::invalid_argument, "stretch_span: note '" + n.id.value +
                                                         "' has a frame_class of the wrong size");
        }
    }
    std::vector<std::uint8_t> indicator(static_cast<std::size_t>(geometry.last - geometry.first),
                                        0);
    for (std::int64_t n = geometry.first; n < geometry.last; ++n) {
        const double t_s = geometry.source_at(static_cast<double>(n)) / geometry.rate_hz;
        const Note* note = note_at(analysis, t_s);
        if (note == nullptr || note->grid.n_frames <= 0) {
            continue;
        }
        const std::int64_t i = frame_index(*note, t_s);
        const bool voiced_class =
            note->noise.frame_class.empty() ||
            params.voiced_classes.contains(note->noise.frame_class[static_cast<std::size_t>(i)]);
        indicator[static_cast<std::size_t>(n - geometry.first)] =
            voiced_class && std::isfinite(cents_at(*note, i)) ? 1 : 0;
    }
    return indicator;
}

std::vector<Run> voiced_runs(const std::vector<std::uint8_t>& indicator, std::int64_t first) {
    std::vector<Run> runs;
    for (std::size_t i = 0; i < indicator.size();) {
        if (indicator[i] == 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < indicator.size() && indicator[j] != 0) {
            ++j;
        }
        runs.push_back({.begin = first + static_cast<std::int64_t>(i),
                        .end = first + static_cast<std::int64_t>(j)});
        i = j;
    }
    return runs;
}

std::vector<grain::CentsPoint> pitch_points(const AnalysisResult& analysis, double rate_hz,
                                            double from, double to) {
    std::vector<grain::CentsPoint> points;
    for (const Note& n : analysis.notes) {
        if (!n.is_active || n.grid.n_frames <= 0 || raw_seconds(n.source_end) * rate_hz < from ||
            raw_seconds(n.source_start) * rate_hz > to) {
            continue;
        }
        for (std::int64_t i = 0; i < n.grid.n_frames; ++i) {
            const double cents = cents_at(n, i);
            if (std::isfinite(cents)) {
                points.push_back(
                    {.position = raw_seconds(frame_time(n.grid, i)) * rate_hz, .cents = cents});
            }
        }
    }
    std::ranges::sort(points, std::ranges::less{}, &grain::CentsPoint::position);
    return points;
}

}  // namespace anno::stretch_detail
