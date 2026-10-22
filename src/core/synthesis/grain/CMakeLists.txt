# Marks, period curve, fractional reader and grain overlap-add shared by the PSOLA and stretch
# renderers (ADR-0023). Moved out of psola_engine.cpp unchanged.
anno_add_library(core/synthesis/grain
    SOURCES
        marks.cpp
        overlap_add.cpp
        period_curve.cpp
    PUBLIC_DEPS anno::core_types)
# Bit-for-bit reproducible renders (render cache keys, ADR-0006): no fused multiply-add.
if(NOT MSVC)
    target_compile_options(anno_core_synthesis_grain PRIVATE -ffp-contract=off)
endif()
