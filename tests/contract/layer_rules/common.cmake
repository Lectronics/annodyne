# Shared setup for layer-rule fixtures: each fixture is a tiny project that declares a dummy target
# graph and runs anno_check_layers() on it. Configure only; nothing is compiled.
if(NOT ANNO_CMAKE_DIR)
    message(FATAL_ERROR "pass -DANNO_CMAKE_DIR=<repo>/cmake")
endif()
include("${ANNO_CMAKE_DIR}/AnnoLayers.cmake")
set(ANNO_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(DUMMY "${CMAKE_CURRENT_LIST_DIR}/dummy.cpp")
