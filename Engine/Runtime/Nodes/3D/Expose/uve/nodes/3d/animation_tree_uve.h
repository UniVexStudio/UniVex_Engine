// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManagerUVE;
struct TransformComponentUVE;
struct Skeleton3DNodeComponentUVE;

/// Authoring definition for the AnimationTree node: a pure Node - no transform, no visibility -
/// whose Inspector is its own section, its AnimationMixer base, then the Node section. It evaluates
/// an animation graph onto the mixer's target (or its parent).
struct AnimationTreeNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimationTree";

    AnimationTreeComponentUVE tree{};
    AnimationMixerComponentUVE mixer{};
};

[[nodiscard]] bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value);

/// Applies the AnimationMixer base (ApplyAnimationMixerBaseUVE) and adds the tree when it is missing.
void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value);

/// The loaded clip for a guid, or null when it is not set, not loaded yet or failed.
using AnimationClipResolverUVE = std::function<const Asset::AnimationClipAssetUVE*(Asset::AssetGuidUVE)>;

/// Advances the graph by `deltaSeconds` and writes the Output node's pose into `target` through the
/// channel masks. Node state lives in `tree.nodeStates` and is rebuilt (every node back to its
/// start) whenever the graph's shape changes. Triggers are consumed by the node or transition that
/// uses them. Returns true when `target` was written: an inactive tree, an invalid graph, or one
/// whose clips are all missing writes nothing.
[[nodiscard]] bool StepAnimationTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                        float deltaSeconds, TransformComponentUVE& target,
                                        const AnimationMixerComponentUVE& mixer = {});

/// Advances the graph by `deltaSeconds` and writes the Output node's pose into `skeleton.pose`: every
/// bone takes its track in each clip (its rest pose where a clip has none), and the graph's blends,
/// layers and transitions mix bone by bone through the mixer's channel masks.
/// - Root motion (mixer on): each clip keeps its root bone over its first frame's ground position
///   and reports its travel; the travel mixes like the pose and lands in `tree.rootMotionDelta`.
/// - State machine transitions follow the mixer's Transition: Inertialize hands over at once and
///   fades the difference out; Crossfade evaluates both states over the fade.
/// - Clip events passed by the clips that count at least half go to `tree.firedEvents`.
/// Returns true when the pose was written; false for an inactive tree, an invalid graph, a skeleton
/// with no bones, or a graph none of whose clips is loaded.
[[nodiscard]] bool StepSkeletalAnimationTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                                float deltaSeconds, Skeleton3DNodeComponentUVE& skeleton,
                                                const AnimationMixerComponentUVE& mixer = {});

/// A Blend Space 2D's weight for each of its points at `at`, adding up to one: gradient band
/// interpolation, which suits points laid out anywhere (no grid or triangle layout needed). A
/// position on a point gives it everything.
[[nodiscard]] std::vector<float> AnimationBlendSpace2DWeightsUVE(const std::vector<Math::Vector2UVE>& points,
                                                                 Math::Vector2UVE at);

/// Puts every node back to its start: clips at 0, state machines in their entry state.
void ResetAnimationTreeUVE(AnimationTreeComponentUVE& tree);

/// Sets a parameter by name. Returns false when the tree has none by that name.
[[nodiscard]] bool SetAnimationTreeParameterUVE(AnimationTreeComponentUVE& tree, std::string_view name, float value);

} // namespace UVE::Scene
