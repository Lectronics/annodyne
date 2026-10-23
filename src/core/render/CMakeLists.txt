find_package(Threads REQUIRED)

anno_add_library(core/render
    SOURCES
        blend_plan.cpp
        render.cpp
        render_cache.cpp
        render_key.cpp
    PUBLIC_DEPS anno::core_types anno::core_edits anno::core_synthesis_engine
        anno::core_synthesis_stretch
    PRIVATE_DEPS anno::core_dsp anno::core_timemap Threads::Threads)
# Bit-for-bit reproducible blends: no fused multiply-add (design note phase2, scope decision 8).
if(NOT MSVC)
    target_compile_options(anno_core_render PRIVATE -ffp-contract=off)
endif()
