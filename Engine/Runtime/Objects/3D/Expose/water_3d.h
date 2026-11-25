// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "entity.h"
#include "primitive_mesh_component.h"
#include "aabb.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Water3DComponent: real-time water surface rendering parameters for lakes, oceans, and rivers.
/// Drives Gerstner wave displacement, depth-based light absorption (Beer-Lambert), screen-space refraction,
/// shoreline contact foam, and surface specular response.
struct Water3DComponent final {
    Math::Vector3 deepColor{0.02F, 0.08F, 0.20F};
    Math::Vector3 shallowColor{0.10F, 0.60F, 0.70F};
    Math::Vector3 foamColor{0.90F, 0.95F, 1.00F};

    float waveAmplitude = 0.15F;
    float waveFrequency = 0.80F;
    float waveSpeed = 1.00F;

    float clarity = 4.00F;
    float refractionStrength = 0.05F;
    float foamDepth = 0.30F;

    float metallic = 0.05F;
    float roughness = 0.05F;

    [[nodiscard]] bool operator==(const Water3DComponent&) const = default;
};

[[nodiscard]] bool IsWater3DComponentValid(const Water3DComponent& value) noexcept;

[[nodiscard]] Water3DComponent SanitizeWater3DComponent(const Water3DComponent& value) noexcept;

/// Authoring definition for the Water3D scene object: attaches water component and a ground plane mesh.
struct Water3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Water3D";

    Water3DComponent water{};
    PrimitiveMeshComponent mesh{PrimitiveMeshKind::Plane, Math::Vector3{1.0F, 1.0F, 1.0F}};
};

[[nodiscard]] bool IsWater3DObjectDefinitionValid(const Water3DObjectDefinition& value) noexcept;

void ApplyWater3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                     const Water3DObjectDefinition& value);

/// Policy and utility methods for Water3D simulation and queries (e.g. buoyant object queries).
class Water3D final {
public:
    Water3D() = delete;

    /// Evaluates Gerstner wave vertical height displacement at local position (x, z) at a given time.
    [[nodiscard]] static float EvaluateWaveHeight(const Water3DComponent& water,
                                                     const Math::Vector2& xz, float time) noexcept;

    /// Evaluates Gerstner wave normal at local position (x, z) at a given time.
    [[nodiscard]] static Math::Vector3 EvaluateWaveNormal(const Water3DComponent& water,
                                                               const Math::Vector2& xz, float time) noexcept;

    /// Returns the local-space bounding box enclosing the displaced water slab.
    [[nodiscard]] static Math::Aabb LocalBounds(const Water3DComponent& water) noexcept;
};

} // namespace UVE::Scene
