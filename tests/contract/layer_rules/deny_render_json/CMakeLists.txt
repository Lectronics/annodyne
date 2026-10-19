cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_deny_render_json LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# R6 (ADR-0007): core/* never links nlohmann/json.
add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/render SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types
                 PRIVATE_DEPS nlohmann_json::nlohmann_json)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
