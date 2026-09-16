# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Started as a minimal partial port: only the data-model trio (TimePoseContractUVE,
# AnimationClipUVE, AnimationTreeUVE) that Engine/Runtime/Scene's AnimationTreeNode3D node facade
# needs to declare its own type. Grown here to add the rest of the animation system
# EngineCoreUVE needs: AnimationStateMachineUVE (playback scheduling over AnimationClipUVE) -
# physically colocated with EngineCoreUVE in the reference's engine/core/ folder despite depending
# on nothing outside this module. Depends only on uve_math.
#
# AnimationRetargetingUVE/RetargetMapUVE were ported here and then removed: retargeting is not
# functional runtime engine code - if/when real retargeting support lands, it belongs behind a
# plugin API boundary (matching the Motion Matching precedent), not as core runtime internals.

add_library(uve_animation STATIC
    Internal/time_pose_contract_uve.cpp
    Internal/animation_clip_uve.cpp
    Internal/animation_tree_uve.cpp
    Internal/animation_state_machine_uve.cpp
)
target_include_directories(uve_animation PUBLIC Expose)
target_link_libraries(uve_animation PUBLIC uve_math PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_animation PUBLIC cxx_std_20)
uve_set_warnings(uve_animation)
