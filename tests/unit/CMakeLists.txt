melo_add_test(NAME units_test SOURCES units_test.cpp LABELS unit DEPS melo::core_types)
melo_add_test(NAME result_test SOURCES result_test.cpp LABELS unit DEPS melo::core_types)
melo_add_test(NAME audio_buffer_test SOURCES audio_buffer_test.cpp LABELS unit DEPS melo::core_types)
melo_add_test(NAME audio_source_test SOURCES audio_source_test.cpp LABELS unit DEPS melo::core_types)
melo_add_test(NAME wav_test SOURCES wav_test.cpp LABELS unit DEPS melo::core_io)
melo_add_test(NAME cli_info_test SOURCES cli_info_test.cpp LABELS unit DEPS melo::cli_commands)

# melo-cli as a process: exit codes 0 (ok), 1 (error), 2 (usage).
add_executable(write_test_wav write_test_wav.cpp)
target_link_libraries(write_test_wav PRIVATE melo::core_io melo_warnings)

set(cli_wav "${CMAKE_CURRENT_BINARY_DIR}/cli_test.wav")
add_test(NAME melo_cli.write_fixture COMMAND write_test_wav "${cli_wav}")
set_tests_properties(melo_cli.write_fixture PROPERTIES FIXTURES_SETUP cli_wav LABELS unit)

add_test(NAME melo_cli.info_ok COMMAND melo_cli info "${cli_wav}")
set_tests_properties(melo_cli.info_ok PROPERTIES
    FIXTURES_REQUIRED cli_wav LABELS unit
    PASS_REGULAR_EXPRESSION "channels: +2")

add_test(NAME melo_cli.info_missing_file_exits_1
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:melo_cli> -DARGS=info$<SEMICOLON>/no/such/file.wav -DEXPECT=1
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
add_test(NAME melo_cli.no_args_exits_2
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:melo_cli> -DEXPECT=2
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
set_tests_properties(melo_cli.info_missing_file_exits_1 melo_cli.no_args_exits_2 PROPERTIES LABELS unit)
