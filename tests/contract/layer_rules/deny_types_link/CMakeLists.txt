cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_types_link LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R3: core/types is data only and links nothing.
melo_add_library(core/dsp SOURCES ${DUMMY})
melo_add_library(core/types SOURCES ${DUMMY} PRIVATE_DEPS melo_core_dsp)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
