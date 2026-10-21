anno_add_library(core/synthesis/harmonic_noise
    SOURCES
        harmonic_noise_engine.cpp
    PUBLIC_DEPS anno::core_synthesis_engine anno::core_types
    PRIVATE_DEPS anno::core_dsp)
# Bit-for-bit reproducible renders (render cache keys, ADR-0006): no fused multiply-add.
if(NOT MSVC)
    target_compile_options(anno_core_synthesis_harmonic_noise PRIVATE -ffp-contract=off)
endif()
