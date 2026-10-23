anno_add_library(core/analysis/harmonic_noise
    SOURCES
        harmonic_noise.cpp
    PUBLIC_DEPS anno::core_types anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_harmonic_noise PRIVATE -ffp-contract=off)
endif()
