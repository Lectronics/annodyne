cmake_minimum_required(VERSION 3.25)

project(annodyne
    VERSION 0.0.0
    DESCRIPTION "Note-based audio editor"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(AnnoOptions)
include(AnnoWarnings)
include(AnnoSanitizers)
include(AnnoLayers)
include(AnnoJson)

add_subdirectory(src)

option(ANNO_BUILD_TESTS "Build the Annodyne test suite" ON)
if(ANNO_BUILD_TESTS)
    enable_testing()
    include(AnnoTesting)
    add_subdirectory(tests)
endif()

# Must run last: checks every target created under src/ against the layer rules.
anno_check_layers(DIRECTORIES "${PROJECT_SOURCE_DIR}/src")
