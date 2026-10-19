# core/timemap

`build_time_map` (layer 3): timing edits -> `TimeMap`. The mapping class itself is in `core/types`
so synthesis can use it.

Method: one anchor list on the source axis — origin, then per active note its start, attack end,
in-body time handles and end. Attack segments scale only with `attack_time_scale`; the body absorbs
`length_scale`; gaps stretch by interpolation. Collisions are repaired deterministically (rigid
shift of the later note, stretched shared boundary, clamped handles) and counted in
`TimeMapBuild::repaired_notes`. With no timing edits the result is exactly `TimeMap::identity()`.

References: architecture spec §2, §5.4; implementation guide §11 (time handles);
`docs/design/phase1-data-model.md`.
Known limitations: monophonic only (overlapping active notes are an error until Phase 11); handles
in inter-note gaps are not supported; handles are absolute output targets (open question 6).
