#include "core/timemap/build_time_map.h"

#include "core/edits/effective_state.h"
#include "core/timemap/detail/anchors.h"

#include <algorithm>
#include <cmath>

// Raw seconds are allowed here (ADR-0003): the builder works in plain doubles and converts at the
// edges.

namespace anno {

using namespace timemap_detail;

Result<TimeMapBuild> build_time_map(std::span<const Note> notes, const EditList& edits,
                                    const TimeMapParams& params) {
    if (auto r = check_notes(notes, edits, params.source_end); !r) {
        return Unexpected{r.error()};
    }
    const double min_segment_s = params.min_segment.value();
    std::vector<bool> repaired(notes.size(), false);
    std::vector<Anchor> done{{.s = 0.0, .t = 0.0, .note = k_no_note}};
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes[i].is_active) {
            continue;
        }
        const EffectiveNote eff = effective_state(notes[i], edits);
        if (has_timing_edit(eff)) {
            append(done, desired_anchors(static_cast<int>(i), notes[i], eff), repaired);
        }
    }
    const bool fixed_end =
        params.tail == TailPolicy::keep_length && raw(params.source_end) > done.back().s;
    if (fixed_end) {
        done.push_back(
            {.s = raw(params.source_end), .t = raw(params.source_end), .note = k_no_note});
    }
    const std::vector<Anchor> requested = done;
    if (!enforce_min_segments(done, fixed_end, min_segment_s)) {
        // Not enough room between the fixed ends: fall back to the unedited timeline.
        done = requested;
        for (Anchor& a : done) {
            a.t = a.s;
        }
        repaired.assign(repaired.size(), true);
    }
    for (std::size_t k = 0; k < done.size(); ++k) {
        if (done[k].note != k_no_note && std::abs(done[k].t - requested[k].t) > k_same_length_s) {
            repaired[static_cast<std::size_t>(done[k].note)] = true;
        }
    }
    const int repaired_notes = static_cast<int>(std::ranges::count(repaired, true));
    if (params.sample_rate.value() > 0.0) {
        // The fixed end has shift 0, so it is unchanged; quantise_shifts refuses to move it.
        (void)quantise_shifts(done, fixed_end, params.sample_rate.value(),
                              params.shift_step.value());
    }
    // The domain ends at the recording's end; when that is unknown (map-only use) at the last note
    // or anchor, whichever is later.
    double length = std::max(raw(params.source_end), done.back().s);
    for (const Note& n : notes) {
        if (n.is_active) {
            length = std::max(length, raw(n.source_end));
        }
    }
    if (is_identity(done)) {
        return TimeMapBuild{.map = TimeMap::identity(Seconds{length}),
                            .repaired_notes = repaired_notes};
    }
    std::vector<TimePair> pairs;
    pairs.reserve(done.size());
    for (const Anchor& a : done) {
        pairs.push_back({.source = SourceTime::unchecked_from_origin(Seconds{a.s}),
                         .output = OutputTime::unchecked_from_origin(Seconds{a.t})});
    }
    auto map = TimeMap::from_anchors(std::move(pairs), Seconds{length});
    if (!map) {
        return fail(ErrorCode::internal,
                    "build_time_map produced non-monotone anchors: " + map.error().message);
    }
    return TimeMapBuild{.map = std::move(*map), .repaired_notes = repaired_notes};
}

}  // namespace anno
