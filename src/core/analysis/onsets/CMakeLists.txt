anno_add_library(core/analysis/onsets
    SOURCES
        onsets.cpp
        detail/energy_step.cpp
        detail/peak_picking.cpp
        detail/pitch_change.cpp
        detail/superflux.cpp
    PUBLIC_DEPS anno::core_types
    PRIVATE_DEPS anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_onsets PRIVATE -ffp-contract=off)
endif()
