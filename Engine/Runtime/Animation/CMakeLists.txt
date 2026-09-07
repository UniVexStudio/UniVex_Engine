# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Started as a minimal partial port: only the data-model trio (TimePoseContractUVE,
# AnimationClipUVE, AnimationTreeUVE) that Engine/Runtime/Scene's AnimationTreeNode3D node facade
# needs to declare its own type. Grown here to add the rest of the animation system
# EngineCoreUVE needs: AnimationStateMachineUVE (playback scheduling over AnimationClipUVE),
# AnimationRetargetingUVE/RetargetMapUVE (both over TimePoseContractUVE) - all three physically
# colocated with EngineCoreUVE in the reference's engine/core/ folder despite depending on
# nothing outside this module, the same historical-colocation pattern already resolved for
# FutureTrajectoryContractUVE (ported alongside Physics). Depends only on uve_math.

add_library(uve_animation STATIC
    src/time_pose_contract_uve.cpp
    src/animation_clip_uve.cpp
    src/animation_tree_uve.cpp
    src/animation_state_machine_uve.cpp
    src/animation_retargeting_uve.cpp
    src/retarget_map_uve.cpp
)
target_include_directories(uve_animation PUBLIC include)
target_link_libraries(uve_animation PUBLIC uve_math PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_animation PUBLIC cxx_std_20)
uve_set_warnings(uve_animation)
