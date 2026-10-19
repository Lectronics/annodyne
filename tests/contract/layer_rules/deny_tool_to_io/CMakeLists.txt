cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_tool_to_io LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R4: core/io may only be linked by layers 6-7 (also R2 for a tool).
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/io SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(tools/pitch SOURCES ${DUMMY} PRIVATE_DEPS anno_core_io)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
