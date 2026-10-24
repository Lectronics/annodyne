anno_add_library(core/timemap
    SOURCES
        build_layout_map.cpp
        build_time_map.cpp
        detail/anchors.cpp
        layout_markers.cpp
        map_diff.cpp
        shift_grid.cpp
        solve_layout.cpp
    PUBLIC_DEPS anno::core_types anno::core_edits)
