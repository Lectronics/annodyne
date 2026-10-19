anno_add_library(cli/checks
    SOURCES checks/audio_checks.cpp
    PUBLIC_DEPS anno_core_types)

anno_add_library(cli/scenario
    SOURCES scenario/scenario.cpp
    PUBLIC_DEPS anno_core_types)

anno_add_library(cli/scenario_json
    SOURCES scenario_json/scenario_json.cpp
    PUBLIC_DEPS anno_cli_scenario
    PRIVATE_DEPS nlohmann_json::nlohmann_json)

anno_add_library(cli/commands
    SOURCES commands/info.cpp commands/render.cpp commands/audition.cpp
    PUBLIC_DEPS anno_core_types anno_core_io anno_core_edits anno_core_render
                anno_core_synthesis_passthrough anno_cli_checks anno_cli_scenario
                anno_cli_scenario_json)

anno_add_executable(cli
    OUTPUT_NAME anno-cli
    SOURCES main.cpp
    PRIVATE_DEPS anno_cli_commands)
