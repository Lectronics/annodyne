#include "core/analysis/segmentation/segmentation.h"

#include "core/analysis/segmentation/detail/merge_runs.h"
#include "core/analysis/segmentation/detail/note_hmm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <utility>

namespace anno {

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_smooth_reach_cents =
    120.0;  // frames this close to the centre frame are averaged
constexpr double k_onset_evidence_s =
    0.010;                              // frames this close to an onset get onset_new_note_prob
constexpr double k_soft_gap_s = 0.020;  // abutting notes: separation is soft
constexpr double k_confidence_band_cents = 60.0;
constexpr int k_min_frames = 10;  // pitch frames every note has at least
constexpr std::size_t k_absorb_gap_frames =
    3;  // unvoiced frames tolerated between a short run and its neighbour

Result<void> validate_params(const SegmentParams& p) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    const auto prob = [](double v) { return std::isfinite(v) && v > 0.0 && v < 1.0; };
    const bool bad = !positive(p.min_note.value()) || !positive(p.vibrato_smooth.value()) ||
                     !positive(p.note_bin_cents) || !positive(p.pitch_sigma_cents) ||
                     !prob(p.stay_stable) || !prob(p.stable_to_silence) || !prob(p.new_note_prob) ||
                     p.stay_stable + p.stable_to_silence > 1.0 || !prob(p.onset_new_note_prob) ||
                     p.voiced_floor_prob < 0.5 || p.voiced_floor_prob >= 1.0 ||
                     std::isnan(p.voiced_floor_prob) || !positive(p.preroll_max.value()) ||
                     !positive(p.stable_band.value()) || !positive(p.stable_min.value()) ||
                     !positive(p.merge_onset_strength) || !positive(p.merge_max_cents.value()) ||
                     !positive(p.merge_onset_zone.value()) ||
                     !positive(p.min_gap_for_split.value()) ||
                     !positive(p.trill_max_plateau.value()) || p.trill_min_steps < 1 ||
                     !positive(p.snap_before.value()) || !positive(p.snap_after.value());
    if (bad) {
        return fail(ErrorCode::invalid_argument, "segment_notes: invalid parameters");
    }
    return {};
}

/// Moving mean of f0 over the window, using only the voiced frames within `k_smooth_reach_cents` of
/// the frame's own value: a vibrato is averaged, a note change is not blurred.
std::vector<double> smooth_f0(const PitchTrack& pitch, Seconds window) {
    const std::size_t n = pitch.f0.size();
    const auto half = static_cast<std::size_t>(
        std::max(1L, std::lround(window.value() / pitch.grid.hop.value() / 2.0)));
    std::vector<double> out(n, k_nan);
    for (std::size_t t = 0; t < n; ++t) {
        if (!is_voiced(pitch, t)) {
            continue;
        }
        const double own = pitch.f0[t].value();
        double sum = 0.0;
        int count = 0;
        for (std::size_t k = t > half ? t - half : 0; k < std::min(n, t + half + 1); ++k) {
            if (is_voiced(pitch, k) &&
                std::abs(pitch.f0[k].value() - own) <= k_smooth_reach_cents) {
                sum += pitch.f0[k].value();
                ++count;
            }
        }
        out[t] = sum / count;
    }
    return out;
}

/// P(a new note starts) per frame: `base` except within k_onset_evidence_s of an onset of at least
/// `min_strength`.
std::vector<double> new_note_probability(const PitchTrack& pitch, const OnsetList& onsets,
                                         double base, double at_onset, double min_strength) {
    std::vector<double> p(pitch.f0.size(), base);
    const double zone = k_onset_evidence_s / pitch.grid.hop.value();
    for (std::size_t k = 0; k < onsets.times.size(); ++k) {
        if (onsets.strength[k] < min_strength) {
            continue;  // too weak to be evidence of a new note
        }
        const SourceTime t = onsets.times[k];
        const double centre = (t - pitch.grid.start).value() / pitch.grid.hop.value();
        const auto lo = static_cast<std::int64_t>(std::max(0.0, std::ceil(centre - zone)));
        const auto hi = static_cast<std::int64_t>(
            std::min(static_cast<double>(p.size()) - 1.0, std::floor(centre + zone)));
        for (std::int64_t i = lo; i <= hi; ++i) {
            p[static_cast<std::size_t>(i)] = at_onset;
        }
    }
    return p;
}

std::size_t run_frames(const NoteRun& r) {
    return r.last - r.first + 1;
}

/// Absorbs runs shorter than `min_frames`, right to left: into the following run when it is
/// adjacent (a glide becomes the attack of the note it reaches, and a chain of short runs collects
/// onto its target), else into a preceding long run when adjacent, else the run is dropped.
void absorb_short_runs(std::vector<NoteRun>& runs, std::size_t min_frames) {
    for (std::size_t i = runs.size(); i-- > 0;) {
        if (run_frames(runs[i]) >= min_frames) {
            continue;
        }
        if (i + 1 < runs.size() && runs[i + 1].first - runs[i].last - 1 <= k_absorb_gap_frames) {
            runs[i + 1].first = runs[i].first;
        } else if (i > 0 && run_frames(runs[i - 1]) >= min_frames &&
                   runs[i].first - runs[i - 1].last - 1 <= k_absorb_gap_frames) {
            runs[i - 1].last = runs[i].last;
        }
        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

double median_of(std::vector<double> v) {
    std::ranges::sort(v);
    return v[v.size() / 2];
}

struct Region {
    std::size_t first = 0;  // frames, inclusive
    std::size_t last = 0;
};

/// The longest-leading run of voiced frames of [first, last] whose smoothed f0 is within `band` of
/// `reference` for at least `min_frames`; scanning forward (or backward). Empty if none.
std::optional<std::size_t> stable_edge(const std::vector<double>& smoothed, const Region& r,
                                       double reference, double band, std::size_t min_frames,
                                       bool forward) {
    std::size_t streak = 0;
    const std::size_t length = r.last - r.first + 1;
    for (std::size_t k = 0; k < length; ++k) {
        const std::size_t t = forward ? r.first + k : r.last - k;
        const bool ok = !std::isnan(smoothed[t]) && std::abs(smoothed[t] - reference) <= band;
        streak = ok ? streak + 1 : 0;
        if (streak >= min_frames) {
            return forward ? t + 1 - min_frames : t + min_frames - 1;
        }
    }
    return std::nullopt;
}

std::string make_id(double start_s, std::set<std::string>& used) {
    std::string digits = std::to_string(std::llround(start_s * 1000.0));
    if (digits.size() < 6) {
        digits.insert(0, 6 - digits.size(), '0');
    }
    const std::string base = "n" + digits;
    std::string id = base;
    for (int suffix = 1; used.contains(id); ++suffix) {
        id = base + "_" + std::to_string(suffix);
    }
    used.insert(id);
    return id;
}

/// Pitch-frame time of `i`.
double time_of(const FrameGrid& g, std::size_t i) {
    return (g.start - SourceTime{}).value() + static_cast<double>(i) * g.hop.value();
}

struct Draft {
    Region frames;  // first/last voiced frame of the run
    double start_s = 0.0;
    double end_s = 0.0;
    double pitch = 0.0;  // reference cents (median of the voiced smoothed f0)
};

/// Pulls a note's start onto an onset: the earliest consonant onset up to `preroll` before it, else
/// the nearest onset within the snap zone; never before `floor_s`.
double snap_start(double start_s, const OnsetList& onsets, const SegmentParams& params,
                  double floor_s) {
    const double preroll = params.preroll_max.value();
    std::optional<double> consonant;
    std::optional<double> nearest;
    for (std::size_t i = 0; i < onsets.times.size(); ++i) {
        const double t = (onsets.times[i] - SourceTime{}).value();
        if (t < floor_s) {
            continue;
        }
        if (onsets.kind[i] == OnsetKind::consonant && t <= start_s && start_s - t <= preroll) {
            consonant = consonant ? std::min(*consonant, t) : t;
        }
        if (t >= start_s - params.snap_before.value() && t <= start_s + params.snap_after.value() &&
            (!nearest || std::abs(t - start_s) < std::abs(*nearest - start_s))) {
            nearest = t;
        }
    }
    if (consonant) {
        return *consonant;
    }
    return nearest ? *nearest : start_s;
}

/// Voiced extent and reference pitch of every run (runs without a voiced frame vanish).
std::vector<Draft> make_drafts(const PitchTrack& pitch, const std::vector<double>& smoothed,
                               const std::vector<NoteRun>& runs) {
    const double hop = pitch.grid.hop.value();
    const double last_time = time_of(pitch.grid, pitch.f0.size() - 1);
    std::vector<Draft> drafts;
    for (const NoteRun& run : runs) {
        std::optional<std::size_t> first;
        std::optional<std::size_t> last;
        std::vector<double> values;
        for (std::size_t t = run.first; t <= run.last; ++t) {
            if (is_voiced(pitch, t)) {
                first = first ? *first : t;
                last = t;
                values.push_back(smoothed[t]);
            }
        }
        if (first) {
            drafts.push_back({.frames = {.first = *first, .last = *last},
                              .start_s = time_of(pitch.grid, *first),
                              .end_s = std::min(last_time, time_of(pitch.grid, *last) + hop),
                              .pitch = median_of(values)});
        }
    }
    return drafts;
}

/// Starts (onset snapping, left to right), then ends (never past the next start).
void place_times(std::vector<Draft>& drafts, const OnsetList& onsets, const SegmentParams& params,
                 double last_time) {
    double previous_end = 0.0;
    for (Draft& d : drafts) {
        d.start_s = std::clamp(snap_start(d.start_s, onsets, params, previous_end), previous_end,
                               last_time);
        previous_end = std::max(previous_end, d.end_s);
    }
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        if (i + 1 < drafts.size()) {
            drafts[i].end_s = std::min(drafts[i].end_s, drafts[i + 1].start_s);
        }
        drafts[i].end_s = std::max(drafts[i].end_s, drafts[i].start_s);
    }
}

struct Statistics {
    double pitch = 0.0;       // median smoothed f0 over the stable part
    double confidence = 0.0;  // 0..1
};

/// Pitch of the stable part [attack_end, release_start) and the confidence of the whole note.
Statistics note_statistics(const PitchTrack& pitch, const std::vector<double>& smoothed,
                           const Draft& d, double attack_end, double release_start) {
    std::vector<double> stable_values;
    double voicing_sum = 0.0;
    int near = 0;
    int voiced_frames = 0;
    for (std::size_t t = d.frames.first; t <= d.frames.last; ++t) {
        voicing_sum += pitch.voicing_prob[t];
        if (!is_voiced(pitch, t)) {
            continue;
        }
        ++voiced_frames;
        near += std::abs(smoothed[t] - d.pitch) <= k_confidence_band_cents ? 1 : 0;
        const double time = time_of(pitch.grid, t);
        if (time >= attack_end && time < release_start) {
            stable_values.push_back(smoothed[t]);
        }
    }
    const auto frames = static_cast<double>(d.frames.last - d.frames.first + 1);
    const double share = static_cast<double>(near) / std::max(1, voiced_frames);
    return {.pitch = stable_values.empty() ? d.pitch : median_of(stable_values),
            .confidence = std::clamp(voicing_sum / frames * share, 0.0, 1.0)};
}

/// The three inner times of a note. `attack_end` is where the vowel starts: the first frame of a
/// voiced stretch of at least `stable_min`; what precedes it (a consonant, breath or the onset's
/// transient) is the attack. `stable_start` / `release_start` bound the part where the smoothed f0
/// holds within `stable_band` of the note's pitch; only the pitch statistics use `stable_start`.
struct InnerTimes {
    double attack_end = 0.0;
    double stable_start = 0.0;
    double release_start = 0.0;
};

std::optional<std::size_t> first_voiced_stretch(const PitchTrack& pitch, const Region& frames,
                                                std::size_t min_frames) {
    std::size_t run = 0;
    for (std::size_t t = frames.first; t <= frames.last; ++t) {
        run = is_voiced(pitch, t) ? run + 1 : 0;
        if (run >= min_frames) {
            return t + 1 - run;
        }
    }
    return std::nullopt;
}

InnerTimes inner_times(const PitchTrack& pitch, const std::vector<double>& smoothed, const Draft& d,
                       const SegmentParams& params) {
    const double hop = pitch.grid.hop.value();
    const auto stable_min =
        static_cast<std::size_t>(std::max(2L, std::lround(params.stable_min.value() / hop)));
    const auto voiced = first_voiced_stretch(pitch, d.frames, stable_min);
    const double attack_end =
        voiced ? std::clamp(time_of(pitch.grid, *voiced), d.start_s, d.end_s) : d.start_s;
    const auto in =
        stable_edge(smoothed, d.frames, d.pitch, params.stable_band.value(), stable_min, true);
    const auto out =
        stable_edge(smoothed, d.frames, d.pitch, params.stable_band.value(), stable_min, false);
    if (!in || !out || *in > *out) {
        return {.attack_end = attack_end, .stable_start = d.start_s, .release_start = d.end_s};
    }
    const double stable_start = std::clamp(time_of(pitch.grid, *in), d.start_s, d.end_s);
    return {.attack_end = attack_end,
            .stable_start = stable_start,
            .release_start = std::clamp(time_of(pitch.grid, *out) + hop,
                                        std::max(attack_end, stable_start), d.end_s)};
}

/// The HMM input of a track: the smoothed f0, the voicing, the per-frame new-note probability and
/// the pitch bins covering the voiced range; empty if no frame is voiced.
std::optional<NoteHmmInput> build_hmm_input(const PitchTrack& pitch, const OnsetList& onsets,
                                            const std::vector<double>& smoothed,
                                            const SegmentParams& params) {
    std::vector<double> voiced_cents;
    for (std::size_t t = 0; t < pitch.f0.size(); ++t) {
        if (is_voiced(pitch, t)) {
            voiced_cents.push_back(pitch.f0[t].value());
        }
    }
    if (voiced_cents.empty()) {
        return std::nullopt;
    }
    const auto [lo_it, hi_it] = std::ranges::minmax_element(voiced_cents);
    const double margin = 2.0 * params.note_bin_cents;
    NoteHmmInput input;
    input.smoothed_cents = smoothed;
    input.voicing_prob = pitch.voicing_prob;
    for (std::size_t t = 0; t < input.voicing_prob.size(); ++t) {
        if (is_voiced(pitch, t)) {
            input.voicing_prob[t] = std::max(input.voicing_prob[t], params.voiced_floor_prob);
        }
    }
    input.new_note_prob =
        new_note_probability(pitch, onsets, params.new_note_prob, params.onset_new_note_prob,
                             params.merge_onset_strength);
    input.min_cents = *lo_it - margin;
    input.bin_cents = params.note_bin_cents;
    input.bin_count =
        static_cast<int>(std::ceil((*hi_it - *lo_it + 2.0 * margin) / params.note_bin_cents)) + 1;
    return input;
}

/// Drops notes that the later stages could not describe: fewer than `min_frames` pitch frames
/// inside [start, end) after the times were snapped and clamped, or fewer than 3 voiced ones.
void drop_unusable(std::vector<Draft>& drafts, const PitchTrack& pitch, std::size_t min_frames) {
    constexpr std::int64_t k_min_voiced = 3;
    std::erase_if(drafts, [&](const Draft& d) {
        NoteSpan probe;
        probe.source_start = SourceTime{} + Seconds{d.start_s};
        probe.source_end = SourceTime{} + Seconds{d.end_s};
        const FrameGrid grid = note_grid(probe, pitch.grid);
        if (std::cmp_less(grid.n_frames, min_frames)) {
            return true;
        }
        const auto first = static_cast<std::size_t>(
            std::llround((grid.start - pitch.grid.start).value() / pitch.grid.hop.value()));
        std::int64_t voiced = 0;
        for (std::int64_t i = 0; i < grid.n_frames; ++i) {
            voiced += is_voiced(pitch, first + static_cast<std::size_t>(i)) ? 1 : 0;
        }
        return voiced < k_min_voiced;
    });
}

}  // namespace

Result<std::vector<NoteSpan>> segment_notes(const PitchTrack& pitch, const OnsetList& onsets,
                                            const SegmentParams& params) {
    if (auto r = validate_params(params); !r) {
        return Unexpected{r.error()};
    }
    if (auto r = validate(pitch); !r) {
        return fail(ErrorCode::invalid_argument,
                    "segment_notes: invalid pitch track: " + r.error().message);
    }
    if (auto r = validate(onsets); !r) {
        return fail(ErrorCode::invalid_argument,
                    "segment_notes: invalid onset list: " + r.error().message);
    }
    std::vector<NoteSpan> notes;
    const std::size_t n = pitch.f0.size();
    const double hop = pitch.grid.hop.value();
    const std::vector<double> smoothed = smooth_f0(pitch, params.vibrato_smooth);
    const std::optional<NoteHmmInput> input = build_hmm_input(pitch, onsets, smoothed, params);
    if (!input) {
        return notes;  // nothing voiced
    }

    // 1. HMM over vibrato-smoothed f0; 2. runs nothing separates are merged; 3. short runs are
    // absorbed or dropped.
    std::vector<NoteRun> runs = decode_note_runs(*input, params);
    runs = merge_runs(
        std::move(runs),
        {.pitch = &pitch, .smoothed_cents = &smoothed, .onsets = &onsets, .params = &params});
    const auto min_frames = static_cast<std::size_t>(
        std::max<long>(k_min_frames, std::lround(params.min_note.value() / hop)));
    absorb_short_runs(runs, min_frames);

    // 3. Voiced extents, snapped times, then the description of every note.
    std::vector<Draft> drafts = make_drafts(pitch, smoothed, runs);
    place_times(drafts, onsets, params, time_of(pitch.grid, n - 1));
    drop_unusable(drafts, pitch, min_frames);
    std::set<std::string> used;
    for (const Draft& d : drafts) {
        const InnerTimes inner = inner_times(pitch, smoothed, d, params);
        const Statistics stats =
            note_statistics(pitch, smoothed, d, inner.stable_start, inner.release_start);
        NoteSpan span;
        span.id = NoteId{make_id(d.start_s, used)};
        span.source_start = SourceTime{} + Seconds{d.start_s};
        span.attack_end = SourceTime{} + Seconds{inner.attack_end};
        span.release_start = SourceTime{} + Seconds{inner.release_start};
        span.source_end = SourceTime{} + Seconds{d.end_s};
        span.rough_pitch = Cents{stats.pitch};
        span.confidence_prob = stats.confidence;
        const bool abuts =
            !notes.empty() &&
            d.start_s - (notes.back().source_end - SourceTime{}).value() <= k_soft_gap_s;
        span.separation_in = abuts ? SeparationIn::soft : SeparationIn::hard;
        notes.push_back(std::move(span));
    }
    return notes;
}

}  // namespace anno
