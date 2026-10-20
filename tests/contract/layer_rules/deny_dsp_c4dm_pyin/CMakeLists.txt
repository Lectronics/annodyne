cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_dsp_c4dm_pyin LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R6 (ADR-0014): the vendored c4dm/pyin is not a DSP building block; core/dsp may not link it.
add_library(anno_c4dm_pyin INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/dsp SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types
                 PRIVATE_DEPS anno_c4dm_pyin)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
