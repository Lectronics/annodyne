anno_add_library(cli/checks
    SOURCES checks/audio_checks.cpp checks/pitch_checks.cpp
    PUBLIC_DEPS anno_core_types
    PRIVATE_DEPS anno_core_dsp)

anno_add_library(cli/scenario
    SOURCES scenario/scenario.cpp
    PUBLIC_DEPS anno_core_types)

anno_add_library(cli/scenario_json
    SOURCES scenario_json/scenario_json.cpp
    PUBLIC_DEPS anno_cli_scenario
    PRIVATE_DEPS nlohmann_json::nlohmann_json)

anno_add_library(cli/analysis_json
    SOURCES analysis_json/analysis_json.cpp
    PUBLIC_DEPS anno_core_types
    PRIVATE_DEPS nlohmann_json::nlohmann_json)

anno_add_library(cli/plot
    SOURCES plot/plot.cpp
    PUBLIC_DEPS anno_core_types)

anno_add_library(cli/commands
    SOURCES commands/info.cpp commands/render.cpp commands/audition.cpp commands/analyse.cpp
    PUBLIC_DEPS anno_core_types anno_core_io anno_core_edits anno_core_render
                anno_core_synthesis_passthrough anno_core_synthesis_harmonic_noise
                anno_core_synthesis_psola anno_core_synthesis_rubberband_pitch anno_macros_correct_pitch anno_macros_leveling anno_tools_formant anno_core_tuning anno_tools_main_tool anno_cli_checks anno_cli_scenario
                anno_cli_scenario_json anno_core_analysis_orchestrator anno_cli_analysis_json
                anno_cli_plot anno_core_analysis_gaps anno_core_analysis_activity anno_core_dsp)

anno_add_executable(cli
    OUTPUT_NAME anno-cli
    SOURCES main.cpp
    PRIVATE_DEPS anno_cli_commands)
