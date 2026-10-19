# melo_warnings: warning flags for every Melo target (coding-standards §1). Third-party code does
# not link this target.

add_library(melo_warnings INTERFACE)
set_target_properties(melo_warnings PROPERTIES MELO_HELPER TRUE)

if(MSVC)
    target_compile_options(melo_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(melo_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
endif()
