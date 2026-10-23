#pragma once

#include "core/edits/dirty_regions.h"
#include "core/edits/edit_list.h"
#include "core/render/gate.h"
#include "core/render/render_cache.h"
#include "core/render/render_result.h"
#include "core/synthesis/engine/synthesis_engine.h"
#include "core/synthesis/stretch/stretch.h"
#include "core/types/analysis_result.h"
#include "core/types/audio_source.h"
#include "core/types/result.h"
#include "core/types/time_range.h"
#include "core/types/units.h"

#include <memory>

// render_region: edits -> audio (architecture spec §4.6, §5.5, §5.6; ADR-0006).
// Design: docs/design/phase2-render-skeleton.md.

namespace anno {

struct RenderInputs {
    AudioSource source;
    std::shared_ptr<const AnalysisResult> analysis;  // must not be null
    std::shared_ptr<const EditList> edits;           // must not be null; edits->track_id() = track
    /// Optional (ADR-0008). If set it is the track's TimeMap, authoritative over any timing edits
    /// in `edits`; its source_length must match the recording (half a sample). If null the map is
    /// built from the edits with the tail shifted (not squeezed) and whole-sample shifts
    /// (ADR-0023); a supplied map must give every slope-1 warp a whole-sample shift.
    std::shared_ptr<const TimeMap> time_map;
};

struct RenderParams {
    /// Crossfade on each side of an edited note: 0 <= length <= k_max_crossfade. 0 is a hard splice
    /// (used by tests; the click detector flags it).
    Seconds crossfade_length{0.010};
    FadeShape crossfade_shape = FadeShape::equal_power;
    RenderCache* cache = nullptr;  // optional, not owned
    StretchParams stretch = {};  // settings of the stretched base (ADR-0023); part of the cache key
    GateParams gate = {};        // the ramps of the gap gate (ADR-0024); part of the cache key
};

/// Renders [region) of the track's output. The result's buffer is the request snapped to whole
/// samples (Rounding::nearest at both ends); samples outside every edited note's blend zone are
/// the source samples, bit for bit, and an empty edit list gives the source exactly (CLAUDE.md
/// rule 5). Edited notes (EffectiveNote::is_edited) are rendered by `engine` over their whole blend
/// zone (the note widened by the crossfade length) and faded into the original; neighbours whose
/// zones meet are faded into each other (see blend_plan.h). A zone the engine returns identical to
/// the source is left untouched.
/// A cache hit returns before any engine call.
/// Errors: invalid_argument (null analysis or edits, source/analysis id mismatch, invalid
/// analysis, crossfade_length outside [0, k_max_crossfade], region end before start, notes on
/// another track than the edits); out_of_range (region outside the recording);
/// invalid_argument also for a supplied TimeMap whose source_length does not match the recording or
/// a note whose start or end maps into a cut; not_implemented (a slope-1 warp has a fractional
/// sample shift; a slope outside the stretch limits; an edited note (pitch, level, formant) whose
/// blend zone meets a stretched warp, the message names the note; or the engine cannot realise an
/// edit);
/// internal (an engine returned the wrong range, channel count or sample rate).
/// Stretched warps (slope != 1, from timing edits) are part of the unedited base: slope-1 warps and
/// inserts as below, every slope != 1 warp rendered by `stretch_span` (pitch and timbre kept).
/// A note whose edits are only timing edits is part of that base, not an edited note.
/// Gap edits (gap_gain, ADR-0024) scale the `silence` regions of their gaps after the blend (see
/// gate_curve); a gap edit whose id is not in `analysis->timeline`, or any gap edit when the
/// timeline is absent, is invalid_argument naming the id (orphaned ids are ignored); samples with
/// no gate are never multiplied. `RenderResult::gaps_rendered` lists the gated gaps that meet the
/// region.
/// Time axes: `region` is output time, within [0, output_length of the map]; with an identity
/// TimeMap it equals source time. Cuts have no output and inserts are digital silence; every other
/// sample outside blend zones is the source sample the map names, bit for bit. Thread-safe for
/// distinct inputs (a shared cache is internally locked); not real-time safe.
[[nodiscard]] Result<std::shared_ptr<const RenderResult>>
render_region(const RenderInputs& inputs, OutputRange region, const SynthesisEngine& engine,
              const RenderParams& params);

}  // namespace anno
