# Golden recordings (label `golden`): the committed Vocadito subset in tests/golden/vocals/vocadito
# (audio, f0 and note CSVs; see its ATTRIBUTION.md and tests/golden/vocals/README.md).
# analyse() runs the whole analysis (pitch, onsets, partials, noise classes, envelope) on five tracks:
# 508 s under TSan on a 4-core laptop, more than the preset's 300 s.
anno_add_test(NAME golden_vocals_test SOURCES golden_vocals_test.cpp LABELS golden TIMEOUT 1500
              DEPS anno::core_analysis_orchestrator anno::core_io)
target_compile_definitions(golden_vocals_test PRIVATE
    ANNO_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vocals/vocadito")
# std::getenv (ANNO_REQUIRE_GOLDEN) is flagged by MSVC's CRT deprecation warning; the test only reads it.
if(MSVC)
    target_compile_definitions(golden_vocals_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Tuning report over a full Vocadito corpus (local use; not a test and never run by ctest):
#   vocadito_report <corpus dir> [first-last] [--tune|--holdout] [-v]   (ANNO_TUNE="beta=0.3,lp=1")
add_executable(vocadito_report vocadito_report.cpp)
target_include_directories(vocadito_report PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(vocadito_report PRIVATE anno::core_analysis_orchestrator anno::core_io)
if(MSVC)
    target_compile_definitions(vocadito_report PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
