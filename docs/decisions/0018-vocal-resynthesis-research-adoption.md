# ADR-0018: Adopting the vocal resynthesis research, and which phase builds what

- **Date:** 2026-10-20
- **Status:** accepted 2026-10-20 (owner: "your instincts are good here"; thresholds stay provisional, ADR-0018)
- **Phase:** 4 (allocation spans 4-13)

## Context
The owner's research (`docs/design/vocal-resynthesis-research.md`) is a routing design: classify each frame, send it to the method that fits. Its thresholds and tolerances are, by its own labels, mostly starting guesses; several sources are patents, lecture notes or abstracts. It overlaps the ROADMAP's fuzzy-thresholds item (thresholds that change locally by waveform tags and globally per performance). The ROADMAP must say where each concept lands so nothing is built twice or too early.

## Decision
1. **Status of the research.** It is the design input for sound-class handling. Every threshold and tolerance in it is a **provisional parameter** (named, in one params struct, with its source and "guess" marking kept in the comments), to be replaced by the fuzzy layer and calibrated per singer. Tolerances taken into `tests/tolerances.h` are marked provisional; none is loosened without the owner.
2. **Citations** are "verify before relying", as with earlier research; the thirteen metrics are definitions, not benchmarks.
3. **Allocation by phase** (checked against the ROADMAP):

| Concept from the research | Phase | Why there |
|---|---|---|
| Frame-class routing for pitch shift: bypass plosive/sibilant/breath/silence/rough; harmonic-below-MVF with pinned noise for breathy; modal default; 4 ms class-boundary crossfades; median-filtered labels, min 20 ms | **4** | Pitch edit is the first audible edit; ADR-0017 |
| Metrics: positive spectral flux + closure dip, centroid, ZCR, HNR, band aperiodicity / MVF, octave ratio from `alternates` | **4** (`analysis/harmonic_noise`) | Needed to route |
| Recording-noise floor from pauses before analysis | **4** reuses ADR-0011 `activity`; **5** `analysis/gaps` (ADR-0008) refines it | The noise floor already exists; gaps are Phase 5 |
| Protected consonant window; stretch vowels only, never consonants; plosive burst repositioned not stretched; pre-echo check | **5** (`synthesis/transient`, note stretching) | These are timing operations |
| Stretch fricatives and breaths as noise: randomised phase and crossfade, never phase-locked | **5** | Time stretch |
| Sibilant 4-16 kHz tonal-peak check; de-ess after resynthesis, not before; noise gains per class | **6** (`sibilant`, `leveling`) | Expression tools |
| Re-apply cycle-to-cycle perturbation after pitch edits; do not low-pass pitch contours; separate slow contour from fast perturbation | **6** (pitch modulation / drift tools) with a Phase 4 note that Main Tool shifts the unmodified curve, which preserves both | Pitch modulation and drift tools |
| Metrics CPPS, GNE, H1-H2, jitter, shimmer, SHR; breathy/rough calibration checks (CPPS within 1 dB, jitter/shimmer ratio 0.7-1.3, octave-error rate under 2 %) | **6** | Refine the breathy/rough routing once the tools that use them exist |
| Per-singer calibration (percentile thresholds such as the lowest 15 % CPPS, 90th percentile SHR), hand-labelled ground truth, confusion matrix, threshold sweep | **8** (global side of the fuzzy layer) | Needs whole-take statistics |
| Calibration profile saved with the project | **7** (`project/`) | Persistence |
| Class and routing display (hover reasons, class lanes) | **10** | UI |
| Two independent pitch trackers for the octave flag (neural estimator as second tracker); noise-conditioned neural vocoders (h-NSF, uSFGAN, VISinger 2 style) for rough and breathy material | **13** | `core/models`; the Phase 4 flag uses the second-best path of pYIN until then |
| Polyphonic material, reverb-heavy vocals, text-to-singing | out of scope (Phase 11 revisits polyphony separately) | The research excludes them |

4. **No new engine family in Phase 4.** WORLD/STRAIGHT and neural vocoders from the comparison are not built; the harmonic-plus-noise engine and PSOLA are the Phase 4 candidates. Revisit only through the listening gate.
5. **ROADMAP** is amended in this change: each phase above lists its items; the Phase 4 section names ADR-0017 and ADR-0018.

## Alternatives considered
- Build the full thirteen-metric classifier in Phase 4: front-loads work whose consumers (Phase 6 tools, Phase 8 calibration) do not exist and cannot be tested yet.
- Treat the research tolerances as fixed gates: they are labelled guesses; fixed gates would invite loosening.

## Consequences
Phase 4 scope grows by the frame classifier's first metric set (already planned as stage 2) and the engine routing. Later phases gain named, sourced items instead of rediscovering them. The fuzzy-thresholds item gets a concrete first customer.

## Spec updates
None; the specs stay read-only. ADR-0017 overrides architecture §3 for `FrameClass`.

## Amendment 2026-10-21 (ADR-0019)
The Phase 13 row "Two independent pitch trackers for the octave flag" is conditionally movable to Phase 6 if ADR-0019's trial keeps SPTK and a Harvest/SWIPE' tracker passes the octave-error check (under 2 %); the neural estimator stays the alternative. No other row changes.
