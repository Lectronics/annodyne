cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_analysis_pocketfft LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R6 (ADR-0009): only core/dsp links the FFT library; analysis goes through core/dsp.
add_library(anno_pocketfft INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/analysis/pitch SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types
                 PRIVATE_DEPS anno_pocketfft)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
