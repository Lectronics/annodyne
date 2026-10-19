cmake_minimum_required(VERSION 3.25)
project(melo_layer_fixture_allow_dna_to_analysis LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control: features/dna may link core/analysis/* (architecture.md exception).
melo_add_library(core/types SOURCES ${DUMMY})
melo_add_library(core/analysis/polyphonic SOURCES ${DUMMY} PUBLIC_DEPS melo_core_types)
melo_add_library(features/dna SOURCES ${DUMMY} PUBLIC_DEPS melo_core_analysis_polyphonic)

melo_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
