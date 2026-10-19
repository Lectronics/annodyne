cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_upward LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R1: layer 0 links layer 6.
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(project SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(core/dsp SOURCES ${DUMMY} PRIVATE_DEPS anno_project)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
