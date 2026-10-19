cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_allow_dna_to_analysis LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control: features/dna may link core/analysis/* (architecture.md exception).
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/analysis/polyphonic SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(features/dna SOURCES ${DUMMY} PUBLIC_DEPS anno_core_analysis_polyphonic)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
