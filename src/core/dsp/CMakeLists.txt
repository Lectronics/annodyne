anno_add_library(core/dsp
    SOURCES
        crossfade.cpp
    PUBLIC_DEPS anno::core_types)
# Bit-for-bit reproducible gains: no fused multiply-add (design note phase2, scope decision 8).
if(NOT MSVC)
    target_compile_options(anno_core_dsp PRIVATE -ffp-contract=off)
endif()
