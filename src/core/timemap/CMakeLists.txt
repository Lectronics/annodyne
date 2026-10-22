anno_add_library(core/timemap
    SOURCES
        build_time_map.cpp
        detail/anchors.cpp
        layout_markers.cpp
    PUBLIC_DEPS anno::core_types anno::core_edits)
