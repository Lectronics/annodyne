cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_cycle LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R5: same-layer link cycle.
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/analysis/pitch SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(core/analysis/onsets SOURCES ${DUMMY} PUBLIC_DEPS melo_core_analysis_pitch)
target_link_libraries(melo_core_analysis_pitch PUBLIC melo_core_analysis_onsets)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
