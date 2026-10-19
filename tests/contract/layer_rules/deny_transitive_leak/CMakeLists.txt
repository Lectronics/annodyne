cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_transitive_leak LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R2 (transitive): core/edits links core/dsp PUBLIC, so the tool would inherit dsp.
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/dsp SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(core/edits SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types melo_core_dsp)
melo_add_library(tools/pitch SOURCES ${DUMMY} PUBLIC_DEPS melo_core_edits)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
