anno_add_library(core/dsp
    SOURCES
        crossfade.cpp
        fft.cpp
        filter.cpp
        interp_curve.cpp
        k_weight.cpp
        mono.cpp
        resample.cpp
        stft.cpp
        window.cpp
    PUBLIC_DEPS anno::core_types
    PRIVATE_DEPS anno_pocketfft)
# Bit-for-bit reproducible results: no fused multiply-add (design note phase2, scope decision 8).
if(NOT MSVC)
    target_compile_options(anno_core_dsp PRIVATE -ffp-contract=off)
endif()
