cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_deny_upward LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R1: layer 0 links layer 6.
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(project SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(core/dsp SOURCES ${DUMMY} PRIVATE_DEPS melo_project)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
