#include "core/analysis/gaps/analyse_gaps.h"

namespace anno {

namespace {

Unexpected stage_error(const char* stage, const Error& e) {
    return Unexpected{Error{.code = e.code, .message = std::string{stage} + ": " + e.message}};
}

}  // namespace

Result<GapAnalysis> analyse_gaps(const AudioSource& source, const MonoAudio& mono,
                                 std::span<const Note> notes, const ActivityTrack& activity,
                                 const GapAnalysisParams& params) {
    auto floor = estimate_noise_floor(source, activity, notes, params.noise_floor);
    if (!floor) {
        return stage_error("noise floor", floor.error());
    }
    auto evidence = classify_gap_frames(mono, activity, *floor, params.evidence);
    if (!evidence) {
        return stage_error("gap evidence", evidence.error());
    }
    const SourceTime end = SourceTime{} + to_seconds(source.frame_count(), source.sample_rate());
    auto timeline = build_segment_timeline(notes, end, activity, *floor, *evidence, params.gaps);
    if (!timeline) {
        return stage_error("segment timeline", timeline.error());
    }
    return GapAnalysis{.noise_floor = std::move(*floor), .timeline = std::move(*timeline)};
}

}  // namespace anno
