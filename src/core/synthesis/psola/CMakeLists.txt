anno_add_library(core/synthesis/psola
    SOURCES
        psola_engine.cpp
    PUBLIC_DEPS anno::core_synthesis_engine anno::core_synthesis_grain anno::core_types)
# Bit-for-bit reproducible renders (render cache keys, ADR-0006): no fused multiply-add.
if(NOT MSVC)
    target_compile_options(anno_core_synthesis_psola PRIVATE -ffp-contract=off)
endif()
