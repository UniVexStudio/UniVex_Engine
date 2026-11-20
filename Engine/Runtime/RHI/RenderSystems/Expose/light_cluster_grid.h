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

inline constexpr std::uint32_t kLightClusterCountXUVE = 16U;
inline constexpr std::uint32_t kLightClusterCountYUVE = 9U;
inline constexpr std::uint32_t kLightClusterCountZUVE = 24U;
inline constexpr std::uint32_t kTotalLightClustersUVE =
    kLightClusterCountXUVE * kLightClusterCountYUVE * kLightClusterCountZUVE;

/// Maximum dynamic lights submitted per frame into the Forward+ clustered SSBO table.
inline constexpr std::size_t kMaxClusteredLightsUVE = 256U;
/// Maximum packed per-cluster light index entries across all 3,456 clusters.
inline constexpr std::size_t kMaxClusterLightIndicesUVE = 32'768U;

/// 64-byte std430-aligned GPU representation of one light for Forward+ clustered shading.
struct alignas(16) ClusteredGpuLightUVE final {
    /// xyz = world position, w = attenuation range.
    std::array<float, 4> positionAndRange{0.0F, 0.0F, 0.0F, 10.0F};
    /// xyz = normalized world direction, w = float(LightTypeUVE).
    std::array<float, 4> directionAndType{0.0F, -1.0F, 0.0F, 0.0F};
    /// rgb = linear light color, w = intensity.
    std::array<float, 4> colorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
    /// x = spotAngleDegrees, y = float(cullMask bits via intBitsToFloat), z = specular,
    /// w = encoded shadow slot (shadowSlot + 1 if shadowed, -1 otherwise).
    std::array<float, 4> spotMaskSpecShadow{30.0F, 0.0F, 1.0F, -1.0F};
};

static_assert(sizeof(ClusteredGpuLightUVE) == 64U, "ClusteredGpuLightUVE must match std430 64-byte stride");

/// 8-byte std430-aligned cluster header indexing into `lightIndices`.
struct alignas(8) LightClusterCellUVE final {
    std::uint32_t offset = 0U;
    std::uint32_t count = 0U;
};

static_assert(sizeof(LightClusterCellUVE) == 8U, "LightClusterCellUVE must match std430 8-byte stride");

struct LightClusterGridDataUVE final {
    std::vector<ClusteredGpuLightUVE> lights;
    std::array<LightClusterCellUVE, kTotalLightClustersUVE> cells{};
    std::vector<std::uint32_t> lightIndices;
    std::uint32_t activeLightCount = 0U;
    float nearPlane = 0.1F;
    float farPlane = 1000.0F;
    bool overflowed = false;
};

/// Logarithmic depth slice index in [0, sliceCount - 1] matching lit_shadowed_3d.glsl.
[[nodiscard]] std::uint32_t ComputeLightClusterSliceUVE(float positiveViewDepth, float nearPlane, float farPlane,
                                                        std::uint32_t sliceCount = kLightClusterCountZUVE) noexcept;

/// Extracts up to `maxLights` active lights (clamped to kMaxClusteredLightsUVE), ranked by
/// radiometric contribution at `viewPosition` with directional lights prioritized first.
[[nodiscard]] std::vector<LightDataUVE> ExtractClusteredLightsForViewUVE(
    Scene::IEntityManagerUVE& entityManager, const Math::Vector3UVE& viewPosition,
    std::size_t maxLights = kMaxClusteredLightsUVE);

/// Populates the 16x9x24 frustum cluster grid for `lights` in `outGrid`.
void BuildLightClusterGridUVE(std::span<const LightDataUVE> lights, const LightShadowPlanUVE* shadowPlan,
                              const Math::Matrix4x4UVE& viewMatrix, const Math::Matrix4x4UVE& projectionMatrix,
                              float nearPlane, float farPlane, LightClusterGridDataUVE& outGrid);

} // namespace UVE::Render
