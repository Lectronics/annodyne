cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_cycle LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R5: same-layer link cycle.
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/analysis/pitch SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(core/analysis/onsets SOURCES ${DUMMY} PUBLIC_DEPS anno_core_analysis_pitch)
target_link_libraries(anno_core_analysis_pitch PUBLIC anno_core_analysis_onsets)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
