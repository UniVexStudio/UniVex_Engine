// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "light_cluster_grid.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "directional_light_3d.h"
#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "quaternion.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

constexpr float kMinimumClusterNear = 0.001F;
constexpr std::uint32_t kMaxLightsPerCluster = 64U;

[[nodiscard]] Math::Vector3 TransformPointMatrix(const Math::Matrix4x4& matrix,
                                                       const Math::Vector3& point) noexcept {
    return Math::Vector3{
        matrix.m[0][0] * point.x + matrix.m[0][1] * point.y + matrix.m[0][2] * point.z + matrix.m[0][3],
        matrix.m[1][0] * point.x + matrix.m[1][1] * point.y + matrix.m[1][2] * point.z + matrix.m[1][3],
        matrix.m[2][0] * point.x + matrix.m[2][1] * point.y + matrix.m[2][2] * point.z + matrix.m[2][3],
    };
}

[[nodiscard]] bool IsLightVisibleInHierarchy(Scene::IEntityManager& entityManager,
                                                const Scene::Entity entity) {
    if (!entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
        return true;
    }
    const Scene::VisibilityComponent& vis = entityManager.GetComponent<Scene::VisibilityComponent>(entity);
    return vis.visible && vis.visibleInHierarchy;
}

} // namespace

std::uint32_t ComputeLightClusterSlice(const float positiveViewDepth, const float nearPlane, const float farPlane,
                                          const std::uint32_t sliceCount) noexcept {
    if (sliceCount <= 1U) {
        return 0U;
    }
    const float safeNear = std::max(nearPlane, kMinimumClusterNear);
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

std::vector<LightData> ExtractClusteredLightsForView(Scene::IEntityManager& entityManager,
                                                           const Math::Vector3& viewPosition,
                                                           const std::size_t maxLights) {
    const std::size_t budget = std::min(maxLights, kMaxClusteredLights);
    if (budget == 0U) {
        return {};
    }

    struct RankedLight final {
        LightData light{};
        float score = 0.0F;
        std::uint64_t orderKey = 0U;
    };

    std::vector<RankedLight> candidates;
    candidates.reserve(64U);

    entityManager.ForEach<Scene::WorldTransformComponent, Scene::LightComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
            const Scene::LightComponent& lightComponent) {
            if (worldTransform.dirty || !IsLightVisibleInHierarchy(entityManager, entity)) {
                return;
            }
            if (!std::isfinite(lightComponent.intensity) || lightComponent.intensity <= 0.0F) {
                return;
            }
            LightData data{};
            data.type = lightComponent.type;
            data.position = worldTransform.worldPosition;
            Math::Quaternion normRot{};
            if (Math::TryNormalize(worldTransform.worldRotation, normRot)) {
                data.rotation = normRot;
                data.direction = Math::RotateVector(normRot, Math::Vector3{0.0F, 0.0F, -1.0F});
            }
            data.color = lightComponent.color;
            data.intensity = lightComponent.intensity;
            data.range = lightComponent.range;
            data.spotAngleDegrees = lightComponent.spotAngleDegrees;
            data.isEditorInternal =
                entityManager.HasComponent<Scene::EditorInternalEntityComponent>(entity);

            if (entityManager.HasComponent<Scene::LightEmitterComponent>(entity)) {
                const auto& emitter = entityManager.GetComponent<Scene::LightEmitterComponent>(entity);
                data.castsShadows = emitter.shadowEnabled;
                data.cullMask = emitter.cullMask;
                data.specular = emitter.specular;
                data.shadowBias = emitter.shadowBias >= 0.0F ? emitter.shadowBias * 0.025F : -1.0F;
                data.shadowNormalBias = emitter.shadowNormalBias;
                data.shadowOpacity = emitter.shadowOpacity;
                data.shadowBlur = emitter.shadowBlur;
            } else {
                data.castsShadows = (data.type == Scene::LightType::Directional);
            }
            if (entityManager.HasComponent<Scene::DirectionalLight3DComponent>(entity)) {
                const auto& directional = entityManager.GetComponent<Scene::DirectionalLight3DComponent>(entity);
                data.shadowMaxDistance = directional.shadowMaxDistance;
                data.shadowSplitBlend = directional.shadowSplitBlend;
                data.shadowDistanceFadeRange = directional.shadowDistanceFadeRange;
            }

            float score = 0.0F;
            if (data.type == Scene::LightType::Directional) {
                score = 1.0e9F + data.intensity;
            } else {
                const float dist = Math::Length(data.position - viewPosition);
                const float safeDistSq = std::max(dist * dist, 0.25F);
                score = data.intensity / safeDistSq;
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(entity.generation) << 32U) | static_cast<std::uint64_t>(entity.index);
            candidates.push_back(RankedLight{data, score, key});
        });

    std::sort(candidates.begin(), candidates.end(), [](const RankedLight& a, const RankedLight& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.orderKey < b.orderKey;
    });

    const std::size_t count = std::min(candidates.size(), budget);
    std::vector<LightData> result;
    result.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        result.push_back(candidates[i].light);
    }
    return result;
}

void BuildLightClusterGrid(const std::span<const LightData> lights, const LightShadowPlan* const shadowPlan,
                              const Math::Matrix4x4& viewMatrix, const Math::Matrix4x4& projectionMatrix,
                              const float nearPlane, const float farPlane, LightClusterGridData& outGrid) {
    const float safeNear = std::max(nearPlane, kMinimumClusterNear);
    const float safeFar = std::max(farPlane, safeNear + 0.01F);
    outGrid.nearPlane = safeNear;
    outGrid.farPlane = safeFar;
    outGrid.overflowed = false;
    outGrid.lights.clear();
    outGrid.lightIndices.clear();
    outGrid.cells.fill(LightClusterCell{});

    const std::size_t lightCount = std::min(lights.size(), kMaxClusteredLights);
    outGrid.activeLightCount = static_cast<std::uint32_t>(lightCount);
    if (lightCount == 0U) {
        return;
    }

    outGrid.lights.resize(lightCount);

    struct LightBounds final {
        std::uint32_t minX = 0U;
        std::uint32_t maxX = 0U;
        std::uint32_t minY = 0U;
        std::uint32_t maxY = 0U;
        std::uint32_t minZ = 0U;
        std::uint32_t maxZ = 0U;
        bool visible = false;
    };
    std::vector<LightBounds> bounds(lightCount);

    const float projX = std::abs(projectionMatrix.m[0][0]);
    const float projY = std::abs(projectionMatrix.m[1][1]);
    const bool isPerspective = std::abs(projectionMatrix.m[3][2]) > 1.0e-5F;

    for (std::size_t i = 0U; i < lightCount; ++i) {
        const LightData& src = lights[i];
        ClusteredGpuLight& dst = outGrid.lights[i];
        dst.positionAndRange = {src.position.x, src.position.y, src.position.z, src.range};
        dst.directionAndType = {src.direction.x, src.direction.y, src.direction.z,
                                static_cast<float>(static_cast<std::int32_t>(src.type))};
        dst.colorAndIntensity = {src.color.r, src.color.g, src.color.b, src.intensity};
        std::int32_t encodedShadowSlot = -1;
        if (shadowPlan != nullptr && i < kMaxLights && shadowPlan->lightToShadowSlot[i] >= 0) {
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

        if (src.type == Scene::LightType::Directional) {
            bounds[i] = LightBounds{0U, kLightClusterCountX - 1U, 0U, kLightClusterCountY - 1U,
                                       0U, kLightClusterCountZ - 1U, true};
            continue;
        }

        const float radius = src.range > 0.0F ? src.range : 100.0F;
        const Math::Vector3 viewPos = TransformPointMatrix(viewMatrix, src.position);
        // Right-handed view space looks down -Z, so positive depth is -viewPos.z.
        const float centerDepth = -viewPos.z;
        if (centerDepth + radius < safeNear || centerDepth - radius > safeFar) {
            continue;
        }

        const float minDepth = std::max(centerDepth - radius, safeNear);
        const float maxDepth = std::min(centerDepth + radius, safeFar);
        const std::uint32_t minZ = ComputeLightClusterSlice(minDepth, safeNear, safeFar);
        const std::uint32_t maxZ = ComputeLightClusterSlice(maxDepth, safeNear, safeFar);

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

        bounds[i].minX = std::min(static_cast<std::uint32_t>(u0 * static_cast<float>(kLightClusterCountX)),
                                  kLightClusterCountX - 1U);
        bounds[i].maxX = std::min(static_cast<std::uint32_t>(u1 * static_cast<float>(kLightClusterCountX)),
                                  kLightClusterCountX - 1U);
        bounds[i].minY = std::min(static_cast<std::uint32_t>(v0 * static_cast<float>(kLightClusterCountY)),
                                  kLightClusterCountY - 1U);
        bounds[i].maxY = std::min(static_cast<std::uint32_t>(v1 * static_cast<float>(kLightClusterCountY)),
                                  kLightClusterCountY - 1U);
        bounds[i].minZ = minZ;
        bounds[i].maxZ = std::max(minZ, maxZ);
        bounds[i].visible = true;
    }

    // Two-pass count + prefix sum + scatter (zero per-cluster heap allocations).
    std::array<std::uint32_t, kTotalLightClusters> counts{};
    for (std::size_t i = 0U; i < lightCount; ++i) {
        if (!bounds[i].visible) {
            continue;
        }
        for (std::uint32_t z = bounds[i].minZ; z <= bounds[i].maxZ; ++z) {
            const std::uint32_t zBase = z * kLightClusterCountX * kLightClusterCountY;
            for (std::uint32_t y = bounds[i].minY; y <= bounds[i].maxY; ++y) {
                const std::uint32_t rowBase = zBase + y * kLightClusterCountX;
                for (std::uint32_t x = bounds[i].minX; x <= bounds[i].maxX; ++x) {
                    const std::uint32_t cluster = rowBase + x;
                    if (counts[cluster] < kMaxLightsPerCluster) {
                        ++counts[cluster];
                    }
                }
            }
        }
    }

    std::uint32_t runningOffset = 0U;
    for (std::uint32_t cluster = 0U; cluster < kTotalLightClusters; ++cluster) {
        const std::uint32_t desired = counts[cluster];
        const std::uint32_t available =
            runningOffset < kMaxClusterLightIndices ? (static_cast<std::uint32_t>(kMaxClusterLightIndices) - runningOffset) : 0U;
        const std::uint32_t granted = std::min(desired, available);
        if (granted < desired) {
            outGrid.overflowed = true;
        }
        outGrid.cells[cluster].offset = runningOffset;
        outGrid.cells[cluster].count = granted;
        runningOffset += granted;
    }

    outGrid.lightIndices.assign(runningOffset, 0U);
    std::array<std::uint32_t, kTotalLightClusters> writeCursors{};
    for (std::size_t i = 0U; i < lightCount; ++i) {
        if (!bounds[i].visible) {
            continue;
        }
        for (std::uint32_t z = bounds[i].minZ; z <= bounds[i].maxZ; ++z) {
            const std::uint32_t zBase = z * kLightClusterCountX * kLightClusterCountY;
            for (std::uint32_t y = bounds[i].minY; y <= bounds[i].maxY; ++y) {
                const std::uint32_t rowBase = zBase + y * kLightClusterCountX;
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
