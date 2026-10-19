anno_add_library(core/types
    SOURCES
        audio_buffer.cpp
        analysis_validate.cpp
        audio_source.cpp
        contract.cpp
        edit.cpp
        error.cpp
        note_span.cpp
        time_map.cpp
        units.cpp)
# Bit-for-bit reproducible results on one build: to_cents / to_hz are on the analysis path, so no
# fused multiply-add here either (same rule as core/dsp and core/analysis/*).
if(NOT MSVC)
    target_compile_options(anno_core_types PRIVATE -ffp-contract=off)
endif()
