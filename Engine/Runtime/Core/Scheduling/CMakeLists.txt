# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# FrameSchedulerUVE - a frame-task scheduler over IThreadPoolUVE, grouping registered tasks by
# FrameTaskDomainUVE (Animation/ECS/RenderPreparation/Physics/Streaming/Audio/Assets/Scripting/
# Editor) and running them with dependency ordering each frame.
#
# Physically colocated with EngineCoreUVE in the reference's engine/core/ folder (compiled into
# the same uve_core static library there) purely because it was authored alongside it - direct
# grep confirms EngineCoreUVE/EngineServicesUVE never include or reference FrameSchedulerUVE at
# all. Same "historical colocation, not a real dependency" pattern already resolved for
# FutureTrajectoryContractUVE (Physics), AnimationStateMachineUVE/AnimationRetargetingUVE/
# RetargetMapUVE (Animation), and TypeMetadataUVE (Object).
#
# Given its own logical home instead: checked real Unreal Engine source
# (Engine/Source/Runtime/Core/Public/Async/TaskGraph*) and confirmed frame/task scheduling
# infrastructure lives inside Unreal's own Core module, alongside Memory/Threading/HAL - not
# inside the separate Engine module that owns UEngine. Landed here as a new sibling of this
# repo's existing Core/{Logging,Memory,Threading,Math,Utilities} split rather than folded into
# Engine/Runtime/Engine (EngineCoreUVE's own new module) for that reason.

add_library(uve_scheduling STATIC
    src/frame_scheduler_uve.cpp
)
target_include_directories(uve_scheduling PUBLIC include)
target_link_libraries(uve_scheduling PUBLIC uve_threading)
target_compile_features(uve_scheduling PUBLIC cxx_std_20)
uve_set_warnings(uve_scheduling)
