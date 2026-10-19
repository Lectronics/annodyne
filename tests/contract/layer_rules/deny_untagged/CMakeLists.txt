cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_untagged LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R0: a plain add_library escapes the layer table.
melo_add_library(core/types SOURCES ${DUMMY})
add_library(rogue STATIC ${DUMMY})

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
