cmake_minimum_required(VERSION 3.25)

project(melo
    VERSION 0.0.0
    DESCRIPTION "Note-based audio editor"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(MeloOptions)
include(MeloWarnings)
include(MeloSanitizers)
include(MeloLayers)

add_subdirectory(src)

option(MELO_BUILD_TESTS "Build the Melo test suite" ON)
if(MELO_BUILD_TESTS)
    enable_testing()
    include(MeloTesting)
    add_subdirectory(tests)
endif()

# Must run last: checks every target created under src/ against the layer rules.
melo_check_layers(DIRECTORIES "${PROJECT_SOURCE_DIR}/src")
