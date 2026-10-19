# cmake -DCMD=<exe> [-DARGS=a;b] -DEXPECT=<code> -P expect_exit_code.cmake
# Passes if CMD exits with exactly EXPECT.

execute_process(COMMAND "${CMD}" ${ARGS} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "${EXPECT}")
    message(FATAL_ERROR "expected exit code ${EXPECT}, got '${result}'\n${out}${err}")
endif()
