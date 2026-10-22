# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

# UVE_BUILD_CONFIGURATION is the product-facing profile name. The underlying
# CMake build type stays conventional so third-party tooling and generators
# continue to recognize the standard optimization/debug-info semantics.
#
# WHO IS IN CHARGE
# ----------------
# Two inputs can describe a build: the conventional CMAKE_BUILD_TYPE (what every
# tool outside this repo passes) and UVE_BUILD_CONFIGURATION (what this engine's
# profiles mean). Earlier this module ignored the first one entirely and forced
# CMAKE_BUILD_TYPE back to Debug, so `-DCMAKE_BUILD_TYPE=Release` silently
# produced an unoptimized binary with assertions enabled - the worst possible
# failure mode, because every test still passes and the artifact just ships
# wrong. The rule now is:
#
#   * An explicitly requested PROFILE wins, and the build type follows it.
#   * Otherwise the profile is DERIVED from the requested build type.
#   * Otherwise both default to Debug.
#
# That makes the conventional flag do what it says while keeping profiles the
# authority when they are actually named, which is the order a product build
# script needs: it says "Shipping" and gets shipping semantics regardless of
# what CMake's type vocabulary happens to be called this year.

set(_uve_supported_build_configurations Debug Development Release Shipping)

# `DEFINED CACHE{...}` distinguishes "the caller asked for this" from "this
# module is about to supply a default" without a second sentinel variable that
# could itself go stale in a cached build directory.
set(_uve_profile_requested FALSE)
if(DEFINED CACHE{UVE_BUILD_CONFIGURATION})
    set(_uve_profile_requested TRUE)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" _uve_requested_build_type_upper)
set(_uve_derived_profile "")
if(_uve_requested_build_type_upper STREQUAL "DEBUG")
    set(_uve_derived_profile Debug)
elseif(_uve_requested_build_type_upper STREQUAL "RELWITHDEBINFO")
    set(_uve_derived_profile Development)
elseif(_uve_requested_build_type_upper STREQUAL "RELEASE")
    set(_uve_derived_profile Release)
elseif(_uve_requested_build_type_upper STREQUAL "MINSIZEREL")
    set(_uve_derived_profile Shipping)
endif()

if(NOT _uve_profile_requested)
    if(NOT _uve_derived_profile STREQUAL "")
        set(UVE_BUILD_CONFIGURATION "${_uve_derived_profile}" CACHE STRING
            "UVE profile: Debug, Development, Release, or Shipping" FORCE)
        message(STATUS "UVE profile '${UVE_BUILD_CONFIGURATION}' derived from CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'")
    else()
        set(UVE_BUILD_CONFIGURATION "Debug" CACHE STRING
            "UVE profile: Debug, Development, Release, or Shipping")
        if(NOT CMAKE_BUILD_TYPE STREQUAL "")
            message(WARNING
                "CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' has no UVE profile equivalent; defaulting to the Debug profile. "
                "Pass -DUVE_BUILD_CONFIGURATION=Debug|Development|Release|Shipping to choose explicitly.")
        endif()
    endif()
else()
    message(STATUS "UVE profile '${UVE_BUILD_CONFIGURATION}' was requested explicitly")
endif()

set_property(CACHE UVE_BUILD_CONFIGURATION PROPERTY STRINGS ${_uve_supported_build_configurations})

if(NOT UVE_BUILD_CONFIGURATION IN_LIST _uve_supported_build_configurations)
    message(FATAL_ERROR
        "UVE_BUILD_CONFIGURATION must be one of Debug, Development, Release, or Shipping; got '${UVE_BUILD_CONFIGURATION}'")
endif()

if(UVE_BUILD_CONFIGURATION STREQUAL "Debug")
    set(_uve_cmake_build_type Debug)
    set(_uve_profile_assertions 1)
    set(_uve_profile_default_log_level 0)
    set(_uve_profile_compiled_log_level 0)
elseif(UVE_BUILD_CONFIGURATION STREQUAL "Development")
    set(_uve_cmake_build_type RelWithDebInfo)
    set(_uve_profile_assertions 1)
    set(_uve_profile_default_log_level 1)
    set(_uve_profile_compiled_log_level 0)
elseif(UVE_BUILD_CONFIGURATION STREQUAL "Release")
    set(_uve_cmake_build_type Release)
    set(_uve_profile_assertions 0)
    set(_uve_profile_default_log_level 2)
    set(_uve_profile_compiled_log_level 0)
else()
    set(_uve_cmake_build_type MinSizeRel)
    set(_uve_profile_assertions 0)
    set(_uve_profile_default_log_level 3)
    # Shipping is the only profile that strips log statements at COMPILE time
    # rather than filtering them at runtime. The threshold matches the runtime
    # default exactly, so nothing that would have been printed disappears - what
    # changes is that the format strings and their arguments are not in the
    # binary at all.
    set(_uve_profile_compiled_log_level 3)
endif()

# The profile owns the conventional CMake type. Single-config generators only;
# a multi-config generator picks the type at build time and this module's job is
# just to publish the profile's own defines.
if(NOT CMAKE_CONFIGURATION_TYPES)
    if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "${_uve_cmake_build_type}")
        message(STATUS
            "UVE profile '${UVE_BUILD_CONFIGURATION}' sets CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}' -> '${_uve_cmake_build_type}'")
    endif()
    set(CMAKE_BUILD_TYPE "${_uve_cmake_build_type}" CACHE STRING "CMake build type" FORCE)
endif()

string(TOUPPER "${UVE_BUILD_CONFIGURATION}" _uve_build_configuration_upper)
add_compile_definitions("UVE_BUILD_CONFIGURATION_${_uve_build_configuration_upper}=1")
if(UVE_BUILD_CONFIGURATION STREQUAL "Shipping")
    add_compile_definitions(UVE_SHIPPING=1)
else()
    add_compile_definitions(UVE_SHIPPING=0)
endif()
add_compile_definitions(UVE_PROFILE_ASSERTIONS_ENABLED=${_uve_profile_assertions})
add_compile_definitions(UVE_PROFILE_DEFAULT_LOG_LEVEL=${_uve_profile_default_log_level})
add_compile_definitions(UVE_COMPILED_LOG_LEVEL=${_uve_profile_compiled_log_level})

unset(_uve_build_configuration_upper)
unset(_uve_cmake_build_type)
unset(_uve_profile_assertions)
unset(_uve_profile_default_log_level)
unset(_uve_profile_compiled_log_level)
unset(_uve_profile_requested)
unset(_uve_requested_build_type_upper)
unset(_uve_derived_profile)
unset(_uve_supported_build_configurations)
