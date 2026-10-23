anno_add_library(core/analysis/orchestrator
    SOURCES
        analyse.cpp
    PUBLIC_DEPS anno::core_types anno::core_analysis_pitch anno::core_analysis_onsets
                anno::core_analysis_segmentation anno::core_analysis_perceived_pitch
                anno::core_analysis_pitch_target
                anno::core_analysis_loudness anno::core_analysis_activity
                anno::core_analysis_partials anno::core_analysis_harmonic_noise
                anno::core_analysis_envelope
    PRIVATE_DEPS anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_orchestrator PRIVATE -ffp-contract=off)
endif()
