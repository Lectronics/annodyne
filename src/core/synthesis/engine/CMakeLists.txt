# Interface only (header-only target): the concrete engines are sibling folders.
anno_add_library(core/synthesis/engine
    SOURCES
        synthesis_engine.cpp
    PUBLIC_DEPS anno::core_types)
