# Objective audio checks (AUDIO_VERIFICATION §1), label `audio`.
anno_add_test(NAME render_audio_checks SOURCES render_audio_checks.cpp LABELS audio
              DEPS anno::cli_checks anno::core_render anno::core_synthesis_passthrough)
