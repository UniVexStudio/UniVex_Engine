// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Pushes this object's transform channels (`position`, `rotation`, `scale`) onto another
/// target entity each frame before scene-graph world-transform propagation. When
/// `useGlobalCoordinates` is true, the source's `WorldTransformComponentUVE` is copied onto a
/// root target's local transform (and cached world transform); when false, the source's local
/// `TransformComponentUVE` is copied directly.
struct RemoteTransform3DComponentUVE final {
    EntityUVE remoteTarget = kInvalidEntityUVE;
    bool useGlobalCoordinates = true;
    bool updatePosition = true;
    bool updateRotation = true;
    bool updateScale = true;
    bool enabled = true;

    [[nodiscard]] bool operator==(const RemoteTransform3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsRemoteTransform3DComponentValidUVE(
    const RemoteTransform3DComponentUVE& value) noexcept;

/// True when `enabled` is true, `remoteTarget` is valid/non-self, and at least one channel is enabled.
[[nodiscard]] bool HasRemoteTransform3DActiveChannelUVE(
    const RemoteTransform3DComponentUVE& value,
    EntityUVE selfEntity = kInvalidEntityUVE) noexcept;

struct RemoteTransform3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "RemoteTransform3D";
    RemoteTransform3DComponentUVE remoteTransform{};
};

[[nodiscard]] bool IsRemoteTransform3DObjectDefinitionValidUVE(
    const RemoteTransform3DObjectDefinitionUVE& value) noexcept;

void ApplyRemoteTransform3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const RemoteTransform3DObjectDefinitionUVE& value);

/// Pushes every enabled `RemoteTransform3DComponentUVE` onto its `remoteTarget` in deterministic
/// entity order and returns the number of targets updated.
std::size_t UpdateRemoteTransform3DObjectsUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
