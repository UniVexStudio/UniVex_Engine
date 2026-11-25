// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "transform_component.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "trs.h"
#include "vector3.h"

namespace UVE::Scene {

/// The cached, derived world-space position/rotation/scale of a scene-graph entity — internal
/// infrastructure SceneGraph owns, not one of the master spec's named built-in components.
/// Kept as a component separate from TransformComponent so setting the authored local
/// transform never has to touch this cached data (and vice versa). `dirty` is set whenever the
/// authored local transform or the parent link changes (see SceneGraph::
/// SetLocalTransform()/SetParent()); SceneGraph::Update() clears it after
/// recomputing worldPosition/worldRotation/worldScale from the parent chain. Ordinary ECS
/// Get/ForEach/View access exposes a const live component; SceneGraph alone can obtain the
/// mutable derived-cache access key. The mutable dirty bit is an invalidation request, not a
/// second world-pose writer. Initial value construction stays compatible; no serialized layout
/// changes (this derived cache is never serialized).
struct WorldTransformComponent final {
    Math::Vector3 worldPosition{};
    Math::Quaternion worldRotation{};
    Math::Vector3 worldScale{1.0F, 1.0F, 1.0F};
    mutable bool dirty = true;
};

/// True when `worldPosition`, `worldRotation`, and `worldScale` are all finite.
[[nodiscard]] bool IsWorldTransformComponentValid(
    const WorldTransformComponent& transform) noexcept;

/// Compares the cached world pose (position, rotation, scale) while ignoring the `dirty` flag.
[[nodiscard]] bool HasSameWorldPose(const WorldTransformComponent& lhs,
                                       const WorldTransformComponent& rhs) noexcept;

/// Converts the cached world pose into a `Math::Trs` value.
[[nodiscard]] Math::Trs ToWorldTrs(const WorldTransformComponent& transform) noexcept;

/// Builds a `WorldTransformComponent` from a `Math::Trs` value.
[[nodiscard]] WorldTransformComponent FromWorldTrs(const Math::Trs& trs,
                                                         bool dirty = false) noexcept;

/// Copies a local transform directly as a root/top-level world transform candidate.
[[nodiscard]] WorldTransformComponent MakeWorldTransformFromLocal(
    const TransformComponent& local, bool dirty = false) noexcept;

/// Composes `local` under `parentWorld` using the scene graph's TRS composition rule.
[[nodiscard]] WorldTransformComponent ComposeWorldTransform(
    const WorldTransformComponent& parentWorld, const TransformComponent& local,
    bool dirty = false) noexcept;

/// Builds the 4x4 world matrix (`ComposeTrs`) for `transform`.
[[nodiscard]] Math::Matrix4x4 BuildWorldTransformMatrix(
    const WorldTransformComponent& transform) noexcept;

/// Transforms a local-space point into world space using `transform`.
[[nodiscard]] Math::Vector3 TransformWorldPoint(
    const WorldTransformComponent& transform, const Math::Vector3& localPoint) noexcept;

/// Transforms a world-space point into `transform`'s local space when invertible.
[[nodiscard]] bool TryInverseTransformWorldPoint(
    const WorldTransformComponent& transform, const Math::Vector3& worldPoint,
    Math::Vector3& outLocalPoint) noexcept;

/// Resolves the unit world-space forward direction (`-Z` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3 ResolveWorldForward(
    const WorldTransformComponent& transform) noexcept;

/// Resolves the unit world-space right direction (`+X` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3 ResolveWorldRight(
    const WorldTransformComponent& transform) noexcept;

/// Resolves the unit world-space up direction (`+Y` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3 ResolveWorldUp(
    const WorldTransformComponent& transform) noexcept;

} // namespace UVE::Scene
