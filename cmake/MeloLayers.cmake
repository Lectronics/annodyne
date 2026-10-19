# Layer table and dependency rules (.claude/rules/architecture.md, ADR-0001).
#
# melo_add_library(<folder> ...) and melo_add_executable(<folder> ...) create the target for a
# src/ folder and tag it with its layer. melo_check_layers() must run after every target exists; it
# reports all violations at once, each prefixed "MELO LAYER VIOLATION: R<n>":
#   R0  a library/executable under src/ was not created by melo_add_library/melo_add_executable
#   R1  a target links a target in a higher layer
#   R2  a layer-5 target (tools, macros, features) links outside its allowlist, directly or through
#       the usage requirements (PUBLIC/INTERFACE links) of something it links
#   R3  core/types links another melo target
#   R4  core/io is linked by something below layer 6
#   R5  link cycle between melo targets
#   R6  a non-melo dependency that is not on the target's external allowlist, or an unparsable
#       generator expression
# Design: docs/design/phase0-foundations.md §2.

include_guard(GLOBAL)

set(MELO_LAYER_VIOLATION_PREFIX "MELO LAYER VIOLATION")

# Layer-5 targets may link these folders (plus other layer-5 targets).
set(MELO_LAYER5_ALLOWED_FOLDERS core/types core/edits core/tuning)
# These layer-5 folders may also link core/analysis/* (architecture.md, re-analysis requests).
set(MELO_ANALYSIS_EXCEPTION_FOLDERS features/dna features/sound_editor)
# core/io may only be linked from this layer upward (ADR-0001).
set(MELO_IO_FOLDER core/io)
set(MELO_IO_MIN_LAYER 6)

# ---- Layer table -------------------------------------------------------------------------------

# Sets <out_var> to the layer (0-7) of a src/ folder, or to "" if the folder is not in the table.
function(melo_folder_layer folder out_var)
    set(layer "")
    if(folder MATCHES "^core/(types|dsp|io)$")
        set(layer 0)
    elseif(folder MATCHES "^core/analysis/[a-z0-9_]+$" OR folder MATCHES "^core/(tuning|models)$")
        set(layer 1)
    elseif(folder STREQUAL "core/edits" OR folder MATCHES "^core/synthesis/[a-z0-9_]+$")
        set(layer 2)
    elseif(folder STREQUAL "core/timemap")
        set(layer 3)
    elseif(folder STREQUAL "core/render")
        set(layer 4)
    elseif(folder MATCHES "^(tools|macros|features)/[a-z0-9_]+$")
        set(layer 5)
    elseif(folder STREQUAL "project")
        set(layer 6)
    elseif(folder MATCHES "^(host/[a-z0-9_]+|ui|cli|cli/[a-z0-9_]+)$")
        set(layer 7)
    endif()
    set(${out_var} "${layer}" PARENT_SCOPE)
endfunction()

# Sets <out_var> to TRUE if <folder> may link the non-melo dependency <dep>. Later phases extend
# this through their design note (e.g. the FFT library for core/dsp, Threads for core/render).
function(melo_external_allowed folder dep out_var)
    set(allowed FALSE)
    if(folder MATCHES "^(host/[a-z0-9_]+|ui)$" AND dep MATCHES "^(juce_|juce::|ARA)")
        set(allowed TRUE)
    endif()
    set(${out_var} ${allowed} PARENT_SCOPE)
endfunction()

# ---- Target creation ---------------------------------------------------------------------------

function(_melo_target_name folder out_var)
    string(REPLACE "/" "_" suffix "${folder}")
    set(${out_var} "melo_${suffix}" PARENT_SCOPE)
endfunction()

function(_melo_tag_target target folder)
    melo_folder_layer("${folder}" layer)
    if(layer STREQUAL "")
        message(FATAL_ERROR "${MELO_LAYER_VIOLATION_PREFIX}: R0: folder '${folder}' is not in the "
                            "layer table (cmake/MeloLayers.cmake). Add it through a design note.")
    endif()
    set_target_properties(${target} PROPERTIES MELO_LAYER ${layer} MELO_FOLDER "${folder}")
    if(TARGET melo_warnings)
        target_link_libraries(${target} PRIVATE melo_warnings)
    endif()
endfunction()

# melo_add_library(<folder> SOURCES <src>... [PUBLIC_DEPS <t>...] [PRIVATE_DEPS <t>...])
# Creates static library melo_<folder with / -> _> and alias melo::<same suffix>. Headers are
# included as "<folder>/<header>.h" relative to MELO_SOURCE_ROOT (default: <project>/src).
function(melo_add_library folder)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS")
    _melo_target_name("${folder}" target)
    add_library(${target} STATIC ${arg_SOURCES})
    string(REPLACE "/" "_" suffix "${folder}")
    add_library(melo::${suffix} ALIAS ${target})
    if(NOT DEFINED MELO_SOURCE_ROOT)
        set(MELO_SOURCE_ROOT "${PROJECT_SOURCE_DIR}/src")
    endif()
    target_include_directories(${target} PUBLIC "${MELO_SOURCE_ROOT}")
    target_compile_features(${target} PUBLIC cxx_std_20)
    _melo_tag_target(${target} "${folder}")
    if(arg_PUBLIC_DEPS)
        target_link_libraries(${target} PUBLIC ${arg_PUBLIC_DEPS})
    endif()
    if(arg_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_DEPS})
    endif()
endfunction()

# melo_add_executable(<folder> OUTPUT_NAME <name> SOURCES <src>... [PRIVATE_DEPS <t>...])
function(melo_add_executable folder)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_NAME" "SOURCES;PRIVATE_DEPS")
    _melo_target_name("${folder}" target)
    add_executable(${target} ${arg_SOURCES})
    if(arg_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${arg_OUTPUT_NAME}")
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
    _melo_tag_target(${target} "${folder}")
    if(arg_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_DEPS})
    endif()
endfunction()

# ---- Checking ----------------------------------------------------------------------------------

function(_melo_violation rule text)
    set_property(GLOBAL APPEND PROPERTY MELO_LAYER_VIOLATIONS
                 "${MELO_LAYER_VIOLATION_PREFIX}: ${rule}: ${text}")
endfunction()

function(_melo_collect_targets dir out_var)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sub IN LISTS subdirs)
        _melo_collect_targets("${sub}" sub_targets)
        list(APPEND targets ${sub_targets})
    endforeach()
    set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

# Normalises one link item. Sets <name_var> (empty = ignore), <usage_var> (FALSE for
# $<LINK_ONLY:...>, i.e. a PRIVATE dependency of a static library) and <bad_var> (TRUE if the item
# is an unparsable generator expression).
function(_melo_normalise_item item name_var usage_var bad_var)
    set(usage TRUE)
    set(bad FALSE)
    set(name "${item}")
    if(name MATCHES "^::@")   # directory markers CMake inserts for cross-directory links
        set(name "")
    endif()
    if(name MATCHES "^\\$<LINK_ONLY:(.+)>$")
        set(name "${CMAKE_MATCH_1}")
        set(usage FALSE)
    endif()
    if(name MATCHES "^\\$<\\$<[^>]*>:(.+)>$")   # $<$<CONFIG:...>:x> and similar conditions
        set(name "${CMAKE_MATCH_1}")
    endif()
    if(name MATCHES "\\$<")
        set(bad TRUE)
    elseif(NOT name STREQUAL "" AND TARGET "${name}")
        get_target_property(aliased "${name}" ALIASED_TARGET)
        if(aliased)
            set(name "${aliased}")
        endif()
        get_target_property(helper "${name}" MELO_HELPER)
        if(helper)
            set(name "")
        endif()
    endif()
    set(${name_var} "${name}" PARENT_SCOPE)
    set(${usage_var} ${usage} PARENT_SCOPE)
    set(${bad_var} ${bad} PARENT_SCOPE)
endfunction()

function(_melo_is_melo_target name out_var)
    set(result FALSE)
    if(TARGET "${name}")
        get_target_property(layer "${name}" MELO_LAYER)
        if(NOT layer STREQUAL "layer-NOTFOUND")
            set(result TRUE)
        endif()
    endif()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# Sets <out_var> to the normalised dependencies of <target> from <property>. With
# USAGE_ONLY, $<LINK_ONLY:...> entries are dropped. Reports R6 for unparsable entries.
function(_melo_deps target property out_var)
    cmake_parse_arguments(PARSE_ARGV 3 arg "USAGE_ONLY" "" "")
    get_target_property(items ${target} ${property})
    set(deps "")
    if(items)
        foreach(item IN LISTS items)
            _melo_normalise_item("${item}" name usage bad)
            if(bad)
                _melo_violation(R6 "${target} links unparsable generator expression '${item}'")
            elseif(NOT name STREQUAL "" AND (usage OR NOT arg_USAGE_ONLY))
                list(APPEND deps "${name}")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES deps)
    set(${out_var} ${deps} PARENT_SCOPE)
endfunction()

function(_melo_layer5_allows target_folder dep_folder dep_layer out_var)
    set(allowed FALSE)
    if(dep_layer EQUAL 5)
        set(allowed TRUE)
    elseif(dep_folder IN_LIST MELO_LAYER5_ALLOWED_FOLDERS)
        set(allowed TRUE)
    elseif(dep_folder MATCHES "^core/analysis/" AND target_folder IN_LIST MELO_ANALYSIS_EXCEPTION_FOLDERS)
        set(allowed TRUE)
    endif()
    set(${out_var} ${allowed} PARENT_SCOPE)
endfunction()

# R2t: walks usage edges from <start> and reports anything a layer-5 <target> may not use.
function(_melo_check_transitive target target_folder start)
    set(queue ${start})
    set(seen ${start})
    while(queue)
        list(POP_FRONT queue node)
        _melo_is_melo_target("${node}" node_is_melo)
        if(NOT node_is_melo)
            continue()
        endif()
        _melo_deps(${node} INTERFACE_LINK_LIBRARIES usage_deps USAGE_ONLY)
        foreach(dep IN LISTS usage_deps)
            if(dep IN_LIST seen)
                continue()
            endif()
            list(APPEND seen "${dep}")
            list(APPEND queue "${dep}")
            _melo_is_melo_target("${dep}" dep_is_melo)
            if(dep_is_melo)
                get_target_property(dep_folder ${dep} MELO_FOLDER)
                get_target_property(dep_layer ${dep} MELO_LAYER)
                _melo_layer5_allows("${target_folder}" "${dep_folder}" ${dep_layer} ok)
            else()
                set(ok FALSE)
            endif()
            if(NOT ok)
                _melo_violation(R2 "${target} (${target_folder}, layer 5) reaches ${dep} through the "
                                   "usage requirements of ${start}; link it PRIVATE there")
            endif()
        endforeach()
    endwhile()
endfunction()

function(_melo_dfs node)
    set_property(GLOBAL PROPERTY _MELO_DFS_${node} gray)
    _melo_deps(${node} LINK_LIBRARIES deps)
    foreach(dep IN LISTS deps)
        _melo_is_melo_target("${dep}" dep_is_melo)
        if(NOT dep_is_melo)
            continue()
        endif()
        get_property(state GLOBAL PROPERTY _MELO_DFS_${dep})
        if(state STREQUAL "gray")
            _melo_violation(R5 "link cycle through ${node} -> ${dep}")
        elseif(NOT state)
            _melo_dfs(${dep})
        endif()
    endforeach()
    set_property(GLOBAL PROPERTY _MELO_DFS_${node} black)
endfunction()

function(_melo_write_layers_json targets)
    set(entries "")
    foreach(target IN LISTS targets)
        get_target_property(folder ${target} MELO_FOLDER)
        get_target_property(layer ${target} MELO_LAYER)
        list(APPEND entries "    \"${folder}\": {\"target\": \"${target}\", \"layer\": ${layer}}")
    endforeach()
    list(JOIN entries ",\n" folders_json)
    list(TRANSFORM MELO_LAYER5_ALLOWED_FOLDERS REPLACE "(.+)" "\"\\1\"" OUTPUT_VARIABLE l5)
    list(JOIN l5 ", " l5_json)
    list(TRANSFORM MELO_ANALYSIS_EXCEPTION_FOLDERS REPLACE "(.+)" "\"\\1\"" OUTPUT_VARIABLE ex)
    list(JOIN ex ", " ex_json)
    file(WRITE "${CMAKE_BINARY_DIR}/melo_layers.json"
"{
  \"folders\": {
${folders_json}
  },
  \"layer5_allowed_folders\": [${l5_json}],
  \"analysis_exception_folders\": [${ex_json}],
  \"io_folder\": \"${MELO_IO_FOLDER}\",
  \"io_min_layer\": ${MELO_IO_MIN_LAYER}
}
")
endfunction()

# melo_check_layers(DIRECTORIES <dir>...)
function(melo_check_layers)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "DIRECTORIES")
    set_property(GLOBAL PROPERTY MELO_LAYER_VIOLATIONS "")

    set(all_targets "")
    foreach(dir IN LISTS arg_DIRECTORIES)
        _melo_collect_targets("${dir}" dir_targets)
        list(APPEND all_targets ${dir_targets})
    endforeach()

    # R0: every library/executable must be tagged.
    set(melo_targets "")
    foreach(target IN LISTS all_targets)
        get_target_property(type ${target} TYPE)
        get_target_property(helper ${target} MELO_HELPER)
        if(type STREQUAL "UTILITY" OR helper)
            continue()
        endif()
        _melo_is_melo_target(${target} is_melo)
        if(is_melo)
            list(APPEND melo_targets ${target})
        else()
            _melo_violation(R0 "${target} (${type}) is not created with melo_add_library or "
                               "melo_add_executable, so its layer is unknown")
        endif()
    endforeach()

    foreach(target IN LISTS melo_targets)
        get_target_property(folder ${target} MELO_FOLDER)
        get_target_property(layer ${target} MELO_LAYER)
        _melo_deps(${target} LINK_LIBRARIES deps)
        foreach(dep IN LISTS deps)
            _melo_is_melo_target("${dep}" dep_is_melo)
            if(NOT dep_is_melo)
                melo_external_allowed("${folder}" "${dep}" ext_ok)
                if(NOT ext_ok)
                    _melo_violation(R6 "${target} (${folder}) links '${dep}', which is not on its "
                                       "external allowlist")
                endif()
                continue()
            endif()
            get_target_property(dep_folder ${dep} MELO_FOLDER)
            get_target_property(dep_layer ${dep} MELO_LAYER)
            if(dep_layer GREATER layer)
                _melo_violation(R1 "${target} (${folder}, layer ${layer}) links ${dep} "
                                   "(${dep_folder}, layer ${dep_layer})")
            endif()
            if(layer EQUAL 5)
                _melo_layer5_allows("${folder}" "${dep_folder}" ${dep_layer} l5_ok)
                if(NOT l5_ok)
                    _melo_violation(R2 "${target} (${folder}, layer 5) links ${dep} (${dep_folder}); "
                                       "tools/macros/features may only use core/types, core/edits, "
                                       "core/tuning and other layer-5 targets")
                endif()
                _melo_check_transitive(${target} "${folder}" ${dep})
            endif()
            if(folder STREQUAL "core/types")
                _melo_violation(R3 "core/types (${target}) links ${dep}; core/types is data only")
            endif()
            if(dep_folder STREQUAL MELO_IO_FOLDER AND layer LESS MELO_IO_MIN_LAYER)
                _melo_violation(R4 "${target} (${folder}, layer ${layer}) links ${dep}; core/io may "
                                   "only be linked by layers ${MELO_IO_MIN_LAYER}-7 (ADR-0001)")
            endif()
        endforeach()
    endforeach()

    # R5: cycles.
    foreach(target IN LISTS melo_targets)
        get_property(state GLOBAL PROPERTY _MELO_DFS_${target})
        if(NOT state)
            _melo_dfs(${target})
        endif()
    endforeach()

    _melo_write_layers_json("${melo_targets}")

    get_property(violations GLOBAL PROPERTY MELO_LAYER_VIOLATIONS)
    if(violations)
        list(JOIN violations "\n" report)
        message(FATAL_ERROR "\n${report}\n")
    endif()
endfunction()
