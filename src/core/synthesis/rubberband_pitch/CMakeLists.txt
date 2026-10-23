# Pitch edits by Rubber Band R3 pitch scaling (ADR-0027): an A/B candidate for the pitch-edit engine.
anno_add_library(core/synthesis/rubberband_pitch
    SOURCES
        rubberband_pitch_engine.cpp
    PUBLIC_DEPS anno::core_synthesis_engine anno::core_types
    PRIVATE_DEPS anno_rubberband)
# Bit-for-bit reproducible renders (render cache keys, ADR-0006): no fused multiply-add.
if(NOT MSVC)
    target_compile_options(anno_core_synthesis_rubberband_pitch PRIVATE -ffp-contract=off)
endif()
