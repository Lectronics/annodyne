cmake_minimum_required(VERSION 3.25)
project(anno_layer_fixture_allow_render_threads LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

# Positive control (ADR-0006): core/render may link Threads::Threads (its cache holds a mutex).
add_library(Threads::Threads INTERFACE IMPORTED)
anno_add_library(core/types SOURCES ${DUMMY})
anno_add_library(core/render SOURCES ${DUMMY} PUBLIC_DEPS anno_core_types
                 PRIVATE_DEPS Threads::Threads)

anno_check_layers(DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
