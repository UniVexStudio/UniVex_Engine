// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/occluder_3d_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsOccluder3DNodeComponentValidUVE(const Occluder3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F &&
           value.mode == Occluder3DNodeModeUVE::ConservativeBox;
}

bool ResolveOccluder3DFullyHiddenUVE(const Occluder3DNodeComponentUVE& config,
                                     const Math::Vector3UVE& occluderWorldPosition,
                                     const Math::Vector3UVE& viewerWorldPosition,
                                     const Math::Vector3UVE& pointWorld) noexcept {
    if (!IsOccluder3DNodeComponentValidUVE(config)) {
        return false;
    }
    // The strictly-contained check is INCLUSIVE on the surface: standing on the cover counts as
    // being in it, which is the fail-open reading at the exact boundary everywhere below.
    const auto containsPoint = [&config](const Math::Vector3UVE& center,
                                         const Math::Vector3UVE& point) noexcept {
        return std::abs(point.x - center.x) <= config.halfExtents.x &&
               std::abs(point.y - center.y) <= config.halfExtents.y &&
               std::abs(point.z - center.z) <= config.halfExtents.z;
    };
    if (!std::isfinite(viewerWorldPosition.x) || !std::isfinite(viewerWorldPosition.y) ||
        !std::isfinite(viewerWorldPosition.z)) {
        return false;
    }
    if (!std::isfinite(pointWorld.x) || !std::isfinite(pointWorld.y) ||
        !std::isfinite(pointWorld.z)) {
        return false;
    }
    if (containsPoint(occluderWorldPosition, viewerWorldPosition) ||
        containsPoint(occluderWorldPosition, pointWorld)) {
        return false; // inside the cover: you cannot be hidden by it, and it never hides its own
    }

    const float directionX = pointWorld.x - viewerWorldPosition.x;
    const float directions[3U] = {directionX, pointWorld.y - viewerWorldPosition.y,
                                  pointWorld.z - viewerWorldPosition.z};
    const float viewerAxes[3U] = {viewerWorldPosition.x, viewerWorldPosition.y,
                                  viewerWorldPosition.z};
    const float lowerAxes[3U] = {occluderWorldPosition.x - config.halfExtents.x,
                                 occluderWorldPosition.y - config.halfExtents.y,
                                 occluderWorldPosition.z - config.halfExtents.z};
    const float upperAxes[3U] = {occluderWorldPosition.x + config.halfExtents.x,
                                 occluderWorldPosition.y + config.halfExtents.y,
                                 occluderWorldPosition.z + config.halfExtents.z};
    if (!std::isfinite(directions[0U]) || !std::isfinite(directions[1U]) ||
        !std::isfinite(directions[2U])) {
        return false;
    }
    // Slab intersection of the segment against the box, expressed as a parametric overlap on
    // t in [0, 1]: only strict interior overlap counts, and only when the box lands strictly
    // before the segment's far end (tEnter < 1) - so "the candidate is exactly inside the cover"
    // and "the ray only grazes the skin" both fail open. Degenerate axes (the segment is exactly
    // parallel to a slab) collapse that axis' interval: harmless because being parallel means
    // this axis carries no restriction beyond the endpoint check contained in tEnter/tExit.
    float tEnter = 0.0F;
    float tExit = 1.0F;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const float direction = directions[axis];
        const float viewer = viewerAxes[axis];
        const float lower = lowerAxes[axis];
        const float upper = upperAxes[axis];
        if (direction == 0.0F) {
            if (viewer <= lower || viewer >= upper) {
                return false; // parallel travel outside this slab never enters the box
            }
            continue;
        }
        float near = (lower - viewer) / direction;
        float far = (upper - viewer) / direction;
        if (near > far) {
            std::swap(near, far);
        }
        tEnter = std::max(tEnter, near);
        tExit = std::min(tExit, far);
        if (tEnter >= tExit) {
            return false; // no common interior interval at all
        }
    }
    return tEnter < tExit;
}

} // namespace UVE::Scene
