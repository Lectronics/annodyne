# Rubber Band Library (ADR-0026): the optional R3 "finer" time-stretch engine of core/synthesis/stretch,
# GPL-2.0-or-later, vendored unmodified in third_party/rubberband (see ANNODYNE.md there). Built from the
# upstream single-file compilation unit: built-in resampler, vDSP FFT on Apple (Accelerate), the built-in
# FFT elsewhere, no threads (the render must be deterministic). Only core/synthesis/stretch links it.

add_library(anno_rubberband STATIC third_party/rubberband/single/RubberBandSingle.cpp)
target_include_directories(anno_rubberband SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rubberband")
set_target_properties(anno_rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    target_link_libraries(anno_rubberband PUBLIC "-framework Accelerate")
endif()
# GCC's libstdc++ implements some std::atomic operations in libatomic (the library asks whether a
# 64-bit atomic is lock free). The library sits in the compiler's own directory, which the compiler
# driver searches but find_library does not, so link it by name.
if(NOT APPLE AND NOT MSVC)
    target_link_libraries(anno_rubberband PUBLIC atomic)
endif()
if(MSVC)
    # <windows.h> must not define min and max: the library writes std::numeric_limits<T>::max().
    target_compile_definitions(anno_rubberband PRIVATE NOMINMAX)
    target_compile_options(anno_rubberband PRIVATE /w)   # upstream code, not ours to warn about
else()
    target_compile_options(anno_rubberband PRIVATE -w -ffp-contract=off)
endif()
