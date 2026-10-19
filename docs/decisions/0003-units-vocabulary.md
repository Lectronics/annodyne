# ADR-0003: Units vocabulary

- **Date:** 2026-10-19
- **Status:** accepted
- **Phase:** 0

## Context
ROADMAP Phase 0 lists `SourceTime`, `OutputTime`, `Seconds`, `Samples`, `Cents`, `Decibels` and
`Hz`. The structure review (`docs/design/phase0-foundations.md`, review log) found that these are
not enough to satisfy CLAUDE.md rule 4 ("no unitless pitch or gain values"):
- Spec §3 has `loudness_lu`.
- Spec §5.3 has many dimensionless scales (`mod_scale`, `drift_scale`, `length_scale`, …).
- `to_linear_gain` would otherwise return a raw `double`.

Time ranges (`source_range`, `out_range`, `dirty_regions`) also need a typed form.

## Decision
- **Added strong types:**
  - `Ratio`: dimensionless multiplier.
  - `LinearGain`: amplitude factor.
  - `LoudnessUnits`: LU, or LUFS when absolute.
  - `TimeRange<Axis>` (`SourceRange`, `OutputRange`): a class with invariant `start <= end`,
    `make() -> Result`, `from_start()`, and a default value that is empty at the origin.
- **Time axes:**
  - `SourceTime` and `OutputTime` are affine points: `point - point = Seconds`.
  - Sample positions come from `to_sample_index(TimePoint<Axis>, Hz, Rounding)`.
  - Raw seconds can be read out only through `unchecked_from_origin` / `unchecked_since_origin`,
    which are allowed only in `core/types` and `core/timemap`.
  - Persistence uses `to_serialized_seconds` / `from_serialized_seconds` (the latter returns
    `Result`), allowed only in `core/types`, `project` and `cli/*`.
  - `scripts/check_includes.py` enforces both restrictions.
- **One type, two meanings, on purpose.** Field names say which meaning is used (`peak_dbfs`,
  `perceived_pitch_cents`, `loudness_lufs`, `sample_rate`):
  - `Cents`: absolute pitch (A4 = 6900) or interval.
  - `Decibels`: dBFS or gain.
  - `LoudnessUnits`: LUFS or LU.
  - `Hz`: frequency or sample rate.
- **Raw doubles that remain** carry suffixes:
  - `_prob` for probabilities in 0..1;
  - `_rad` for phase;
  - `_ratio` inside kernels.

## Alternatives considered
- **Separate point and delta types for pitch, level and loudness.** This is more type-safe, but
  roughly doubles the number of types and conversions before any DSP exists. It can be revisited
  if confusing absolute and relative values causes bugs.
- **Leave scales as `double` with a `_scale` suffix.** Then `Cents * double` would compile with any
  double, so the type would carry no meaning.

## Consequences
- `coding-standards.md` §2 lists the new types and suffixes.
- Phase 1 `EffectiveNote` uses `Ratio` for scales, `Cents` for offsets, `Decibels` for gains and
  `Seconds` for fade lengths.

## Spec updates
`.claude/rules/coding-standards.md` §2 is updated in the same commit.
