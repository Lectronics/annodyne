# core/timemap

`build_time_map` (layer 3): timing edits -> `TimeMap`. The mapping class itself is in `core/types`
so synthesis can use it.

Method (Flex-style, ADR-0005 §16): anchors come only from notes that have a timing edit — their
start, attack end, in-body time handles and end — plus the fixed origin and `source_end`. The
TimeMap is linear between neighbouring anchors, so moving a handle stretches the audio on both
sides up to the next changed handle or the file ends; unedited notes in between are stretched too.
Attack segments scale only with `attack_time_scale`; the body absorbs `length_scale`. Conflicts are
repaired deterministically (outputs pushed later, pulled back from the fixed end, coincident
boundary takes the later output, unedited timeline if there is no room) and counted in
`TimeMapBuild::repaired_notes`. With no timing edits the result is exactly `TimeMap::identity(length)`.

References: architecture spec §2, §5.4; implementation guide §11 (time handles);
`docs/design/phase1-data-model.md`.
Known limitations: monophonic only (overlapping active notes are an error until Phase 11); handles
in inter-note gaps are not supported; handles are absolute output targets. Open: whether the renderer stretches before or after pitch correction (ADR-0005 §16).

## Layout (ADR-0025)

`layout_markers` (pinned markers from the effective edit state), `solve_layout` (per interval: the adjacent gap is cut when the interval shrinks, held at slope 1 while the audio is stretched when it grows; slope limits 0.25-4; right-marker repair; fixed-end fallback), `build_layout_map` (spans, whole samples on both axes with a sample rate), `build_layout_time_map` (the entry point; mode `stretch` is `build_time_map`), `shift_step_samples` (the shift grid: a 1/512 note at the tempo, whole samples), `changed_source_ranges` (the source ranges whose mapping changed: local invalidation). Design: `docs/design/phase5-layout.md`. Limits: one absorbing gap per interval (by position, not by loudness); the cut sits at the centre of the longest silence region; the insert is digital silence until room tone.
