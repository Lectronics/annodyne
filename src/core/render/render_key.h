#pragma once

#include "core/render/blend_plan.h"
#include "core/render/gate.h"
#include "core/synthesis/engine/synthesis_engine.h"
#include "core/synthesis/stretch/stretch.h"
#include "core/types/analysis_result.h"
#include "core/types/effective_gap.h"
#include "core/types/effective_note.h"
#include "core/types/note.h"
#include "core/types/segments.h"
#include "core/types/time_map.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The canonical cache key of one render (design note phase 2, "Cache key"; ADR-0006).

namespace anno {

/// An edited note taking part in a render.
struct EditedNote {
    const Note* note = nullptr;
    EffectiveNote eff;
    SampleSpan span;        // snapped to whole samples
    TouchingNotes context;  // the touching neighbours (ADR-0030); part of the cache key
};

/// An edited gap taking part in a render (ADR-0024).
struct GateInput {
    const Gap* gap = nullptr;
    EffectiveGap eff;
    std::vector<GatedSpan> spans;  // its silence regions on the output axis, samples
};

struct RenderKeyInputs {
    const SourceId* source_id = nullptr;
    std::uint32_t analysis_version = 0;
    const SynthesisEngine* engine = nullptr;
    std::int64_t margin_samples = 0;  // crossfade length in samples
    FadeShape shape = FadeShape::equal_power;
    std::int64_t region_begin = 0;  // snapped region, samples
    std::int64_t region_end = 0;
    double sample_rate_hz = 0.0;
    std::span<const EditedNote> edited;
    std::span<const Ramp> ramps;
    const TimeMap* time_map = nullptr;  // never null; its lengths and the spans near the region
    /// The settings of the stretched base (ADR-0023); null hashes as "none", for callers whose map
    /// has no stretched span.
    const StretchParams* stretch = nullptr;
    // Edited gaps (ADR-0024) and the gate's fade in samples; appended last (key version 4).
    std::span<const GateInput> gates;
    std::int64_t gate_fade_samples = 0;
    std::int64_t splice_fade_samples = 0;  // the layout's splice fade (key version 5)
};

/// 128-bit content fingerprint of every field of a Note (two independent 64-bit FNV-style streams).
/// Equal notes give equal fingerprints; a collision between different notes has probability about
/// 2^-128. Pure; thread-safe.
[[nodiscard]] std::string note_fingerprint(const Note& note);

/// Canonical key: every variable-length field is length-prefixed, so field boundaries are
/// unambiguous. Everything except the note fingerprints compares exactly. Includes the ramps
/// that meet the region (so a neighbour outside the region that changes a ramp length changes the
/// key) and, for every edited note whose zone meets the region, its snapped span, its EffectiveNote
/// and its fingerprint, and the TimeMap (its lengths, and the spans that meet the region widened by
/// the margin and the splice fade, unless it is the identity), so no span a zone reads can be
/// missed. Pure; thread-safe; not real-time safe.
[[nodiscard]] std::string make_render_key(const RenderKeyInputs& in);

/// Short hex identifier of a key, for reports. Not unique.
[[nodiscard]] std::string short_id(std::string_view key);

}  // namespace anno
