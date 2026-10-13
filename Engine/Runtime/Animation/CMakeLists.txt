# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The animation DATA AND EVALUATION MODEL, in namespace UVE::Core. Started as a minimal partial
# port of the data-model trio (TimePoseContractUVE, AnimationClipUVE, PoseGraphUVE) that a Scene
# node facade was meant to declare its type in terms of, then grown to add
# AnimationStateMachineUVE (playback scheduling over AnimationClipUVE).
#
# THAT FACADE WAS NEVER BUILT. An earlier revision of this comment named it "PoseGraphObject3D";
# no such type exists in the tree. What exists instead is a separate, richer authored graph -
# Scene::AnimationGraphComponentUVE (uve_component), drawn by the editor and evaluated by
# Engine/Runtime/Objects/Animation/Internal/animation_graph_uve.cpp. The two are not the same
# thing and neither uses the other: this module addresses clips by string id with fixed inputA/
# inputB, while the authored graph uses asset GUIDs, N inputs, editor positions and blend spaces.
#
# So, as it stands, the only part of this module anything in production consumes is
# TransformPoseUVE (Engine/Runtime/Objects/Animation's sequencer and graph runtime). The rest -
# PoseGraphUVE, AnimationClipUVE, AnimationStateMachineUVE, UnifiedTimeContractUVE and the
# skeleton/pose-buffer data model - is tested (Test/Integration/Animation) but unwired. uve_scene
# links this library without including any of its headers.
#
# Kept rather than deleted: it is a coherent evaluation model, and Sync/Subtree/PoseCache are node
# kinds the authored graph does not have. Depends only on uve_math.
#
# AnimationRetargetingUVE/RetargetMapUVE were ported here and then removed. Retargeting now lives
# in its own module, Engine/Runtime/Retarget (uve_retarget), so this one stays free of rig
# knowledge.

add_library(uve_animation STATIC
    Internal/time_pose_contract_uve.cpp
    Internal/animation_clip_uve.cpp
    Internal/pose_graph_uve.cpp
    Internal/animation_state_machine_uve.cpp
)
target_include_directories(uve_animation PUBLIC Expose)
target_link_libraries(uve_animation PUBLIC uve_math PRIVATE nlohmann_json::nlohmann_json uve_utilities)
target_compile_features(uve_animation PUBLIC cxx_std_23)
uve_set_warnings(uve_animation)
