#include "core/timemap/layout.h"

#include <algorithm>
#include <cmath>

// Spans from a solved layout (ADR-0025). Raw seconds are allowed here (ADR-0003), as in
// build_time_map.cpp.

namespace anno {

namespace {

constexpr double k_tol_s = 1e-9;

double sec(SourceTime t) {
    return (t - SourceTime{}).value();
}
double sec(OutputTime t) {
    return (t - OutputTime{}).value();
}

/// Accumulates spans; zero-length pieces are dropped and slope-1 neighbours with one shift merge.
class SpanList {
public:
    void warp(double s0, double s1, double o0, double o1, const SegmentId* segment) {
        if (s1 - s0 <= k_tol_s && o1 - o0 <= k_tol_s) {
            return;
        }
        push({.kind = SpanKind::warp, .s0 = s0, .s1 = s1, .o0 = o0, .o1 = o1, .segment = segment});
    }
    void cut(double s0, double s1, double o, const SegmentId* segment) {
        if (s1 - s0 > k_tol_s) {
            push({.kind = SpanKind::cut, .s0 = s0, .s1 = s1, .o0 = o, .o1 = o, .segment = segment});
        }
    }
    void insert(double s, double o0, double o1, const SegmentId* segment) {
        if (o1 - o0 > k_tol_s) {
            push({.kind = SpanKind::insert,
                  .s0 = s,
                  .s1 = s,
                  .o0 = o0,
                  .o1 = o1,
                  .segment = segment});
        }
    }

    [[nodiscard]] Result<TimeMap> make(double source_length_s) const {
        if (pieces_.size() == 1 && pieces_[0].kind == SpanKind::warp && pieces_[0].s0 == 0.0 &&
            pieces_[0].o0 == 0.0 && pieces_[0].o1 == pieces_[0].s1) {
            return TimeMap::identity(Seconds{source_length_s});
        }
        std::vector<TimeSpan> spans;
        spans.reserve(pieces_.size());
        for (const Piece& p : pieces_) {
            TimeSpan span;
            span.kind = p.kind;
            span.source_start = SourceTime::unchecked_from_origin(Seconds{p.s0});
            span.source_end = SourceTime::unchecked_from_origin(Seconds{p.s1});
            span.output_start = OutputTime::unchecked_from_origin(Seconds{p.o0});
            span.output_end = OutputTime::unchecked_from_origin(Seconds{p.o1});
            if (p.segment != nullptr) {
                span.segment = *p.segment;
            }
            spans.push_back(std::move(span));
        }
        return TimeMap::make(std::move(spans));
    }

private:
    struct Piece {
        SpanKind kind = SpanKind::warp;
        double s0 = 0.0;
        double s1 = 0.0;
        double o0 = 0.0;
        double o1 = 0.0;
        const SegmentId* segment = nullptr;
    };

    static bool same_shift(const Piece& a, const Piece& b) {
        const bool slope_one = std::abs((a.o1 - a.o0) - (a.s1 - a.s0)) <= k_tol_s &&
                               std::abs((b.o1 - b.o0) - (b.s1 - b.s0)) <= k_tol_s;
        return slope_one && std::abs((a.o0 - a.s0) - (b.o0 - b.s0)) <= k_tol_s;
    }

    void push(Piece p) {
        if (!pieces_.empty() && p.kind == SpanKind::warp && pieces_.back().kind == SpanKind::warp &&
            p.segment == nullptr && pieces_.back().segment == nullptr &&
            same_shift(pieces_.back(), p)) {
            pieces_.back().s1 = p.s1;
            pieces_.back().o1 = p.o1;
            return;
        }
        pieces_.push_back(p);
    }

    std::vector<Piece> pieces_;
};

struct Builder {
    const SegmentTimeline* timeline;
    double rate_hz;

    [[nodiscard]] double snap(double s) const {
        return rate_hz > 0.0 ? std::round(s * rate_hz) / rate_hz : s;
    }
};

/// Whether [at, at + length] lies inside a silence region of `gap`.
bool inside_silence(const Gap& gap, double at, double length) {
    return std::ranges::any_of(gap.regions, [&](const GapRegion& r) {
        return r.kind == GapRegionKind::silence && at >= sec(r.start) - k_tol_s &&
               at + length <= sec(r.end) + k_tol_s;
    });
}

/// Emits one interval's spans. `shift_left`/`shift_right` are output minus source at its markers.
Result<void> emit_interval(const Builder& b, const IntervalPlan& plan, SpanList& spans) {
    const double from = b.snap(sec(plan.from));
    const double to = b.snap(sec(plan.to));
    const double out_from = from + (sec(plan.out_from) - sec(plan.from));
    const double out_to = to + (sec(plan.out_to) - sec(plan.to));
    if (!plan.gap) {
        spans.warp(from, to, out_from, out_to, nullptr);
        return {};
    }
    const GapEdit& edit = *plan.gap;
    if (edit.gap >= b.timeline->gaps().size()) {
        return fail(ErrorCode::invalid_argument, "build_layout_map: a gap index is out of range");
    }
    const Gap& gap = b.timeline->gaps()[edit.gap];
    const double length = edit.length.value();
    const double at = b.snap(sec(edit.at));
    if ((length > 0.0 || edit.cut) && !inside_silence(gap, sec(edit.at), edit.cut ? length : 0.0)) {
        return fail(ErrorCode::invalid_argument,
                    "build_layout_map: a cut or insert lies outside the gap's silence");
    }
    const double gs = b.snap(sec(gap.start));
    const double ge = b.snap(sec(gap.end));
    const double grown = edit.cut ? -length : length;  // the gap's change in output length
    const bool gap_first = gs - from <= k_tol_s;
    // Output position where the gap starts.
    const double gap_out = gap_first ? out_from : out_to - ((ge - gs) + grown);
    const SegmentId* id = &gap.id;
    if (!gap_first) {
        spans.warp(from, gs, out_from, gap_out, nullptr);
    }
    spans.warp(gs, at, gap_out, gap_out + (at - gs), id);
    double cursor = gap_out + (at - gs);
    if (edit.cut) {
        spans.cut(at, at + length, cursor, id);
        spans.warp(at + length, ge, cursor, cursor + (ge - at - length), id);
        cursor += ge - at - length;
    } else {
        spans.insert(at, cursor, cursor + length, id);
        spans.warp(at, ge, cursor + length, cursor + length + (ge - at), id);
        cursor += length + (ge - at);
    }
    if (gap_first) {
        spans.warp(ge, to, cursor, out_to, nullptr);
    }
    return {};
}

}  // namespace

Result<TimeMapBuild> build_layout_map(const SegmentTimeline& timeline, const Layout& layout,
                                      const TimeMapParams& time_map) {
    const Builder b{.timeline = &timeline, .rate_hz = time_map.sample_rate.value()};
    SpanList spans;
    if (!layout.intervals.empty()) {
        const double lead = sec(layout.intervals.front().out_from);
        if (lead > k_tol_s) {
            spans.insert(0.0, 0.0, lead, nullptr);  // a note at the start pushed later
        }
    }
    for (const IntervalPlan& plan : layout.intervals) {
        if (auto r = emit_interval(b, plan, spans); !r) {
            return Unexpected{r.error()};
        }
    }
    auto map = spans.make(sec(timeline.source_end()));
    if (!map) {
        return fail(ErrorCode::internal,
                    "build_layout_map produced spans that do not tile: " + map.error().message);
    }
    return TimeMapBuild{.map = std::move(*map),
                        .repaired_notes =
                            static_cast<int>(std::ranges::count(layout.repaired, true))};
}

Result<TimeMapBuild> build_layout_time_map(std::span<const Note> notes,
                                           const SegmentTimeline& timeline, const EditList& edits,
                                           const LayoutParams& params,
                                           const TimeMapParams& time_map) {
    TimeMapParams tm = time_map;
    tm.source_end = timeline.source_end();
    tm.tail = TailPolicy::keep_length;
    if (params.mode == LayoutMode::stretch) {
        return build_time_map(notes, edits, tm);
    }
    auto markers = layout_markers(notes, edits, timeline.source_end());
    if (!markers) {
        return Unexpected{markers.error()};
    }
    auto layout = solve_layout(timeline, *markers, params, tm);
    if (!layout) {
        return Unexpected{layout.error()};
    }
    return build_layout_map(timeline, *layout, tm);
}

}  // namespace anno
