cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_tool_to_render LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R2: a tool links core/render (tools never produce audio).
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/render SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(tools/pitch SOURCES ${DUMMY} PRIVATE_DEPS melo_core_render)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
