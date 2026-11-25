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

[[nodiscard]] bool IntersectRayBox(const Math::Vector3& origin, const Math::Vector3& invDir,
                                      const RtSceneBoxNode& box, const float tMin, const float tMax,
                                      float& outT, Math::Vector3& outNormal) noexcept {
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
        outNormal = Math::Vector3{invDir.x < 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
    } else if (tEnter == tminY) {
        outNormal = Math::Vector3{0.0F, invDir.y < 0.0F ? 1.0F : -1.0F, 0.0F};
    } else {
        outNormal = Math::Vector3{0.0F, 0.0F, invDir.z < 0.0F ? 1.0F : -1.0F};
    }
    return true;
}

} // namespace

std::vector<RtSceneBoxNode> BuildRtSceneBoxesFromEntities(Scene::IEntityManager& entityManager,
                                                                const std::size_t maxBoxes) {
    const std::size_t limit = std::min(maxBoxes, kMaxRtSceneBoxes);
    std::vector<RtSceneBoxNode> boxes;
    boxes.reserve(64U);

    entityManager.ForEach<Scene::WorldTransformComponent, Scene::PrimitiveMeshComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& wt,
            const Scene::PrimitiveMeshComponent& prim) {
            if (boxes.size() >= limit || wt.dirty) {
                return;
            }
            if (entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
                const auto& vis = entityManager.GetComponent<Scene::VisibilityComponent>(entity);
                if (!vis.visible || !vis.visibleInHierarchy) {
                    return;
                }
            }
            const Math::Vector3 half{
                std::max(std::abs(wt.worldScale.x) * 0.5F, 0.01F),
                std::max(std::abs(wt.worldScale.y) * 0.5F, 0.01F),
                std::max(std::abs(wt.worldScale.z) * 0.5F, 0.01F),
            };
            RtSceneBoxNode node{};
            node.minAndMetallic = {wt.worldPosition.x - half.x, wt.worldPosition.y - half.y,
                                   wt.worldPosition.z - half.z, 0.0F};
            node.maxAndRoughness = {wt.worldPosition.x + half.x, wt.worldPosition.y + half.y,
                                    wt.worldPosition.z + half.z, 0.5F};
            node.albedo = {prim.baseColor.x, prim.baseColor.y, prim.baseColor.z, 1.0F};
            boxes.push_back(node);
        });
    return boxes;
}

RtRayHit TraceRayAgainstSceneBoxes(const std::span<const RtSceneBoxNode> boxes,
                                         const Math::Vector3& origin, const Math::Vector3& direction,
                                         const float tMin, const float tMax) noexcept {
    RtRayHit best{};
    const Math::Vector3 dir = Math::Normalize(direction);
    const Math::Vector3 invDir{
        std::abs(dir.x) > 1.0e-6F ? (1.0F / dir.x) : 1.0e6F,
        std::abs(dir.y) > 1.0e-6F ? (1.0F / dir.y) : 1.0e6F,
        std::abs(dir.z) > 1.0e-6F ? (1.0F / dir.z) : 1.0e6F,
    };

    float closestT = tMax;
    for (std::size_t i = 0U; i < boxes.size(); ++i) {
        float hitT = 0.0F;
        Math::Vector3 hitN{};
        if (IntersectRayBox(origin, invDir, boxes[i], tMin, closestT, hitT, hitN)) {
            closestT = hitT;
            best.hit = true;
            best.distance = hitT;
            best.boxIndex = static_cast<std::uint32_t>(i);
            best.normal = hitN;
            best.albedo = Math::Vector3{boxes[i].albedo[0], boxes[i].albedo[1], boxes[i].albedo[2]};
            best.emissive = Math::Vector3{boxes[i].emissive[0], boxes[i].emissive[1], boxes[i].emissive[2]};
        }
    }
    return best;
}

RtSurfaceResult EvaluateRtShadowAndBounce(const std::span<const RtSceneBoxNode> boxes,
                                                const Math::Vector3& worldPos, const Math::Vector3& normal,
                                                const Math::Vector3& viewDir, const Math::Vector3& sunDir,
                                                const Math::Vector3& sunRadiance,
                                                const Math::Vector3& skyColor) noexcept {
    RtSurfaceResult result{};
    const Math::Vector3 n = Math::Normalize(normal);
    const Math::Vector3 origin = worldPos + n * 0.01F;

    // 1. Ray-traced shadow ray toward the sun.
    const Math::Vector3 sDir = Math::Normalize(sunDir);
    if (Math::Dot(n, sDir) > 0.0F) {
        const RtRayHit shadowHit = TraceRayAgainstSceneBoxes(boxes, origin, sDir, 0.005F, 250.0F);
        result.sunVisibility = shadowHit.hit ? 0.0F : 1.0F;
    }

    // 2. Ray-traced 1-bounce specular reflection ray.
    const Math::Vector3 v = Math::Normalize(viewDir);
    const Math::Vector3 reflDir = (n * (2.0F * Math::Dot(n, v))) - v;
    const RtRayHit reflHit = TraceRayAgainstSceneBoxes(boxes, origin, reflDir, 0.005F, 250.0F);
    if (reflHit.hit) {
        const float hitNdl = std::max(Math::Dot(reflHit.normal, sDir), 0.0F);
        result.reflectionRadiance =
            Math::Vector3{reflHit.albedo.x * (skyColor.x * 0.25F + sunRadiance.x * hitNdl) + reflHit.emissive.x,
                             reflHit.albedo.y * (skyColor.y * 0.25F + sunRadiance.y * hitNdl) + reflHit.emissive.y,
                             reflHit.albedo.z * (skyColor.z * 0.25F + sunRadiance.z * hitNdl) + reflHit.emissive.z};
    } else {
        result.reflectionRadiance = skyColor;
    }

    // 3. Ray-traced 1-bounce diffuse GI ray along the surface normal.
    const RtRayHit giHit = TraceRayAgainstSceneBoxes(boxes, origin, n, 0.005F, 64.0F);
    if (giHit.hit) {
        const float hitNdl = std::max(Math::Dot(giHit.normal, sDir), 0.0F);
        result.giBounceRadiance =
            Math::Vector3{giHit.albedo.x * sunRadiance.x * hitNdl * 0.35F + giHit.emissive.x,
                             giHit.albedo.y * sunRadiance.y * hitNdl * 0.35F + giHit.emissive.y,
                             giHit.albedo.z * sunRadiance.z * hitNdl * 0.35F + giHit.emissive.z};
    } else {
        result.giBounceRadiance = skyColor * 0.25F;
    }
    return result;
}

} // namespace UVE::Render
