# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

# Resolves desktop packages the rest of the tree expects as imported targets.
# ZLIB and JPEG fall back to FetchContent when -dev packages are missing.
# GLFW/OpenGL/GLEW stay system-only: FetchContent GLFW still needs GL headers,
# which this host may not have. UVE_HAS_DESKTOP_GL is then OFF and Window/OpenGL/
# EngineCore/Editor/App stay on Null/INTERFACE stubs so CPU tests still configure.

include(FetchContent)
enable_language(C)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "UVE: ZLIB not found - fetching zlib v1.3.1")
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        uve_zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(uve_zlib)
    if(NOT TARGET ZLIB::ZLIB)
        if(TARGET zlibstatic)
            add_library(ZLIB::ZLIB ALIAS zlibstatic)
        elseif(TARGET zlib)
            add_library(ZLIB::ZLIB ALIAS zlib)
        else()
            message(FATAL_ERROR "UVE: fetched zlib but neither zlibstatic nor zlib was created")
        endif()
    endif()
endif()

if(NOT ANDROID)
    find_package(JPEG QUIET)
    if(NOT JPEG_FOUND)
        # libjpeg-turbo refuses add_subdirectory()/FetchContent_MakeAvailable.
        message(STATUS "UVE: JPEG not found - fetching libjpeg-turbo 3.0.3 via ExternalProject")
        include(ExternalProject)
        set(_uve_jpeg_install "${CMAKE_BINARY_DIR}/_deps/uve_jpeg-install")
        ExternalProject_Add(uve_jpeg_ep
            GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
            GIT_TAG 3.0.3
            GIT_SHALLOW TRUE
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX=${_uve_jpeg_install}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DWITH_SIMD=OFF
                -DWITH_TURBOJPEG=OFF
            BUILD_BYPRODUCTS "${_uve_jpeg_install}/lib/libjpeg.a"
        )
        file(MAKE_DIRECTORY "${_uve_jpeg_install}/include")
        add_library(JPEG::JPEG STATIC IMPORTED GLOBAL)
        set_target_properties(JPEG::JPEG PROPERTIES
            IMPORTED_LOCATION "${_uve_jpeg_install}/lib/libjpeg.a"
            INTERFACE_INCLUDE_DIRECTORIES "${_uve_jpeg_install}/include"
        )
        add_dependencies(JPEG::JPEG uve_jpeg_ep)
    endif()
endif()

find_package(glfw3 QUIET)
find_package(OpenGL QUIET)
find_package(GLEW QUIET)

set(UVE_HAS_DESKTOP_GL OFF)
if(glfw3_FOUND AND OpenGL_FOUND)
    set(UVE_HAS_DESKTOP_GL ON)
endif()
if(NOT UVE_HAS_DESKTOP_GL)
    message(WARNING
        "UVE: GLFW/OpenGL not found - configuring CPU-only (Null window, no GlRenderDevice, no editor)")
endif()
