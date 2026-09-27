# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#
# UVScript: the text scripting language for entity nodes (see README.md). Standard library only.

add_library(uve_uvscript STATIC
    Internal/uvscript_parser_uve.cpp
    Internal/uvscript_value_uve.cpp
    Internal/uvscript_compiler_uve.cpp
    Internal/uvscript_instance_uve.cpp
    Internal/uvscript_native_uve.cpp
    Internal/uvscript_codegen_uve.cpp
    Internal/uvscript_host_description_uve.cpp
)
target_include_directories(uve_uvscript PUBLIC Expose)
target_compile_features(uve_uvscript PUBLIC cxx_std_23)
uve_set_warnings(uve_uvscript)

# uvsc: .uvs to C++23 at build time (see Tool/uvsc_main.cpp).
add_executable(uvsc Tool/uvsc_main.cpp)
target_link_libraries(uvsc PRIVATE uve_uvscript)
uve_set_warnings(uvsc)

# Compiles SCRIPTS to C++ against the node described by HOST and adds the result to TARGET, so
# every node running one of those scripts runs native code in that build:
#
#     uve_add_uvscript_native(my_game HOST scripts/player.uvhost SCRIPTS scripts/player.uvs)
function(uve_add_uvscript_native target)
    cmake_parse_arguments(ARG "" "HOST" "SCRIPTS" ${ARGN})
    if(NOT ARG_HOST OR NOT ARG_SCRIPTS)
        message(FATAL_ERROR "uve_add_uvscript_native(${target}) needs HOST and SCRIPTS")
    endif()
    get_filename_component(host "${ARG_HOST}" ABSOLUTE)
    foreach(script IN LISTS ARG_SCRIPTS)
        get_filename_component(source "${script}" ABSOLUTE)
        get_filename_component(name "${script}" NAME)
        set(output "${CMAKE_CURRENT_BINARY_DIR}/uvscript_native/${name}.cpp")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND uvsc --host "${host}" --out "${output}" "${source}"
            DEPENDS uvsc "${source}" "${host}"
            COMMENT "uvsc ${name}"
            VERBATIM)
        target_sources(${target} PRIVATE "${output}")
    endforeach()
    target_link_libraries(${target} PRIVATE uve_uvscript)
endfunction()
