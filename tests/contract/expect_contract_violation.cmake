# cmake -P wrapper: runs HELPER with CASE and passes only if the helper failed (non-zero exit or a
# signal such as SIGABRT) AND printed "contract violation". With EXPECT_SUCCESS=ON it instead
# requires exit code 0 (positive control).
#   cmake -DHELPER=<exe> -DCASE=<name> [-DEXPECT_SUCCESS=ON] -P expect_contract_violation.cmake

execute_process(
    COMMAND "${HELPER}" "${CASE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

if(EXPECT_SUCCESS)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "case '${CASE}': expected exit 0, got '${result}'\n${out}${err}")
    endif()
    return()
endif()

if(result EQUAL 0)
    message(FATAL_ERROR "case '${CASE}': expected a contract violation, but the helper exited 0")
endif()
if(NOT "${out}${err}" MATCHES "contract violation")
    message(FATAL_ERROR "case '${CASE}': helper failed ('${result}') without reporting a contract "
                        "violation:\n${out}${err}")
endif()
message(STATUS "case '${CASE}': contract violation reported as expected ('${result}')")
