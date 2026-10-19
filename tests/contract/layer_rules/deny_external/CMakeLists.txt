cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_external LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R6: a non-melo dependency outside the external allowlist.
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/analysis/pitch SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
target_link_libraries(melo_core_analysis_pitch PRIVATE some_third_party_lib)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
