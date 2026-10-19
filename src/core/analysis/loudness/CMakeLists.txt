anno_add_library(core/analysis/loudness
    SOURCES
        loudness.cpp
    PUBLIC_DEPS anno::core_types anno::core_dsp anno::core_analysis_segmentation)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_loudness PRIVATE -ffp-contract=off)
endif()
