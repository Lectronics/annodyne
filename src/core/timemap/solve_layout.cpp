#include "core/timemap/detail/anchors.h"
#include "core/timemap/layout.h"

#include <algorithm>
#include <cmath>

// The interval solve of ADR-0025. Raw seconds are allowed here (ADR-0003), as in
// build_time_map.cpp: the solver works in plain doubles and converts at the edges.

namespace anno {

namespace {

using timemap_detail::Anchor;
using timemap_detail::k_no_note;
using timemap_detail::k_same_length_s;
using timemap_detail::raw;

/// A marker matches a timeline boundary within this (the markers come from the notes' own times,
/// the timeline from the same notes; this only absorbs floating-point dust).
constexpr double k_match_s = 1e-7;
/// Slope comparisons tolerate this much dust.
constexpr double k_slope_dust = 1e-9;

struct Context {
    const SegmentTimeline* timeline;
    const LayoutParams* params;
    double rate_hz;             // 0: exact arithmetic
    std::int64_t step_samples;  // the shift grid, >= 1
    double min_segment_s;
};

double seconds_of(SourceTime t) {
    return raw(t);
}

SourceTime source_at(double s) {
    return SourceTime::unchecked_from_origin(Seconds{s});
}

OutputTime output_at(double s) {
    return OutputTime::unchecked_from_origin(Seconds{s});
}

/// `seconds` rounded to a whole number of samples (toward zero when `toward_zero`), or unchanged.
double on_units(const Context& c, double seconds, double unit_samples, bool toward_zero) {
    if (c.rate_hz <= 0.0) {
        return seconds;
    }
    const double units = seconds * c.rate_hz / unit_samples;
    return (toward_zero ? std::trunc(units + std::copysign(k_slope_dust, units))
                        : std::round(units)) *
           unit_samples / c.rate_hz;
}

/// `seconds` on the shift grid (the unit every marker shift and gap change is a multiple of).
double on_grid(const Context& c, double seconds, bool toward_zero = false) {
    return on_units(c, seconds, static_cast<double>(c.step_samples), toward_zero);
}

/// `seconds` on the sample grid: where a cut or insert sits.
double on_sample(const Context& c, double seconds) {
    return on_units(c, seconds, 1.0, false);
}

[[nodiscard]] bool marker_fits(const SegmentTimeline& tl, const LayoutMarker& m) {
    const double s = seconds_of(m.source);
    const auto note_at = [&](double t) -> const NoteSegment* {
        if (t < 0.0 || t > seconds_of(tl.source_end())) {
            return nullptr;
        }
        return std::get_if<NoteSegment>(&tl.at(source_at(t)));
    };
    switch (m.kind) {
    case MarkerKind::origin: return s == 0.0;
    case MarkerKind::end: return std::abs(s - seconds_of(tl.source_end())) < k_match_s;
    case MarkerKind::note_start: {
        const NoteSegment* n = note_at(s);
        return n != nullptr && std::abs(seconds_of(n->start) - s) < k_match_s;
    }
    case MarkerKind::note_end: {
        const NoteSegment* n = note_at(s - 2.0 * k_match_s);
        return n != nullptr && std::abs(seconds_of(n->end) - s) < k_match_s;
    }
    case MarkerKind::attack_end:
    case MarkerKind::handle: {
        const NoteSegment* n = note_at(s);
        return n != nullptr && s > seconds_of(n->start) && s < seconds_of(n->end);
    }
    }
    return false;
}

Result<void> check_markers(const SegmentTimeline& tl, const LayoutMarkers& markers) {
    if (tl.segments().empty() || markers.markers.size() < 2) {
        return fail(ErrorCode::invalid_argument, "solve_layout: empty timeline or markers");
    }
    const auto& list = markers.markers;
    const bool ends_at_end =
        std::abs(seconds_of(list.back().source) - seconds_of(tl.source_end())) < k_match_s;
    const bool starts_at_origin =
        seconds_of(list.front().source) == 0.0 &&
        (list.front().kind == MarkerKind::origin || list.front().kind == MarkerKind::note_start);
    if (!starts_at_origin || !ends_at_end) {
        return fail(ErrorCode::invalid_argument,
                    "solve_layout: markers must run from the origin to the timeline's end");
    }
    for (const LayoutMarker& m : list) {
        if (!marker_fits(tl, m)) {
            return fail(ErrorCode::invalid_argument,
                        "solve_layout: a marker is not where the timeline has its note");
        }
    }
    return {};
}

/// Index of the gap that starts at `t` / ends at `t` (the gaps are sorted), or nullopt.
std::optional<std::size_t> gap_starting_at(const SegmentTimeline& tl, double t) {
    const auto gaps = tl.gaps();
    const auto it = std::ranges::lower_bound(gaps, t - k_match_s, {},
                                             [](const Gap& g) { return seconds_of(g.start); });
    if (it != gaps.end() && std::abs(seconds_of(it->start) - t) < k_match_s) {
        return static_cast<std::size_t>(it - gaps.begin());
    }
    return std::nullopt;
}

std::optional<std::size_t> gap_ending_at(const SegmentTimeline& tl, double t) {
    const auto gaps = tl.gaps();
    const auto it = std::ranges::lower_bound(gaps, t - k_match_s, {},
                                             [](const Gap& g) { return seconds_of(g.end); });
    if (it != gaps.end() && std::abs(seconds_of(it->end) - t) < k_match_s) {
        return static_cast<std::size_t>(it - gaps.begin());
    }
    return std::nullopt;
}

/// The gap that absorbs the interval's change: the first of rules a-d that finds a gap.
std::optional<std::size_t> absorbing_gap(const SegmentTimeline& tl, const LayoutMarker& left,
                                         const LayoutMarker& right) {
    if (left.kind == MarkerKind::note_end) {
        if (auto g = gap_starting_at(tl, seconds_of(left.source))) {
            return g;
        }
    }
    if (left.kind == MarkerKind::origin) {
        if (auto g = gap_starting_at(tl, 0.0)) {
            return g;
        }
    }
    if (right.kind == MarkerKind::note_start || right.kind == MarkerKind::end) {
        return gap_ending_at(tl, seconds_of(right.source));
    }
    return std::nullopt;
}

/// The longest silence region of the gap (ties: the earliest).
const GapRegion* longest_silence(const Gap& gap) {
    const GapRegion* best = nullptr;
    for (const GapRegion& r : gap.regions) {
        if (r.kind != GapRegionKind::silence) {
            continue;
        }
        if (best == nullptr || seconds_of(r.end) - seconds_of(r.start) >
                                   seconds_of(best->end) - seconds_of(best->start)) {
            best = &r;
        }
    }
    return best;
}

/// What a cut can take out of `silence_s` of silence: it keeps min_gap, and a note margin plus the
/// splice fade on both sides, in whole samples.
double cut_room(const Context& c, double silence_s) {
    const double reserve = (*c.params).min_gap.value() + 2.0 * ((*c.params).note_margin.value() +
                                                                (*c.params).splice_fade.value());
    const double room = silence_s - reserve;
    if (c.rate_hz <= 0.0) {
        return room > k_same_length_s ? room : 0.0;
    }
    const auto step = static_cast<double>(c.step_samples);
    return std::max(0.0, std::floor(room * c.rate_hz / step + k_slope_dust) * step / c.rate_hz);
}

/// The cut or insert for a change of `delta_s` in `gap`, or nullopt if the gap can take none of it.
std::optional<GapEdit> gap_edit(const Context& c, std::size_t index, double delta_s) {
    const Gap& gap = (*c.timeline).gaps()[index];
    const GapRegion* silence = longest_silence(gap);
    if (silence == nullptr) {
        return std::nullopt;
    }
    const double start = seconds_of(silence->start);
    const double length_s = seconds_of(silence->end) - start;
    const double centre = start + length_s / 2.0;
    GapEdit edit;
    edit.gap = index;
    edit.cut = delta_s < 0.0;
    if (edit.cut) {
        const double length = std::min(-delta_s, cut_room(c, length_s));
        if (length <= k_same_length_s) {
            return std::nullopt;
        }
        edit.length = Seconds{length};
        edit.at = source_at(on_sample(c, centre - length / 2.0));
    } else {
        // Growth stretches the audio and holds the silence (slope 1): length 0 until solve_interval
        // finds the audio cannot take it all.
        edit.length = Seconds{0.0};
        edit.at = source_at(on_sample(c, centre));
    }
    return edit;
}

struct Solved {
    IntervalPlan plan;
    bool feasible = true;
    double feasible_delta_s = 0.0;  // when infeasible: the largest change the interval can take
};

/// Whether the audio the gap edit did not cover can take `rest_s` more or less.
bool stretch_fits(const Context& c, double other_s, double rest_s) {
    if (std::abs(rest_s) < k_same_length_s) {
        return true;
    }
    if (other_s < std::max(c.min_segment_s, k_same_length_s)) {
        return false;
    }
    const double slope = (other_s + rest_s) / other_s;
    return slope >= k_min_layout_slope - k_slope_dust && slope <= k_max_layout_slope + k_slope_dust;
}

Solved solve_interval(const Context& c, const LayoutMarker& left, const LayoutMarker& right,
                      const Anchor& a_left, const Anchor& a_right) {
    Solved out;
    IntervalPlan& plan = out.plan;
    plan.from = left.source;
    plan.to = right.source;
    plan.out_from = output_at(a_left.t);
    plan.out_to = output_at(a_right.t);
    const double span_s = a_right.s - a_left.s;
    const double delta_s = on_grid(c, (a_right.t - a_right.s) - (a_left.t - a_left.s));
    if (std::abs(delta_s) < k_same_length_s) {
        return out;
    }
    const bool absorb = (*c.params).mode == LayoutMode::absorb_then_stretch;
    if (!absorb) {
        plan.residual_slope = (span_s + delta_s) / span_s;
        return out;
    }
    double other_s = span_s;
    double rest_s = delta_s;
    if (const auto index = absorbing_gap(*c.timeline, left, right)) {
        plan.gap = gap_edit(c, *index, delta_s);
        if (plan.gap) {
            const Gap& gap = c.timeline->gaps()[*index];
            other_s = span_s - (seconds_of(gap.end) - seconds_of(gap.start));
            if (plan.gap->cut) {
                rest_s = delta_s + plan.gap->length.value();
            } else {
                // Only if the audio cannot take the growth does the silence lengthen.
                const double fits_s = other_s >= std::max(c.min_segment_s, k_same_length_s)
                                          ? on_grid(c, (k_max_layout_slope - 1.0) * other_s, true)
                                          : 0.0;
                plan.gap->length = Seconds{std::max(0.0, delta_s - fits_s)};
                rest_s = delta_s - plan.gap->length.value();
            }
        }
    }
    if (stretch_fits(c, other_s, rest_s)) {
        plan.residual_slope = other_s > k_same_length_s ? (other_s + rest_s) / other_s : 1.0;
        return out;
    }
    const double taken_s = plan.gap && plan.gap->cut ? plan.gap->length.value() : 0.0;
    const double squish_s = other_s >= c.min_segment_s ? (1.0 - k_min_layout_slope) * other_s : 0.0;
    const double grow_s = other_s >= c.min_segment_s ? (k_max_layout_slope - 1.0) * other_s : 0.0;
    out.feasible = false;
    out.feasible_delta_s = on_grid(c, delta_s < 0.0 ? -(taken_s + squish_s) : grow_s, true);
    return out;
}

void unedited_layout(Layout& layout, const std::vector<LayoutMarker>& list) {
    layout.intervals.clear();
    for (std::size_t k = 1; k < list.size(); ++k) {
        IntervalPlan plan;
        plan.from = list[k - 1].source;
        plan.to = list[k].source;
        plan.out_from = output_at(seconds_of(plan.from));
        plan.out_to = output_at(seconds_of(plan.to));
        layout.intervals.push_back(plan);
    }
    layout.repaired.assign(layout.repaired.size(), true);
    layout.fell_back = true;
}

std::vector<Anchor> anchors_of(const LayoutMarkers& markers) {
    std::vector<Anchor> anchors;
    anchors.reserve(markers.markers.size());
    for (const LayoutMarker& m : markers.markers) {
        anchors.push_back(
            {.s = seconds_of(m.source),
             .t = raw(m.output),
             .note = m.note == k_no_marker_note ? k_no_note : static_cast<int>(m.note)});
    }
    return anchors;
}

void mark_changed(Layout& layout, const std::vector<Anchor>& now, const std::vector<Anchor>& was) {
    for (std::size_t k = 0; k < now.size(); ++k) {
        if (now[k].note != k_no_note && std::abs(now[k].t - was[k].t) > k_same_length_s) {
            layout.repaired[static_cast<std::size_t>(now[k].note)] = true;
        }
    }
}

}  // namespace

Result<Layout> solve_layout(const SegmentTimeline& timeline, const LayoutMarkers& markers,
                            const LayoutParams& params, const TimeMapParams& time_map) {
    if (auto r = check_markers(timeline, markers); !r) {
        return Unexpected{r.error()};
    }
    const Context c{.timeline = &timeline,
                    .params = &params,
                    .rate_hz = time_map.sample_rate.value(),
                    .step_samples = std::max<std::int64_t>(1, time_map.shift_step.value()),
                    .min_segment_s = time_map.min_segment.value()};
    const auto& list = markers.markers;
    Layout layout;
    layout.repaired = markers.repaired;
    std::vector<Anchor> anchors = anchors_of(markers);
    const bool fixed_end = list.back().kind == MarkerKind::end;
    const std::vector<Anchor> requested = anchors;
    if (!timemap_detail::enforce_min_segments(anchors, fixed_end, c.min_segment_s)) {
        unedited_layout(layout, list);
        return layout;
    }
    mark_changed(layout, anchors, requested);
    if (c.rate_hz > 0.0) {
        (void)timemap_detail::quantise_shifts(anchors, fixed_end, c.rate_hz, c.step_samples);
    }
    for (std::size_t k = 1; k < anchors.size(); ++k) {
        Solved solved = solve_interval(c, list[k - 1], list[k], anchors[k - 1], anchors[k]);
        if (!solved.feasible) {
            // The interval cannot give what is asked: move the right marker to what it can give.
            if (fixed_end && k + 1 == anchors.size()) {
                unedited_layout(layout, list);
                return layout;
            }
            const double shift_left = anchors[k - 1].t - anchors[k - 1].s;
            anchors[k].t = anchors[k].s + shift_left + solved.feasible_delta_s;
            if (anchors[k].note != k_no_note) {
                layout.repaired[static_cast<std::size_t>(anchors[k].note)] = true;
            }
            solved = solve_interval(c, list[k - 1], list[k], anchors[k - 1], anchors[k]);
            if (!solved.feasible) {
                unedited_layout(layout, list);
                return layout;
            }
        }
        layout.intervals.push_back(solved.plan);
    }
    return layout;
}

}  // namespace anno
