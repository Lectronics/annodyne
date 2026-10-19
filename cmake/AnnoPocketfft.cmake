# pocketfft (ADR-0009): header-only FFT, BSD-3-Clause. Only core/dsp may link anno_pocketfft
# (AnnoLayers.cmake external allowlist), privately: no pocketfft header appears in a public header.

include(FetchContent)
FetchContent_Declare(pocketfft
    GIT_REPOSITORY https://github.com/mreineck/pocketfft.git
    GIT_TAG        4d865cba24359b05b37da4e75aee24498c3b78e7   # branch cpp
    GIT_SHALLOW    FALSE
    SOURCE_SUBDIR  anno-no-cmake-project)   # populate only: we define the target ourselves
FetchContent_MakeAvailable(pocketfft)

add_library(anno_pocketfft INTERFACE)
# SYSTEM: third-party headers must not trip -Werror -Wconversion -Wshadow.
target_include_directories(anno_pocketfft SYSTEM INTERFACE "${pocketfft_SOURCE_DIR}")
target_compile_definitions(anno_pocketfft INTERFACE POCKETFFT_NO_MULTITHREADING)
