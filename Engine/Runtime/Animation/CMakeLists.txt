# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Minimal, honest partial port: only the data-model trio (TimePoseContractUVE, AnimationClipUVE,
# AnimationTreeUVE) that Engine/Runtime/Scene's AnimationTreeNode3D node facade needs to declare
# its own type. Depends only on uve_math. The full animation SYSTEM (playback scheduling,
# AnimationStateMachineUVE, retargeting) is out of scope for this slice and stays in the
# reference repo - this is not a stand-in for that system, just the data types this slice's
# already-ported Scene node catalog references.

add_library(uve_animation STATIC
    src/time_pose_contract_uve.cpp
    src/animation_clip_uve.cpp
    src/animation_tree_uve.cpp
)
target_include_directories(uve_animation PUBLIC include)
target_link_libraries(uve_animation PUBLIC uve_math)
target_compile_features(uve_animation PUBLIC cxx_std_20)
uve_set_warnings(uve_animation)
