# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

add_library(uve_platform STATIC
    Internal/application_runtime.cpp
    Internal/application_window_settings.cpp
    Internal/crash_reporter.cpp
    Internal/editor_project_package.cpp
)
target_include_directories(uve_platform PUBLIC Expose)
target_link_libraries(uve_platform PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(uve_platform PRIVATE dbghelp)
endif()
target_compile_features(uve_platform PUBLIC cxx_std_23)
uve_set_warnings(uve_platform)
