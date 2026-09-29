# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# uve_retarget: the humanoid reference (embedded from Data/humanoid_reference.json at build time),
# bone-name keys, matching any rig to the reference, and conforming skeletons, meshes and clips
# to it. nlohmann_json stays PRIVATE: no header here names a JSON type.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(uve_retarget STATIC
    Internal/retarget_names_uve.cpp
    Internal/retarget_skeleton_uve.cpp
    Internal/retarget_humanoid_uve.cpp
    Internal/retarget_match_uve.cpp
    Internal/retarget_pose_uve.cpp
    Internal/retarget_conform_uve.cpp
)

set(_uve_retarget_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_uve_retarget_reference_inc "${_uve_retarget_generated_dir}/humanoid_reference_json.inc")
add_custom_command(
    OUTPUT "${_uve_retarget_reference_inc}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Engine/Tools/embed_file.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/Data/humanoid_reference.json"
            "${_uve_retarget_reference_inc}"
            uve_retarget_humanoid_json
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Data/humanoid_reference.json"
            "${CMAKE_SOURCE_DIR}/Engine/Tools/embed_file.py"
    COMMENT "Embedding humanoid_reference.json -> humanoid_reference_json.inc"
    VERBATIM
)
target_sources(uve_retarget PRIVATE "${_uve_retarget_reference_inc}")
target_include_directories(uve_retarget PRIVATE "${_uve_retarget_generated_dir}" Internal)
target_include_directories(uve_retarget PUBLIC Expose)
target_link_libraries(uve_retarget PUBLIC uve_math uve_asset PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_retarget PUBLIC cxx_std_23)
uve_set_warnings(uve_retarget)

# Rebuilds Data/humanoid_reference.json from the source rig (an FBX kept outside the repo):
#   uve_retarget_make_reference <source.fbx> <Data/humanoid_reference.json>
add_executable(uve_retarget_make_reference Tools/make_humanoid_reference.cpp)
target_link_libraries(uve_retarget_make_reference PRIVATE uve_retarget uve_asset)
uve_set_warnings(uve_retarget_make_reference)
