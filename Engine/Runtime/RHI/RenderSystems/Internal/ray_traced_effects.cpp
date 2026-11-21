// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ray_traced_effects.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "primitive_mesh_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

[[nodiscard]] bool IntersectRayBoxUVE(const Math::Vector3UVE& origin, const Math::Vector3UVE& invDir,
                                      const RtSceneBoxNodeUVE& box, const float tMin, const float tMax,
                                      float& outT, Math::Vector3UVE& outNormal) noexcept {
    const float tx0 = (box.minAndMetallic[0] - origin.x) * invDir.x;
    const float tx1 = (box.maxAndRoughness[0] - origin.x) * invDir.x;
    const float ty0 = (box.minAndMetallic[1] - origin.y) * invDir.y;
    const float ty1 = (box.maxAndRoughness[1] - origin.y) * invDir.y;
    const float tz0 = (box.minAndMetallic[2] - origin.z) * invDir.z;
    const float tz1 = (box.maxAndRoughness[2] - origin.z) * invDir.z;

    const float tminX = std::min(tx0, tx1);
    const float tmaxX = std::max(tx0, tx1);
    const float tminY = std::min(ty0, ty1);
    const float tmaxY = std::max(ty0, ty1);
    const float tminZ = std::min(tz0, tz1);
    const float tmaxZ = std::max(tz0, tz1);

    const float tEnter = std::max(std::max(tminX, tminY), std::max(tminZ, tMin));
    const float tExit = std::min(std::min(tmaxX, tmaxY), std::min(tmaxZ, tMax));
    if (tExit < tEnter || tEnter > tMax) {
        return false;
    }

    outT = tEnter;
    if (tEnter == tminX) {
        outNormal = Math::Vector3UVE{invDir.x < 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
    } else if (tEnter == tminY) {
        outNormal = Math::Vector3UVE{0.0F, invDir.y < 0.0F ? 1.0F : -1.0F, 0.0F};
    } else {
        outNormal = Math::Vector3UVE{0.0F, 0.0F, invDir.z < 0.0F ? 1.0F : -1.0F};
    }
    return true;
}

} // namespace

std::vector<RtSceneBoxNodeUVE> BuildRtSceneBoxesFromEntitiesUVE(Scene::IEntityManagerUVE& entityManager,
                                                                const std::size_t maxBoxes) {
    const std::size_t limit = std::min(maxBoxes, kMaxRtSceneBoxesUVE);
    std::vector<RtSceneBoxNodeUVE> boxes;
    boxes.reserve(64U);

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::PrimitiveMeshComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& wt,
            const Scene::PrimitiveMeshComponentUVE& prim) {
            if (boxes.size() >= limit || wt.dirty) {
                return;
            }
            if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
                const auto& vis = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
                if (!vis.visible || !vis.visibleInHierarchy) {
                    return;
                }
            }
            const Math::Vector3UVE half{
                std::max(std::abs(wt.worldScale.x) * 0.5F, 0.01F),
                std::max(std::abs(wt.worldScale.y) * 0.5F, 0.01F),
                std::max(std::abs(wt.worldScale.z) * 0.5F, 0.01F),
            };
            RtSceneBoxNodeUVE node{};
            node.minAndMetallic = {wt.worldPosition.x - half.x, wt.worldPosition.y - half.y,
                                   wt.worldPosition.z - half.z, 0.0F};
            node.maxAndRoughness = {wt.worldPosition.x + half.x, wt.worldPosition.y + half.y,
                                    wt.worldPosition.z + half.z, 0.5F};
            node.albedo = {prim.baseColor.x, prim.baseColor.y, prim.baseColor.z, 1.0F};
            boxes.push_back(node);
        });
    return boxes;
}

RtRayHitUVE TraceRayAgainstSceneBoxesUVE(const std::span<const RtSceneBoxNodeUVE> boxes,
                                         const Math::Vector3UVE& origin, const Math::Vector3UVE& direction,
                                         const float tMin, const float tMax) noexcept {
    RtRayHitUVE best{};
    const Math::Vector3UVE dir = Math::NormalizeUVE(direction);
    const Math::Vector3UVE invDir{
        std::abs(dir.x) > 1.0e-6F ? (1.0F / dir.x) : 1.0e6F,
        std::abs(dir.y) > 1.0e-6F ? (1.0F / dir.y) : 1.0e6F,
        std::abs(dir.z) > 1.0e-6F ? (1.0F / dir.z) : 1.0e6F,
    };

    float closestT = tMax;
    for (std::size_t i = 0U; i < boxes.size(); ++i) {
        float hitT = 0.0F;
        Math::Vector3UVE hitN{};
        if (IntersectRayBoxUVE(origin, invDir, boxes[i], tMin, closestT, hitT, hitN)) {
            closestT = hitT;
            best.hit = true;
            best.distance = hitT;
            best.boxIndex = static_cast<std::uint32_t>(i);
            best.normal = hitN;
            best.albedo = Math::Vector3UVE{boxes[i].albedo[0], boxes[i].albedo[1], boxes[i].albedo[2]};
            best.emissive = Math::Vector3UVE{boxes[i].emissive[0], boxes[i].emissive[1], boxes[i].emissive[2]};
        }
    }
    return best;
}

RtSurfaceResultUVE EvaluateRtShadowAndBounceUVE(const std::span<const RtSceneBoxNodeUVE> boxes,
                                                const Math::Vector3UVE& worldPos, const Math::Vector3UVE& normal,
                                                const Math::Vector3UVE& viewDir, const Math::Vector3UVE& sunDir,
                                                const Math::Vector3UVE& sunRadiance,
                                                const Math::Vector3UVE& skyColor) noexcept {
    RtSurfaceResultUVE result{};
    const Math::Vector3UVE n = Math::NormalizeUVE(normal);
    const Math::Vector3UVE origin = worldPos + n * 0.01F;

    // 1. Ray-traced shadow ray toward the sun.
    const Math::Vector3UVE sDir = Math::NormalizeUVE(sunDir);
    if (Math::DotUVE(n, sDir) > 0.0F) {
        const RtRayHitUVE shadowHit = TraceRayAgainstSceneBoxesUVE(boxes, origin, sDir, 0.005F, 250.0F);
        result.sunVisibility = shadowHit.hit ? 0.0F : 1.0F;
    }

    // 2. Ray-traced 1-bounce specular reflection ray.
    const Math::Vector3UVE v = Math::NormalizeUVE(viewDir);
    const Math::Vector3UVE reflDir = (n * (2.0F * Math::DotUVE(n, v))) - v;
    const RtRayHitUVE reflHit = TraceRayAgainstSceneBoxesUVE(boxes, origin, reflDir, 0.005F, 250.0F);
    if (reflHit.hit) {
        const float hitNdl = std::max(Math::DotUVE(reflHit.normal, sDir), 0.0F);
        result.reflectionRadiance =
            Math::Vector3UVE{reflHit.albedo.x * (skyColor.x * 0.25F + sunRadiance.x * hitNdl) + reflHit.emissive.x,
                             reflHit.albedo.y * (skyColor.y * 0.25F + sunRadiance.y * hitNdl) + reflHit.emissive.y,
                             reflHit.albedo.z * (skyColor.z * 0.25F + sunRadiance.z * hitNdl) + reflHit.emissive.z};
    } else {
        result.reflectionRadiance = skyColor;
    }

    // 3. Ray-traced 1-bounce diffuse GI ray along the surface normal.
    const RtRayHitUVE giHit = TraceRayAgainstSceneBoxesUVE(boxes, origin, n, 0.005F, 64.0F);
    if (giHit.hit) {
        const float hitNdl = std::max(Math::DotUVE(giHit.normal, sDir), 0.0F);
        result.giBounceRadiance =
            Math::Vector3UVE{giHit.albedo.x * sunRadiance.x * hitNdl * 0.35F + giHit.emissive.x,
                             giHit.albedo.y * sunRadiance.y * hitNdl * 0.35F + giHit.emissive.y,
                             giHit.albedo.z * sunRadiance.z * hitNdl * 0.35F + giHit.emissive.z};
    } else {
        result.giBounceRadiance = skyColor * 0.25F;
    }
    return result;
}

} // namespace UVE::Render
