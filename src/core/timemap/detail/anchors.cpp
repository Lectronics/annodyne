#include "core/timemap/detail/anchors.h"

#include "core/edits/effective_state.h"

#include <algorithm>
#include <cmath>

namespace anno::timemap_detail {

double raw(SourceTime t) {
    return t.unchecked_since_origin().value();
}
double raw(OutputTime t) {
    return t.unchecked_since_origin().value();
}

/// True if any timing edit survives in the note's effective state. Only such notes pin anchors:
/// every other note is stretched along with the gaps (Flex-style, ADR-0005 §11).
bool has_timing_edit(const EffectiveNote& eff) {
    return eff.onset_shift != Seconds{} || eff.length_scale != Ratio{1.0} ||
           eff.attack_time_scale != Ratio{1.0} || !eff.handles.empty();
}

/// The anchors a note asks for, before any repair: start, attack end, handles in the body, end.
/// Unedited terms are added as exact zeros so an untouched note maps s -> s bit-exactly.
std::vector<Anchor> desired_anchors(int index, const Note& note, const EffectiveNote& eff) {
    const double start = raw(note.source_start);
    const double attack_end = raw(note.attack_end);
    const double end = raw(note.source_end);
    const double shift = eff.onset_shift.value();
    const double attack_extra = (attack_end - start) * (eff.attack_time_scale.value() - 1.0);
    const double body_extra = (end - attack_end) * (eff.length_scale.value() - 1.0);

    std::vector<Anchor> anchors{
        {.s = start, .t = start + shift, .note = index},
        {.s = attack_end, .t = attack_end + shift + attack_extra, .note = index}};
    for (const TimePair& h : eff.handles) {
        if (raw(h.source) > attack_end && raw(h.source) < end) {
            anchors.push_back({.s = raw(h.source), .t = raw(h.output), .note = index});
        }
    }
    anchors.push_back({.s = end, .t = end + shift + attack_extra + body_extra, .note = index});

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

/// Appends `note` anchors after `done`. A coincident source point keeps the later output; an
/// output that differs from the earlier request marks the later note as repaired.
void append(std::vector<Anchor>& done, const std::vector<Anchor>& anchors,
            std::vector<bool>& repaired) {
    for (const Anchor& a : anchors) {
        if (!done.empty() && done.back().s == a.s) {
            if (done.back().note != k_no_note && done.back().t != a.t) {
                repaired[static_cast<std::size_t>(a.note)] = true;
            }
            done.back().t = std::max(done.back().t, a.t);
            done.back().note = a.note;
        } else {
            done.push_back(a);
        }
    }
}

/// Smallest output length a segment may have: the builder's minimum, or its source length if
/// that is shorter.
double needed_length(const Anchor& a, const Anchor& b, double min_segment_s) {
    return std::min(min_segment_s, b.s - a.s);
}

/// Pushes outputs later so every segment keeps its needed length; the last anchor is the fixed
/// end when `fixed_end`. Then pulls earlier anchors back from the fixed end. Returns false if the
/// two passes cannot both be satisfied (the anchors are then left unspecified).
bool enforce_min_segments(std::vector<Anchor>& done, bool fixed_end, double min_segment_s) {
    const std::size_t last = done.size() - (fixed_end ? 2 : 1);
    for (std::size_t k = 1; k <= last; ++k) {
        done[k].t =
            std::max(done[k].t, done[k - 1].t + needed_length(done[k - 1], done[k], min_segment_s));
    }
    if (!fixed_end) {
        return true;
    }
    for (std::size_t k = done.size() - 1; k-- > 0;) {
        done[k].t =
            std::min(done[k].t, done[k + 1].t - needed_length(done[k], done[k + 1], min_segment_s));
    }
    for (std::size_t k = 1; k < done.size(); ++k) {
        if (done[k].t <
            done[k - 1].t + needed_length(done[k - 1], done[k], min_segment_s) - k_same_length_s) {
            return false;
        }
    }
    return true;
}

Result<void> check_notes(std::span<const Note> notes, const EditList& edits,
                         SourceTime source_end) {
    const Note* previous = nullptr;
    for (const Note& n : notes) {
        if (!n.is_active) {
            continue;
        }
        if (auto r = validate(n); !r) {
            return fail(ErrorCode::invalid_argument,
                        "build_time_map: invalid note: " + r.error().message);
        }
        if (!(n.track_id == edits.track_id())) {
            return fail(ErrorCode::invalid_argument, "build_time_map: note is on another track");
        }
        if (previous != nullptr && n.source_start < previous->source_end) {
            return fail(
                ErrorCode::invalid_argument,
                "build_time_map: active notes overlap or are unsorted (polyphony is Phase 11)");
        }
        if (source_end != SourceTime{} && n.source_end > source_end) {
            return fail(ErrorCode::invalid_argument,
                        "build_time_map: a note ends after params.source_end");
        }
        previous = &n;
    }
    return {};
}

/// Rounds the shift t - s of every anchor to a whole number of samples (ADR-0023), so two anchors
/// that bound a slope-1 span (equal shift) keep equal shifts. Strictness: an output at or before
/// the previous one takes one more sample of shift until it is later. Returns false, leaving the
/// anchors as they were, if that would push an anchor to or past the fixed end.
bool quantise_shifts(std::vector<Anchor>& anchors, bool fixed_end, double rate_hz) {
    const std::vector<Anchor> before = anchors;
    const double sample = 1.0 / rate_hz;
    for (std::size_t k = 1; k < anchors.size(); ++k) {
        double shift = std::round((anchors[k].t - anchors[k].s) * rate_hz) * sample;
        double t = anchors[k].s + shift;
        while (t <= anchors[k - 1].t) {
            shift += sample;
            t = anchors[k].s + shift;
        }
        anchors[k].t = t;
    }
    const bool end_moved = fixed_end && anchors.back().t != before.back().t;
    if (end_moved) {
        anchors = before;
        return false;
    }
    return true;
}

bool is_identity(const std::vector<Anchor>& anchors) {
    return std::ranges::all_of(anchors, [](const Anchor& a) { return a.s == a.t; });
}

}  // namespace anno::timemap_detail
