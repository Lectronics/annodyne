melo_add_library(cli/commands
    SOURCES commands/info.cpp
    PUBLIC_DEPS melo_core_types melo_core_io)

melo_add_executable(cli
    OUTPUT_NAME melo-cli
    SOURCES main.cpp
    PRIVATE_DEPS melo_cli_commands)
