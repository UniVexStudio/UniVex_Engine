# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Roadmap item #7 (minimal packaging/export pipeline): ProjectPackagerUVE (pure filesystem
# copy - runtime binary + content root + the project's own .uveditor manifest into one
# distributable folder) and LoadAndActivateProjectSceneUVE (the EngineCoreUVE-side counterpart:
# loads a packaged/authored project's startup scene and activates its camera, used by both
# uve_runtime's own --project flag and this module's tests). Mirrors ProjectCheck's own
# lightweight standalone-tool layout (Expose/Internal/app), but needs uve_core (not just uve_asset)
# since the launcher half drives a real EngineCoreUVE/Scene::SceneSerializerUVE.

add_library(uve_pack_lib STATIC
    Internal/project_packager_uve.cpp
    Internal/project_launcher_uve.cpp
)
target_include_directories(uve_pack_lib PUBLIC Expose)
target_link_libraries(uve_pack_lib PUBLIC uve_core)
target_compile_features(uve_pack_lib PUBLIC cxx_std_20)
uve_set_warnings(uve_pack_lib)

add_executable(uve_pack app/main.cpp)
target_link_libraries(uve_pack PRIVATE uve_pack_lib)
target_compile_features(uve_pack PRIVATE cxx_std_20)
uve_set_warnings(uve_pack)
