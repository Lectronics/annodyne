anno_add_test(NAME units_test SOURCES units_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME result_test SOURCES result_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME audio_buffer_test SOURCES audio_buffer_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME audio_source_test SOURCES audio_source_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME analysis_types_test SOURCES analysis_types_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME edit_types_test SOURCES edit_types_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME time_map_test SOURCES time_map_test.cpp LABELS unit DEPS anno::core_types)
anno_add_test(NAME edit_list_test SOURCES edit_list_test.cpp LABELS unit DEPS anno::core_edits)
anno_add_test(NAME effective_state_test SOURCES effective_state_test.cpp LABELS unit DEPS anno::core_edits)
anno_add_test(NAME dirty_regions_test SOURCES dirty_regions_test.cpp LABELS unit DEPS anno::core_edits)
anno_add_test(NAME remap_test SOURCES remap_test.cpp LABELS unit DEPS anno::core_edits)
anno_add_test(NAME wav_test SOURCES wav_test.cpp LABELS unit DEPS anno::core_io)
anno_add_test(NAME cli_info_test SOURCES cli_info_test.cpp LABELS unit DEPS anno::cli_commands)

# anno-cli as a process: exit codes 0 (ok), 1 (error), 2 (usage).
add_executable(write_test_wav write_test_wav.cpp)
target_link_libraries(write_test_wav PRIVATE anno::core_io anno_warnings)

set(cli_wav "${CMAKE_CURRENT_BINARY_DIR}/cli_test.wav")
add_test(NAME anno_cli.write_fixture COMMAND write_test_wav "${cli_wav}")
set_tests_properties(anno_cli.write_fixture PROPERTIES FIXTURES_SETUP cli_wav LABELS unit)

add_test(NAME anno_cli.info_ok COMMAND anno_cli info "${cli_wav}")
set_tests_properties(anno_cli.info_ok PROPERTIES
    FIXTURES_REQUIRED cli_wav LABELS unit
    PASS_REGULAR_EXPRESSION "channels: +2")

add_test(NAME anno_cli.info_missing_file_exits_1
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:anno_cli> -DARGS=info$<SEMICOLON>/no/such/file.wav -DEXPECT=1
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
add_test(NAME anno_cli.no_args_exits_2
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:anno_cli> -DEXPECT=2
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
set_tests_properties(anno_cli.info_missing_file_exits_1 anno_cli.no_args_exits_2 PROPERTIES LABELS unit)
