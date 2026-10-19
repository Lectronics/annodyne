#pragma once

#include "core/types/ids.h"
#include "core/types/result.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The edit vocabulary (architecture spec §5.1, §5.2; ADR-0005). Tools and macros return groups of
// EditSpec; only EditList (core/edits) turns them into recorded Edits.
// Design: docs/design/phase1-data-model.md.

namespace anno {

/// A source instant and where it lands in the output. One anchor of a TimeMap, and one time handle
/// of a `time_handle` edit.
struct TimePair {
    SourceTime source;
    OutputTime output;

    friend bool operator==(const TimePair&, const TimePair&) = default;
};

/// Shape of a fade (and of crossfades in core/dsp later).
enum class FadeShape { equal_power, linear, log };

enum class EditCategory {
    pitch,
    pitch_mod,
    pitch_drift,
    transition,
    formant,
    amplitude,
    fade,
    sibilant,
    time,
    attack,
    separation,  // no parameters yet (Phase 5)
    assignment,  // no parameters yet (Phase 7)
    sound        // no parameters yet (Phase 12)
};

enum class EditOp { set, add, scale, reset };

/// Closed set of editable parameters. The comment gives the category and value type.
enum class EditParam {
    center_offset,         // pitch        Cents
    mod_scale,             // pitch_mod    Ratio
    drift_scale,           // pitch_drift  Ratio
    transition_in_scale,   // transition   Ratio
    transition_out_scale,  // transition   Ratio
    formant_offset,        // formant      Cents
    gain,                  // amplitude    Decibels
    within_note_flatten,   // amplitude    Ratio, 0..1
    fade_in,               // fade         Seconds
    fade_out,              // fade         Seconds
    fade_shape,            // fade         FadeShape
    noise_gain_voiced,     // sibilant     Decibels
    noise_gain_sibilant,   // sibilant     Decibels
    noise_gain_breath,     // sibilant     Decibels
    noise_gain_plosive,    // sibilant     Decibels
    onset_shift,           // time         Seconds
    length_scale,          // time         Ratio, > 0
    time_handle,           // time         TimePair
    attack_time_scale      // attack       Ratio, > 0
};

inline constexpr int k_edit_param_count = 19;

enum class ValueKind { cents, ratio, decibels, seconds, fade_shape, time_pair };

/// Value carried by an edit. monostate for `reset` without a handle.
using EditValue =
    std::variant<std::monostate, Cents, Ratio, Decibels, Seconds, FadeShape, TimePair>;

/// Static facts about one parameter. The value type of every op is `kind`.
struct ParamInfo {
    EditParam param;
    EditCategory category;
    ValueKind kind;
    bool allows_add;    // offsets (cents, decibels, seconds)
    bool allows_scale;  // multipliers (ratio with neutral 1); amounts such as within_note_flatten allow set only
    double neutral;     // 0 for offsets, 1 for multipliers; unused for fade_shape/time_handle
    double set_min;     // legal range of a `set` value (inclusive)
    double set_max;
    double scale_min;   // legal lower bound (exclusive) of a `scale` value
};

/// Table lookup. Pure; real-time safe.
[[nodiscard]] const ParamInfo& param_info(EditParam param) noexcept;

/// Smallest legal value of length_scale / attack_time_scale after folding (1/1000 = 1000x
/// compression); keeps the TimeMap invertible.
inline constexpr Ratio k_min_time_scale{1e-3};
/// Tolerance for the key of a time handle: two handles whose source times differ by less than this
/// are the same handle.
inline constexpr Seconds k_handle_key{1e-6};

/// Who an edit applies to.
struct EditTarget {
    std::vector<NoteId> note_ids;           // live targets
    std::vector<NoteId> orphaned_note_ids;  // re-analysis could not re-attach these; kept, ignored by the fold
};

/// What a tool or macro returns: the content of one edit, without bookkeeping.
struct EditSpec {
    std::string origin;  // "main_tool", "correct_pitch", "reset", ...
    EditTarget target;
    EditCategory category = EditCategory::pitch;
    std::optional<EditParam> param;  // empty only for op == reset (resets the whole category)
    EditOp op = EditOp::set;
    EditValue value;
};

/// History metadata only; never influences the fold.
using WallTime = std::chrono::system_clock::time_point;

/// A recorded edit. By convention only EditList creates these.
struct Edit {
    EditSeq seq;
    GroupId group;
    WallTime created_at;
    EditSpec spec;
};

/// Checks a spec as it may appear in a recorded Edit: non-empty origin; note_ids or
/// orphaned_note_ids non-empty, no duplicates; the param belongs to the category; param is empty
/// only for reset; the op is allowed for the parameter; the value type matches (reset: monostate,
/// or a TimePair when the param is time_handle); numbers are finite and inside the legal range.
/// Errors: invalid_argument. Pure; thread-safe; not real-time safe.
[[nodiscard]] Result<void> validate(const EditSpec& spec);

/// validate() plus the rules for a spec entering EditList::add: note_ids non-empty and
/// orphaned_note_ids empty. Errors: invalid_argument.
[[nodiscard]] Result<void> validate_for_add(const EditSpec& spec);

}  // namespace anno
