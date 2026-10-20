anno_add_library(core/analysis/pitch_target
    SOURCES
        detail/ornament.cpp
        detail/reliability.cpp
        detail/tonal_segments.cpp
        pitch_target.cpp
    PUBLIC_DEPS anno::core_types anno::core_analysis_perceived_pitch)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_pitch_target PRIVATE -ffp-contract=off)
endif()
