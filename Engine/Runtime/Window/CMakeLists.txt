# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

# Native window backends: GLFW on desktop, EGL/ANativeWindow on Android,
# and NullWindowManagerUVE for headless runs. Android lifecycle is driven by
# the NativeActivity host in Engine/App/Android.

set(_uve_window_sources
    Internal/null_window_manager_uve.cpp
    Internal/presentation_layout_uve.cpp
    Internal/window_title_format_uve.cpp
    Internal/window_desc_validation_uve.cpp
    Internal/monitor_info_validation_uve.cpp
    Internal/display_mode_validation_uve.cpp
    Internal/framebuffer_size_validation_uve.cpp
)
if(UVE_HAS_DESKTOP_GL)
    list(APPEND _uve_window_sources Internal/window_manager_uve.cpp)
endif()

add_library(uve_window STATIC ${_uve_window_sources})
target_include_directories(uve_window PUBLIC Expose)
target_link_libraries(uve_window
    PUBLIC uve_platform uve_logging uve_events uve_input
)
if(UVE_HAS_DESKTOP_GL)
    target_link_libraries(uve_window PRIVATE glfw)
endif()

if(APPLE)
    find_library(UVE_IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(UVE_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(uve_window PRIVATE ${UVE_IOKIT_FRAMEWORK} ${UVE_COREFOUNDATION_FRAMEWORK})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(UVE_SYSTEMD_INCLUDE_DIR systemd/sd-bus.h)
    find_library(UVE_SYSTEMD_LIBRARY systemd)
    if(UVE_SYSTEMD_INCLUDE_DIR AND UVE_SYSTEMD_LIBRARY)
        target_include_directories(uve_window PRIVATE ${UVE_SYSTEMD_INCLUDE_DIR})
        target_link_libraries(uve_window PRIVATE ${UVE_SYSTEMD_LIBRARY})
        target_compile_definitions(uve_window PRIVATE UVE_HAS_SYSTEMD=1)
    endif()
endif()

target_compile_features(uve_window PUBLIC cxx_std_23)
uve_set_warnings(uve_window)

if(ANDROID)
    target_sources(uve_window PRIVATE Internal/android_window_manager_uve.cpp)
    target_link_libraries(uve_window PRIVATE android EGL GLESv3)
endif()
