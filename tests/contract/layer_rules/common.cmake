# Shared setup for layer-rule fixtures: each fixture is a tiny project that declares a dummy target
# graph and runs melo_check_layers() on it. Configure only; nothing is compiled.
if(NOT MELO_CMAKE_DIR)
    message(FATAL_ERROR "pass -DMELO_CMAKE_DIR=<repo>/cmake")
endif()
include("${MELO_CMAKE_DIR}/MeloLayers.cmake")
set(MELO_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(DUMMY "${CMAKE_CURRENT_LIST_DIR}/dummy.cpp")
