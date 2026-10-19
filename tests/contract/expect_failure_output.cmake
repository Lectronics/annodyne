# cmake -DCMD=<exe;args...> -DPATTERNS=<regex;...> -P expect_failure_output.cmake
# Passes if CMD exits non-zero and its output matches every regex in PATTERNS.

execute_process(COMMAND ${CMD} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(output "${out}${err}")
if(result EQUAL 0)
    message(FATAL_ERROR "expected a failure, but the command exited 0:\n${output}")
endif()
foreach(pattern IN LISTS PATTERNS)
    if(NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "output does not match '${pattern}':\n${output}")
    endif()
endforeach()
