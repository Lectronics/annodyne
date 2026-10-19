# Golden recordings (label `golden`): real vocals with hand-checked note lists and pitch spot checks.
# tests/golden/vocals/ holds <name>.wav, <name>.notes.json and <name>.pitch.json (see README.md).
anno_add_test(NAME golden_vocals_test SOURCES golden_vocals_test.cpp LABELS golden
              DEPS anno::core_analysis_orchestrator anno::core_io nlohmann_json::nlohmann_json)
target_compile_definitions(golden_vocals_test PRIVATE
    ANNO_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vocals")
# std::getenv (ANNO_REQUIRE_GOLDEN) is flagged by MSVC's CRT deprecation warning; the test only reads it.
if(MSVC)
    target_compile_definitions(golden_vocals_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
