# Layer table and dependency rules (.claude/rules/architecture.md, ADR-0001).
#
# anno_add_library(<folder> ...) and anno_add_executable(<folder> ...) create the target for a
# src/ folder and tag it with its layer. anno_check_layers() must run after every target exists; it
# reports all violations at once, each prefixed "ANNO LAYER VIOLATION: R<n>":
#   R0  a library/executable under src/ was not created by anno_add_library/anno_add_executable
#   R1  a target links a target in a higher layer
#   R2  a layer-5 target (tools, macros, features) links outside its allowlist, directly or through
#       the usage requirements (PUBLIC/INTERFACE links) of something it links
#   R3  core/types links another anno target
#   R4  core/io is linked by something below layer 6
#   R5  link cycle between anno targets
#   R6  a non-anno dependency that is not on the target's external allowlist, or an unparsable
#       generator expression
# Design: docs/design/phase0-foundations.md §2.

include_guard(GLOBAL)

set(ANNO_LAYER_VIOLATION_PREFIX "ANNO LAYER VIOLATION")

# Layer-5 targets may link these folders (plus other layer-5 targets).
set(ANNO_LAYER5_ALLOWED_FOLDERS core/types core/edits core/tuning)
# These layer-5 folders may also link core/analysis/* (architecture.md, re-analysis requests).
set(ANNO_ANALYSIS_EXCEPTION_FOLDERS features/dna features/sound_editor)
# core/io may only be linked from this layer upward (ADR-0001).
set(ANNO_IO_FOLDER core/io)
set(ANNO_IO_MIN_LAYER 6)

# ---- Layer table -------------------------------------------------------------------------------

# Sets <out_var> to the layer (0-7) of a src/ folder, or to "" if the folder is not in the table.
function(anno_folder_layer folder out_var)
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

# Sets <out_var> to TRUE if <folder> may link the non-anno dependency <dep>. Later phases extend
# this through their design note and an ADR (the FFT library for core/dsp, Threads for core/render).
function(anno_external_allowed folder dep out_var)
    set(allowed FALSE)
    if(folder MATCHES "^(host/[a-z0-9_]+|ui)$" AND dep MATCHES "^(juce_|juce::|ARA)")
        set(allowed TRUE)
    endif()
    # core/render guards its cache with a std::mutex (ADR-0006).
    if(folder STREQUAL "core/render" AND dep STREQUAL "Threads::Threads")
        set(allowed TRUE)
    endif()
    # pocketfft (ADR-0009, amended by ADR-0014): the FFT library, core/dsp; the vendored c4dm/pyin
    # target (anno_c4dm_pyin) also links it, but it is not an anno_add_library target.
    if(folder STREQUAL "core/dsp" AND dep STREQUAL "anno_pocketfft")
        set(allowed TRUE)
    endif()
    # c4dm/pyin (ADR-0014): the vendored pYIN core, core/analysis/pitch only.
    if(folder STREQUAL "core/analysis/pitch" AND dep STREQUAL "anno_c4dm_pyin")
        set(allowed TRUE)
    endif()
    # Rubber Band (ADR-0026): the optional R3 stretch engine, core/synthesis/stretch only.
    if((folder STREQUAL "core/synthesis/stretch" OR folder STREQUAL "core/synthesis/rubberband_pitch")
       AND dep STREQUAL "anno_rubberband")
        set(allowed TRUE)
    endif()
    # nlohmann/json: CLI targets only; core/* never parses JSON (ADR-0007).
    if(folder MATCHES "^cli(/[a-z0-9_]+)?$" AND dep MATCHES "^nlohmann_json(::nlohmann_json)?$")
        set(allowed TRUE)
    endif()
    set(${out_var} ${allowed} PARENT_SCOPE)
endfunction()

# ---- Target creation ---------------------------------------------------------------------------

function(_anno_target_name folder out_var)
    string(REPLACE "/" "_" suffix "${folder}")
    set(${out_var} "anno_${suffix}" PARENT_SCOPE)
endfunction()

function(_anno_tag_target target folder)
    anno_folder_layer("${folder}" layer)
    if(layer STREQUAL "")
        message(FATAL_ERROR "${ANNO_LAYER_VIOLATION_PREFIX}: R0: folder '${folder}' is not in the "
                            "layer table (cmake/AnnoLayers.cmake). Add it through a design note.")
    endif()
    set_target_properties(${target} PROPERTIES ANNO_LAYER ${layer} ANNO_FOLDER "${folder}")
    if(TARGET anno_warnings)
        target_link_libraries(${target} PRIVATE anno_warnings)
    endif()
endfunction()

# anno_add_library(<folder> SOURCES <src>... [PUBLIC_DEPS <t>...] [PRIVATE_DEPS <t>...])
# Creates static library anno_<folder with / -> _> and alias anno::<same suffix>. Headers are
# included as "<folder>/<header>.h" relative to ANNO_SOURCE_ROOT (default: <project>/src).
function(anno_add_library folder)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS")
    _anno_target_name("${folder}" target)
    add_library(${target} STATIC ${arg_SOURCES})
    string(REPLACE "/" "_" suffix "${folder}")
    add_library(anno::${suffix} ALIAS ${target})
    if(NOT DEFINED ANNO_SOURCE_ROOT)
        set(ANNO_SOURCE_ROOT "${PROJECT_SOURCE_DIR}/src")
    endif()
    target_include_directories(${target} PUBLIC "${ANNO_SOURCE_ROOT}")
    target_compile_features(${target} PUBLIC cxx_std_20)
    _anno_tag_target(${target} "${folder}")
    if(arg_PUBLIC_DEPS)
        target_link_libraries(${target} PUBLIC ${arg_PUBLIC_DEPS})
    endif()
    if(arg_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_DEPS})
    endif()
endfunction()

# anno_add_executable(<folder> OUTPUT_NAME <name> SOURCES <src>... [PRIVATE_DEPS <t>...])
function(anno_add_executable folder)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "OUTPUT_NAME" "SOURCES;PRIVATE_DEPS")
    _anno_target_name("${folder}" target)
    add_executable(${target} ${arg_SOURCES})
    if(arg_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${arg_OUTPUT_NAME}")
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
    _anno_tag_target(${target} "${folder}")
    if(arg_PRIVATE_DEPS)
        target_link_libraries(${target} PRIVATE ${arg_PRIVATE_DEPS})
    endif()
endfunction()

# ---- Checking ----------------------------------------------------------------------------------

function(_anno_violation rule text)
    set_property(GLOBAL APPEND PROPERTY ANNO_LAYER_VIOLATIONS
                 "${ANNO_LAYER_VIOLATION_PREFIX}: ${rule}: ${text}")
endfunction()

function(_anno_collect_targets dir out_var)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sub IN LISTS subdirs)
        _anno_collect_targets("${sub}" sub_targets)
        list(APPEND targets ${sub_targets})
    endforeach()
    set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

# Normalises one link item. Sets <name_var> (empty = ignore), <usage_var> (FALSE for
# $<LINK_ONLY:...>, i.e. a PRIVATE dependency of a static library) and <bad_var> (TRUE if the item
# is an unparsable generator expression).
function(_anno_normalise_item item name_var usage_var bad_var)
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
        get_target_property(helper "${name}" ANNO_HELPER)
        if(helper)
            set(name "")
        endif()
    endif()
    set(${name_var} "${name}" PARENT_SCOPE)
    set(${usage_var} ${usage} PARENT_SCOPE)
    set(${bad_var} ${bad} PARENT_SCOPE)
endfunction()

function(_anno_is_anno_target name out_var)
    set(result FALSE)
    if(TARGET "${name}")
        get_target_property(layer "${name}" ANNO_LAYER)
        if(NOT layer STREQUAL "layer-NOTFOUND")
            set(result TRUE)
        endif()
    endif()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# Sets <out_var> to the normalised dependencies of <target> from <property>. With
# USAGE_ONLY, $<LINK_ONLY:...> entries are dropped. Reports R6 for unparsable entries.
function(_anno_deps target property out_var)
    cmake_parse_arguments(PARSE_ARGV 3 arg "USAGE_ONLY" "" "")
    get_target_property(items ${target} ${property})
    set(deps "")
    if(items)
        foreach(item IN LISTS items)
            _anno_normalise_item("${item}" name usage bad)
            if(bad)
                _anno_violation(R6 "${target} links unparsable generator expression '${item}'")
            elseif(NOT name STREQUAL "" AND (usage OR NOT arg_USAGE_ONLY))
                list(APPEND deps "${name}")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES deps)
    set(${out_var} ${deps} PARENT_SCOPE)
endfunction()

function(_anno_layer5_allows target_folder dep_folder dep_layer out_var)
    set(allowed FALSE)
    if(dep_layer EQUAL 5)
        set(allowed TRUE)
    elseif(dep_folder IN_LIST ANNO_LAYER5_ALLOWED_FOLDERS)
        set(allowed TRUE)
    elseif(dep_folder MATCHES "^core/analysis/" AND target_folder IN_LIST ANNO_ANALYSIS_EXCEPTION_FOLDERS)
        set(allowed TRUE)
    endif()
    set(${out_var} ${allowed} PARENT_SCOPE)
endfunction()

# R2t: walks usage edges from <start> and reports anything a layer-5 <target> may not use.
function(_anno_check_transitive target target_folder start)
    set(queue ${start})
    set(seen ${start})
    while(queue)
        list(POP_FRONT queue node)
        _anno_is_anno_target("${node}" node_is_anno)
        if(NOT node_is_anno)
            continue()
        endif()
        _anno_deps(${node} INTERFACE_LINK_LIBRARIES usage_deps USAGE_ONLY)
        foreach(dep IN LISTS usage_deps)
            if(dep IN_LIST seen)
                continue()
            endif()
            list(APPEND seen "${dep}")
            list(APPEND queue "${dep}")
            _anno_is_anno_target("${dep}" dep_is_anno)
            if(dep_is_anno)
                get_target_property(dep_folder ${dep} ANNO_FOLDER)
                get_target_property(dep_layer ${dep} ANNO_LAYER)
                _anno_layer5_allows("${target_folder}" "${dep_folder}" ${dep_layer} ok)
            else()
                set(ok FALSE)
            endif()
            if(NOT ok)
                _anno_violation(R2 "${target} (${target_folder}, layer 5) reaches ${dep} through the "
                                   "usage requirements of ${start}; link it PRIVATE there")
            endif()
        endforeach()
    endwhile()
endfunction()

function(_anno_dfs node)
    set_property(GLOBAL PROPERTY _ANNO_DFS_${node} gray)
    _anno_deps(${node} LINK_LIBRARIES deps)
    foreach(dep IN LISTS deps)
        _anno_is_anno_target("${dep}" dep_is_anno)
        if(NOT dep_is_anno)
            continue()
        endif()
        get_property(state GLOBAL PROPERTY _ANNO_DFS_${dep})
        if(state STREQUAL "gray")
            _anno_violation(R5 "link cycle through ${node} -> ${dep}")
        elseif(NOT state)
            _anno_dfs(${dep})
        endif()
    endforeach()
    set_property(GLOBAL PROPERTY _ANNO_DFS_${node} black)
endfunction()

function(_anno_write_layers_json targets)
    set(entries "")
    foreach(target IN LISTS targets)
        get_target_property(folder ${target} ANNO_FOLDER)
        get_target_property(layer ${target} ANNO_LAYER)
        list(APPEND entries "    \"${folder}\": {\"target\": \"${target}\", \"layer\": ${layer}}")
    endforeach()
    list(JOIN entries ",\n" folders_json)
    list(TRANSFORM ANNO_LAYER5_ALLOWED_FOLDERS REPLACE "(.+)" "\"\\1\"" OUTPUT_VARIABLE l5)
    list(JOIN l5 ", " l5_json)
    list(TRANSFORM ANNO_ANALYSIS_EXCEPTION_FOLDERS REPLACE "(.+)" "\"\\1\"" OUTPUT_VARIABLE ex)
    list(JOIN ex ", " ex_json)
    file(WRITE "${CMAKE_BINARY_DIR}/anno_layers.json"
"{
  \"folders\": {
${folders_json}
  },
  \"layer5_allowed_folders\": [${l5_json}],
  \"analysis_exception_folders\": [${ex_json}],
  \"io_folder\": \"${ANNO_IO_FOLDER}\",
  \"io_min_layer\": ${ANNO_IO_MIN_LAYER}
}
")
endfunction()

# anno_check_layers(DIRECTORIES <dir>...)
function(anno_check_layers)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "DIRECTORIES")
    set_property(GLOBAL PROPERTY ANNO_LAYER_VIOLATIONS "")

    set(all_targets "")
    foreach(dir IN LISTS arg_DIRECTORIES)
        _anno_collect_targets("${dir}" dir_targets)
        list(APPEND all_targets ${dir_targets})
    endforeach()

    # R0: every library/executable must be tagged.
    set(anno_targets "")
    foreach(target IN LISTS all_targets)
        get_target_property(type ${target} TYPE)
        get_target_property(helper ${target} ANNO_HELPER)
        if(type STREQUAL "UTILITY" OR helper)
            continue()
        endif()
        _anno_is_anno_target(${target} is_anno)
        if(is_anno)
            list(APPEND anno_targets ${target})
        else()
            _anno_violation(R0 "${target} (${type}) is not created with anno_add_library or "
                               "anno_add_executable, so its layer is unknown")
        endif()
    endforeach()

    foreach(target IN LISTS anno_targets)
        get_target_property(folder ${target} ANNO_FOLDER)
        get_target_property(layer ${target} ANNO_LAYER)
        _anno_deps(${target} LINK_LIBRARIES deps)
        foreach(dep IN LISTS deps)
            _anno_is_anno_target("${dep}" dep_is_anno)
            if(NOT dep_is_anno)
                anno_external_allowed("${folder}" "${dep}" ext_ok)
                if(NOT ext_ok)
                    _anno_violation(R6 "${target} (${folder}) links '${dep}', which is not on its "
                                       "external allowlist")
                endif()
                continue()
            endif()
            get_target_property(dep_folder ${dep} ANNO_FOLDER)
            get_target_property(dep_layer ${dep} ANNO_LAYER)
            if(dep_layer GREATER layer)
                _anno_violation(R1 "${target} (${folder}, layer ${layer}) links ${dep} "
                                   "(${dep_folder}, layer ${dep_layer})")
            endif()
            if(layer EQUAL 5)
                _anno_layer5_allows("${folder}" "${dep_folder}" ${dep_layer} l5_ok)
                if(NOT l5_ok)
                    _anno_violation(R2 "${target} (${folder}, layer 5) links ${dep} (${dep_folder}); "
                                       "tools/macros/features may only use core/types, core/edits, "
                                       "core/tuning and other layer-5 targets")
                endif()
                _anno_check_transitive(${target} "${folder}" ${dep})
            endif()
            if(folder STREQUAL "core/types")
                _anno_violation(R3 "core/types (${target}) links ${dep}; core/types is data only")
            endif()
            if(dep_folder STREQUAL ANNO_IO_FOLDER AND layer LESS ANNO_IO_MIN_LAYER)
                _anno_violation(R4 "${target} (${folder}, layer ${layer}) links ${dep}; core/io may "
                                   "only be linked by layers ${ANNO_IO_MIN_LAYER}-7 (ADR-0001)")
            endif()
        endforeach()
    endforeach()

    # R5: cycles.
    foreach(target IN LISTS anno_targets)
        get_property(state GLOBAL PROPERTY _ANNO_DFS_${target})
        if(NOT state)
            _anno_dfs(${target})
        endif()
    endforeach()

    _anno_write_layers_json("${anno_targets}")

    get_property(violations GLOBAL PROPERTY ANNO_LAYER_VIOLATIONS)
    if(violations)
        list(JOIN violations "\n" report)
        message(FATAL_ERROR "\n${report}\n")
    endif()
endfunction()
