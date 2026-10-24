#pragma once

#include "core/types/effective_note.h"
#include "support/test_data.h"

#include <algorithm>
#include <random>
#include <vector>

// Seeded generators for property-based contract tests. Every generator takes the engine by
// reference, so a failing case is reproduced by its seed alone (printed by the tests).

namespace anno::test {

using Rng = std::mt19937_64;

inline double uniform(Rng& rng, double lo, double hi) {
    return std::uniform_real_distribution<double>{lo, hi}(rng);
}
inline int uniform_int(Rng& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>{lo, hi}(rng);
}

/// n sorted, non-overlapping notes. Deliberately includes the awkward shapes: a note at 0,
/// abutting notes, sub-millisecond gaps, zero-length attacks and releases.
inline std::vector<Note> random_notes(Rng& rng, int n) {
    std::vector<Note> notes;
    double cursor = uniform_int(rng, 0, 3) == 0 ? 0.0 : uniform(rng, 0.0, 1.0);
    for (int i = 0; i < n; ++i) {
        const double length = uniform(rng, 0.02, 1.5);
        Note note = make_note("n" + std::to_string(i), cursor, cursor + length,
                              uniform(rng, 5000.0, 8000.0));
        if (uniform_int(rng, 0, 5) == 0) {
            note.attack_end = note.source_start;
        }
        if (uniform_int(rng, 0, 5) == 0) {
            note.release_start = note.source_end;
        }
        notes.push_back(std::move(note));
        const int gap_kind = uniform_int(rng, 0, 3);
        double gap = 0.0;  // abutting
        if (gap_kind == 1) {
            gap = 0.0004;  // sub-millisecond
        } else if (gap_kind >= 2) {
            gap = uniform(rng, 0.0, 1.0);
        }
        cursor += length + gap;
    }
    return notes;
}

inline bool is_timing(EditParam p) {
    return param_info(p).category == EditCategory::time || p == EditParam::attack_time_scale;
}

/// A legal EditSpec for one random note and one random non-reset operation.
inline EditSpec random_spec(Rng& rng, const std::vector<Note>& notes, bool allow_timing = true) {
    const Note& note =
        notes[static_cast<std::size_t>(uniform_int(rng, 0, static_cast<int>(notes.size()) - 1))];
    EditParam p = EditParam::center_offset;
    do {
        p = static_cast<EditParam>(uniform_int(rng, 0, k_edit_param_count - 1));
    } while ((!allow_timing && is_timing(p)) || p == EditParam::gap_gain);  // gap edits target gaps
    const ParamInfo& info = param_info(p);
    EditOp op = EditOp::set;
    if (info.allows_add && uniform_int(rng, 0, 1) == 0) {
        op = EditOp::add;
    }
    if (info.allows_scale && uniform_int(rng, 0, 1) == 0) {
        op = EditOp::scale;
    }
    EditValue value;
    switch (info.kind) {
    case ValueKind::cents: value = Cents{uniform(rng, -300.0, 300.0)}; break;
    case ValueKind::decibels: value = Decibels{uniform(rng, -12.0, 12.0)}; break;
    case ValueKind::seconds:
        value =
            Seconds{(p == EditParam::onset_shift || op != EditOp::set) ? uniform(rng, -1.5, 1.5)
                                                                       : uniform(rng, 0.0, 0.5)};
        break;
    case ValueKind::ratio:
        if (p == EditParam::within_note_flatten) {
            value = Ratio{uniform(rng, 0.0, 1.0)};
        } else if (op == EditOp::scale) {
            value = Ratio{uniform(rng, 0.25, 4.0)};
        } else {
            value = Ratio{uniform(rng, 0.5, 2.0)};
        }
        break;
    case ValueKind::fade_shape: value = static_cast<FadeShape>(uniform_int(rng, 0, 2)); break;
    case ValueKind::time_pair: {
        const double s = uniform(rng, note.source_start.unchecked_since_origin().value(),
                                 note.source_end.unchecked_since_origin().value());
        value = TimePair{.source = src(s), .output = out(s + uniform(rng, -0.5, 0.5))};
        break;
    }
    }
    return make_spec(note.id.value, info.category, p, op, value);
}

/// A random group of 1..3 specs.
inline std::vector<EditSpec> random_group(Rng& rng, const std::vector<Note>& notes,
                                          bool allow_timing = true) {
    std::vector<EditSpec> group;
    const int n = uniform_int(rng, 1, 3);
    group.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        group.push_back(random_spec(rng, notes, allow_timing));
    }
    return group;
}

/// Neutral value of an EffectiveNote for `note`.
inline EffectiveNote neutral_state(const Note& note) {
    EffectiveNote eff;
    eff.note_id = note.id;
    eff.center = note.pitch_center;
    return eff;
}

inline constexpr int k_property_cases = 300;

}  // namespace anno::test
