#include "core/analysis/gaps/build_segment_timeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace anno {

namespace {

Unexpected invalid(const char* what) {
    return fail(ErrorCode::invalid_argument, std::string{"build_segment_timeline: "} + what);
}

double raw(SourceTime t) {
    return (t - SourceTime{}).value();
}

Result<void> check_params(const GapParams& p) {
    const auto bad_nonneg = [](double v) { return !std::isfinite(v) || v < 0.0; };
    const bool bad = bad_nonneg(p.tail_margin.value()) || bad_nonneg(p.preroll_guard.value()) ||
                     bad_nonneg(p.min_audible.value()) || bad_nonneg(p.audible_pad.value()) ||
                     bad_nonneg(p.evidence_guard.value()) ||
                     !std::isfinite(p.event_margin.value()) || !std::isfinite(p.max_tail.value()) ||
                     p.max_tail.value() <= 0.0;
    return bad ? Result<void>{invalid("margins, guards and pads must be finite and >= 0, "
                                      "max_tail finite and > 0")}
               : Result<void>{};
}

/// The active notes in order; errors for any that overlap, are unsorted, have no length or lie
/// outside [0, source_end].
Result<std::vector<const Note*>> active_notes(std::span<const Note> notes, double end) {
    std::vector<const Note*> out;
    for (const Note& n : notes) {
        if (!n.is_active) {
            continue;
        }
        if (!(raw(n.source_start) >= 0.0) || !(raw(n.source_end) <= end) ||
            !(raw(n.source_end) > raw(n.source_start))) {
            return invalid("an active note has no length or lies outside the recording");
        }
        if (!out.empty() && raw(n.source_start) < raw(out.back()->source_end)) {
            return invalid("active notes overlap or are not sorted");
        }
        out.push_back(&n);
    }
    return out;
}

/// The median of the floor curve over the frames in [start, end) (a boundary frame belongs to the
/// later segment); the nearest frame when none.
Decibels floor_over(const NoiseFloor& floor, double start, double end) {
    const double hop = floor.grid.hop.value();
    const double origin = raw(floor.grid.start);
    const auto last_frame = floor.grid.n_frames - 1;
    const auto first = static_cast<std::int64_t>(std::ceil((start - origin) / hop));
    const auto last = static_cast<std::int64_t>(std::ceil((end - origin) / hop)) - 1;
    std::vector<double> values;
    for (std::int64_t i = std::max<std::int64_t>(0, first); i <= std::min(last, last_frame); ++i) {
        values.push_back(floor.curve_dbfs[static_cast<std::size_t>(i)].value());
    }
    if (values.empty()) {
        const auto nearest = std::clamp<std::int64_t>(
            std::llround((0.5 * (start + end) - origin) / hop), 0, last_frame);
        return floor.curve_dbfs[static_cast<std::size_t>(nearest)];
    }
    std::ranges::sort(values);
    const std::size_t mid = values.size() / 2;
    return Decibels{values.size() % 2 == 1 ? values[mid] : 0.5 * (values[mid - 1] + values[mid])};
}

/// Everything the region classifier reads about one gap.
struct GapView {
    double start = 0.0;
    double end = 0.0;
    bool has_before = false;  // a note ends at `start`: its decay can run into the gap
    bool has_after = false;   // a note starts at `end`: its lead-in is protected
};

struct Levels {
    const ActivityTrack* activity = nullptr;
    const NoiseFloor* floor = nullptr;
    const GapEvidence* evidence = nullptr;
    double margin_db = 0.0;

    [[nodiscard]] double frame_time(std::int64_t i) const {
        return raw(activity->grid.start) + static_cast<double>(i) * activity->grid.hop.value();
    }
    /// Strictly above the floor curve + margin.
    [[nodiscard]] bool above(std::int64_t i) const {
        const auto at = static_cast<std::size_t>(i);
        return activity->level[at].value() > floor->curve_dbfs[at].value() + margin_db;
    }
};

struct Span {
    double start = 0.0;
    double end = 0.0;
};

/// Frame index range [first, last] of the frames whose time is in [from, to).
std::pair<std::int64_t, std::int64_t> frames_in(const FrameGrid& grid, double from, double to) {
    const double hop = grid.hop.value();
    const double origin = raw(grid.start);
    const auto first =
        std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil((from - origin) / hop)));
    const auto last = std::min<std::int64_t>(
        grid.n_frames - 1, static_cast<std::int64_t>(std::ceil((to - origin) / hop)) - 1);
    return {first, last};
}

/// The previous note's decay: from the gap start while the level stays above the floor, at most
/// max_tail and not into the guard before the next note. Empty if the first frame is not above.
std::optional<Span> find_tail(const GapView& g, double limit, const Levels& lv,
                              const GapParams& p) {
    if (!g.has_before) {
        return std::nullopt;
    }
    const auto [first, last] = frames_in(lv.activity->grid, g.start, limit);
    std::int64_t k = first;
    while (k <= last && lv.above(k) && lv.frame_time(k) - g.start < p.max_tail.value()) {
        ++k;
    }
    if (k == first) {
        return std::nullopt;
    }
    const double end = k <= last ? lv.frame_time(k) : limit;
    return Span{.start = g.start, .end = std::min({end, limit, g.start + p.max_tail.value()})};
}

/// Runs of frames above the floor in [from, to) that last at least min_audible, padded and merged.
std::vector<Span> find_audible(double from, double to, const Levels& lv, const GapParams& p) {
    const auto [first, last] = frames_in(lv.activity->grid, from, to);
    const double hop = lv.activity->grid.hop.value();
    std::vector<Span> out;
    for (std::int64_t i = first; i <= last; ++i) {
        if (!lv.above(i)) {
            continue;
        }
        std::int64_t j = i;
        while (j + 1 <= last && lv.above(j + 1)) {
            ++j;
        }
        const double length = static_cast<double>(j - i + 1) * hop;
        if (length >= p.min_audible.value()) {
            const double begin = std::max(from, lv.frame_time(i) - p.audible_pad.value());
            const double stop = std::min(to, lv.frame_time(j) + hop + p.audible_pad.value());
            if (!out.empty() && begin <= out.back().end) {
                out.back().end = std::max(out.back().end, stop);
            } else {
                out.push_back({.start = begin, .end = stop});
            }
        }
        i = j;
    }
    return out;
}

struct Classified {
    GapRegionKind kind = GapRegionKind::audible;
    Span span;
};

std::optional<GapRegionKind> kind_of(FrameClass c) {
    switch (c) {
    case FrameClass::sibilant: return GapRegionKind::sibilant;
    case FrameClass::breath: return GapRegionKind::breath;
    case FrameClass::plosive: return GapRegionKind::plosive;
    default: return std::nullopt;
    }
}

/// The kind a frame is evidence for: its class, if it is above the event margin.
std::optional<GapRegionKind> evidence_kind(const Levels& lv, std::int64_t i, const GapParams& p) {
    const auto at = static_cast<std::size_t>(i);
    if (!(lv.evidence->excess_db[at].value() > p.event_margin.value())) {
        return std::nullopt;
    }
    return kind_of(lv.evidence->frame_class[at]);
}

/// Maximal single-class runs of evidence in [from, to) (the guard after a note's end excluded):
/// sibilant and breath need min_audible, a plosive any length; padded, clipped to the earlier
/// region and to [lower, to).
std::vector<Classified> find_classified(const GapView& g, double lower, double to, const Levels& lv,
                                        const GapParams& p) {
    const double from = g.has_before ? std::max(lower, g.start + p.evidence_guard.value()) : lower;
    const auto [first, last] = frames_in(lv.activity->grid, from, to);
    const double hop = lv.activity->grid.hop.value();
    std::vector<Classified> out;
    for (std::int64_t i = first; i <= last; ++i) {
        const auto kind = evidence_kind(lv, i, p);
        if (!kind) {
            continue;
        }
        std::int64_t j = i;
        while (j + 1 <= last && evidence_kind(lv, j + 1, p) == kind) {
            ++j;
        }
        const double length = static_cast<double>(j - i + 1) * hop;
        if (*kind == GapRegionKind::plosive || length >= p.min_audible.value()) {
            const double floor_at = out.empty() ? lower : out.back().span.end;
            const double begin = std::max(floor_at, lv.frame_time(i) - p.audible_pad.value());
            const double stop = std::min(to, lv.frame_time(j) + hop + p.audible_pad.value());
            if (stop > begin) {
                out.push_back({.kind = *kind, .span = {.start = begin, .end = stop}});
            }
        }
        i = j;
    }
    return out;
}

/// `s` without the parts covered by `classified` (sorted, disjoint).
std::vector<Span> minus(const Span& s, const std::vector<Classified>& classified) {
    std::vector<Span> out;
    double cursor = s.start;
    for (const Classified& c : classified) {
        if (c.span.end <= cursor || c.span.start >= s.end) {
            continue;
        }
        if (c.span.start > cursor) {
            out.push_back({.start = cursor, .end = c.span.start});
        }
        cursor = std::max(cursor, c.span.end);
    }
    if (cursor < s.end) {
        out.push_back({.start = cursor, .end = s.end});
    }
    return out;
}

/// The audible stretches left after the classified regions are taken out; a remainder shorter than
/// min_audible joins the classified region it touches (and is dropped if it touches none).
std::vector<Span> audible_remainders(const std::vector<Span>& level_runs,
                                     std::vector<Classified>& classified, const GapParams& p) {
    constexpr double k_touch = 1e-9;
    std::vector<Span> out;
    for (const Span& run : level_runs) {
        for (const Span& piece : minus(run, classified)) {
            if (piece.end - piece.start >= p.min_audible.value()) {
                out.push_back(piece);
                continue;
            }
            // Joins exactly one region (the one before it if both touch), or none.
            const auto before = std::ranges::find_if(classified, [&](const Classified& c) {
                return std::abs(c.span.end - piece.start) < k_touch;
            });
            const auto after = std::ranges::find_if(classified, [&](const Classified& c) {
                return std::abs(c.span.start - piece.end) < k_touch;
            });
            if (before != classified.end()) {
                before->span.end = piece.end;
            } else if (after != classified.end()) {
                after->span.start = piece.start;
            }
        }
    }
    return out;
}

/// The regions of one gap, in order, tiling [start, end]: tail, classified and audible stretches
/// with silence between, and the protected preroll before the next note.
std::vector<GapRegion> classify_regions(const GapView& g, const Levels& lv, const GapParams& p) {
    const double pre_start =
        g.has_after ? std::max(g.start, g.end - p.preroll_guard.value()) : g.end;
    std::vector<std::pair<GapRegionKind, Span>> kinds;
    std::vector<Classified> classified = find_classified(g, g.start, pre_start, lv, p);
    double at = g.start;
    if (auto tail = find_tail(g, pre_start, lv, p)) {
        // A decay is low-frequency and continuous with the note; a sibilant or a plosive inside it
        // is a consonant (the zero-phase high-pass rings after a loud note, so the level can stay
        // up through it): it cuts the tail short. A breath inside the tail is the decay itself.
        const auto cut = std::ranges::find_if(classified, [&](const Classified& c) {
            return c.kind != GapRegionKind::breath && c.span.start < tail->end;
        });
        if (cut != classified.end()) {
            tail->end = std::max(g.start, cut->span.start);
        }
        if (tail->end > g.start) {
            kinds.emplace_back(GapRegionKind::tail, *tail);
            at = tail->end;
        }
        std::erase_if(classified, [&](const Classified& c) { return c.span.end <= at; });
        for (Classified& c : classified) {
            c.span.start = std::max(c.span.start, at);
        }
    }
    const std::vector<Span> audible =
        audible_remainders(find_audible(at, pre_start, lv, p), classified, p);
    std::vector<std::pair<GapRegionKind, Span>> stretches;
    stretches.reserve(classified.size() + audible.size());
    for (const Classified& c : classified) {
        stretches.emplace_back(c.kind, c.span);
    }
    for (const Span& s : audible) {
        stretches.emplace_back(GapRegionKind::audible, s);
    }
    std::ranges::sort(stretches, {}, [](const auto& k) { return k.second.start; });
    kinds.insert(kinds.end(), stretches.begin(), stretches.end());
    if (g.has_after && pre_start < g.end) {
        kinds.emplace_back(GapRegionKind::protected_preroll,
                           Span{.start = pre_start, .end = g.end});
    }
    std::vector<GapRegion> regions;
    const auto push = [&regions](GapRegionKind kind, double from, double to) {
        if (to > from) {
            regions.push_back({.kind = kind,
                               .start = SourceTime{} + Seconds{from},
                               .end = SourceTime{} + Seconds{to}});
        }
    };
    double cursor = g.start;
    for (const auto& [kind, span] : kinds) {
        push(GapRegionKind::silence, cursor, span.start);
        push(kind, span.start, span.end);
        cursor = span.end;
    }
    push(GapRegionKind::silence, cursor, g.end);
    return regions;
}

Gap make_gap(const NoiseFloor& floor, const Levels& lv, const GapParams& params, double start,
             double end, const Note* before, const Note* after) {
    Gap g;
    g.before = before != nullptr ? std::optional<NoteId>{before->id} : std::nullopt;
    g.after = after != nullptr ? std::optional<NoteId>{after->id} : std::nullopt;
    g.id = gap_segment_id(g.before, g.after);
    g.start = SourceTime{} + Seconds{start};
    g.end = SourceTime{} + Seconds{end};
    g.regions = classify_regions({.start = start,
                                  .end = end,
                                  .has_before = before != nullptr,
                                  .has_after = after != nullptr},
                                 lv, params);
    g.floor_dbfs = floor_over(floor, start, end);
    return g;
}

}  // namespace

Result<SegmentTimeline> build_segment_timeline(std::span<const Note> notes, SourceTime source_end,
                                               const ActivityTrack& activity,
                                               const NoiseFloor& floor, const GapEvidence& evidence,
                                               const GapParams& params) {
    if (auto r = check_params(params); !r) {
        return Unexpected{r.error()};
    }
    const double end = raw(source_end);
    if (!std::isfinite(end) || end < 0.0) {
        return invalid("source_end must be finite and >= 0");
    }
    if (!(activity.grid == floor.grid)) {
        return invalid("the floor and the activity track are on different grids");
    }
    const auto frames = static_cast<std::size_t>(activity.grid.n_frames);
    if (activity.level.size() != frames || floor.curve_dbfs.size() != frames) {
        return invalid("the activity levels and the floor curve must match their grid");
    }
    if (!(evidence.grid == activity.grid) || evidence.frame_class.size() != frames ||
        evidence.excess_db.size() != frames) {
        return invalid("the evidence must match the activity grid");
    }
    const Levels levels{.activity = &activity,
                        .floor = &floor,
                        .evidence = &evidence,
                        .margin_db = params.tail_margin.value()};
    auto active = active_notes(notes, end);
    if (!active) {
        return Unexpected{active.error()};
    }
    if (end == 0.0) {
        return SegmentTimeline::make({}, {}, source_end);
    }
    std::vector<Segment> segments;
    std::vector<Gap> gaps;
    const auto push_gap = [&](double start, double stop, const Note* before, const Note* after) {
        gaps.push_back(make_gap(floor, levels, params, start, stop, before, after));
        segments.emplace_back(gaps.size() - 1);
    };
    double at = 0.0;
    const Note* previous = nullptr;
    for (const Note* note : *active) {
        if (raw(note->source_start) > at) {
            push_gap(at, raw(note->source_start), previous, note);
        }
        segments.emplace_back(
            NoteSegment{.note = note->id, .start = note->source_start, .end = note->source_end});
        at = raw(note->source_end);
        previous = note;
    }
    if (end > at) {
        push_gap(at, end, previous, nullptr);
    }
    return SegmentTimeline::make(std::move(segments), std::move(gaps), source_end);
}

}  // namespace anno
