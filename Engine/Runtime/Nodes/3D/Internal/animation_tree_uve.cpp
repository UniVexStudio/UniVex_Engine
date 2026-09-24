// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_tree_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUsableClipUVE(const Asset::AnimationClipAssetUVE* const clip) noexcept {
    return clip != nullptr && !clip->samples.empty() && std::isfinite(clip->durationSeconds) &&
           clip->durationSeconds > 0.0;
}

/// Wraps `seconds` into [0, duration).
[[nodiscard]] double WrapUVE(const double seconds, const double duration) noexcept {
    double wrapped = std::fmod(seconds, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return wrapped;
}

} // namespace

bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) noexcept {
    return IsAnimationTreeComponentValidUVE(value.tree);
}

void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value) {
    EnsureNodeBaselineUVE(entityManager, entity, AnimationTreeNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationTreeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationTreeComponentUVE>(entity, value.tree);
    }
}

bool StepAnimationTreeUVE(AnimationTreeComponentUVE& tree, const Asset::AnimationClipAssetUVE* const clipA,
                          const Asset::AnimationClipAssetUVE* const clipB, const float deltaSeconds,
                          TransformComponentUVE& target) noexcept {
    const bool hasA = IsUsableClipUVE(clipA);
    const bool hasB = IsUsableClipUVE(clipB);
    if (!tree.active || (!hasA && !hasB) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return false;
    }

    // The applied blend chases the authored one, so a script or the Inspector can move Blend in
    // one jump and the character still eases between clips.
    const float goal = std::clamp(tree.blend, 0.0F, 1.0F);
    if (!tree.started || tree.blendSmoothing <= 0.0F) {
        tree.currentBlend = goal;
    } else {
        const float stepSize = tree.blendSmoothing * deltaSeconds;
        tree.currentBlend += std::clamp(goal - tree.currentBlend, -stepSize, stepSize);
    }
    tree.started = true;

    // A missing clip leaves the other playing alone, whatever the blend says.
    const float weight = !hasA ? 1.0F : (!hasB ? 0.0F : tree.currentBlend);
    const double advance = static_cast<double>(tree.speed) * static_cast<double>(deltaSeconds);

    double secondsA = 0.0;
    double secondsB = 0.0;
    if (tree.syncPhase && hasA && hasB) {
        // One shared phase: its length is the weighted mix of the two clips, so the cycle stretches
        // smoothly from walk to run and both clips land their feet together.
        const double length = (1.0 - weight) * clipA->durationSeconds + weight * clipB->durationSeconds;
        const double phase = WrapUVE(static_cast<double>(tree.timeA) + advance / length, 1.0);
        tree.timeA = static_cast<float>(phase);
        secondsA = phase * clipA->durationSeconds;
        secondsB = phase * clipB->durationSeconds;
    } else {
        if (hasA) {
            secondsA = WrapUVE(static_cast<double>(tree.timeA) + advance, clipA->durationSeconds);
            tree.timeA = static_cast<float>(secondsA);
        }
        if (hasB) {
            secondsB = WrapUVE(static_cast<double>(tree.timeB) + advance, clipB->durationSeconds);
            tree.timeB = static_cast<float>(secondsB);
        }
    }

    Core::TransformPoseUVE pose;
    if (!hasB || weight <= 0.0F) {
        pose = SampleAnimationClipAssetUVE(*clipA, secondsA);
    } else if (!hasA || weight >= 1.0F) {
        pose = SampleAnimationClipAssetUVE(*clipB, secondsB);
    } else {
        const Core::TransformPoseUVE a = SampleAnimationClipAssetUVE(*clipA, secondsA);
        const Core::TransformPoseUVE b = SampleAnimationClipAssetUVE(*clipB, secondsB);
        pose.position = a.position + (b.position - a.position) * weight;
        pose.scale = a.scale + (b.scale - a.scale) * weight;
        if (!Math::TrySlerpUVE(a.rotation, b.rotation, weight, pose.rotation)) {
            pose.rotation = weight < 0.5F ? a.rotation : b.rotation;
        }
    }
    WriteAnimatedPoseUVE(pose, tree.animatePosition, tree.animateRotation, tree.animateScale, target);
    return true;
}

} // namespace UVE::Scene
