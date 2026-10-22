anno_add_library(core/analysis/onsets
    SOURCES
        stages/band_spectrogram.cpp
        stages/level_norm.cpp
        stages/localise.cpp
        stages/peak_pick.cpp
        stages/spectral_flux.cpp
        transient_pipeline.cpp
        onsets.cpp
        detail/energy_step.cpp
        detail/merge_onsets.cpp
        detail/onset_params_check.cpp
        detail/peak_picking.cpp
        detail/pitch_change.cpp
        detail/spectral_onsets.cpp
        detail/superflux.cpp
    PUBLIC_DEPS anno::core_types
    PRIVATE_DEPS anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_onsets PRIVATE -ffp-contract=off)
endif()
