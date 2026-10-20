anno_add_library(core/analysis/activity
    SOURCES
        activity.cpp
    PUBLIC_DEPS anno::core_types anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_activity PRIVATE -ffp-contract=off)
endif()
