anno_add_library(core/analysis/gaps
    SOURCES
        analyse_gaps.cpp
        build_segment_timeline.cpp
        classify_gap_frames.cpp
        estimate_noise_floor.cpp
        gap_evidence.cpp
    PUBLIC_DEPS anno::core_types anno::core_dsp anno::core_analysis_activity
                anno::core_analysis_harmonic_noise)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_gaps PRIVATE -ffp-contract=off)
endif()
