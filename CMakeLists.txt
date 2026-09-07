# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# This repository is being rebuilt module by module (see the restructuring plan) from the
# original UNIVEX-ENGINE codebase, ported into the Engine/Runtime taxonomy described in
# UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt. Modules are added below in dependency
# order as each one lands; add_subdirectory calls appear only for modules that already exist
# here - see the plan for what's ported so far and what's still pending.

cmake_minimum_required(VERSION 3.24)
project(UniVexEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(UVE_BUILD_TESTS "Build UniVex test suites" ON)
enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Engine/CMake")

include(UveCompilerWarnings)
include(UveBuildConfiguration)
include(UveFormatSupport)

include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_Examples OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(Engine/Runtime/Platform)
add_subdirectory(Engine/Runtime/Object)
add_subdirectory(Engine/Runtime/Core/Logging)
add_subdirectory(Engine/Runtime/Core/Memory)
add_subdirectory(Engine/Runtime/Core/Threading)
add_subdirectory(Engine/Runtime/Core/Math)
add_subdirectory(Engine/Runtime/Core/Utilities)
add_subdirectory(Engine/Runtime/Events)
add_subdirectory(Engine/Runtime/Input)
add_subdirectory(Engine/Runtime/Window)
add_subdirectory(Engine/Runtime/RHI/RHI)
add_subdirectory(Engine/Runtime/RHI/OpenGL)
add_subdirectory(Engine/Runtime/RHI/Null)
add_subdirectory(Engine/Runtime/Commandline)
add_subdirectory(Engine/Runtime/Config)
add_subdirectory(Engine/Runtime/Network)
add_subdirectory(Engine/Runtime/Asset)
add_subdirectory(Engine/Runtime/RHI/Shader)
add_subdirectory(Engine/Runtime/Plugins)
add_subdirectory(Engine/Runtime/Component)
add_subdirectory(Engine/Runtime/Entity)
add_subdirectory(Engine/Runtime/Animation)
add_subdirectory(Engine/Runtime/Scene)
add_subdirectory(Engine/Runtime/Save)
add_subdirectory(Engine/Runtime/RHI/RenderSystems)
add_subdirectory(Engine/Runtime/Audio)
add_subdirectory(Engine/Runtime/Physics)
add_subdirectory(Engine/Runtime/World)
add_subdirectory(Engine/Runtime/ProjectCheck)
add_subdirectory(Engine/Editor/Viewport)
add_subdirectory(Engine/Editor/EditorApp)

if(UVE_BUILD_TESTS)
    add_subdirectory(Test)
endif()
