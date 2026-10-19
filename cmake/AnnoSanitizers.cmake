# ANNO_SANITIZER selects the sanitizer set for the whole build (set by CMakePresets.json).
# Applied globally so third-party code (Catch2) is instrumented consistently.

set(ANNO_SANITIZER "none" CACHE STRING "Sanitizers: none | address_undefined | thread")
set_property(CACHE ANNO_SANITIZER PROPERTY STRINGS none address_undefined thread)

if(ANNO_SANITIZER STREQUAL "none")
    return()
endif()

if(MSVC)
    if(ANNO_SANITIZER STREQUAL "address_undefined")
        # MSVC has ASan only. /RTC1 (CMake's Debug default) is incompatible with /fsanitize=address.
        foreach(flags_var CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
            string(REGEX REPLACE "/RTC[1csu]*" "" ${flags_var} "${${flags_var}}")
        endforeach()
        add_compile_options(/fsanitize=address /Zi)
    else()
        message(FATAL_ERROR "ANNO_SANITIZER=${ANNO_SANITIZER} is not supported with MSVC")
    endif()
    return()
endif()

if(ANNO_SANITIZER STREQUAL "address_undefined")
    set(anno_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(ANNO_SANITIZER STREQUAL "thread")
    set(anno_sanitizer_flags -fsanitize=thread)
else()
    message(FATAL_ERROR "Unknown ANNO_SANITIZER '${ANNO_SANITIZER}'")
endif()

add_compile_options(${anno_sanitizer_flags} -fno-omit-frame-pointer)
add_link_options(${anno_sanitizer_flags})
