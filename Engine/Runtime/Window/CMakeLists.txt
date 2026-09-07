# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Desktop-only port of the reference engine's Window module: the real GLFW3-backed
# WindowManagerUVE, NullWindowManagerUVE (headless/testing), and the validation helpers both rely
# on. The reference module also has an Android backend (android_window_manager_uve.*, EGL/GLESv3)
# behind an ANDROID branch - not ported here since this project doesn't target Android; add it back
# the same way the reference does if that ever changes.
#
# Uses find_package(glfw3), not FetchContent, deliberately matching Engine/Editor/Viewport's own
# GLFW dependency - both need the same "glfw" target, and FetchContent-building our own copy here
# would collide with the system package's imported "glfw" target the moment both subdirectories
# are configured in one build.

find_package(glfw3 REQUIRED)

add_library(uve_window STATIC
    src/window_manager_uve.cpp
    src/null_window_manager_uve.cpp
    src/window_desc_validation_uve.cpp
    src/monitor_info_validation_uve.cpp
    src/display_mode_validation_uve.cpp
    src/framebuffer_size_validation_uve.cpp
)
target_include_directories(uve_window PUBLIC include)
target_link_libraries(uve_window
    PUBLIC uve_platform uve_debug uve_events uve_input
    PRIVATE glfw
)
target_compile_features(uve_window PUBLIC cxx_std_20)
uve_set_warnings(uve_window)
