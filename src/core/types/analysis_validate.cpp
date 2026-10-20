#include "core/types/analysis_result.h"
#include "core/types/frame_grid.h"
#include "core/types/note.h"
#include "core/types/tracks.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace anno {

namespace {

Unexpected invalid(std::string message) {
    return fail(ErrorCode::invalid_argument, std::move(message));
}

bool is_finite_time(SourceTime t) {
    return std::isfinite(t.unchecked_since_origin().value());
}

bool is_prob(double p) {
    return p >= 0.0 && p <= 1.0;
}  // false for NaN

bool all_prob(const std::vector<double>& values) {
    return std::ranges::all_of(values, is_prob);
}

/// A frame-aligned array/matrix has exactly grid.n_frames entries; an empty track has none.
bool matches(const FrameGrid& grid, std::size_t size) {
    return std::cmp_equal(size, grid.n_frames);
}

bool matrix_matches(const FrameGrid& grid, const FrameMatrix& m) {
    return m.rows == grid.n_frames || (m.rows == 0 && m.cols == 0);
}

}  // namespace

Result<void> validate(const FrameGrid& grid) {
    if (!(grid.hop.value() > 0.0) || !std::isfinite(grid.hop.value())) {
        return invalid("frame grid hop must be finite and > 0");
    }
    if (grid.n_frames < 0) {
        return invalid("frame grid has a negative frame count");
    }
    return {};
}

SourceTime frame_time(const FrameGrid& grid, std::int64_t index) noexcept {
    ANNO_EXPECTS(index >= 0 && index < grid.n_frames);
    return grid.start + grid.hop * static_cast<double>(index);
}

Result<void> validate(const FrameMatrix& matrix) {
    if (matrix.rows < 0 || matrix.cols < 0) {
        return invalid("frame matrix has a negative dimension");
    }
    if (static_cast<std::int64_t>(matrix.data.size()) != matrix.rows * matrix.cols) {
        return invalid("frame matrix data size does not equal rows * cols");
    }
    return {};
}

Result<void> validate(const PitchTrack& track) {
    if (auto r = validate(track.grid); !r) {
        return r;
    }
    if (!matches(track.grid, track.f0.size()) ||
        !matches(track.grid, track.confidence_prob.size()) ||
        !matches(track.grid, track.voicing_prob.size())) {
        return invalid("pitch track arrays must have grid.n_frames entries");
    }
    if (!all_prob(track.confidence_prob) || !all_prob(track.voicing_prob)) {
        return invalid("pitch track probabilities must lie in 0..1");
    }
    for (const auto& alt : track.alternates) {
        if (!matches(track.grid, alt.size())) {
            return invalid("pitch track alternate has the wrong length");
        }
    }
    return {};
}

Result<void> validate(const OnsetList& onsets) {
    if (onsets.strength.size() != onsets.times.size() ||
        onsets.kind.size() != onsets.times.size()) {
        return invalid("onset list arrays must have equal length");
    }
    if (!std::ranges::all_of(onsets.strength, [](double v) { return v >= 0.0; })) {
        return invalid("onset strength must be >= 0");
    }
    return {};
}

Result<void> validate(const HarmonicTrack& track) {
    if (auto r = validate(track.grid); !r) {
        return r;
    }
    for (const FrameMatrix* m : {&track.freq_hz, &track.amp_linear, &track.phase_rad}) {
        if (auto r = validate(*m); !r) {
            return r;
        }
        if (!matrix_matches(track.grid, *m)) {
            return invalid("harmonic matrix must have grid.n_frames rows");
        }
    }
    if (track.freq_hz.cols != track.amp_linear.cols || track.freq_hz.cols != track.phase_rad.cols) {
        return invalid("harmonic matrices must have the same number of harmonics");
    }
    return {};
}

Result<void> validate(const NoiseTrack& track) {
    if (auto r = validate(track.grid); !r) {
        return r;
    }
    if (auto r = validate(track.band_energy_linear); !r) {
        return r;
    }
    if (!matrix_matches(track.grid, track.band_energy_linear)) {
        return invalid("noise band matrix must have grid.n_frames rows");
    }
    if (!track.frame_class.empty() && !matches(track.grid, track.frame_class.size())) {
        return invalid("noise frame classes must have grid.n_frames entries");
    }
    return {};
}

Result<void> validate(const EnvelopeTrack& track) {
    if (auto r = validate(track.grid); !r) {
        return r;
    }
    if (auto r = validate(track.log_mag_db); !r) {
        return r;
    }
    if (!matrix_matches(track.grid, track.log_mag_db)) {
        return invalid("envelope matrix must have grid.n_frames rows");
    }
    if (track.log_mag_db.rows > 0 &&
        std::cmp_not_equal(track.log_mag_db.cols, track.freqs.size())) {
        return invalid("envelope matrix columns must equal the number of frequencies");
    }
    return {};
}

namespace {

bool grid_empty_or_equal(const FrameGrid& nested, const FrameGrid& parent) {
    return nested.n_frames == 0 || nested == parent;
}

bool inside_note(const Note& note, SourceTime t) {
    return note.source_start <= t && t <= note.source_end;
}

Result<void> validate_pitch_target(const Note& note) {
    const PitchTarget& pt = note.pitch_target;
    if (!std::isfinite(pt.target.value()) ||
        (pt.secondary && !std::isfinite(pt.secondary->value()))) {
        return invalid("note pitch target must be finite");
    }
    if (!is_prob(pt.confidence_prob)) {
        return invalid("pitch target confidence must lie in 0..1");
    }
    if ((pt.kind == PitchTargetKind::glide || pt.kind == PitchTargetKind::ornament) &&
        !pt.secondary) {
        return invalid("a glide or ornament target needs a secondary pitch");
    }
    const auto finite_optional = [](const std::optional<Cents>& c) {
        return !c || std::isfinite(c->value());
    };
    if (!finite_optional(pt.target_whole) || !finite_optional(pt.target_settled) ||
        !finite_optional(pt.target_end)) {
        return invalid("the candidate pitch targets must be finite");
    }
    if (pt.core.duration() > Seconds{0.0} &&
        (!inside_note(note, pt.core.start()) || !inside_note(note, pt.core.end()))) {
        return invalid("the pitch target core must lie inside the note");
    }
    SourceTime previous_end = note.source_start;
    for (const TonalSegment& seg : pt.segments) {
        if (seg.range.start() < previous_end || !inside_note(note, seg.range.end()) ||
            !std::isfinite(seg.start_pitch.value()) || !std::isfinite(seg.end_pitch.value())) {
            return invalid("pitch target segments must be sorted, finite and inside the note");
        }
        previous_end = seg.range.end();
    }
    return {};
}

}  // namespace

Result<void> validate(const Note& note) {
    if (note.id.value.empty() || note.track_id.value.empty()) {
        return invalid("note needs a non-empty id and track id");
    }
    if (note.voice < 0) {
        return invalid("note voice must be >= 0");
    }
    if (!is_finite_time(note.source_start) || !is_finite_time(note.attack_end) ||
        !is_finite_time(note.release_start) || !is_finite_time(note.source_end) ||
        note.source_start < SourceTime{}) {
        return invalid("note times must be finite and not before the source origin");
    }
    if (!std::isfinite(note.pitch_center.value()) || !std::isfinite(note.loudness.value())) {
        return invalid("note pitch and loudness must be finite");
    }
    if (!(note.source_start <= note.attack_end && note.attack_end <= note.release_start &&
          note.release_start <= note.source_end)) {
        return invalid("note times must satisfy start <= attack_end <= release_start <= end");
    }
    if (!is_prob(note.confidence_prob)) {
        return invalid("note confidence must lie in 0..1");
    }
    if (auto r = validate_pitch_target(note); !r) {
        return r;
    }
    if (auto r = validate(note.grid); !r) {
        return r;
    }
    const auto finite_cents = [](Cents c) { return std::isfinite(c.value()); };
    if (!std::ranges::all_of(note.pitch_drift, finite_cents) ||
        !std::ranges::all_of(note.pitch_mod, finite_cents) ||
        !std::ranges::all_of(note.amp, [](Decibels d) { return std::isfinite(d.value()); })) {
        return invalid("note curves must be finite");
    }
    if (!matches(note.grid, note.pitch_drift.size()) ||
        !matches(note.grid, note.pitch_mod.size()) || !matches(note.grid, note.amp.size())) {
        return invalid("note curves must have grid.n_frames entries");
    }
    if (!grid_empty_or_equal(note.harmonics.grid, note.grid) ||
        !grid_empty_or_equal(note.noise.grid, note.grid) ||
        !grid_empty_or_equal(note.envelope.grid, note.grid)) {
        return invalid("a note's nested track grids must be empty or equal to the note grid");
    }
    if (auto r = validate(note.harmonics); !r) {
        return r;
    }
    if (auto r = validate(note.noise); !r) {
        return r;
    }
    return validate(note.envelope);
}

Result<void> validate(const AnalysisResult& analysis) {
    if (!(analysis.tuning_ref.value() > 0.0) || !std::isfinite(analysis.tuning_ref.value())) {
        return invalid("tuning reference must be finite and > 0");
    }
    if (analysis.pitch) {
        if (auto r = validate(*analysis.pitch); !r) {
            return r;
        }
    }
    if (analysis.residual) {
        if (auto r = validate(*analysis.residual); !r) {
            return r;
        }
    }
    if (auto r = validate(analysis.onsets); !r) {
        return r;
    }
    std::set<NoteId> seen;
    const Note* previous = nullptr;
    for (const Note& note : analysis.notes) {
        if (auto r = validate(note); !r) {
            return r;
        }
        if (!seen.insert(note.id).second) {
            return invalid("duplicate note id '" + note.id.value + "'");
        }
        if (previous != nullptr &&
            (note.source_start < previous->source_start ||
             (note.source_start == previous->source_start && note.voice < previous->voice))) {
            return invalid("notes must be sorted by (source_start, voice)");
        }
        previous = &note;
    }
    return {};
}

const Note* find_note(const AnalysisResult& analysis, const NoteId& id) noexcept {
    const auto it = std::ranges::find(analysis.notes, id, &Note::id);
    return it == analysis.notes.end() ? nullptr : &*it;
}

}  // namespace anno
