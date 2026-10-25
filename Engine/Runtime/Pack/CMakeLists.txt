# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Roadmap item #7 (minimal packaging/export pipeline): ProjectPackagerUVE (pure filesystem
# copy - runtime binary + content root + the project's own .uvproject manifest into one
# distributable folder) and LoadAndActivateProjectSceneUVE (the EngineCoreUVE-side counterpart:
# loads a packaged/authored project's startup scene and activates its camera, used by both
# uve_runtime's own --project flag and this module's tests). Mirrors ProjectCheck's own
# lightweight standalone-tool layout (Expose/Internal/app), but needs uve_core (not just uve_asset)
# since the launcher half drives a real EngineCoreUVE/Scene::SceneSerializerUVE. EngineCoreUVE's
# Null-backed headless runtime is CPU-buildable too, so the packager and project loader can be
# tested without desktop GL.

add_library(uve_pack_lib STATIC
    Internal/asset_cooker_uve.cpp
    Internal/project_packager_uve.cpp
    Internal/project_launcher_uve.cpp
)
target_include_directories(uve_pack_lib PUBLIC Expose)
target_link_libraries(uve_pack_lib PUBLIC uve_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_pack_lib PUBLIC cxx_std_23)
uve_set_warnings(uve_pack_lib)

add_executable(uve_pack app/main.cpp)
target_link_libraries(uve_pack PRIVATE uve_pack_lib)
target_compile_features(uve_pack PRIVATE cxx_std_23)
uve_set_warnings(uve_pack)

if(NOT ANDROID)
    add_executable(uve_asset_workload app/asset_workload.cpp)
    target_link_libraries(uve_asset_workload PRIVATE uve_pack_lib nlohmann_json::nlohmann_json)
    target_compile_features(uve_asset_workload PRIVATE cxx_std_23)
    uve_set_warnings(uve_asset_workload)
endif()
