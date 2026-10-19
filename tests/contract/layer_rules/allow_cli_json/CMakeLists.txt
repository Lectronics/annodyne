cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_allow_cli_json LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control (ADR-0007): cli targets may link nlohmann/json.
add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(cli/scenario SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types)
anno_add_library(cli/scenario_json SOURCES ${DUMMY} PUBLIC_DEPS anno_cli_scenario
                 PRIVATE_DEPS nlohmann_json::nlohmann_json)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
