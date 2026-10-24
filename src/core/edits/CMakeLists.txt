anno_add_library(core/edits
    SOURCES
        detail/fold.cpp
        dirty_regions.cpp
        edit_list.cpp
        effective_gap.cpp
        effective_state.cpp
        remap.cpp
    PUBLIC_DEPS anno::core_types)
