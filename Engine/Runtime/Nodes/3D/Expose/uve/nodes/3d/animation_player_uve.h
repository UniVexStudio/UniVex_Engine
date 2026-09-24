// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/animation/time_pose_contract_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManagerUVE;
struct TransformComponentUVE;

/// Authoring definition for the AnimationPlayer node: a pure Node - no transform, no visibility -
/// whose Inspector is its own section and then the Node section, exactly like the scene root's.
/// It plays a clip on another node (`target`, or its parent), so it can sit anywhere in the tree,
/// directly under the scene root included.
struct AnimationPlayerNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimationPlayer";

    AnimationPlayerComponentUVE player{};
};

[[nodiscard]] bool IsAnimationPlayerNodeDefinitionValidUVE(const AnimationPlayerNodeDefinitionUVE& value) noexcept;

/// Makes `entity` a pure Node (EnsureNodeBaselineUVE) and adds the player when it is missing.
void ApplyAnimationPlayerNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const AnimationPlayerNodeDefinitionUVE& value);

// ---- Playback ------------------------------------------------------------------------------------
// Pure functions over the component, the clip and the target's transform, so the whole behaviour
// is testable without an engine. EngineCoreUVE::SyncAnimationPlayersUVE drives them.

/// Starts playback from `startOffsetSeconds` (from the end when speed is negative), remembering
/// the target's current pose for the blend-in, relative playback and Return To Start.
void PlayAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const TransformComponentUVE& targetNow,
                            double clipDurationSeconds) noexcept;

/// Stops playback where it is. The target keeps its current pose.
void StopAnimationPlayerUVE(AnimationPlayerComponentUVE& player) noexcept;

/// Advances a playing player by `deltaSeconds` and writes the clip's pose into `target`, through the
/// channel masks, the blend-in and relative mode. Returns true when `target` was written. A clip
/// that is empty or invalid stops the player and writes nothing.
[[nodiscard]] bool StepAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const Asset::AnimationClipAssetUVE& clip,
                                          float deltaSeconds, TransformComponentUVE& target) noexcept;

/// The clip's pose at `timeSeconds`: linear position and scale, spherical rotation between the two
/// samples around it, clamped to the first and last. The clip must have at least one sample.
[[nodiscard]] Core::TransformPoseUVE SampleAnimationClipAssetUVE(const Asset::AnimationClipAssetUVE& clip,
                                                                double timeSeconds) noexcept;

/// Writes the chosen channels of `pose` into `target`. Rotation is normalized and mirrored into the
/// stored Euler angles, so the Inspector shows what is on screen.
void WriteAnimatedPoseUVE(const Core::TransformPoseUVE& pose, bool position, bool rotation, bool scale,
                          TransformComponentUVE& target) noexcept;

} // namespace UVE::Scene
