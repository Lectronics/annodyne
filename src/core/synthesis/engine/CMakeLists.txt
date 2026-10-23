# The engine interface, read_through_map and the helpers the pitch engines share; the concrete
# engines are sibling folders.
anno_add_library(core/synthesis/engine
    SOURCES
        engine_support.cpp
        source_through_map.cpp
        synthesis_engine.cpp
    PUBLIC_DEPS anno::core_types)
