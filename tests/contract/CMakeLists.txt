melo_add_test(NAME units_contract_test SOURCES units_contract_test.cpp LABELS contract DEPS melo::core_types)
melo_add_test(NAME result_contract_test SOURCES result_contract_test.cpp LABELS contract DEPS melo::core_types)

# ---- Precondition aborts (MELO_EXPECTS) -------------------------------------------------------
add_executable(contract_violation_cases contract_violation_cases.cpp)
target_link_libraries(contract_violation_cases PRIVATE melo::core_types melo_warnings)

set(violation_cases
    to_samples_nan to_samples_zero_rate to_samples_too_large to_cents_zero_hz to_hz_nan
    to_decibels_inf result_value_on_error result_error_on_value time_range_negative_duration
    audio_buffer_bad_channel)
foreach(case IN LISTS violation_cases)
    add_test(NAME contract_violation.${case}
        COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:contract_violation_cases> -DCASE=${case}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_contract_violation.cmake")
    set_tests_properties(contract_violation.${case} PROPERTIES LABELS contract)
endforeach()
add_test(NAME contract_violation.helper_ok
    COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:contract_violation_cases> -DCASE=ok
            -DEXPECT_SUCCESS=ON -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_contract_violation.cmake")
set_tests_properties(contract_violation.helper_ok PROPERTIES LABELS contract)

# ---- Layer rules: configure fixture projects ----------------------------------------------------
# Negative fixtures pass only if configure reports the expected rule; positive controls must
# configure cleanly (design note §2).
function(melo_layer_fixture name expected)
    set(build_dir "${CMAKE_CURRENT_BINARY_DIR}/layer_rules/${name}")
    add_test(NAME layer_rules.${name}
        COMMAND ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/layer_rules/${name}" -B "${build_dir}"
                -G "${CMAKE_GENERATOR}" -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DMELO_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake)
    set_tests_properties(layer_rules.${name} PROPERTIES LABELS contract)
    if(NOT expected STREQUAL "PASS")
        set_tests_properties(layer_rules.${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "MELO LAYER VIOLATION: ${expected}:")
    endif()
endfunction()

melo_layer_fixture(allowed PASS)
melo_layer_fixture(allow_dna_to_analysis PASS)
melo_layer_fixture(allow_private_transitive PASS)
melo_layer_fixture(deny_untagged R0)
melo_layer_fixture(deny_upward R1)
melo_layer_fixture(deny_tool_to_render R2)
melo_layer_fixture(deny_tool_to_analysis R2)
melo_layer_fixture(deny_transitive_leak R2)
melo_layer_fixture(deny_types_link R3)
melo_layer_fixture(deny_tool_to_io R4)
melo_layer_fixture(deny_cycle R5)
melo_layer_fixture(deny_external R6)

# ---- Include-level rules ------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME layer_includes
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/scripts/check_includes.py"
            --layers "${PROJECT_BINARY_DIR}/melo_layers.json" --src "${PROJECT_SOURCE_DIR}/src")
set_tests_properties(layer_includes PROPERTIES LABELS contract)

# The checker must catch violations, not just pass on clean code: every listed pattern must appear.
add_test(NAME layer_includes.detects_violations
    COMMAND ${CMAKE_COMMAND}
            "-DCMD=$<TARGET_FILE:Python3::Interpreter>$<SEMICOLON>${PROJECT_SOURCE_DIR}/scripts/check_includes.py$<SEMICOLON>--layers$<SEMICOLON>${CMAKE_CURRENT_SOURCE_DIR}/include_rules/layers.json$<SEMICOLON>--src$<SEMICOLON>${CMAKE_CURRENT_SOURCE_DIR}/include_rules/src"
            "-DPATTERNS=bad_render.cpp.*R2.*core/render$<SEMICOLON>bad_upward.cpp.*R1.*project$<SEMICOLON>bad_io.cpp.*R4.*core/io$<SEMICOLON>bad_detail.cpp.*detail$<SEMICOLON>bad_unchecked.cpp.*unchecked_since_origin$<SEMICOLON>bad_serialized.cpp.*to_serialized_seconds"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_failure_output.cmake")
set_tests_properties(layer_includes.detects_violations PROPERTIES LABELS contract)
