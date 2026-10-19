cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_allow_private_transitive LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control: core/edits links core/dsp PRIVATE, so a tool linking core/edits does not use dsp.
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/dsp SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(core/edits SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types PRIVATE_DEPS melo_core_dsp)
melo_add_library(tools/pitch SOURCES ${DUMMY} PUBLIC_DEPS melo_core_edits)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
