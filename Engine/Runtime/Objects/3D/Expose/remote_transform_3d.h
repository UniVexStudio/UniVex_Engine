// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Pushes this object's transform channels (`position`, `rotation`, `scale`) onto another
/// target entity each frame before scene-graph world-transform propagation. When
/// `useGlobalCoordinates` is true, the source's `WorldTransformComponent` is copied onto a
/// root target's local transform (and cached world transform); when false, the source's local
/// `TransformComponent` is copied directly.
struct RemoteTransform3DComponent final {
    Entity remoteTarget = kInvalidEntity;
    bool useGlobalCoordinates = true;
    bool updatePosition = true;
    bool updateRotation = true;
    bool updateScale = true;
    bool enabled = true;

    [[nodiscard]] bool operator==(const RemoteTransform3DComponent&) const = default;
};

[[nodiscard]] bool IsRemoteTransform3DComponentValid(
    const RemoteTransform3DComponent& value) noexcept;

/// True when `enabled` is true, `remoteTarget` is valid/non-self, and at least one channel is enabled.
[[nodiscard]] bool HasRemoteTransform3DActiveChannel(
    const RemoteTransform3DComponent& value,
    Entity selfEntity = kInvalidEntity) noexcept;

struct RemoteTransform3DObjectDefinition final {
    static constexpr std::string_view defaultName = "RemoteTransform3D";
    RemoteTransform3DComponent remoteTransform{};
};

[[nodiscard]] bool IsRemoteTransform3DObjectDefinitionValid(
    const RemoteTransform3DObjectDefinition& value) noexcept;

void ApplyRemoteTransform3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                               const RemoteTransform3DObjectDefinition& value);

/// Pushes every enabled `RemoteTransform3DComponent` onto its `remoteTarget` in deterministic
/// entity order and returns the number of targets updated.
std::size_t UpdateRemoteTransform3DObjects(IEntityManager& entityManager);

} // namespace UVE::Scene
