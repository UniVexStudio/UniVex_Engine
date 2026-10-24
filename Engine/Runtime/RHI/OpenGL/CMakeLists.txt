# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

# Shared OpenGL/GLES render device. Desktop resolves functions through GLFW;
# Android uses EGL and GLES entry points. The window backend owns the context.

if(NOT UVE_HAS_DESKTOP_GL AND NOT ANDROID)
    add_library(uve_rhi_opengl INTERFACE)
    return()
endif()

if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

add_library(uve_rhi_opengl STATIC
    Internal/gl_functions_uve.cpp
    Internal/gl_error_check_uve.cpp
    Internal/gl_command_buffer_uve.cpp
    Internal/gl_render_device_uve.cpp
)
target_include_directories(uve_rhi_opengl PUBLIC Expose)
target_link_libraries(uve_rhi_opengl
    PUBLIC uve_rhi uve_window uve_platform uve_logging

)
target_compile_features(uve_rhi_opengl PUBLIC cxx_std_23)
uve_set_warnings(uve_rhi_opengl)

if(ANDROID)
    target_link_libraries(uve_rhi_opengl PRIVATE EGL GLESv3 log dl)
else()
    target_link_libraries(uve_rhi_opengl PRIVATE glfw OpenGL::GL)
endif()
