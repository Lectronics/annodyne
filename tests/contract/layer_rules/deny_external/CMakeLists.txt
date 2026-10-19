cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_external LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R6: a non-anno dependency outside the external allowlist.
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/analysis/pitch SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
target_link_libraries(anno_core_analysis_pitch PRIVATE some_third_party_lib)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
