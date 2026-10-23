anno_add_library(core/analysis/envelope
    SOURCES
        envelope.cpp
    PUBLIC_DEPS anno::core_types anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_envelope PRIVATE -ffp-contract=off)
endif()
