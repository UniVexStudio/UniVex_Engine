// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "entity.h"
#include "world_transform_component.h"
#include "aabb.h"
#include "frustum.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Tracks whether a local-space bounding box (`aabbCenterOffset` +/- `aabbHalfExtents`) intersects
/// the active 3D camera frustum each frame, pulsing `enteredScreenThisFrame` and
/// `exitedScreenThisFrame` on transitions so gameplay or scripts can pause off-screen effects or
/// trigger visibility callbacks.
struct VisibleOnScreenNotifier3DComponent final {
    Math::Vector3 aabbCenterOffset{0.0F, 0.0F, 0.0F};
    Math::Vector3 aabbHalfExtents{0.5F, 0.5F, 0.5F};
    bool enabled = true;
    /// Runtime state, never persisted: true while the notifier's world AABB intersects the camera frustum.
    bool isOnScreen = false;
    /// Runtime state, never persisted: true for the single tick the notifier enters the camera frustum.
    bool enteredScreenThisFrame = false;
    /// Runtime state, never persisted: true for the single tick the notifier leaves the camera frustum.
    bool exitedScreenThisFrame = false;

    [[nodiscard]] bool operator==(const VisibleOnScreenNotifier3DComponent&) const = default;
};

[[nodiscard]] bool IsVisibleOnScreenNotifier3DComponentValid(
    const VisibleOnScreenNotifier3DComponent& value) noexcept;

[[nodiscard]] VisibleOnScreenNotifier3DComponent SanitizeVisibleOnScreenNotifier3DComponent(
    const VisibleOnScreenNotifier3DComponent& value) noexcept;

/// Computes the world-space axis-aligned bounding box of `value` transformed by `worldTransform`.
[[nodiscard]] Math::Aabb ResolveVisibleOnScreenNotifier3DWorldAabb(
    const VisibleOnScreenNotifier3DComponent& value,
    const WorldTransformComponent& worldTransform) noexcept;

/// Advances `isOnScreen`, `enteredScreenThisFrame`, and `exitedScreenThisFrame` from `currentlyVisible`.
void StepVisibleOnScreenNotifier3DState(VisibleOnScreenNotifier3DComponent& value,
                                           bool currentlyVisible) noexcept;

struct VisibleOnScreenNotifier3DObjectDefinition final {
    static constexpr std::string_view defaultName = "VisibleOnScreenNotifier3D";
    VisibleOnScreenNotifier3DComponent notifier{};
};

[[nodiscard]] bool IsVisibleOnScreenNotifier3DObjectDefinitionValid(
    const VisibleOnScreenNotifier3DObjectDefinition& value) noexcept;

void ApplyVisibleOnScreenNotifier3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const VisibleOnScreenNotifier3DObjectDefinition& value);

/// Evaluates all `VisibleOnScreenNotifier3DComponent` entities against `activeCameraFrustum`
/// (`nullptr` marks all notifiers off-screen) and returns the number of currently visible notifiers.
std::size_t UpdateVisibleOnScreenNotifier3DObjects(
    IEntityManager& entityManager, const Math::Frustum* activeCameraFrustum);

} // namespace UVE::Scene
