#include "core/timemap/build_time_map.h"

#include "core/edits/effective_state.h"

#include <algorithm>
#include <cmath>

// Raw seconds are allowed here (ADR-0003): the builder works in plain doubles and converts at the
// edges.

namespace anno {

namespace {

/// Floating-point dust below which two durations count as equal.
constexpr double k_same_length_s = 1e-12;

struct Anchor {
    double s;  // source seconds
    double t;  // output seconds
};

double raw(SourceTime t) { return t.unchecked_since_origin().value(); }
double raw(OutputTime t) { return t.unchecked_since_origin().value(); }

bool segment_edited(const Anchor& a, const Anchor& b) {
    return std::abs((b.t - a.t) - (b.s - a.s)) > k_same_length_s;
}

/// Smallest output length a segment may have: its source length if no edit touched it, else the
/// builder's minimum.
double needed_length(const Anchor& a, const Anchor& b, double min_segment_s) {
    return segment_edited(a, b) ? min_segment_s : b.s - a.s;
}

/// The anchors a note asks for, before any repair: start, attack end, handles in the body, end.
/// Unedited terms are added as exact zeros so an untouched note maps s -> s bit-exactly.
std::vector<Anchor> desired_anchors(const Note& note, const EffectiveNote& eff) {
    const double start = raw(note.source_start);
    const double attack_end = raw(note.attack_end);
    const double end = raw(note.source_end);
    const double shift = eff.onset_shift.value();
    const double attack_extra = (attack_end - start) * (eff.attack_time_scale.value() - 1.0);
    const double body_extra = (end - attack_end) * (eff.length_scale.value() - 1.0);

    std::vector<Anchor> anchors{{start, start + shift}, {attack_end, attack_end + shift + attack_extra}};
    for (const TimePair& h : eff.handles) {
        if (raw(h.source) > attack_end && raw(h.source) < end) {
            anchors.push_back({raw(h.source), raw(h.output)});
        }
    }
    anchors.push_back({end, end + shift + attack_extra + body_extra});

    // Coincident source points (zero-length attack or body) keep the later output.
    std::vector<Anchor> merged;
    for (const Anchor& a : anchors) {
        if (!merged.empty() && merged.back().s == a.s) {
            merged.back().t = std::max(merged.back().t, a.t);
        } else {
            merged.push_back(a);
        }
    }
    return merged;
}

/// Clamps outputs inside one note so each segment keeps its needed length. True if it changed any.
bool clamp_inside(std::vector<Anchor>& anchors, double min_segment_s) {
    bool changed = false;
    for (std::size_t k = 1; k < anchors.size(); ++k) {
        const double floor_t = anchors[k - 1].t + needed_length(anchors[k - 1], anchors[k], min_segment_s);
        if (anchors[k].t < floor_t - k_same_length_s) {
            anchors[k].t = floor_t;
            changed = true;
        }
    }
    return changed;
}

void shift_output(std::vector<Anchor>& anchors, double delta) {
    for (Anchor& a : anchors) {
        a.t += delta;
    }
}

/// Joins a note's anchors onto the map built so far. Returns true if the note's request was not
/// honoured exactly (it was shifted, or the previous boundary was stretched).
bool join(std::vector<Anchor>& done, std::vector<Anchor>& note, bool previous_is_origin,
          double min_segment_s) {
    const Anchor prev = done.back();
    if (note.front().s == prev.s) {  // abutting the previous note (or starting at the origin)
        bool repaired = false;
        if (note.front().t >= prev.t) {
            repaired = !previous_is_origin && note.front().t != prev.t;
            done.back().t = note.front().t;  // the earlier segment stretches to meet the later start
        } else {
            shift_output(note, prev.t - note.front().t);
            repaired = true;
        }
        note.erase(note.begin());
        return repaired;
    }
    const double need = needed_length(prev, note.front(), min_segment_s);
    if (note.front().t < prev.t + need - k_same_length_s) {
        shift_output(note, prev.t + need - note.front().t);
        return true;
    }
    return false;
}

Result<void> check_notes(std::span<const Note> notes, const EditList& edits) {
    const Note* previous = nullptr;
    for (const Note& n : notes) {
        if (!n.is_active) {
            continue;
        }
        if (!(n.track_id == edits.track_id())) {
            return fail(ErrorCode::invalid_argument, "build_time_map: note is on another track");
        }
        if (previous != nullptr && n.source_start < previous->source_end) {
            return fail(ErrorCode::invalid_argument,
                        "build_time_map: active notes overlap or are unsorted (polyphony is Phase 11)");
        }
        previous = &n;
    }
    return {};
}

bool is_identity(const std::vector<Anchor>& anchors) {
    return std::ranges::all_of(anchors, [](const Anchor& a) { return a.s == a.t; });
}

}  // namespace

Result<TimeMapBuild> build_time_map(std::span<const Note> notes, const EditList& edits,
                                    const TimeMapParams& params) {
    if (auto r = check_notes(notes, edits); !r) {
        return Unexpected{r.error()};
    }
    const double min_segment_s = params.min_segment.value();
    std::vector<Anchor> done{{0.0, 0.0}};
    int repaired_notes = 0;
    for (const Note& note : notes) {
        if (!note.is_active) {
            continue;
        }
        std::vector<Anchor> anchors = desired_anchors(note, effective_state(note, edits));
        bool repaired = clamp_inside(anchors, min_segment_s);
        repaired = join(done, anchors, done.size() == 1, min_segment_s) || repaired;
        done.insert(done.end(), anchors.begin(), anchors.end());
        repaired_notes += repaired ? 1 : 0;
    }
    if (is_identity(done)) {
        return TimeMapBuild{TimeMap::identity(), repaired_notes};
    }
    std::vector<TimePair> pairs;
    pairs.reserve(done.size());
    for (const Anchor& a : done) {
        pairs.push_back({SourceTime::unchecked_from_origin(Seconds{a.s}),
                         OutputTime::unchecked_from_origin(Seconds{a.t})});
    }
    auto map = TimeMap::from_anchors(std::move(pairs));
    if (!map) {
        return fail(ErrorCode::internal, "build_time_map produced non-monotone anchors: " +
                                             map.error().message);
    }
    return TimeMapBuild{std::move(*map), repaired_notes};
}

}  // namespace anno
