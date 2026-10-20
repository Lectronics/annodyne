# c4dm/pyin (ADR-0014): the pYIN pitch-tracking core, GPL-2.0-or-later, vendored verbatim in
# third_party/c4dm_pyin (see ANNODYNE.md there). Only core/analysis/pitch may link anno_c4dm_pyin
# (AnnoLayers.cmake external allowlist), privately: no c4dm header appears in a public header.

add_library(anno_c4dm_pyin STATIC
    third_party/c4dm_pyin/Yin.cpp
    third_party/c4dm_pyin/YinUtil.cpp
    third_party/c4dm_pyin/MonoPitchHMM.cpp
    third_party/c4dm_pyin/SparseHMM.cpp)
# SYSTEM: third-party headers must not trip -Werror -Wconversion -Wshadow.
target_include_directories(anno_c4dm_pyin SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(anno_c4dm_pyin PUBLIC anno_pocketfft)
set_target_properties(anno_c4dm_pyin PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(anno_c4dm_pyin PRIVATE /w)   # upstream code, not ours to warn about
else()
    # Upstream code is not ours to warn about; no fused multiply-add keeps one build reproducible.
    target_compile_options(anno_c4dm_pyin PRIVATE -w -ffp-contract=off)
endif()
