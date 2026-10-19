anno_add_library(cli/commands
    SOURCES commands/info.cpp
    PUBLIC_DEPS anno_core_types anno_core_io)

anno_add_executable(cli
    OUTPUT_NAME anno-cli
    SOURCES main.cpp
    PRIVATE_DEPS anno_cli_commands)
