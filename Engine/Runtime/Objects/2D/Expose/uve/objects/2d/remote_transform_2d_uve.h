// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Pushes this 2D node's X/Y position, Z-rotation, and X/Y scale channels onto `remoteTarget` each
/// frame.
struct RemoteTransform2DComponentUVE final {
    EntityUVE remoteTarget = kInvalidEntityUVE;
    bool useGlobalCoordinates = true;
    bool updatePosition = true;
    bool updateRotation = true;
    bool updateScale = true;
    bool enabled = true;

    [[nodiscard]] bool operator==(const RemoteTransform2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsRemoteTransform2DComponentValidUVE(
    const RemoteTransform2DComponentUVE& value) noexcept;

[[nodiscard]] bool HasRemoteTransform2DActiveChannelUVE(
    const RemoteTransform2DComponentUVE& value,
    EntityUVE selfEntity = kInvalidEntityUVE) noexcept;

struct RemoteTransform2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "RemoteTransform2D";
    static constexpr std::string_view defaultName = "RemoteTransform2D";
    RemoteTransform2DComponentUVE remoteTransform{};

    [[nodiscard]] bool operator==(const RemoteTransform2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsRemoteTransform2DObjectDefinitionValidUVE(
    const RemoteTransform2DObjectDefinitionUVE& value) noexcept;

void ApplyRemoteTransform2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const RemoteTransform2DObjectDefinitionUVE& value);

/// Pushes every enabled `RemoteTransform2DComponentUVE`'s 2D channels (`x/y` position, rotation,
/// `x/y` scale) onto its `remoteTarget` and returns the number of targets updated.
std::size_t UpdateRemoteTransform2DObjectsUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
