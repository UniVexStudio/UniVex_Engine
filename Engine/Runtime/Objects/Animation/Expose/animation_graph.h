// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <functional>
#include <string_view>
#include <vector>

#include "asset_guid.h"
#include "animation_driver_component.h"
#include "animation_graph_component.h"
#include "entity.h"

namespace UVE::Asset {
struct AnimationClipAsset;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManager;
struct TransformComponent;
struct Skeleton3DComponent;

/// Authoring definition for the AnimationGraph object: a pure Object - no transform, no visibility -
/// whose Inspector is its own section, its AnimationDriver base, then the Object section. It evaluates
/// an animation graph onto the mixer's target (or its parent).
struct AnimationGraphObjectDefinition final {
    static constexpr std::string_view defaultName = "AnimationGraph";

    AnimationGraphComponent tree{};
    AnimationDriverComponent mixer{};
};

[[nodiscard]] bool IsAnimationGraphObjectDefinitionValid(const AnimationGraphObjectDefinition& value);

/// Applies the AnimationDriver base (ApplyAnimationDriverBase) and adds the tree when it is missing.
void ApplyAnimationGraphObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const AnimationGraphObjectDefinition& value);

/// The loaded clip for a guid, or null when it is not set, not loaded yet or failed.
using AnimationClipResolver = std::function<const Asset::AnimationClipAsset*(Asset::AssetGuid)>;

/// Advances the graph by `deltaSeconds` and writes the Output object's pose into `target` through the
/// channel masks. Object state lives in `tree.nodeStates` and is rebuilt (every object back to its
/// start) whenever the graph's shape changes. Triggers are consumed by the object or transition that
/// uses them. Returns true when `target` was written: an inactive tree, an invalid graph, or one
/// whose clips are all missing writes nothing.
[[nodiscard]] bool StepAnimationGraph(AnimationGraphComponent& tree, const AnimationClipResolver& clips,
                                        float deltaSeconds, TransformComponent& target,
                                        const AnimationDriverComponent& mixer = {});

/// Advances the graph by `deltaSeconds` and writes the Output object's pose into `skeleton.pose`: every
/// bone takes its track in each clip (its rest pose where a clip has none), and the graph's blends,
/// layers and transitions mix bone by bone through the mixer's channel masks.
/// - Root motion (mixer on): each clip keeps its root bone over its first frame's ground position
///   and reports its travel; the travel mixes like the pose and lands in `tree.rootMotionDelta`.
/// - State machine transitions follow the mixer's Transition: Inertialize hands over at once and
///   fades the difference out; Crossfade evaluates both states over the fade.
/// - Clip events passed by the clips that count at least half go to `tree.firedEvents`.
/// Returns true when the pose was written; false for an inactive tree, an invalid graph, a skeleton
/// with no bones, or a graph none of whose clips is loaded.
[[nodiscard]] bool StepSkeletalAnimationGraph(AnimationGraphComponent& tree, const AnimationClipResolver& clips,
                                                float deltaSeconds, Skeleton3DComponent& skeleton,
                                                const AnimationDriverComponent& mixer = {});

/// A Blend Space 1D's weight for each of its (rising) points at `at`: the two either side share it
/// by distance; before the first or past the last, that end takes everything.
[[nodiscard]] std::vector<float> AnimationBlendSpace1DWeights(const std::vector<float>& points, float at);

/// A Blend Space 2D's triangles: a Delaunay triangulation of its points (no triangle has another
/// point inside its circumcircle, so the triangles are as close to equilateral as the layout
/// allows), as index triples. Empty until three points stand off one line: a plane blends inside
/// triangles, so fewer points, or points all on one line, blend nothing.
[[nodiscard]] std::vector<std::array<std::uint32_t, 3>> TriangulateBlendSpace(const std::vector<Math::Vector2>& points);

/// A Blend Space 2D's weight for each of its points at `at`, adding up to one: the corners of the
/// triangle `at` is in, by its barycentric coordinates. Outside every triangle, the nearest place on
/// the triangles' edges is used, so the result slides along the outline. All zero without triangles.
[[nodiscard]] std::vector<float> AnimationBlendSpace2DWeights(const std::vector<Math::Vector2>& points,
                                                                 const std::vector<std::array<std::uint32_t, 3>>& triangles,
                                                                 Math::Vector2 at);

/// Moves `value` toward `goal` over `deltaSeconds` as a critically damped spring whose gap halves
/// every `halfLifeSeconds`, with `velocity` carried between calls. It never overshoots, and it lands
/// the same whatever the step sizes. A half-life of 0 jumps to the goal.
void SmoothBlendPosition(Math::Vector2& value, Math::Vector2& velocity, Math::Vector2 goal,
                            float halfLifeSeconds, float deltaSeconds) noexcept;

/// A crossfade's weight at `progress` (0..1, clamped) along `curve`: 0 at the start, 1 at the end.
[[nodiscard]] float AnimationTransitionCurveWeight(AnimationTransitionCurve curve, float progress) noexcept;

/// Puts every object back to its start: clips at 0, state machines in their entry state.
void ResetAnimationGraph(AnimationGraphComponent& tree);

/// Sets a parameter by name. Returns false when the tree has none by that name.
[[nodiscard]] bool SetAnimationGraphParameter(AnimationGraphComponent& tree, std::string_view name, float value);

/// Writes the parameters the engine feeds from a character (see AnimationParameterSource): its
/// ground speed (the velocity across `up`), its heading against `facing`'s forward (-Z) in degrees,
/// -180..180 and positive to the right, and whether it is in the air or on the ground. Returns how
/// many parameters it wrote.
std::size_t WriteCharacterAnimationParameters(AnimationGraphComponent& tree, const Math::Vector3& velocity,
                                                 const Math::Vector3& up, const Math::Quaternion& facing, bool grounded);

/// Sets the Vector3 parameter `name` (an IK effector, a look target, in the skeleton's space).
/// False when there is no such Vector3 parameter or the value is not finite.
[[nodiscard]] bool SetAnimationGraphVectorParameter(AnimationGraphComponent& tree, std::string_view name,
                                                       const Math::Vector3& value);

} // namespace UVE::Scene
