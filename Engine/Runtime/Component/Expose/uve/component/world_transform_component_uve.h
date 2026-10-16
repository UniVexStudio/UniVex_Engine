// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/component/transform_component_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/trs_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// The cached, derived world-space position/rotation/scale of a scene-graph entity — internal
/// infrastructure SceneGraphUVE owns, not one of the master spec's named built-in components.
/// Kept as a component separate from TransformComponentUVE so setting the authored local
/// transform never has to touch this cached data (and vice versa). `dirty` is set whenever the
/// authored local transform or the parent link changes (see SceneGraphUVE::
/// SetLocalTransformUVE()/SetParentUVE()); SceneGraphUVE::UpdateUVE() clears it after
/// recomputing worldPosition/worldRotation/worldScale from the parent chain. Ordinary ECS
/// Get/ForEach/View access exposes a const live component; SceneGraphUVE alone can obtain the
/// mutable derived-cache access key. The mutable dirty bit is an invalidation request, not a
/// second world-pose writer. Initial value construction stays compatible; no serialized layout
/// changes (this derived cache is never serialized).
struct WorldTransformComponentUVE final {
    Math::Vector3UVE worldPosition{};
    Math::QuaternionUVE worldRotation{};
    Math::Vector3UVE worldScale{1.0F, 1.0F, 1.0F};
    mutable bool dirty = true;
};

/// True when `worldPosition`, `worldRotation`, and `worldScale` are all finite.
[[nodiscard]] bool IsWorldTransformComponentValidUVE(
    const WorldTransformComponentUVE& transform) noexcept;

/// Compares the cached world pose (position, rotation, scale) while ignoring the `dirty` flag.
[[nodiscard]] bool HasSameWorldPoseUVE(const WorldTransformComponentUVE& lhs,
                                       const WorldTransformComponentUVE& rhs) noexcept;

/// Converts the cached world pose into a `Math::TrsUVE` value.
[[nodiscard]] Math::TrsUVE ToWorldTrsUVE(const WorldTransformComponentUVE& transform) noexcept;

/// Builds a `WorldTransformComponentUVE` from a `Math::TrsUVE` value.
[[nodiscard]] WorldTransformComponentUVE FromWorldTrsUVE(const Math::TrsUVE& trs,
                                                         bool dirty = false) noexcept;

/// Copies a local transform directly as a root/top-level world transform candidate.
[[nodiscard]] WorldTransformComponentUVE MakeWorldTransformFromLocalUVE(
    const TransformComponentUVE& local, bool dirty = false) noexcept;

/// Composes `local` under `parentWorld` using the scene graph's TRS composition rule.
[[nodiscard]] WorldTransformComponentUVE ComposeWorldTransformUVE(
    const WorldTransformComponentUVE& parentWorld, const TransformComponentUVE& local,
    bool dirty = false) noexcept;

/// Builds the 4x4 world matrix (`ComposeTrsUVE`) for `transform`.
[[nodiscard]] Math::Matrix4x4UVE BuildWorldTransformMatrixUVE(
    const WorldTransformComponentUVE& transform) noexcept;

/// Transforms a local-space point into world space using `transform`.
[[nodiscard]] Math::Vector3UVE TransformWorldPointUVE(
    const WorldTransformComponentUVE& transform, const Math::Vector3UVE& localPoint) noexcept;

/// Transforms a world-space point into `transform`'s local space when invertible.
[[nodiscard]] bool TryInverseTransformWorldPointUVE(
    const WorldTransformComponentUVE& transform, const Math::Vector3UVE& worldPoint,
    Math::Vector3UVE& outLocalPoint) noexcept;

/// Resolves the unit world-space forward direction (`-Z` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3UVE ResolveWorldForwardUVE(
    const WorldTransformComponentUVE& transform) noexcept;

/// Resolves the unit world-space right direction (`+X` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3UVE ResolveWorldRightUVE(
    const WorldTransformComponentUVE& transform) noexcept;

/// Resolves the unit world-space up direction (`+Y` rotated by `worldRotation`).
[[nodiscard]] Math::Vector3UVE ResolveWorldUpUVE(
    const WorldTransformComponentUVE& transform) noexcept;

} // namespace UVE::Scene
