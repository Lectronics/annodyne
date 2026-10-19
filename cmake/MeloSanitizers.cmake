# MELO_SANITIZER selects the sanitizer set for the whole build (set by CMakePresets.json).
# Applied globally so third-party code (Catch2) is instrumented consistently.

set(MELO_SANITIZER "none" CACHE STRING "Sanitizers: none | address_undefined | thread")
set_property(CACHE MELO_SANITIZER PROPERTY STRINGS none address_undefined thread)

if(MELO_SANITIZER STREQUAL "none")
    return()
endif()

if(MSVC)
    if(MELO_SANITIZER STREQUAL "address_undefined")
        # MSVC has ASan only. /RTC1 (CMake's Debug default) is incompatible with /fsanitize=address.
        foreach(flags_var CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
            string(REGEX REPLACE "/RTC[1csu]*" "" ${flags_var} "${${flags_var}}")
        endforeach()
        add_compile_options(/fsanitize=address /Zi)
    else()
        message(FATAL_ERROR "MELO_SANITIZER=${MELO_SANITIZER} is not supported with MSVC")
    endif()
    return()
endif()

if(MELO_SANITIZER STREQUAL "address_undefined")
    set(melo_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(MELO_SANITIZER STREQUAL "thread")
    set(melo_sanitizer_flags -fsanitize=thread)
else()
    message(FATAL_ERROR "Unknown MELO_SANITIZER '${MELO_SANITIZER}'")
endif()

add_compile_options(${melo_sanitizer_flags} -fno-omit-frame-pointer)
add_link_options(${melo_sanitizer_flags})
