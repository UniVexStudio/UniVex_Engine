// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Non-rendering 2D position/orientation hint in local or world 2D space (`positionOffset`,
/// `rotationRadiansOffset`, `gizmoExtents`).
struct Marker2DComponentUVE final {
    Math::Vector2UVE positionOffset{0.0F, 0.0F};
    float rotationRadiansOffset = 0.0F;
    float gizmoExtents = 10.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const Marker2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsMarker2DComponentValidUVE(const Marker2DComponentUVE& value) noexcept;

[[nodiscard]] Marker2DComponentUVE SanitizeMarker2DComponentUVE(
    const Marker2DComponentUVE& value) noexcept;

struct Marker2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Marker2D";
    static constexpr std::string_view defaultName = "Marker2D";
    Marker2DComponentUVE marker{};

    [[nodiscard]] bool operator==(const Marker2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsMarker2DObjectDefinitionValidUVE(
    const Marker2DObjectDefinitionUVE& value) noexcept;

void ApplyMarker2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Marker2DObjectDefinitionUVE& value);

class Marker2DUVE final {
public:
    Marker2DUVE() = delete;

    /// Composes `marker.positionOffset` with `entityPosition2D` rotated by `entityRotationRadians`.
    [[nodiscard]] static Math::Vector2UVE ComposeWorldPositionUVE(
        const Marker2DComponentUVE& marker, const Math::Vector2UVE& entityPosition2D,
        float entityRotationRadians = 0.0F) noexcept;

    /// Resolves the unit 2D forward (+X) direction for `entityRotationRadians + marker.rotationRadiansOffset`.
    [[nodiscard]] static Math::Vector2UVE ResolveForwardDirectionUVE(
        const Marker2DComponentUVE& marker, float entityRotationRadians = 0.0F) noexcept;
};

} // namespace UVE::Scene
