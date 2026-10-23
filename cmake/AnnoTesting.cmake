# Catch2 v3 and the anno_add_test() helper. Test labels: unit, contract, audio, golden, torture.

include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3)
FetchContent_MakeAvailable(Catch2)
include(Catch)

set(ANNO_TEST_LABELS unit contract audio golden torture)

# anno_add_test(NAME <name> SOURCES <src>... LABELS <label>... [DEPS <target>...] [TIMEOUT <seconds>])
# TIMEOUT overrides the test preset's default (300 s) for every test case of the executable.
function(anno_add_test)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;TIMEOUT" "SOURCES;LABELS;DEPS")
    foreach(label IN LISTS arg_LABELS)
        if(NOT label IN_LIST ANNO_TEST_LABELS)
            message(FATAL_ERROR "anno_add_test(${arg_NAME}): unknown label '${label}'")
        endif()
    endforeach()
    add_executable(${arg_NAME} ${arg_SOURCES})
    target_link_libraries(${arg_NAME} PRIVATE Catch2::Catch2WithMain anno_warnings ${arg_DEPS})
    target_include_directories(${arg_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    set(properties LABELS "${arg_LABELS}")
    if(arg_TIMEOUT)
        list(APPEND properties TIMEOUT "${arg_TIMEOUT}")
    endif()
    catch_discover_tests(${arg_NAME}
        TEST_PREFIX "${arg_NAME}."
        PROPERTIES ${properties})
endfunction()
