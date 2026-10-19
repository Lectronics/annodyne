# The engine interface and read_through_map; the concrete engines are sibling folders.
anno_add_library(core/synthesis/engine
    SOURCES
        source_through_map.cpp
        synthesis_engine.cpp
    PUBLIC_DEPS anno::core_types)
