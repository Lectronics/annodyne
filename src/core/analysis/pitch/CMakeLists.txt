anno_add_library(core/analysis/pitch
    SOURCES
        pitch_estimator.cpp
        detail/pitch_hmm.cpp
        pyin.cpp
        detail/pyin_observe.cpp
        detail/yin.cpp
    PUBLIC_DEPS anno::core_types
    PRIVATE_DEPS anno::core_dsp)
# Bit-for-bit reproducible results on one build: no fused multiply-add (same rule as core/dsp).
if(NOT MSVC)
    target_compile_options(anno_core_analysis_pitch PRIVATE -ffp-contract=off)
endif()
