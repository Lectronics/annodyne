cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_allow_private_transitive LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control: core/edits links core/dsp PRIVATE, so a tool linking core/edits does not use dsp.
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/dsp SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(core/edits SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types PRIVATE_DEPS anno_core_dsp)
anno_add_library(tools/pitch SOURCES ${DUMMY} PUBLIC_DEPS anno_core_edits)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
