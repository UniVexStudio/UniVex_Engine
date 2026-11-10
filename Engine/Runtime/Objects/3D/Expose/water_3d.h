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

class IEntityManagerUVE;

/// Water3DComponent: real-time water surface rendering parameters for lakes, oceans, and rivers.
/// Drives Gerstner wave displacement, depth-based light absorption (Beer-Lambert), screen-space refraction,
/// shoreline contact foam, and surface specular response.
struct Water3DComponentUVE final {
    Math::Vector3UVE deepColor{0.02F, 0.08F, 0.20F};
    Math::Vector3UVE shallowColor{0.10F, 0.60F, 0.70F};
    Math::Vector3UVE foamColor{0.90F, 0.95F, 1.00F};

    float waveAmplitude = 0.15F;
    float waveFrequency = 0.80F;
    float waveSpeed = 1.00F;

    float clarity = 4.00F;
    float refractionStrength = 0.05F;
    float foamDepth = 0.30F;

    float metallic = 0.05F;
    float roughness = 0.05F;

    [[nodiscard]] bool operator==(const Water3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsWater3DComponentValidUVE(const Water3DComponentUVE& value) noexcept;

[[nodiscard]] Water3DComponentUVE SanitizeWater3DComponentUVE(const Water3DComponentUVE& value) noexcept;

/// Authoring definition for the Water3D scene object: attaches water component and a ground plane mesh.
struct Water3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Water3D";

    Water3DComponentUVE water{};
    PrimitiveMeshComponentUVE mesh{PrimitiveMeshKindUVE::Plane, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
};

[[nodiscard]] bool IsWater3DObjectDefinitionValidUVE(const Water3DObjectDefinitionUVE& value) noexcept;

void ApplyWater3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const Water3DObjectDefinitionUVE& value);

/// Policy and utility methods for Water3D simulation and queries (e.g. buoyant object queries).
class Water3DUVE final {
public:
    Water3DUVE() = delete;

    /// Evaluates Gerstner wave vertical height displacement at local position (x, z) at a given time.
    [[nodiscard]] static float EvaluateWaveHeightUVE(const Water3DComponentUVE& water,
                                                     const Math::Vector2UVE& xz, float time) noexcept;

    /// Evaluates Gerstner wave normal at local position (x, z) at a given time.
    [[nodiscard]] static Math::Vector3UVE EvaluateWaveNormalUVE(const Water3DComponentUVE& water,
                                                               const Math::Vector2UVE& xz, float time) noexcept;

    /// Returns the local-space bounding box enclosing the displaced water slab.
    [[nodiscard]] static Math::AabbUVE LocalBoundsUVE(const Water3DComponentUVE& water) noexcept;
};

} // namespace UVE::Scene
