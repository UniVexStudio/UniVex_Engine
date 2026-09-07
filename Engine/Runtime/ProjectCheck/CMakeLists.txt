# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

add_library(uve_project_check_lib STATIC
    src/project_checker_uve.cpp
)
target_include_directories(uve_project_check_lib PUBLIC include)
target_link_libraries(uve_project_check_lib PUBLIC uve_asset)
target_compile_features(uve_project_check_lib PUBLIC cxx_std_20)
uve_set_warnings(uve_project_check_lib)

#
# From the reference engine's app module: only the project_check CLI entry point is portable
# right now - app/src/main.cpp (uve_runtime) and app/src/editor/main.cpp (uve_editor_app) both
# need uve_core (EngineCoreUVE, the whole-engine bootstrap class) or uve_editor, neither of which
# is ported yet.

add_executable(uve_project_check app/main.cpp)
target_link_libraries(uve_project_check PRIVATE uve_project_check_lib)
target_compile_features(uve_project_check PRIVATE cxx_std_20)
uve_set_warnings(uve_project_check)
