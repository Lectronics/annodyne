# anno_warnings: warning flags for every Annodyne target (coding-standards §1). Third-party code does
# not link this target.

add_library(anno_warnings INTERFACE)
set_target_properties(anno_warnings PROPERTIES ANNO_HELPER TRUE)

if(MSVC)
    target_compile_options(anno_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(anno_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
endif()
