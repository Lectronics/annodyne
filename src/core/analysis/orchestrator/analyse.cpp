#include "core/analysis/orchestrator/analyse.h"

#include "core/dsp/k_weight.h"
#include "core/dsp/mono.h"

#include <cmath>
#include <utility>

namespace anno {

namespace {

constexpr double k_min_source_rate_hz = 4000.0;  // K-weighting design limit (core/dsp/k_weight.h)

/// Adds the stage name to an error.
Unexpected stage_error(const char* stage, const Error& e) {
    return Unexpected{Error{.code = e.code, .message = std::string{stage} + ": " + e.message}};
}

/// What the pitch target needs from the neighbouring spans: their rough pitch and the silent time
/// between the notes.
NoteContext context_of(const std::vector<NoteSpan>& spans, std::size_t i) {
    NoteContext ctx;
    if (i > 0) {
        ctx.previous_pitch = spans[i - 1].rough_pitch;
        ctx.gap_before = spans[i].source_start - spans[i - 1].source_end;
    }
    if (i + 1 < spans.size()) {
        ctx.next_pitch = spans[i + 1].rough_pitch;
        ctx.gap_after = spans[i + 1].source_start - spans[i].source_end;
    }
    return ctx;
}

/// The pitch tracker's voicing and second-best f0 on the note's frames (the note grid is a window
/// of the pitch grid with the same hop).
struct NoteEvidence {
    std::vector<double> voicing_prob;
    std::vector<Cents> alternate_f0;  // empty when the tracker gives no usable second path
};

NoteEvidence evidence_of(const FrameGrid& note_grid, const PitchTrack& pitch) {
    const auto first = static_cast<std::int64_t>(
        std::llround(((note_grid.start - pitch.grid.start) / pitch.grid.hop).value()));
    NoteEvidence e;
    const bool has_alt = !pitch.alternates.empty();
    bool any_alt = false;
    for (std::int64_t i = 0; i < note_grid.n_frames; ++i) {
        const auto k = static_cast<std::size_t>(first + i);
        e.voicing_prob.push_back(pitch.voicing_prob[k]);
        const Cents alt = has_alt ? pitch.alternates[0][k] : Cents{std::nan("")};
        any_alt = any_alt || std::isfinite(alt.value());
        e.alternate_f0.push_back(alt);
    }
    if (!any_alt) {
        e.alternate_f0.clear();
    }
    return e;
}

/// Harmonics, noise bands and sound classes, and envelope of the note (Phase 4).
Result<void> fill_phase4_tracks(Note& note, const PitchTrack& pitch, const MonoAudio& plain,
                                const AnalyseParams& params) {
    const NoteCurves curves{.grid = note.grid,
                            .pitch_center = note.pitch_center,
                            .pitch_drift = note.pitch_drift,
                            .pitch_mod = note.pitch_mod};
    auto harmonics = analyse_partials(plain.samples, plain.sample_rate, curves, params.partials);
    if (!harmonics) {
        return stage_error("partials", harmonics.error());
    }
    const NoteEvidence evidence = evidence_of(note.grid, pitch);
    auto noise = split_harmonic_noise(
        plain.samples, plain.sample_rate, curves, *harmonics,
        {.voicing_prob = evidence.voicing_prob, .alternate_f0 = evidence.alternate_f0},
        params.harmonic_noise);
    if (!noise) {
        return stage_error("harmonic/noise split", noise.error());
    }
    auto envelope = estimate_envelope(curves, *harmonics, plain.sample_rate, params.envelope);
    if (!envelope) {
        return stage_error("envelope", envelope.error());
    }
    note.harmonics = std::move(*harmonics);
    note.noise = std::move(*noise);
    note.envelope = std::move(*envelope);
    return {};
}

/// One finished Note from a span, with its pitch target.
Result<Note> describe_note(const NoteSpan& span, const NoteContext& context,
                           const PitchTrack& pitch, const OnsetList& onsets, const MonoAudio& plain,
                           const MonoAudio& weighted, const AnalyseParams& params) {
    const FrameGrid grid = note_grid(span, pitch.grid);
    auto loud = measure_loudness(plain, weighted, span, grid, params.loudness);
    if (!loud) {
        return stage_error("loudness", loud.error());
    }
    auto decomposed = decompose_pitch_curve(span, pitch, loud->amp, params.decomposition);
    if (!decomposed) {
        return stage_error("pitch decomposition", decomposed.error());
    }
    Note note;
    note.id = span.id;
    note.track_id = params.track;
    note.voice = 0;
    note.source_start = span.source_start;
    note.attack_end = span.attack_end;
    note.release_start = span.release_start;
    note.source_end = span.source_end;
    note.pitch_center = decomposed->center;
    note.grid = grid;
    note.pitch_drift = std::move(decomposed->drift);
    note.pitch_mod = std::move(decomposed->mod);
    note.amp = std::move(loud->amp);
    note.loudness = loud->loudness;
    note.separation_in = span.separation_in;
    note.confidence_prob = span.confidence_prob;
    note.is_active = true;
    auto target =
        estimate_pitch_target(note, {.pitch = &pitch, .onsets = &onsets, .disagreement = {}},
                              context, params.pitch_target);
    if (!target) {
        return stage_error("pitch target", target.error());
    }
    note.pitch_target = std::move(*target);
    if (auto filled = fill_phase4_tracks(note, pitch, plain, params); !filled) {
        return Unexpected{filled.error()};
    }
    return note;
}

constexpr double k_min_evidence_rate_hz = 8000.0;  // measure_frames' minimum

bool active_notes_overlap(const std::vector<Note>& notes) {
    const Note* previous = nullptr;
    for (const Note& n : notes) {
        if (!n.is_active) {
            continue;
        }
        if (previous != nullptr && n.source_start < previous->source_end) {
            return true;
        }
        previous = &n;
    }
    return false;
}

/// ADR-0024: the noise floor and the segment timeline. Both stay empty for an empty source and
/// when active notes overlap (the partition needs them to be disjoint).
Result<void> add_gap_analysis(const AudioSource& src, const MonoAudio& mono,
                              const ActivityTrack& activity, const AnalyseParams& params,
                              AnalysisResult& result) {
    if (src.frame_count().value() == 0 || active_notes_overlap(result.notes)) {
        return {};
    }
    auto floor = estimate_noise_floor(src, activity, result.notes, params.noise_floor);
    if (!floor) {
        return stage_error("noise floor", floor.error());
    }
    const SourceTime end = SourceTime{} + Seconds{static_cast<double>(src.frame_count().value()) /
                                                  src.sample_rate().value()};
    // measure_frames needs 8 kHz or more; below that the level rule alone protects the gaps.
    auto evidence = mono.sample_rate.value() >= k_min_evidence_rate_hz
                        ? classify_gap_frames(mono, activity, *floor, params.gap_evidence)
                        : Result<GapEvidence>{neutral_gap_evidence(activity.grid)};
    if (!evidence) {
        return stage_error("gap evidence", evidence.error());
    }
    auto timeline =
        build_segment_timeline(result.notes, end, activity, *floor, *evidence, params.gaps);
    if (!timeline) {
        return stage_error("segment timeline", timeline.error());
    }
    result.noise_floor = std::move(*floor);
    result.timeline = std::move(*timeline);
    return {};
}

}  // namespace

Result<std::shared_ptr<const AnalysisResult>> analyse(const AudioSource& src,
                                                      const AnalyseParams& params) {
    if (params.algorithm != Algorithm::melodic) {
        return fail(ErrorCode::not_implemented,
                    "analyse: only the melodic algorithm exists so far");
    }
    if (src.sample_rate().value() < k_min_source_rate_hz) {
        return fail(ErrorCode::invalid_argument,
                    "analyse: the source sample rate must be at least 4 kHz");
    }
    auto mono = read_mono(src);
    if (!mono) {
        return stage_error("read", mono.error());
    }
    auto pitch = params.engine == PitchEngine::c4dm_pyin
                     ? C4dmPYinEstimator{params.c4dm_pyin}.estimate(src, params.pitch)
                     : PYinEstimator{params.pyin}.estimate(src, params.pitch);
    if (!pitch) {
        return stage_error("pitch", pitch.error());
    }
    auto activity = detect_activity(*mono, pitch->grid, params.activity);
    if (!activity) {
        return stage_error("activity", activity.error());
    }
    auto gated = gate_pitch(*pitch, *activity);
    if (!gated) {
        return stage_error("activity gate", gated.error());
    }
    *pitch = std::move(*gated);
    auto onsets = detect_onsets(src, *pitch, params.onsets);
    if (!onsets) {
        return stage_error("onsets", onsets.error());
    }
    auto spans = segment_notes(*pitch, *onsets, params.segmentation);
    if (!spans) {
        return stage_error("segmentation", spans.error());
    }
    const MonoAudio weighted{.samples = k_weight(mono->samples, mono->sample_rate),
                             .sample_rate = mono->sample_rate};

    auto result = std::make_shared<AnalysisResult>();
    result->version = params.version;
    result->source_id = src.id();
    result->algorithm = params.algorithm;
    for (std::size_t i = 0; i < spans->size(); ++i) {
        const NoteSpan& span = (*spans)[i];
        auto note =
            describe_note(span, context_of(*spans, i), *pitch, *onsets, *mono, weighted, params);
        if (!note) {
            return Unexpected{
                Error{.code = note.error().code,
                      .message = "note " + span.id.value + ": " + note.error().message}};
        }
        result->notes.push_back(std::move(*note));
    }
    // Flags that need the whole take (blue notes, phrase peaks, runs), added once all notes exist.
    auto take_flags = annotate_take(result->notes, params.pitch_target);
    if (!take_flags) {
        return stage_error("take flags", take_flags.error());
    }
    for (std::size_t i = 0; i < take_flags->size(); ++i) {
        result->notes[i].pitch_target.flags.bits = static_cast<std::uint16_t>(
            result->notes[i].pitch_target.flags.bits | (*take_flags)[i].bits);
    }
    if (auto r = add_gap_analysis(src, *mono, *activity, params, *result); !r) {
        return Unexpected{r.error()};
    }
    result->onsets = std::move(*onsets);
    result->pitch = std::move(*pitch);
    if (auto r = validate(*result); !r) {
        return stage_error("validation", r.error());
    }
    return std::shared_ptr<const AnalysisResult>{std::move(result)};
}

}  // namespace anno
