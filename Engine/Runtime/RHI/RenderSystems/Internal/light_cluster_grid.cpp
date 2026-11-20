// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "light_cluster_grid.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "quaternion.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

constexpr float kMinimumClusterNearUVE = 0.001F;
constexpr std::uint32_t kMaxLightsPerClusterUVE = 64U;

[[nodiscard]] Math::Vector3UVE TransformPointMatrixUVE(const Math::Matrix4x4UVE& matrix,
                                                       const Math::Vector3UVE& point) noexcept {
    return Math::Vector3UVE{
        matrix.m[0][0] * point.x + matrix.m[0][1] * point.y + matrix.m[0][2] * point.z + matrix.m[0][3],
        matrix.m[1][0] * point.x + matrix.m[1][1] * point.y + matrix.m[1][2] * point.z + matrix.m[1][3],
        matrix.m[2][0] * point.x + matrix.m[2][1] * point.y + matrix.m[2][2] * point.z + matrix.m[2][3],
    };
}

[[nodiscard]] bool IsLightVisibleInHierarchyUVE(Scene::IEntityManagerUVE& entityManager,
                                                const Scene::EntityUVE entity) {
    if (!entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        return true;
    }
    const Scene::VisibilityComponentUVE& vis = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
    return vis.visible && vis.resolvedVisibleInHierarchy;
}

} // namespace

std::uint32_t ComputeLightClusterSliceUVE(const float positiveViewDepth, const float nearPlane, const float farPlane,
                                          const std::uint32_t sliceCount) noexcept {
    if (sliceCount <= 1U) {
        return 0U;
    }
    const float safeNear = std::max(nearPlane, kMinimumClusterNearUVE);
    const float safeFar = std::max(farPlane, safeNear + 0.01F);
    if (!std::isfinite(positiveViewDepth) || positiveViewDepth <= safeNear) {
        return 0U;
    }
    if (positiveViewDepth >= safeFar) {
        return sliceCount - 1U;
    }
    const float ratio = std::log(positiveViewDepth / safeNear) / std::log(safeFar / safeNear);
    const float scaled = std::clamp(ratio * static_cast<float>(sliceCount), 0.0F,
                                    static_cast<float>(sliceCount - 1U));
    return static_cast<std::uint32_t>(scaled);
}

std::vector<LightDataUVE> ExtractClusteredLightsForViewUVE(Scene::IEntityManagerUVE& entityManager,
                                                           const Math::Vector3UVE& viewPosition,
                                                           const std::size_t maxLights) {
    const std::size_t budget = std::min(maxLights, kMaxClusteredLightsUVE);
    if (budget == 0U) {
        return {};
    }

    struct RankedLightUVE final {
        LightDataUVE light{};
        float score = 0.0F;
        std::uint64_t orderKey = 0U;
    };

    std::vector<RankedLightUVE> candidates;
    candidates.reserve(64U);

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::LightComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::LightComponentUVE& lightComponent) {
            if (worldTransform.dirty || !IsLightVisibleInHierarchyUVE(entityManager, entity)) {
                return;
            }
            if (!std::isfinite(lightComponent.intensity) || lightComponent.intensity <= 0.0F) {
                return;
            }
            LightDataUVE data{};
            data.type = lightComponent.type;
            data.position = worldTransform.worldPosition;
            Math::QuaternionUVE normRot{};
            if (Math::TryNormalizeUVE(worldTransform.worldRotation, normRot)) {
                data.rotation = normRot;
                data.direction = Math::RotateVectorUVE(normRot, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
            }
            data.color = lightComponent.color;
            data.intensity = lightComponent.intensity;
            data.range = lightComponent.range;
            data.spotAngleDegrees = lightComponent.spotAngleDegrees;
            data.isEditorInternal =
                entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(entity);

            if (entityManager.HasComponentUVE<Scene::LightEmitterComponentUVE>(entity)) {
                const auto& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(entity);
                data.castsShadows = emitter.shadowEnabled;
                data.cullMask = emitter.cullMask;
                data.specular = emitter.specular;
                data.shadowMaxDistance = emitter.shadowMaxDistance;
                data.shadowSplitBlend = emitter.shadowSplitBlend;
                data.shadowDistanceFadeRange = emitter.shadowDistanceFadeRange;
                data.shadowBias = emitter.shadowBias >= 0.0F ? emitter.shadowBias * 0.025F : -1.0F;
                data.shadowNormalBias = emitter.shadowNormalBias;
                data.shadowOpacity = emitter.shadowOpacity;
                data.shadowBlur = emitter.shadowBlur;
            } else {
                data.castsShadows = (data.type == Scene::LightTypeUVE::Directional);
            }

            float score = 0.0F;
            if (data.type == Scene::LightTypeUVE::Directional) {
                score = 1.0e9F + data.intensity;
            } else {
                const float dist = Math::LengthUVE(data.position - viewPosition);
                if (data.range > 0.0F && dist > data.range) {
                    return;
                }
                const float safeDistSq = std::max(dist * dist, 0.25F);
                score = data.intensity / safeDistSq;
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(entity.generation) << 32U) | static_cast<std::uint64_t>(entity.index);
            candidates.push_back(RankedLightUVE{data, score, key});
        });

    std::sort(candidates.begin(), candidates.end(), [](const RankedLightUVE& a, const RankedLightUVE& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.orderKey < b.orderKey;
    });

    const std::size_t count = std::min(candidates.size(), budget);
    std::vector<LightDataUVE> result;
    result.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        result.push_back(candidates[i].light);
    }
    return result;
}

void BuildLightClusterGridUVE(const std::span<const LightDataUVE> lights, const LightShadowPlanUVE* const shadowPlan,
                              const Math::Matrix4x4UVE& viewMatrix, const Math::Matrix4x4UVE& projectionMatrix,
                              const float nearPlane, const float farPlane, LightClusterGridDataUVE& outGrid) {
    const float safeNear = std::max(nearPlane, kMinimumClusterNearUVE);
    const float safeFar = std::max(farPlane, safeNear + 0.01F);
    outGrid.nearPlane = safeNear;
    outGrid.farPlane = safeFar;
    outGrid.overflowed = false;
    outGrid.lights.clear();
    outGrid.lightIndices.clear();
    outGrid.cells.fill(LightClusterCellUVE{});

    const std::size_t lightCount = std::min(lights.size(), kMaxClusteredLightsUVE);
    outGrid.activeLightCount = static_cast<std::uint32_t>(lightCount);
    if (lightCount == 0U) {
        return;
    }

    outGrid.lights.resize(lightCount);

    struct LightBoundsUVE final {
        std::uint32_t minX = 0U;
        std::uint32_t maxX = 0U;
        std::uint32_t minY = 0U;
        std::uint32_t maxY = 0U;
        std::uint32_t minZ = 0U;
        std::uint32_t maxZ = 0U;
        bool visible = false;
    };
    std::vector<LightBoundsUVE> bounds(lightCount);

    const float projX = std::abs(projectionMatrix.m[0][0]);
    const float projY = std::abs(projectionMatrix.m[1][1]);
    const bool isPerspective = std::abs(projectionMatrix.m[3][2]) > 1.0e-5F;

    for (std::size_t i = 0U; i < lightCount; ++i) {
        const LightDataUVE& src = lights[i];
        ClusteredGpuLightUVE& dst = outGrid.lights[i];
        dst.positionAndRange = {src.position.x, src.position.y, src.position.z, src.range};
        dst.directionAndType = {src.direction.x, src.direction.y, src.direction.z,
                                static_cast<float>(static_cast<std::int32_t>(src.type))};
        dst.colorAndIntensity = {src.color.r, src.color.g, src.color.b, src.intensity};
        std::int32_t encodedShadowSlot = -1;
        if (shadowPlan != nullptr && i < kMaxLightsUVE && shadowPlan->lightToShadowSlot[i] >= 0) {
            encodedShadowSlot = shadowPlan->lightToShadowSlot[i] + 1;
        }
        dst.spotMaskSpecShadow = {
            src.spotAngleDegrees,
            std::bit_cast<float>(src.cullMask),
            src.specular,
            static_cast<float>(encodedShadowSlot),
        };

        if (src.intensity <= 0.0F) {
            continue;
        }

        if (src.type == Scene::LightTypeUVE::Directional) {
            bounds[i] = LightBoundsUVE{0U, kLightClusterCountXUVE - 1U, 0U, kLightClusterCountYUVE - 1U,
                                       0U, kLightClusterCountZUVE - 1U, true};
            continue;
        }

        const float radius = src.range > 0.0F ? src.range : 100.0F;
        const Math::Vector3UVE viewPos = TransformPointMatrixUVE(viewMatrix, src.position);
        // Right-handed view space looks down -Z, so positive depth is -viewPos.z.
        const float centerDepth = -viewPos.z;
        if (centerDepth + radius < safeNear || centerDepth - radius > safeFar) {
            continue;
        }

        const float minDepth = std::max(centerDepth - radius, safeNear);
        const float maxDepth = std::min(centerDepth + radius, safeFar);
        const std::uint32_t minZ = ComputeLightClusterSliceUVE(minDepth, safeNear, safeFar);
        const std::uint32_t maxZ = ComputeLightClusterSliceUVE(maxDepth, safeNear, safeFar);

        float ndcMinX = -1.0F;
        float ndcMaxX = 1.0F;
        float ndcMinY = -1.0F;
        float ndcMaxY = 1.0F;
        if (isPerspective) {
            if (centerDepth - radius > safeNear) {
                const float invMinZ = 1.0F / std::max(centerDepth - radius, safeNear);
                ndcMinX = (viewPos.x - radius) * projX * invMinZ;
                ndcMaxX = (viewPos.x + radius) * projX * invMinZ;
                ndcMinY = (viewPos.y - radius) * projY * invMinZ;
                ndcMaxY = (viewPos.y + radius) * projY * invMinZ;
            }
        } else {
            ndcMinX = (viewPos.x - radius) * projX;
            ndcMaxX = (viewPos.x + radius) * projX;
            ndcMinY = (viewPos.y - radius) * projY;
            ndcMaxY = (viewPos.y + radius) * projY;
        }

        if (ndcMaxX < -1.0F || ndcMinX > 1.0F || ndcMaxY < -1.0F || ndcMinY > 1.0F) {
            continue;
        }

        const float u0 = std::clamp(ndcMinX * 0.5F + 0.5F, 0.0F, 1.0F);
        const float u1 = std::clamp(ndcMaxX * 0.5F + 0.5F, 0.0F, 1.0F);
        const float v0 = std::clamp(ndcMinY * 0.5F + 0.5F, 0.0F, 1.0F);
        const float v1 = std::clamp(ndcMaxY * 0.5F + 0.5F, 0.0F, 1.0F);

        bounds[i].minX = std::min(static_cast<std::uint32_t>(u0 * static_cast<float>(kLightClusterCountXUVE)),
                                  kLightClusterCountXUVE - 1U);
        bounds[i].maxX = std::min(static_cast<std::uint32_t>(u1 * static_cast<float>(kLightClusterCountXUVE)),
                                  kLightClusterCountXUVE - 1U);
        bounds[i].minY = std::min(static_cast<std::uint32_t>(v0 * static_cast<float>(kLightClusterCountYUVE)),
                                  kLightClusterCountYUVE - 1U);
        bounds[i].maxY = std::min(static_cast<std::uint32_t>(v1 * static_cast<float>(kLightClusterCountYUVE)),
                                  kLightClusterCountYUVE - 1U);
        bounds[i].minZ = minZ;
        bounds[i].maxZ = std::max(minZ, maxZ);
        bounds[i].visible = true;
    }

    // Two-pass count + prefix sum + scatter (zero per-cluster heap allocations).
    std::array<std::uint32_t, kTotalLightClustersUVE> counts{};
    for (std::size_t i = 0U; i < lightCount; ++i) {
        if (!bounds[i].visible) {
            continue;
        }
        for (std::uint32_t z = bounds[i].minZ; z <= bounds[i].maxZ; ++z) {
            const std::uint32_t zBase = z * kLightClusterCountXUVE * kLightClusterCountYUVE;
            for (std::uint32_t y = bounds[i].minY; y <= bounds[i].maxY; ++y) {
                const std::uint32_t rowBase = zBase + y * kLightClusterCountXUVE;
                for (std::uint32_t x = bounds[i].minX; x <= bounds[i].maxX; ++x) {
                    const std::uint32_t cluster = rowBase + x;
                    if (counts[cluster] < kMaxLightsPerClusterUVE) {
                        ++counts[cluster];
                    }
                }
            }
        }
    }

    std::uint32_t runningOffset = 0U;
    for (std::uint32_t cluster = 0U; cluster < kTotalLightClustersUVE; ++cluster) {
        const std::uint32_t desired = counts[cluster];
        const std::uint32_t available =
            runningOffset < kMaxClusterLightIndicesUVE ? (static_cast<std::uint32_t>(kMaxClusterLightIndicesUVE) - runningOffset) : 0U;
        const std::uint32_t granted = std::min(desired, available);
        if (granted < desired) {
            outGrid.overflowed = true;
        }
        outGrid.cells[cluster].offset = runningOffset;
        outGrid.cells[cluster].count = granted;
        runningOffset += granted;
    }

    outGrid.lightIndices.assign(runningOffset, 0U);
    std::array<std::uint32_t, kTotalLightClustersUVE> writeCursors{};
    for (std::size_t i = 0U; i < lightCount; ++i) {
        if (!bounds[i].visible) {
            continue;
        }
        for (std::uint32_t z = bounds[i].minZ; z <= bounds[i].maxZ; ++z) {
            const std::uint32_t zBase = z * kLightClusterCountXUVE * kLightClusterCountYUVE;
            for (std::uint32_t y = bounds[i].minY; y <= bounds[i].maxY; ++y) {
                const std::uint32_t rowBase = zBase + y * kLightClusterCountXUVE;
                for (std::uint32_t x = bounds[i].minX; x <= bounds[i].maxX; ++x) {
                    const std::uint32_t cluster = rowBase + x;
                    const std::uint32_t cursor = writeCursors[cluster];
                    if (cursor < outGrid.cells[cluster].count) {
                        outGrid.lightIndices[outGrid.cells[cluster].offset + cursor] = static_cast<std::uint32_t>(i);
                        writeCursors[cluster] = cursor + 1U;
                    }
                }
            }
        }
    }
}

} // namespace UVE::Render
