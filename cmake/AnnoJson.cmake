# nlohmann/json (ADR-0007): header-only, MIT. Only cli targets and tests may link it (AnnoLayers.cmake).

include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3.11)
FetchContent_MakeAvailable(nlohmann_json)
