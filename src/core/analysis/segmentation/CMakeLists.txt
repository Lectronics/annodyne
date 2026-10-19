anno_add_library(core/analysis/segmentation
    SOURCES
        note_hmm.cpp
        segmentation.cpp
    PUBLIC_DEPS anno::core_types)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_segmentation PRIVATE -ffp-contract=off)
endif()
