// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Tracks whether a local-space bounding box (`aabbCenterOffset` +/- `aabbHalfExtents`) intersects
/// the active 3D camera frustum each frame, pulsing `enteredScreenThisFrame` and
/// `exitedScreenThisFrame` on transitions so gameplay or scripts can pause off-screen effects or
/// trigger visibility callbacks.
struct VisibleOnScreenNotifier3DComponentUVE final {
    Math::Vector3UVE aabbCenterOffset{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE aabbHalfExtents{0.5F, 0.5F, 0.5F};
    bool enabled = true;
    /// Runtime state, never persisted: true while the notifier's world AABB intersects the camera frustum.
    bool isOnScreen = false;
    /// Runtime state, never persisted: true for the single tick the notifier enters the camera frustum.
    bool enteredScreenThisFrame = false;
    /// Runtime state, never persisted: true for the single tick the notifier leaves the camera frustum.
    bool exitedScreenThisFrame = false;

    [[nodiscard]] bool operator==(const VisibleOnScreenNotifier3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsVisibleOnScreenNotifier3DComponentValidUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value) noexcept;

[[nodiscard]] VisibleOnScreenNotifier3DComponentUVE SanitizeVisibleOnScreenNotifier3DComponentUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value) noexcept;

/// Computes the world-space axis-aligned bounding box of `value` transformed by `worldTransform`.
[[nodiscard]] Math::AabbUVE ResolveVisibleOnScreenNotifier3DWorldAabbUVE(
    const VisibleOnScreenNotifier3DComponentUVE& value,
    const WorldTransformComponentUVE& worldTransform) noexcept;

/// Advances `isOnScreen`, `enteredScreenThisFrame`, and `exitedScreenThisFrame` from `currentlyVisible`.
void StepVisibleOnScreenNotifier3DStateUVE(VisibleOnScreenNotifier3DComponentUVE& value,
                                           bool currentlyVisible) noexcept;

struct VisibleOnScreenNotifier3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "VisibleOnScreenNotifier3D";
    VisibleOnScreenNotifier3DComponentUVE notifier{};
};

[[nodiscard]] bool IsVisibleOnScreenNotifier3DObjectDefinitionValidUVE(
    const VisibleOnScreenNotifier3DObjectDefinitionUVE& value) noexcept;

void ApplyVisibleOnScreenNotifier3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const VisibleOnScreenNotifier3DObjectDefinitionUVE& value);

/// Evaluates all `VisibleOnScreenNotifier3DComponentUVE` entities against `activeCameraFrustum`
/// (`nullptr` marks all notifiers off-screen) and returns the number of currently visible notifiers.
std::size_t UpdateVisibleOnScreenNotifier3DObjectsUVE(
    IEntityManagerUVE& entityManager, const Math::FrustumUVE* activeCameraFrustum);

} // namespace UVE::Scene
