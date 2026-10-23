# Time-stretch rendering of TimeMap warps with slope != 1 (ADR-0023).
anno_add_library(core/synthesis/stretch
    SOURCES
        stretch.cpp
        detail/class_map.cpp
        detail/noise_stretch.cpp
        detail/voiced_stretch.cpp
    PUBLIC_DEPS anno::core_synthesis_grain anno::core_types)
# Bit-for-bit reproducible renders (render cache keys, ADR-0006): no fused multiply-add.
if(NOT MSVC)
    target_compile_options(anno_core_synthesis_stretch PRIVATE -ffp-contract=off)
endif()
