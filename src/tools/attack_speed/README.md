# tools/attack_speed

The Attack Speed Tool's edit (features spec §12): `attack_speed_edit` sets `attack_time_scale` (origin "attack_speed_tool", category attack): below 1 a sharper attack, above 1 a softer one. Layer 5; links only `core/types`. Errors: `invalid_argument` (unknown/inactive note, non-finite or non-positive scale, a note with no attack), `out_of_range` (a scale below `k_min_time_scale`, checked after the others). Design: `docs/design/phase5-time-tools.md`. Tests: `tests/unit/time_tools_test.cpp`.
