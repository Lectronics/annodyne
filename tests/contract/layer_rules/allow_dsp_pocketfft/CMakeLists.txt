cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_allow_dsp_pocketfft LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control (ADR-0009): core/dsp may link the FFT library, privately.
add_library(anno_pocketfft INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/dsp SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types
                 PRIVATE_DEPS anno_pocketfft)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
