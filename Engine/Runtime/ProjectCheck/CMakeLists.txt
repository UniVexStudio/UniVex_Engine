# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

add_library(uve_project_check_lib STATIC
    src/project_checker_uve.cpp
)
target_include_directories(uve_project_check_lib PUBLIC include)
target_link_libraries(uve_project_check_lib PUBLIC uve_asset)
target_compile_features(uve_project_check_lib PUBLIC cxx_std_20)
uve_set_warnings(uve_project_check_lib)

#
# From the reference engine's app module: this ported the project_check CLI entry point early,
# since it doesn't need uve_core/uve_editor - the other two reference app executables
# (uve_runtime, uve_editor_app) landed later at Engine/App once EngineCoreUVE and EditorUVE were
# both real.

add_executable(uve_project_check app/main.cpp)
target_link_libraries(uve_project_check PRIVATE uve_project_check_lib)
target_compile_features(uve_project_check PRIVATE cxx_std_20)
uve_set_warnings(uve_project_check)
