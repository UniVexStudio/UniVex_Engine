// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "i_entity_manager.h"
#include "i_light_system.h"
#include "light_shadow_planner.h"
#include "matrix4x4.h"
#include "vector3.h"

namespace UVE::Render {

inline constexpr std::uint32_t kLightClusterCountX = 16U;
inline constexpr std::uint32_t kLightClusterCountY = 9U;
inline constexpr std::uint32_t kLightClusterCountZ = 24U;
inline constexpr std::uint32_t kTotalLightClusters =
    kLightClusterCountX * kLightClusterCountY * kLightClusterCountZ;

/// Maximum dynamic lights submitted per frame into the Forward+ clustered SSBO table.
inline constexpr std::size_t kMaxClusteredLights = 256U;
/// Maximum packed per-cluster light index entries across all 3,456 clusters.
inline constexpr std::size_t kMaxClusterLightIndices = 32'768U;

/// 64-byte std430-aligned GPU representation of one light for Forward+ clustered shading.
struct alignas(16) ClusteredGpuLight final {
    /// xyz = world position, w = attenuation range.
    std::array<float, 4> positionAndRange{0.0F, 0.0F, 0.0F, 10.0F};
    /// xyz = normalized world direction, w = float(LightType).
    std::array<float, 4> directionAndType{0.0F, -1.0F, 0.0F, 0.0F};
    /// rgb = linear light color, w = intensity.
    std::array<float, 4> colorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
    /// x = spotAngleDegrees, y = float(cullMask bits via intBitsToFloat), z = specular,
    /// w = encoded shadow slot (shadowSlot + 1 if shadowed, -1 otherwise).
    std::array<float, 4> spotMaskSpecShadow{30.0F, 0.0F, 1.0F, -1.0F};
};

static_assert(sizeof(ClusteredGpuLight) == 64U, "ClusteredGpuLight must match std430 64-byte stride");

/// 8-byte std430-aligned cluster header indexing into `lightIndices`.
struct alignas(8) LightClusterCell final {
    std::uint32_t offset = 0U;
    std::uint32_t count = 0U;
};

static_assert(sizeof(LightClusterCell) == 8U, "LightClusterCell must match std430 8-byte stride");

struct LightClusterGridData final {
    std::vector<ClusteredGpuLight> lights;
    std::array<LightClusterCell, kTotalLightClusters> cells{};
    std::vector<std::uint32_t> lightIndices;
    std::uint32_t activeLightCount = 0U;
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
    bool overflowed = false;
};

/// Logarithmic depth slice index in [0, sliceCount - 1] matching lit_shadowed_3d.glsl.
[[nodiscard]] std::uint32_t ComputeLightClusterSlice(float positiveViewDepth, float nearPlane, float farPlane,
                                                        std::uint32_t sliceCount = kLightClusterCountZ) noexcept;

/// Extracts up to `maxLights` active lights (clamped to kMaxClusteredLights), ranked by
/// radiometric contribution at `viewPosition` with directional lights prioritized first.
[[nodiscard]] std::vector<LightData> ExtractClusteredLightsForView(
    Scene::IEntityManager& entityManager, const Math::Vector3& viewPosition,
    std::size_t maxLights = kMaxClusteredLights);

/// Populates the 16x9x24 frustum cluster grid for `lights` in `outGrid`.
void BuildLightClusterGrid(std::span<const LightData> lights, const LightShadowPlan* shadowPlan,
                              const Math::Matrix4x4& viewMatrix, const Math::Matrix4x4& projectionMatrix,
                              float nearPlane, float farPlane, LightClusterGridData& outGrid);

} // namespace UVE::Render
