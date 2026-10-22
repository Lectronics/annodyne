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
anno_add_test(NAME build_time_map_test SOURCES build_time_map_test.cpp LABELS unit DEPS anno::core_timemap)
anno_add_test(NAME crossfade_test SOURCES crossfade_test.cpp LABELS unit DEPS anno::core_dsp)
anno_add_test(NAME blend_plan_test SOURCES blend_plan_test.cpp LABELS unit DEPS anno::core_render)
anno_add_test(NAME render_cache_test SOURCES render_cache_test.cpp LABELS unit DEPS anno::cli_checks anno::core_render)
anno_add_test(NAME render_test SOURCES render_test.cpp LABELS unit DEPS anno::cli_checks anno::core_render anno::core_synthesis_passthrough)
anno_add_test(NAME render_stretch_test SOURCES render_stretch_test.cpp LABELS unit DEPS anno::cli_checks anno::core_render anno::core_synthesis_passthrough)
anno_add_test(NAME source_through_map_test SOURCES source_through_map_test.cpp LABELS unit DEPS anno::cli_checks anno::core_synthesis_engine)
anno_add_test(NAME render_spans_test SOURCES render_spans_test.cpp LABELS unit DEPS anno::cli_checks anno::core_render anno::core_synthesis_passthrough)
anno_add_test(NAME passthrough_engine_test SOURCES passthrough_engine_test.cpp LABELS unit DEPS anno::cli_checks anno::core_synthesis_passthrough)
anno_add_test(NAME audio_checks_test SOURCES audio_checks_test.cpp LABELS unit DEPS anno::cli_checks anno::core_types)
anno_add_test(NAME scenario_test SOURCES scenario_test.cpp LABELS unit DEPS anno::cli_scenario_json anno::cli_checks)
anno_add_test(NAME wav_test SOURCES wav_test.cpp LABELS unit DEPS anno::core_io)
anno_add_test(NAME cli_info_test SOURCES cli_info_test.cpp LABELS unit DEPS anno::cli_commands)
anno_add_test(NAME cli_analyse_test SOURCES cli_analyse_test.cpp LABELS unit DEPS anno::cli_commands anno::cli_analysis_json anno::cli_plot anno::core_io nlohmann_json::nlohmann_json)

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

# anno-cli analyse on the fixture WAV, a missing file (1) and a missing argument (2).
add_test(NAME anno_cli.analyse_ok COMMAND anno_cli analyse "${cli_wav}")
set_tests_properties(anno_cli.analyse_ok PROPERTIES
    FIXTURES_REQUIRED cli_wav LABELS unit
    PASS_REGULAR_EXPRESSION "analysed cli_test.wav: [0-9]+ notes")
add_test(NAME anno_cli.analyse_missing_file_exits_1
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:anno_cli> -DARGS=analyse$<SEMICOLON>/no/such/file.wav -DEXPECT=1
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
add_test(NAME anno_cli.analyse_without_file_exits_2
    COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:anno_cli> -DARGS=analyse -DEXPECT=2
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
set_tests_properties(anno_cli.analyse_missing_file_exits_1 anno_cli.analyse_without_file_exits_2 PROPERTIES LABELS unit)

# anno-cli render / audition on the fixture WAV (0.1 s, 48 kHz stereo). Scenarios are written at
# configure time so the source path is absolute.
set(scenario_dir "${CMAKE_CURRENT_BINARY_DIR}/scenarios")
file(MAKE_DIRECTORY "${scenario_dir}")
file(WRITE "${scenario_dir}/pitch_edit.json" "{
  \"name\": \"pitch_edit\",
  \"source\": \"${cli_wav}\",
  \"notes\": [{\"id\": \"n1\", \"start_s\": 0.02, \"end_s\": 0.05}],
  \"edits\": [{\"category\": \"pitch\", \"op\": \"add\", \"param\": \"center_offset\", \"value\": 200,
               \"target\": {\"note_id\": \"n1\"}}]
}")
file(WRITE "${scenario_dir}/timing_edit.json" "{
  \"name\": \"timing_edit\",
  \"source\": \"${cli_wav}\",
  \"notes\": [{\"id\": \"n1\", \"start_s\": 0.02, \"end_s\": 0.05}],
  \"edits\": [{\"category\": \"time\", \"op\": \"add\", \"param\": \"onset_shift\", \"value\": 0.01,
               \"target\": {\"note_id\": \"n1\"}}]
}")
set(render_out "${CMAKE_CURRENT_BINARY_DIR}/cli_render_out.wav")
set(audition_out "${CMAKE_CURRENT_BINARY_DIR}/cli_audition")

add_test(NAME anno_cli.render_ok
    COMMAND anno_cli render "${scenario_dir}/pitch_edit.json" --out "${render_out}")
set_tests_properties(anno_cli.render_ok PROPERTIES
    FIXTURES_REQUIRED cli_wav FIXTURES_SETUP cli_render LABELS unit)
add_test(NAME anno_cli.render_output_is_readable COMMAND anno_cli info "${render_out}")
set_tests_properties(anno_cli.render_output_is_readable PROPERTIES
    FIXTURES_REQUIRED cli_render LABELS unit PASS_REGULAR_EXPRESSION "frames: +4800")

add_test(NAME anno_cli.audition_ok
    COMMAND anno_cli audition "${scenario_dir}/pitch_edit.json" --out "${audition_out}")
set_tests_properties(anno_cli.audition_ok PROPERTIES
    FIXTURES_REQUIRED cli_wav FIXTURES_SETUP cli_audition LABELS unit
    PASS_REGULAR_EXPRESSION "Overall: PASS")
foreach(file report.md INDEX.md edits.json)
    add_test(NAME anno_cli.audition_writes_${file}
        COMMAND ${CMAKE_COMMAND} -E cat "${audition_out}/${file}")
    set_tests_properties(anno_cli.audition_writes_${file} PROPERTIES
        FIXTURES_REQUIRED cli_audition LABELS unit)
endforeach()
foreach(file original.wav edited.wav original_matched.wav difference.wav)
    add_test(NAME anno_cli.audition_writes_${file} COMMAND anno_cli info "${audition_out}/${file}")
    set_tests_properties(anno_cli.audition_writes_${file} PROPERTIES
        FIXTURES_REQUIRED cli_audition LABELS unit PASS_REGULAR_EXPRESSION "frames: +4800")
endforeach()

# Exit codes: 2 usage, 1 error (timing edits are not rendered until Phase 6, unknown engine).
function(anno_cli_exit_test name expect)
    add_test(NAME anno_cli.${name}
        COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:anno_cli> "-DARGS=${ARGN}" -DEXPECT=${expect}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_exit_code.cmake")
    set_tests_properties(anno_cli.${name} PROPERTIES FIXTURES_REQUIRED cli_wav LABELS unit)
endfunction()
anno_cli_exit_test(render_without_out_exits_2 2 render$<SEMICOLON>x.json)
anno_cli_exit_test(render_missing_scenario_exits_1 1
    render$<SEMICOLON>/no/such.json$<SEMICOLON>--out$<SEMICOLON>/tmp/x.wav)
anno_cli_exit_test(render_unknown_engine_exits_1 1
    render$<SEMICOLON>${scenario_dir}/pitch_edit.json$<SEMICOLON>--out$<SEMICOLON>${render_out}$<SEMICOLON>--engine$<SEMICOLON>magic)
anno_cli_exit_test(render_timing_edit_exits_1 1
    render$<SEMICOLON>${scenario_dir}/timing_edit.json$<SEMICOLON>--out$<SEMICOLON>${render_out})
anno_add_test(NAME dsp_window_fft_stft_test SOURCES dsp_window_fft_stft_test.cpp LABELS unit DEPS anno::core_dsp)
anno_add_test(NAME dsp_filter_test SOURCES dsp_filter_test.cpp LABELS unit DEPS anno::core_dsp)
anno_add_test(NAME dsp_resample_kweight_interp_test SOURCES dsp_resample_kweight_interp_test.cpp LABELS unit DEPS anno::core_dsp)
anno_add_test(NAME pitch_test SOURCES pitch_test.cpp LABELS unit DEPS anno::core_analysis_pitch)
anno_add_test(NAME onsets_test SOURCES onsets_test.cpp LABELS unit DEPS anno::core_analysis_onsets anno::core_analysis_pitch)
anno_add_test(NAME onset_stages_test SOURCES onset_stages_test.cpp LABELS unit DEPS anno::core_analysis_onsets anno::core_analysis_orchestrator anno::core_dsp)
# The bit-exact gate runs 40 detections on 12 s of audio: over 300 s under TSan on macos-15.
anno_add_test(NAME onset_bench_test SOURCES onset_bench_test.cpp LABELS unit TIMEOUT 1500 DEPS anno::core_analysis_onsets anno::core_dsp anno::core_io)
target_compile_definitions(onset_bench_test PRIVATE ANNO_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden/vocals/vocadito")
if(MSVC)
    target_compile_definitions(onset_bench_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
anno_add_test(NAME segmentation_test SOURCES segmentation_test.cpp LABELS unit DEPS anno::core_analysis_segmentation anno::core_analysis_perceived_pitch anno::core_analysis_onsets anno::core_analysis_pitch)
anno_add_test(NAME perceived_pitch_test SOURCES perceived_pitch_test.cpp LABELS unit DEPS anno::core_analysis_perceived_pitch)
anno_add_test(NAME pitch_target_test SOURCES pitch_target_test.cpp LABELS unit DEPS anno::core_analysis_pitch_target)
anno_add_test(NAME take_annotation_test SOURCES take_annotation_test.cpp LABELS unit DEPS anno::core_analysis_pitch_target)
anno_add_test(NAME activity_test SOURCES activity_test.cpp LABELS unit DEPS anno::core_analysis_activity)
anno_add_test(NAME loudness_test SOURCES loudness_test.cpp LABELS unit DEPS anno::core_analysis_loudness anno::core_dsp)
anno_add_test(NAME partials_test SOURCES partials_test.cpp LABELS unit DEPS anno::core_analysis_partials anno::core_dsp)
anno_add_test(NAME harmonic_noise_test SOURCES harmonic_noise_test.cpp LABELS unit DEPS anno::core_analysis_partials anno::core_analysis_harmonic_noise anno::core_dsp)
anno_add_test(NAME envelope_test SOURCES envelope_test.cpp LABELS unit DEPS anno::core_analysis_partials anno::core_analysis_envelope anno::core_dsp)
anno_add_test(NAME harmonic_noise_engine_test SOURCES harmonic_noise_engine_test.cpp LABELS unit DEPS anno::core_synthesis_harmonic_noise anno::core_analysis_orchestrator anno::core_analysis_partials anno::core_analysis_envelope anno::core_dsp)
anno_add_test(NAME stretch_span_test SOURCES stretch_span_test.cpp LABELS unit DEPS anno::core_synthesis_stretch anno::core_analysis_orchestrator anno::core_analysis_partials anno::core_analysis_envelope anno::core_dsp)
anno_add_test(NAME psola_engine_test SOURCES psola_engine_test.cpp LABELS unit DEPS anno::core_synthesis_psola anno::core_analysis_orchestrator anno::core_analysis_partials anno::core_analysis_envelope anno::core_dsp)
anno_add_test(NAME chromatic_grid_test SOURCES chromatic_grid_test.cpp LABELS unit DEPS anno::core_tuning)
anno_add_test(NAME main_tool_test SOURCES main_tool_test.cpp LABELS unit DEPS anno::tools_main_tool anno::core_edits)
anno_add_test(NAME time_tools_test SOURCES time_tools_test.cpp LABELS unit DEPS anno::tools_time anno::tools_attack_speed anno::core_edits anno::core_timemap)
anno_add_test(NAME engine_support_test SOURCES engine_support_test.cpp LABELS unit DEPS anno::core_synthesis_engine)
anno_add_test(NAME cli_engines_test SOURCES cli_engines_test.cpp LABELS unit DEPS anno::cli_commands anno::core_io nlohmann_json::nlohmann_json)
anno_add_test(NAME pitch_checks_test SOURCES pitch_checks_test.cpp LABELS unit DEPS anno::cli_checks anno::core_analysis_orchestrator)
anno_add_test(NAME voicing_probes_test SOURCES voicing_probes_test.cpp LABELS unit DEPS anno::core_analysis_orchestrator anno::core_dsp)
anno_add_test(NAME c4dm_pyin_test SOURCES c4dm_pyin_test.cpp LABELS unit DEPS anno::core_analysis_pitch anno_c4dm_pyin)
anno_add_test(NAME analyse_test SOURCES analyse_test.cpp LABELS unit DEPS anno::core_analysis_orchestrator)
anno_add_test(NAME analysis_metrics_test SOURCES analysis_metrics_test.cpp LABELS unit DEPS anno::core_types)
target_compile_definitions(cli_engines_test PRIVATE ANNO_REPO_ROOT="${PROJECT_SOURCE_DIR}")
