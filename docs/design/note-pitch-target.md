# Design note: Note pitch target

- **Phase:** 3 (Monophonic analysis), extends `perceived_pitch`
- **Status:** implemented in Phase 3 (stages 1-6, 2026-10-20) except the items listed in section 17; undefined items resolved in section 11, structure review answered in section 12, owner decisions in section 13
- **Related:** transient-based segmentation design note; `note-stretching.md` (vibrato model); implementation guide B3–B4
- **Folder / CMake target:** `src/core/analysis/pitch_target/` (new, layer 1); data types in `src/core/types/pitch_target.h` (see section 11)

## Problem

A note's **pitch target** is the value that Correct Pitch and snap-to-grid move onto the grid. Averaging the whole pitch curve works for steady notes and vibrato notes, but fails for many real singing behaviours:

- the singer **rolls into** the note (scoop up or down) or **rolls off** at the end;
- the note **overshoots** and settles;
- the note is a deliberate **bend, blue note or ornament**, not a mistake;
- consonants, creak or tracking errors put **non-sung pitch values** into the curve;
- the note is **too short** to have a stable part at all.

This note defines how a single number (or, for glides, a start and end) is chosen for every note, and which behaviours must be left alone. It also lists pitch problems that are **not** solved here and need their own design notes (section 9).

---

## 1. Research summary (current as of October 2026)

### How listeners hear a note's pitch
1. **Glissando threshold.** A pitch change is heard as movement only if its rate exceeds G = 0.16/T² semitones/s (T = duration of the change); about 0.32/T² fits continuous speech. Below it, variation fuses into one pitch. ([Prosogram](https://sites.google.com/site/prosogram/); [d'Alessandro & Mertens 1995](https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1995/13_ICPhS_1995_Vol_4/p13.4_228.pdf))
2. **Contours are heard as level and moving segments.** The tonal-perception model smooths F0, splits it at turning points, and gives each level segment one pitch and each moving segment a start and end target. ([d'Alessandro & Mertens 1995](https://www.coli.uni-saarland.de/Phonetics/icphs/ICPhS1995/13_ICPhS_1995_Vol_4/p13.4_228.pdf))
3. **Level tones: weighted time average, end counts more, ~0.14 s integration.** Above the threshold, start and end are heard separately. ([d'Alessandro & Castellengo](https://journals.lub.lu.se/LWPL/article/download/17200/15577); [short vibrato tones](https://hal.archives-ouvertes.fr/hal-02008510))
4. **Stability weighting is real.** In asymmetric frequency modulation, perceived pitch moves away from the mean towards the slowly changing (flat) part, by up to about 13–15 cents. ([Etchemendy & Eguia](https://www.aacademica.org/manuel.eguia/19.pdf); [Mesz & Eguia](https://www.aacademica.org/manuel.eguia/27.pdf))
5. **In sung tones the steady centre dominates in-tune judgments, but scoops still count, and ending scoops more than starting ones.** Plain averaging doesn't explain listener ratings; listeners also judge scoops against neighbouring notes. ([Larrouy-Maestri & Pfordresher](https://ubwp.buffalo.edu/apal-pfordresher/wp-content/uploads/sites/79/2018/09/Larrouy-Maestri-Pfordresher_inpress.pdf))
6. **Pitch of very short notes is imprecise.** Identifying a note's pitch class needs roughly 8 or more periods (about 30 ms at C4, 60 ms at C3), and more for less musical listeners. ([Robinson & Patterson](https://www.pdn.cam.ac.uk/system/files/documents/RPmusper95.pdf))
7. **How much mistuning people notice.** With pop vocals over accompaniment, detection thresholds ranged roughly 36–83 cents (median about 55); trained listeners in other studies detect 10–40 cents. ([Mistuning Perception Test](https://pmc.ncbi.nlm.nih.gov/articles/PMC6478636)) A recent APC paper used **−10 to +15 cents** around the annotated stationary pitch as its "perceptually tolerated" range. ([BERT-APC](https://arxiv.org/pdf/2511.20006))

### How singers actually move pitch within notes
8. **Transients last about 120 ms at each end of a note, set by absolute time, not note length.** Coming from a lower note, singers overshoot and come back down; coming from a higher note they land almost immediately. Before a higher next note, pitch rises near the end; before a lower one it falls. ([Dai & Dixon, JASA 2019](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2019/DaiDixon-JASA-Quartet-AcceptedVersion.pdf))
9. **Four kinds of F0 fluctuation in singing, all perceptually important:** overshoot after a note change, vibrato, *preparation* (a small dip opposite to an upcoming change, just before it), and fine fluctuation above 10 Hz. Removing any one lowered perceived quality; overshoot mattered most. ([Saitou et al. 2004](https://www.isca-archive.org/interspeech_2004/saitou04_interspeech.pdf))
10. **Consonants bend F0 for 30–150 ms** (microprosody): voiced obstruents pull it down, voiceless ones push the following vowel's onset up, on the order of 1–2 semitones in speech. ([Krug et al. 2021](https://www.vocaltractlab.de/publications/krug-2021-jasa.pdf)) These are articulation effects, not intonation errors.
11. **Creaky voice and fry make F0 jump by an octave, and the jumps can be real** (period doubling), not tracker errors. ([Watkins, Boersma & Hamann 2024](https://www.fon.hum.uva.nl/paul/papers/2024-intsp-WatkinsBoersmaHamann.pdf))

### Intentional deviations that must not be "fixed"
12. **Phrase-peak sharpening is expressive.** Flattening sharp phrase peaks to equal temperament with Melodyne made performances sound less expressive to a musician panel. ([single-case study, J. Voice](https://e-spacio.uned.es/entities/publication/2a67f364-c87f-48e9-a6be-6c926dc66719/full))
13. **Blue notes sit between semitones and vary by performer** (e.g. a blue third cluster near 319 cents, flat sevenths spread over several clusters). ([Empirical Musicology Review](https://www.osu.tests.sfulib4.publicknowledgeproject.org/index.php/EMR/article/view/6316))
14. **In gamaka-based styles the movement is the note.** Quantizing every pitch sample destroyed the character; quantizing only the turning points kept it. ([ISMIR 2017](https://archives.ismir.net/ismir2017/paper/000061.pdf))
15. **Ensemble singers don't tune to pure ET or just intonation**; adjacent voices sat about 8 cents wider than written. ([Dai & Dixon](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2019/DaiDixon-JASA-Quartet.pdf))

### How current pitch-correction research picks the target
16. **Learned stability weighting beats averaging.** On 6,578 annotated notes, a plain average put 76.9% of notes within the tolerance range, a weighted median 89.2%, and a learned weighting (down-weighting transitions and vibrato) 94.3%. The correction is then one uniform shift per note, which keeps vibrato, bends and portamento. ([BERT-APC, Nov 2025](https://arxiv.org/pdf/2511.20006))
17. **Deciding *which* semitone was intended needs musical context.** BERT-APC uses a symbolic music model over neighbouring notes; dropping it hurt most on badly detuned singing. A newer system (Oct 2026) predicts per note *whether* it needs fixing and by how many semitones, with a soft key prior, and reports a "harm rate" (correct notes made wrong). ([BERT-APC](https://arxiv.org/pdf/2511.20006); [Music-constrained sequence editing](https://arxiv.org/html/2610.11524)) MIDIBack (Sep 2026) adds the accompaniment as context. ([MIDIBack](https://arxiv.org/abs/2609.28008))
18. **Commercial direction:** Melodyne 5 weights the most perceptually significant part of a note and ignores, for example, a quiet sharp ending ([Sound On Sound](https://www.soundonsound.com/reviews/celemony-melodyne-5)); Auto-Tune's newer engine detects vibrato and leaves it alone as "a decision the vocalist made" ([Production Expert](https://www.production-expert.com/production-expert-1/antares-autotune-advanced-new-pitch-correction-plugin-with-vibrato-engine)). Dynamic pitch warping corrects only after the pitch has been stable for a critical time and leaves unstable motion alone ([DAFx 2023](https://www.dafx.de/paper-archive/2023/DAFx23_paper_67.pdf)).
19. **Pitch accuracy metrics and naturalness disagree.** In one study the most accurate contour sounded least natural; continuous contours were preferred. ([arXiv 2010.12196](https://arxiv.org/pdf/2010.12196))

### What this means for the design
- Use **level segments** for the target, with **stability × loudness × reliability** weights inside them (items 1–5, 16, 18).
- Treat transients as roughly **fixed-duration (about 120 ms)** zones, and label their direction against neighbouring notes (8, 9).
- **Exclude non-sung pitch** (consonant bends, creak, tracking errors) from the target (10, 11).
- **Don't force a target on behaviours that are intended**: bends, blue notes, trills, gamakas, phrase-peak sharpening (12–14).
- **Keep target choice (this note) separate from intended-note choice** (which semitone), which needs context (17) and gets its own design note.

---

## 2. Catalogue of pitch behaviours

Every behaviour the analysis must recognise, how it is detected, and what the target is.

| # | Behaviour | Detected by | Kind | Target / handling |
|---|---|---|---|---|
| 1 | Steady note, slightly off | one level segment | `whole` | weighted average of the voiced note |
| 2 | Symmetric vibrato | periodic mod, 3–9 Hz | `whole` | average over whole cycles |
| 3 | Asymmetric or off-centre vibrato | vibrato with a flat side | `whole` | stability weighting pulls towards the flat side (item 4) |
| 4 | Vibrato with drift | vibrato + slow trend | `whole` | average; Drift tool straightens the trend |
| 5 | Scoop / roll-up / roll-down into the note | moving then level | `settled` | level core only; approach = `scoop` |
| 6 | Overshoot then settle | moving past target, back, level | `settled` | level core; approach = `overshoot` |
| 7 | Legato approach from previous note | glide starting near previous note's pitch | `settled` | level core; approach = `legato` |
| 8 | Fall-off / roll-up at the end | level then moving | `settled` | level core; release = `fall` / `rise` |
| 9 | Preparation before the next note | small end movement opposite to the next interval | `settled` | level core; release = `preparation` |
| 10 | Slow sag within a note (< 40 ¢) | level, small trend | `whole` | average; Drift tool |
| 11 | Intentional bend / slide (no level part) | moving only | `glide` | start and end targets; snap the end |
| 12 | Two plateaus in one note | level–moving–level, plateaus far apart | `multi` | suggest a split; no automatic correction |
| 13 | Trill / mordent / turn | oscillation ≥ ~80 ¢ extent with flat-topped extremes, or alternating plateaus | `ornament` | targets are the two pitches; correction moves both by the main note's offset; never averaged |
| 14 | Gamaka-style movement | dense turning points, no long level part, style preset | `ornament` | turning points are the targets; default: leave alone |
| 15 | Blue note / consistent microtonal pitch | same scale degree repeatedly at a similar off-grid pitch | `whole` + flag `consistent_off_grid` | target computed normally; Correct Pitch skips it unless the user chooses otherwise |
| 16 | Phrase-peak or high-note sharpening | highest note of a phrase sharp by ≤ ~20 ¢ | `whole`/`settled` + flag `expressive_peak` | target computed normally; Correct Pitch uses a wider tolerance |
| 17 | Very short note (< ~120 ms voiced) | duration | `whole` + flag `short` | weighted average; low confidence; context decides (section 9) |
| 18 | Melisma / fast run | many short notes, mostly moving | `whole` or `glide` per note + flag `run` | correct the run's overall offset gently, not note by note |
| 19 | Consonant pitch bends | frames near consonants (from transient/phoneme segmentation) | — | frames down-weighted towards zero, never decide the target |
| 20 | Creak / fry / octave jump | voicing irregularity, F0 ratio ~0.5 or ~2 | flag `creak` | frames excluded; never read as a wrong note |
| 21 | Register flip (yodel, falsetto break) | sudden jump inside a note, both sides level | `multi` + flag `register_break` | leave alone; suggest split |
| 22 | Fine fluctuation (> 10 Hz jitter) | removed by smoothing | — | not part of the target; **must be preserved** in resynthesis |
| 23 | Breathy, whispered or spoken notes | low voicing / periodicity | `unpitched` | no target; never corrected |
| 24 | Tracker errors (reverb tail, bleed) | low confidence, disagreement between trackers | — | frames excluded; note flagged if its core is affected |
| 25 | Wrong note (off by about a semitone or more) | target far from grid | any + flag `ambiguous_intent` when 40–60 ¢ from two grid lines | target computed here; **which** note is intended is decided in context (section 9) |

---

## 3. Algorithm

### Step 1: Frame weights (reliability first)
For each voiced frame in the note, compute a reliability weight `r(t)` from 0 to 1:
- pitch-tracker confidence and voicing probability;
- **consonant proximity**: down-weight frames within about 30–150 ms of a consonant boundary (from the transient/phoneme segmentation), most strongly right next to it;
- **creak / jump**: zero weight where period doubling or halving is detected (F0 ratio roughly 2 or 0.5 between neighbouring stretches);
- **tracker disagreement**: down-weight where two trackers (e.g. neural and pYIN) disagree by more than 30 ¢.

Frames with `r < 0.2` are ignored for segmenting and for the target.

### Step 2: Remove vibrato and fine fluctuation before judging stability
Use `decompose_pitch_curve` (centre + drift + mod) or a weighted moving average with about 0.14 s integration. Result: smoothed curve `P(t)` in cents. Vibrato and > 10 Hz fluctuation are **removed only for this decision**; the audio keeps them.

### Step 3: Ornament check (before segmenting)
On the unsmoothed curve, look for oscillation that is **not** vibrato:
- extent ≥ `trill_min_extent` (80 ¢), and
- flat-topped extremes (time spent near the extremes ≥ 50% of each cycle) or extremes that land near two different grid pitches.
If found, classify `ornament` (trill/mordent/turn) and stop here. Gamaka-style handling applies only when the style preset says so.

### Step 4: Split `P(t)` into tonal segments
1. **Turning points:** recursively split where `P(t)` departs from the straight line between segment ends by more than `turn_threshold` (35 ¢).
2. **Merge** neighbours whose slopes differ by less than `slope_merge_cents_per_s`, and adjacent level segments whose pitches differ by less than `plateau_merge`.

### Step 5: Label segments level or moving
A segment (duration `T` seconds, change `Δ` cents) is **moving** only if
`|Δ| > g·100/T` (the glissando threshold G = g/T² semitones/s, applied to a change of duration T) **and** `|Δ| ≥ min_glide_extent` (40 ¢).
Additionally, any segment inside the first or last `transient_window` (120 ms) that changes by ≥ 25 ¢ counts as moving, since singers' transients sit in roughly that fixed window (research item 8).

### Step 6: Classify and choose the target
Apply the catalogue (section 2) in this order: `unpitched` → `ornament` → `glide` (no level part) → `multi` → `settled` → `whole`.

**Target value** for `whole` and `settled`: a weighted average over the level core, in cents, over whole vibrato cycles where vibrato is present, with frame weight
`w(t) = r(t) × stability(t) × loudness(t) × end_bias(t)`, where
- `stability` = higher for slowly changing frames (rate-of-change weighting),
- `loudness` = lower for quiet frames (a quiet sharp ending must not pull the target),
- `end_bias` = slightly higher towards the end of the **core** (never applied to releases, which are outside the core).

Use a weighted **median** instead of the mean when fewer than 5 vibrato cycles fit the core and the curve is noisy (research item 16 shows weighted median is a strong simple baseline).

**Target for `glide`:** start and end targets from `P(t)` at the glide ends; the end target is the one snapped.

### Step 7: Labels, flags and confidence
- **Approach** (`none`, `legato`, `scoop`, `overshoot`) using the previous note's target: a glide starting within 50 ¢ of the previous target is `legato`; one that passes the level core and returns is `overshoot`; otherwise `scoop`.
- **Release** (`none`, `fall`, `rise`, `preparation`) using the next note: an end movement **opposite** to the next interval and under 60 ¢ is `preparation`.
- **Flags:** `short`, `run`, `creak`, `register_break`, `consistent_off_grid`, `expressive_peak`, `ambiguous_intent`.
- **Confidence** (0–1) drops for short cores (< 80 ms), decisions near a threshold, many excluded frames, or tracker disagreement.

`consistent_off_grid` and `expressive_peak` need the whole take, so they are set in a second pass over all notes (`annotate_take`, below), not per note.

---

## 4. Public types and functions

```cpp
namespace anno {  // section 11 replaces `annodyne::analysis`; types marked * are defined there

enum class PitchTargetKind { whole, settled, glide, multi, ornament, unpitched };
enum class SegmentShape   { level, rising, falling };
enum class ApproachKind   { none, legato, scoop, overshoot };
enum class ReleaseKind    { none, fall, rise, preparation };
enum class PitchFlag      { short_note, run, creak, register_break,
                            consistent_off_grid, expressive_peak, ambiguous_intent };

struct TonalSegment {
    SourceTime start, end;
    SegmentShape shape;
    Cents start_pitch, end_pitch;          // smoothed curve P(t) at the ends
};

struct PitchTarget {
    PitchTargetKind kind;
    Cents target;                          // value to snap; glide = end target; ornament = main note
    std::optional<Cents> secondary;        // glide start, or the ornament's other pitch
    SourceTime core_start, core_end;       // frames used for the target
    ApproachKind approach;
    ReleaseKind release;
    std::vector<TonalSegment> segments;
    PitchFlags flags;                      // * bit set of PitchFlag, no allocation
    double confidence;                     // 0..1
};

struct PitchTargetParams {
    Seconds smoothing{0.14};
    double  glissando_g = 0.32;            // 0.16 isolated tones, 0.32 continuous speech
    Cents   turn_threshold{35.0};
    double  slope_merge_cents_per_s = 150.0;
    Cents   min_glide_extent{40.0};
    Seconds transient_window{0.120};
    Cents   transient_min_change{25.0};
    Cents   plateau_merge{30.0};
    Seconds min_core{0.080};
    Seconds short_note{0.120};
    Cents   trill_min_extent{80.0};
    Seconds consonant_zone{0.150};
    Cents   tracker_disagreement{30.0};
    double  end_bias = 0.3;
    StylePreset style = StylePreset::pop;  // pop, classical, jazz_blues, gamaka
};

/// Decide how one note's pitch should be judged and return its target.
/// Inputs (source time): the note's pitch curve, amplitude curve, frame reliability,
/// consonant boundaries; previous and next notes' targets for labelling (may be empty).
/// Returns kind, target in cents (A4 = 6900), core range, labels, flags, confidence.
/// Error if the note has fewer than 3 reliable voiced frames (returns kind unpitched instead
/// when the note is clearly unvoiced). Pure; thread-safe; not RT-safe.
[[nodiscard]] Result<PitchTarget> estimate_pitch_target(const Note& note,
                                                        const NoteContext& neighbours,
                                                        const PitchTargetParams& params);

/// Second pass over a whole take: sets flags that need many notes —
/// consistent_off_grid (same scale degree repeatedly off-grid by a similar amount,
/// at least 3 occurrences within ±15 ¢ of each other), expressive_peak (phrase-highest
/// notes sharp by up to params' peak tolerance), run (sequences of short notes).
/// Returns updated targets; never changes target values. Pure; thread-safe.
[[nodiscard]] std::vector<PitchTarget> annotate_take(std::span<const PitchTarget> targets,
                                                     std::span<const Note> notes,
                                                     const TakeContext& key_and_phrases,
                                                     const PitchTargetParams& params);
}
```

`Note` gains `PitchTarget pitch_target`; `Note::perceived_pitch` is kept and equals `pitch_target.target` (ADR-0013; removing the old field is a later ADR).

---

## 5. How correction uses the target

- **One uniform offset per note.** Correcting shifts the whole pitch curve by `grid − target`, so vibrato, scoops, releases, overshoot, preparation and fine fluctuation all survive (research items 9, 16, 19).
- **Tolerance:** default asymmetric tolerance of −10/+15 ¢ (notes inside it are left alone), widened for `expressive_peak` notes (default +25 ¢).
- **Kinds:**
  - `whole`, `settled`: target onto the grid.
  - `glide`: end target onto the grid; start follows by the same offset (option to keep it).
  - `ornament`: both pitches move by the main note's offset; the ornament's shape is kept.
  - `multi`, `unpitched`: not corrected; UI suggests a split or shows "no pitch".
- **Flags:** `consistent_off_grid` notes are skipped by default (blue notes); `creak` and `register_break` notes are never auto-corrected; `ambiguous_intent` notes are left to the intended-note stage (section 9).
- **User override (non-destructive edit):** `pitch_target_mode` = `auto | whole | settled | end | leave` per note. It changes only how the target is chosen.

## 6. UI

- A short horizontal tick across the core at the target pitch; glides show an arrow at the end target; ornaments show both pitches.
- Faint marker on notes with confidence < 0.5 or any of `ambiguous_intent`, `creak`, `register_break`.
- Notes skipped by Correct Pitch because of a flag show why on hover ("Left alone: consistent blue third").

## 7. Split / join decisions

- **`estimate_pitch_target` stays one function** with private helpers (reliability weights, ornament check, segmenting, classification). The steps share all their state and only make sense in order (keep-together rules 1 and 5); each helper has its own unit tests (split rule 4).
- **`annotate_take` is separate**: it needs the whole take and runs after every note has a target (split function rule 5, different data scope).
- **Intended-note choice is not here**: it needs key, neighbours and possibly accompaniment, changes for different reasons, and gets its own design note (split class rule 3).

## 8. Tests

**Synthetic (unit)**

| Signal | Expected |
|---|---|
| Steady A4 | `whole`, 6900 ± 2 ¢ |
| A4, ±50 ¢ sinusoidal vibrato at 5.5 Hz | `whole`, 6900 ± 3 ¢ |
| A4 vibrato with a flat top half (asymmetric) | `whole`, target shifted towards the flat side by 5–15 ¢ |
| Scoop −200 ¢ over 80 ms, then steady | `settled`, 6900 ± 3 ¢, approach `scoop` |
| Previous note G4, legato glide up into A4 | `settled`, approach `legato` |
| Overshoot +60 ¢ then settle | `settled`, approach `overshoot` |
| Steady, then fall −300 ¢ in last 150 ms | `settled`, release `fall` |
| Steady, then rise +80 ¢ in last 120 ms | `settled`, release `rise` |
| Next note higher; small −30 ¢ dip in the last 80 ms | `settled`, release `preparation` |
| Scoop in, steady, fall out (`M L M`) | `settled`, core = middle |
| 60 ms hold then 400 ms fall | `settled`, confidence < 0.5 |
| Steady with +25 ¢ end drift over 200 ms | `whole`; target within ~5 ¢ of the steady part |
| 1.0 s note sagging 20 ¢ | `whole` |
| 400 ms bend A4 → B♭4, no plateau | `glide`, target B♭4 ± 5 ¢, secondary ≈ A4 |
| Two plateaus 100 ¢ apart | `multi` |
| Trill A4–B4 at 6 Hz, flat-topped | `ornament`, target A4, secondary B4 |
| 90 ms note | flag `short`, confidence < 0.6 |
| Voiced consonant dip of −150 ¢ over 60 ms before the vowel | target unaffected (± 3 ¢) |
| F0 halving for 100 ms inside a note (creak) | flag `creak`; target unaffected |
| Breathy noise with no periodicity | `unpitched` |
| Quiet (−18 dB) +40 ¢ ending on a long note | target within 5 ¢ of the loud part |

**Take-level (`annotate_take`)**
- Third degree sung at ~+20 ¢ off the minor third in 4 of 5 occurrences → `consistent_off_grid` on those 4.
- Highest note of each phrase sharp by 15 ¢ → `expressive_peak`.

**Golden data:** Vocadito and CSD (hand-annotated notes). Measure the share of notes within −10/+15 ¢ of annotated note pitch (the same tolerance BERT-APC reports), and compare plain average, weighted median and this method. Target: better than weighted median.

**Listening scenarios:** `correct_pitch_scooped_notes`, `correct_pitch_falls`, `correct_pitch_bends`, `correct_pitch_trills`, `correct_pitch_blue_notes_left_alone`, `correct_pitch_phrase_peaks`.

---

## 9. Pitch problems that need their own design notes

These came out of the research and are **not** solved by choosing a per-note target. Each should be designed before Correct Pitch is finished.

1. **Intended note (which semitone?).** When a note is 40–60 ¢ between grid lines or more than a semitone off, the nearest grid line may be wrong. Needs context: key, neighbouring intervals, phrase, and optionally accompaniment. Current research uses symbolic music models and reports a *harm rate* (correct notes made wrong) alongside accuracy; we should measure both. ([BERT-APC](https://arxiv.org/pdf/2511.20006); [sequence editing, Oct 2026](https://arxiv.org/html/2610.11524); [MIDIBack, Sep 2026](https://arxiv.org/abs/2609.28008))
2. **Reference tuning that moves (pitch drift across a take).** Unaccompanied singers drift: median about 11 ¢ over 50 s, significant in about one in five takes. The best-fitting model updates the reference from recent notes (memory weight about 0.85) rather than holding it fixed. Correct Pitch needs a choice between "fixed A4 = 440 (or detected)", "follow the singer's tuning", and "follow the accompaniment". ([Mauch, Frieler & Dixon 2014](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2014/MauchFrielerDixon_IntonationInUnaccompaniedSinging.pdf))
3. **Ensemble and harmony tuning.** Backing vocals and choirs neither sit on ET nor on just intonation, and voices are often spread wider than written. Hard-snapping harmonies to ET may remove natural spread; vertical (chord-relative) tuning belongs with the adaptive-JI and multitrack work. ([Dai & Dixon](https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2019/DaiDixon-JASA-Quartet.pdf))
4. **Style presets.** Pop, classical, jazz/blues and gamaka-based styles need different defaults for tolerance, `consistent_off_grid` handling and ornament treatment. ([blue notes](https://www.osu.tests.sfulib4.publicknowledgeproject.org/index.php/EMR/article/view/6316); [gamaka quantization](https://archives.ismir.net/ismir2017/paper/000061.pdf))
5. **Resynthesis artifacts of large shifts.** Large corrections add formant shift and robotic colour; recent work restores the audio with a small diffusion model after pitch shifting. Belongs in the synthesis design, not here. ([arXiv 2601.10345](https://arxiv.org/abs/2601.10345))

## 10. Open questions

1. `glissando_g`: 0.16 or 0.32 for sung phrases? Calibrate with the listening sheet.
2. Default tolerance: −10/+15 ¢ (from the APC literature) or symmetric ±10 ¢?
3. Should `consistent_off_grid` notes be skipped by default in the pop preset, or only in jazz/blues?
4. Is a learned stationary-pitch weighting (as in BERT-APC) worth adding in Phase 13, trained on our own annotated notes, with this rule-based method kept as the default and fallback?

---

## 11. Definitions and replacements (resolving what sections 1-10 leave undefined)

Review of 2026-10-19. Each line names the undefined item in the draft and what it now means in this repository.

### Naming, files, types
- **Namespace:** `anno` (repo rule), not `annodyne::analysis`. Types live in `core/types/pitch_target.h`; analysis in `src/core/analysis/pitch_target/` (layer 1; links `core_types`, `core_dsp`, and the `perceived_pitch` target for the shared weights).
- **Note attachment:** `Note` gains `PitchTarget pitch_target`; `Note::perceived_pitch` (Cents; the draft calls it `perceived_pitch_cents`) stays and is set to `pitch_target.target` so existing consumers keep working. `validate(Note)` checks that the two agree and that `core_start <= core_end` lie inside the note. ADR-0013 records this; the owner has not yet confirmed keep-vs-replace.
- **`PitchFlags`:** `std::uint8_t` bit set over `PitchFlag` (7 flags), so a `Note` never allocates for flags.
- **`TonalSegment`/`segments`:** analysis-time data kept in `PitchTarget` (not on the audio thread); a note has at most a few.
- **`Result`, `SourceTime`, `Cents`, `Seconds`:** already in `core/types`.

### Contexts and the take
- **`NoteContext`** = `{ const PitchTarget* previous; const PitchTarget* next; Seconds gap_before; Seconds gap_after; }`. Neighbour targets are null at the ends of a phrase; gaps are silent time between notes from `ActivityTrack` (0 when notes touch).
- **Phrase:** a run of notes separated by silence of at least `phrase_gap` (0.35 s, `ActivityTrack`). **`TakeContext`** = `{ std::vector<SourceRange> phrases; Hz reference_a4{440.0}; }`.
- **Key is not used in Phase 3.** `consistent_off_grid` is defined on pitch class: nearest semitone modulo 12 on the A4 = 6900 grid, so "the same degree" means the same pitch class, with no key. Key-aware behaviour belongs to the intended-note note (section 9.1).

### Style presets
`enum class StylePreset { pop, classical, jazz_blues, gamaka }`. `StyleDefaults` (one table row per preset) holds: `tolerance_low/high` (cents), `glissando_g`, `skip_consistent_off_grid`, `ornament_handling` (`detect_and_keep | leave_alone`), `quantise_turning_points_only`. Only `pop` is validated against data in Phase 3; the other rows are provisional values from the cited studies and are flagged `provisional` in the table and the review.

### Parameters (additions and fixes to the struct in section 4)
`slope_merge` in the text is `slope_merge_cents_per_s`. `plateau_merge` (30 cents) merges adjacent level segments. Added: `peak_tolerance` 20 cents (`expressive_peak`), `phrase_gap` 0.35 s, `run_max_note` 0.25 s (`run`), `off_grid_min_count` 3, `off_grid_spread` 15 cents, `merge_onset_strength` (transient merge threshold, see the segmentation design), `creak_min_stretch` 0.030 s, `creak_tolerance` 50 cents.

### Weights (section 3)
- `stability(t) = 1 / (1 + (|dP/dt| / 300 cents/s)^2)` on the smoothed curve.
- `loudness(t)`: linear amplitude from the note's amp curve, divided by the note's median, clamped to 0.1..1.
- `end_bias(t) = 1 + end_bias * u`, u = position within the core, 0 at its start to 1 at its end.
- `r(t) = voicing_prob x confidence_prob x consonant_factor x creak_factor x disagreement_factor`; each factor in 0..1. `consonant_factor` ramps from 0 at a boundary to 1 at `consonant_zone`.
- Weighted median and "whole vibrato cycles": from `decompose_pitch_curve` / `perceived_pitch` (shared weights, modulation period); not re-implemented.

### Replacements for things the repository does not have
- **Consonant boundaries / phoneme segmentation** -> `OnsetKind::consonant` onsets and unvoiced-to-voiced boundaries of the pitch track. Distance to the nearest of these drives `consonant_factor`.
- **Two trackers (neural + pYIN)** -> disagreement between the best pitch path and `PitchTrack::alternates` (an alternate that is not just the octave) plus low `confidence_prob`. The two-tracker form is the Phase 13 hook; the interface takes a per-frame `disagreement` span so it can be filled later.
- **Creak / period doubling:** a stretch of at least `creak_min_stretch` whose f0 lies within `creak_tolerance` of half or double the note's median f0 (the alternates already hold the octave candidates). Those frames get `creak_factor` 0 and the note gets flag `creak`.
- **`register_break`:** a jump inside one note between two level stretches both longer than `min_core`, not an octave ratio (otherwise it is `creak`).
- **`pitch_target_mode`** (`auto | whole | settled | end | leave`) is a new per-note edit parameter, i.e. a new `Edit` category; specified here, built with Correct Pitch in Phase 4, ADR-0014.

### Citations
Every URL in section 1 comes from the owner's research and is not verified by the implementer; each is listed under "verify before relying" in `docs/reviews/phase3-review.md`.

### Follow-up design notes (section 9), all required before Correct Pitch closes
`intended-note.md`, `reference-tuning-drift.md`, `ensemble-tuning.md`, `style-presets.md` (extends the table above with validated values), and the large-shift artifact item in the synthesis design. They are listed in `docs/ROADMAP.md` as carried items.

---

## 12. Review resolutions (structure-reviewer, 2026-10-19; these override sections 3-11 where they differ)

### Blocking
- **B1 Evidence input.** `estimate_pitch_target(const Note&, const NoteEvidence&, const NoteContext&, const PitchTargetParams&)`; `NoteEvidence` = `{ const PitchTrack& pitch; const OnsetList& onsets; std::span<const double> disagreement; }` (the span may be empty until Phase 13). `note.grid` equals the pitch-track frames covered by the note, so frames are indexed by offset from the note's first pitch frame. Reliability is its own kernel `frame_reliability(note, evidence, params) -> std::vector<double>` in `pitch_target/detail/` (independently tested).
- **B2 Neighbours.** `NoteContext` = `{ std::optional<Cents> previous_pitch; std::optional<Cents> next_pitch; Seconds gap_before; Seconds gap_after; }` using each neighbour's `pitch_center` (available before any target exists), so there is no ordering dependency. `legato` needs `gap_before` < 20 ms; `preparation` needs `gap_after` < 20 ms; after a longer gap the labels are `none`/`scoop`/`fall`.
- **B3 `pitch_target_mode` leaves this note.** Not specified here; it belongs to the Phase 4 Correct Pitch design and ADR-0014. What analysis guarantees instead: `PitchTarget` also carries `std::optional<Cents> target_whole, target_settled, target_end`, filled at analysis time, so a tool (layer 5, no analysis calls) can switch mode from the stored result. Section 5's override paragraph is informative only.
- **B4 ADR-0013 (written before implementation, accepted by the owner)** states: new field `Note::pitch_target`; spec text `perceived_pitch_cents` is the code's `Note::perceived_pitch`; invariant `perceived_pitch == target` when `target` has a value, and for `unpitched` (`target` empty) `perceived_pitch == pitch_center` (finite, as `validate(AnalysisResult)` requires); `validate(Note)` rules (core lies in `[source_start, source_end]`, segments sorted/non-overlapping/inside the note, flag/kind combinations: `ornament` needs `secondary`, `glide` needs `secondary`, `unpitched` has no flags except `creak`/`short_note`); a default `PitchTarget` is kind `unpitched`, empty target, confidence 0 and **validates**, so hand-built notes (scenarios, fixtures, polyphonic/percussive algorithms) stay valid; `render_key` hashes `perceived_pitch` only, never `pitch_target`; JSON gains a `pitch_target` object and keeps `perceived_pitch_cents`; for `glide` `perceived_pitch` is the end target, for `ornament` the main note; spec §4.2 gets `estimate_pitch_target`, `annotate_take` and the `pitch_target` folder (ADR-0010 precedent). Keep-vs-replace of `perceived_pitch`: **keep** (owner confirmation requested; replacing is a later ADR).
- **B5 One concept, one set of weights.** `perceived_pitch(const Note&)` stays as the kernel for `whole`/`settled` targets. The target reuses `stability_weights` and `StabilityWeightParams`; this note's own `stability(t)`/`loudness(t)` formulas in section 11 are **withdrawn**. Public additions to `perceived_pitch.h`: `vibrato_period(const Note&) -> std::optional<Seconds>` and `weighted_median(values, weights) -> Cents`. Orchestrator order: segment -> description (pitch curve, loudness) -> `perceived_pitch` provisional -> `estimate_pitch_target` -> final `Note::perceived_pitch`.
- **B6 No stage-fatal errors for thin evidence.** `Result` errors only for invalid params or inconsistent inputs (curve length mismatch). Fewer than 3 reliable frames gives a valid result: `unpitched` if the median voicing probability over the note is below `unpitched_voicing` (0.3), else `whole` with flag `short_note` and low confidence. Tests for both.
- **B7** applies to the segmentation note (below).
- **B8 One owner of "one note or two".** Segmentation owns boundaries. `multi` is the fallback for plateaus that segmentation kept together: two level segments, each >= `min_core`, separated by `multi_min_separation` (60 cents) up to the merge guard (150 cents); beyond that segmentation already split them. Trills: segmentation ignores `pitch_change` onsets that belong to a **trill cluster** (>= 3 alternating-direction steps, each plateau <= 150 ms) so the trill stays one note and reaches the `ornament` check. Integration tests feed a trill and a slow legato pair through `segment_notes` into the target.
- **B9 Glissando arithmetic.** Two separate concepts: (1) **transient zones**: in the first/last `transient_window` (120 ms) any segment with |Δ| >= `turn_threshold` (35 cents) is an approach/release and is excluded from the core, whatever its rate; `transient_min_change` is removed (it equals `turn_threshold`). (2) **Moving** (kinds `glide`, `multi`) uses the glissando test on segments longer than the zone: |Δ| > g*100/T with T the segment duration after segmentation and g in semitone-seconds; g = 0.32 default (owner, 2026-10-20), 0.16 an option for isolated tones (style table). The test table is re-checked on this basis: scoop 200 c/80 ms and rise 80 c/120 ms are transient-zone labels (`scoop`, `rise`); +25 cent end drift is below `turn_threshold` and stays `whole`; the 400 ms bend of 100 cents (threshold 40 cents) is `glide`.

### Should-fix (decisions)
- **S1** `P(t)` for segmenting = `pitch_center + pitch_drift + pitch_mod` smoothed by a separate short window `segment_smoothing` (0.05 s); the 0.14 s window is used only for the level-core mean.
- **S2** Ornament check: `flat_top_share` >= 0.5 (share of each half-cycle within 15 cents of its extreme) is a named parameter; a +-100 cent sinusoidal vibrato must be `whole` (test).
- **S3** `consistent_off_grid`: subtract the take's median off-grid offset first; flag only classes whose offset differs from it by >= `off_grid_min_offset` (15 cents) with >= 3 occurrences within `off_grid_spread`. A uniformly sharp take flags nothing (test). Reference A4 is `AnalysisResult::tuning_ref` (single source); `TakeContext` has no `reference_a4`.
- **S4** `annotate_take(std::span<const Note>, const TakeContext&, const PitchTargetParams&) -> std::vector<PitchFlags>`; the orchestrator applies the result in a second pass (Notes are immutable after construction, so it builds the final Note then). Flag ownership: per-note pass = `short_note`, `creak`, `register_break`, `ambiguous_intent` (needs `tuning_ref`, an input of `NoteContext`); take pass = `run`, `consistent_off_grid`, `expressive_peak`.
- **S5** Types: `confidence_prob`; `SourceRange core`; `TonalShape { level, moving }` (rising/falling via sign); `PitchFlags` is `std::uint16_t` with `[[nodiscard]] constexpr has/set/clear`; `PitchTargetParams` split into nested groups (reliability, segmentation, classification, take); `StylePreset`/`StyleDefaults` (tolerance, skip rules, g) live in `core/tuning` (layer 1, usable by tools), and `PitchTargetParams` takes only `glissando_g` from it. No `core_dsp` link unless used.
- **S6** Sections 5 and 6 (correction use, UI) are informative only; the normative homes are the Correct Pitch tool and UI design notes (Phase 4).
- **S7** Audio code reads only `EffectiveNote`; it never copies a `Note` or touches `segments`. Both functions allocate: not real-time safe.
- **S12** Disagreement and creak: `alternates` are other Viterbi paths, not a second tracker, so the **disagreement factor is 1 until Phase 13** (the `disagreement` span is the hook). Creak is detected from the note's own f0: a stretch >= `creak_min_stretch` at half/double the note median f0 (within `creak_tolerance` 50 cents of the median), using the best path only.
- **S13** `annotate_take` runs only inside the orchestrator; `phrases` are derived from note gaps (>= `phrase_gap`), not from `ActivityTrack` (internal, ADR-0011).
- Sections 1 and 9 stay as research context; citations unverified. `glissando_g` and the default tolerance are explicit **owner gates** in the Phase 3 review.

### Tests added
Error cases (`< 3` frames, bad params, size mismatch); NaN-free output; determinism; `validate(Note)` agreement (contract test) and the `perceived_pitch == target` check on every note from `analyse()`; `unpitched` threshold; `register_break`; `run`; `ambiguous_intent`; ornament secondary pitch; +-100 cent vibrato is `whole`; uniformly sharp take flags nothing; 2 occurrences do not flag; phrase-end null neighbours; `overshoot` vs `scoop`; weighted-median fallback. Each tolerance gets an entry in `tests/tolerances.h`. The "CSD" golden set is dropped for Phase 3 (not in the repo, licence unchecked); Vocadito only.

---

## 13. Owner decisions of 2026-10-20 (override sections 4, 11 and 12 where they differ)

1. **`Note::perceived_pitch` is replaced by `Note::pitch_target`** (owner: "that concept is more accurate"). The field `perceived_pitch` is removed from `Note`; consumers read `note.pitch_target.target` (render key, JSON `pitch_target.target_cents`, plots, validation, scenario fixtures, tests). `PitchTarget::target` is always a finite `Cents`: for `unpitched` it is the note's `pitch_center`, and `kind` tells consumers it is not a real target (so no `optional`, and section 12 B4's `perceived_pitch == target` invariant becomes: `validate(Note)` requires `target` finite and, for kind `unpitched`, equal to `pitch_center`). The function `perceived_pitch(Note)` stays as the internal kernel for `whole`/`settled` (section 12 B5) but no longer names a Note field; it may be renamed `stable_pitch` in the implementation. A default `PitchTarget` is `unpitched`, target 0, confidence 0 and validates. The ADR is ADR-0013 (accepted 2026-10-20); the architecture spec's `perceived_pitch` is overridden by it.
2. **`glissando_g` default 0.32** (not 0.16); 0.16 remains an option for isolated tones in the style table. The transient-zone rule (section 12 B9) stands. If listening shows the wrong split, `g` is changed later; it is a parameter, not a structure.
3. Default correction tolerance (-10/+15 cents) is not yet decided and stays an owner gate for Phase 4.

---

## 14. Baseline measurements (stage 1, 2026-10-20; Vocadito, annotator 1, `vocadito_report`)

Matched notes = annotated notes with one of our starts within 50 ms. "APC" = estimate within -10/+15 cents of the annotated pitch.

| Estimate | 35 tuning tracks | 5 held-out |
|---|---|---|
| Current `perceived_pitch` | 0.528 | 0.466 |
| Plain mean of the note's voiced frames | 0.524 | 0.513 |
| Plain median of the note's voiced frames | 0.632 | 0.576 |

Segmentation shape: annotated notes held by two or more of our note middles (over-segmented) 18.6 % / 18.8 %; our notes holding two or more annotated onsets (under-segmented) 1.2 % / 4.3 %. Stage targets: over-segmentation falls without under-segmentation rising and without a drop in onset F20/F50; the new target must beat the median (0.632 / 0.576) on APC.

---

## 15. Stage 3 result (2026-10-20): target core implemented

Implemented: `core/analysis/pitch_target` (reliability, tonal segments, kinds `whole/settled/glide/multi/unpitched`, approach/release labels, `short_note` and `creak` flags, confidence, candidate targets), wired into `analyse()`; `Note::perceived_pitch` is gone (ADR-0013). 24 unit tests from §8 plus the review additions, all passing on debug-asan and release.

Changes made while measuring (on the 35 tuning tracks; the held-out 5 were checked, not tuned on):
- The stability factor and the end bias (research item 4/16) are **off** by default (`gamma = 0`, `end_bias = 0`): with them on, the share of targets within -10/+15 cents fell from 0.60 to 0.51. The loudness term stays.
- A glide needs a net change of >= 60 cents and a note of >= 250 ms; shorter "moving" notes are judged as a whole (many real 100-200 ms notes are moving everywhere).
- Transient zones are at most 20 % of a short note each; slow drifts use T capped at 0.4 s in the glissando test; a fast step of >= 60 cents separates cores (`multi`); vibrato is detected by zero-crossing rate as well as autocorrelation (a step passed the autocorrelation test), and a note with vibrato is segmented on centre + drift.

| Held-out 5, annotator 1 | before (perceived_pitch) | pitch target |
|---|---|---|
| matched-note pitch within 50 cents | 0.740 | 0.784 |
| within -10/+15 cents (APC) | 0.520 | 0.568 |
| plain mean / median of the note's voiced frames | 0.573 / 0.630 | same |

The target **does not beat the plain median** on the APC metric (the §8 golden goal). Reason, to be decided by the owner: the annotators' pitch is itself close to a median over the whole note, scoops included, while the target excludes approach and release by design (owner principle: rolls are not averaged). The APC guard is therefore 0.52 (regression), and the 50-cent figure improved. Remaining stages: ornaments (4), take pass (5), presets and spec of the mode edit (6), close (7).

## 16. Stage 4 result (2026-10-20): ornaments

`find_ornament` (detail/ornament): two flat levels >= 80 cents apart, every plateau (>= 40 ms) stationary for at least 35 % of its frames (slope under 400 cents/s; the tracker smooths a clean step over ~50 ms, so a stricter test missed a real trill, while a 6 Hz vibrato is never stationary), at least 2 level changes. Kind `ornament`: `target` is the level the note starts on, `secondary` the other; never averaged. Gamaka handling stays style-gated and arrives with the style table (stage 6). Segmentation keeps a trill whole (ADR-0010a, 5 alternating steps): an `analyse()` test on a synthetic 6 Hz trill gives one note of kind `ornament`. No change on the Vocadito subsets (no false ornaments); 5 steps was chosen because 3 merged real fast melodic alternations.

## 17. Stages 5-6 result (2026-10-20) and what is left

- `annotate_take` (take_annotation.h): `run`, `consistent_off_grid` (the take's median offset is removed first, so a singer sharp throughout flags nothing), `expressive_peak`; the orchestrator ORs the result into `Note::pitch_target.flags` once all notes exist. Per-note flags added to `estimate_pitch_target`: `ambiguous_intent`, `register_break`. 7 take-level tests and 4 flag tests.
- `core/tuning` (new layer-1 target): `StylePreset`, `StyleDefaults`, `style_defaults()`; `pitch_target_params_for(preset)` applies `glissando_g` and gamaka detection. Only `pop` is non-provisional. A synthetic gamaka-style note is an ornament only with the gamaka preset.
- Plots: the SVG draws the whole note faintly and the core (what the target is computed from) in the kind's colour with the kind in the label; glide start / ornament's other pitch as a dashed tick. JSON exports the full `pitch_target` object.
- **Left for later (not done):** `pitch_target_mode` edit and the tolerance/skip behaviour (Phase 4 Correct Pitch, ADR-0014); the `intended-note`, `reference-tuning-drift`, `ensemble-tuning`, `style-presets` design notes and large-shift artifacts (section 9); two-tracker disagreement (Phase 13; the `disagreement` span is the hook); the tuning reference (`tuning_offset` is 0 until Phase 8); thresholds of `register_break`, `ambiguous_intent`, `expressive_peak` and all non-pop presets need annotated data or listening; the owner's plot check (tracks 13 and 28 again, and a trill/glide example).
