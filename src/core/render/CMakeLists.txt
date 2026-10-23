find_package(Threads REQUIRED)

anno_add_library(core/render
    SOURCES
        blend_plan.cpp
        gate.cpp
        level.cpp
        render.cpp
        render_cache.cpp
        render_key.cpp
        splice.cpp
    PUBLIC_DEPS anno::core_types anno::core_edits anno::core_synthesis_engine
        anno::core_synthesis_stretch anno::core_timemap
    PRIVATE_DEPS anno::core_dsp Threads::Threads)
# Bit-for-bit reproducible blends: no fused multiply-add (design note phase2, scope decision 8).
if(NOT MSVC)
    target_compile_options(anno_core_render PRIVATE -ffp-contract=off)
endif()
