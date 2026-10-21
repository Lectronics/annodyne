# Objective audio checks (AUDIO_VERIFICATION §1), label `audio`.
anno_add_test(NAME render_audio_checks SOURCES render_audio_checks.cpp LABELS audio
              DEPS anno::cli_checks anno::core_render anno::core_synthesis_passthrough)
anno_add_test(NAME pitch_engine_checks SOURCES pitch_engine_checks.cpp LABELS audio
              DEPS anno::cli_commands anno::cli_checks anno::core_io)
target_compile_definitions(pitch_engine_checks PRIVATE ANNO_REPO_ROOT="${PROJECT_SOURCE_DIR}")
