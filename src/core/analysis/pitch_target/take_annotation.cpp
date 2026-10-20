#include "core/analysis/pitch_target/take_annotation.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace anno {

namespace {

double median_of(std::vector<double> v) {
    std::ranges::sort(v);
    return v[v.size() / 2];
}

bool has_plain_target(const PitchTarget& t) {
    return t.kind == PitchTargetKind::whole || t.kind == PitchTargetKind::settled;
}

/// Offset (cents, in (-50, 50]) of a pitch from the nearest semitone of the grid.
double offset_of(double pitch_cents, double tuning_offset) {
    const double rel = pitch_cents - tuning_offset;
    return rel - 100.0 * std::round(rel / 100.0);
}

int pitch_class_of(double pitch_cents, double tuning_offset) {
    const long semitone = std::lround((pitch_cents - tuning_offset) / 100.0);
    return static_cast<int>(((semitone % 12) + 12) % 12);
}

double gap_between(const Note& a, const Note& b) {
    return (b.source_start - a.source_end).value();
}

void flag_runs(std::span<const Note> notes, const TakeParams& tp, std::vector<PitchFlags>& out) {
    std::size_t begin = 0;
    while (begin < notes.size()) {
        std::size_t end = begin;
        while (
            end < notes.size() &&
            (notes[end].source_end - notes[end].source_start) <= tp.run_max_note &&
            (end == begin || gap_between(notes[end - 1], notes[end]) <= tp.run_max_gap.value())) {
            ++end;
        }
        if (end > begin && end - begin >= static_cast<std::size_t>(tp.run_min_notes)) {
            for (std::size_t i = begin; i < end; ++i) {
                out[i].set(PitchFlag::run);
            }
        }
        begin = std::max(end, begin + 1);
    }
}

/// The take's median offset from the grid over its whole/settled notes (0 if none).
double take_offset(std::span<const Note> notes, double tuning_offset) {
    std::vector<double> offsets;
    for (const Note& n : notes) {
        if (has_plain_target(n.pitch_target)) {
            offsets.push_back(offset_of(n.pitch_target.target.value(), tuning_offset));
        }
    }
    return offsets.empty() ? 0.0 : median_of(offsets);
}

void flag_off_grid(std::span<const Note> notes, const PitchTargetParams& params, double common,
                   std::vector<PitchFlags>& out) {
    const TakeParams& tp = params.take;
    const double tuning = params.tuning_offset.value();
    std::map<int, std::vector<std::size_t>> by_class;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (has_plain_target(notes[i].pitch_target)) {
            by_class[pitch_class_of(notes[i].pitch_target.target.value(), tuning)].push_back(i);
        }
    }
    for (const auto& [pitch_class, members] : by_class) {
        std::vector<double> adjusted;
        for (const std::size_t i : members) {
            adjusted.push_back(offset_of(notes[i].pitch_target.target.value(), tuning) - common);
        }
        const double centre = median_of(adjusted);
        std::vector<std::size_t> agreeing;
        for (std::size_t k = 0; k < members.size(); ++k) {
            if (std::abs(adjusted[k] - centre) <= tp.off_grid_spread.value()) {
                agreeing.push_back(members[k]);
            }
        }
        if (agreeing.size() >= static_cast<std::size_t>(tp.off_grid_min_count) &&
            std::abs(centre) >= tp.off_grid_min_offset.value()) {
            for (const std::size_t i : agreeing) {
                out[i].set(PitchFlag::consistent_off_grid);
            }
        }
    }
}

void flag_peaks(std::span<const Note> notes, const PitchTargetParams& params, double common,
                std::vector<PitchFlags>& out) {
    const TakeParams& tp = params.take;
    std::size_t begin = 0;
    while (begin < notes.size()) {
        std::size_t end = begin + 1;
        while (end < notes.size() &&
               gap_between(notes[end - 1], notes[end]) < tp.phrase_gap.value()) {
            ++end;
        }
        if (end - begin >= static_cast<std::size_t>(tp.peak_min_phrase_notes)) {
            std::optional<std::size_t> top;
            for (std::size_t i = begin; i < end; ++i) {
                if (has_plain_target(notes[i].pitch_target) &&
                    (!top || notes[i].pitch_target.target > notes[*top].pitch_target.target)) {
                    top = i;
                }
            }
            if (top) {
                const double off = offset_of(notes[*top].pitch_target.target.value(),
                                             params.tuning_offset.value()) -
                                   common;
                if (off >= tp.peak_min_offset.value() && off <= tp.peak_tolerance.value()) {
                    out[*top].set(PitchFlag::expressive_peak);
                }
            }
        }
        begin = end;
    }
}

}  // namespace

std::vector<PitchFlags> annotate_take(std::span<const Note> notes,
                                      const PitchTargetParams& params) {
    std::vector<PitchFlags> out(notes.size());
    if (notes.empty()) {
        return out;
    }
    flag_runs(notes, params.take, out);
    const double common = take_offset(notes, params.tuning_offset.value());
    flag_off_grid(notes, params, common, out);
    flag_peaks(notes, params, common, out);
    return out;
}

}  // namespace anno
