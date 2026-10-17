// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/camera_2d_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsCamera2DComponentValidUVE(const Camera2DComponentUVE& camera) noexcept {
    return Math::IsFiniteUVE(camera.zoom) && camera.zoom.x >= kMinimumCamera2DZoomUVE &&
           camera.zoom.y >= kMinimumCamera2DZoomUVE && Math::IsFiniteUVE(camera.offset);
}

Math::Vector2UVE ClampCamera2DZoomUVE(const Math::Vector2UVE& zoom) noexcept {
    return Math::Vector2UVE{
        std::isfinite(zoom.x) ? std::max(zoom.x, kMinimumCamera2DZoomUVE) : kDefaultCamera2DZoomUVE,
        std::isfinite(zoom.y) ? std::max(zoom.y, kMinimumCamera2DZoomUVE) : kDefaultCamera2DZoomUVE,
    };
}

Camera2DComponentUVE SanitizeCamera2DComponentUVE(const Camera2DComponentUVE& camera) noexcept {
    return Camera2DComponentUVE{
        .zoom = ClampCamera2DZoomUVE(camera.zoom),
        .offset = Math::IsFiniteUVE(camera.offset) ? camera.offset : Math::Vector2UVE{0.0F, 0.0F},
        .current = camera.current,
    };
}

Math::Vector2UVE ResolveCamera2DViewCenterUVE(const Camera2DComponentUVE& camera,
                                              const Math::Vector2UVE& worldPosition) noexcept {
    const Math::Vector2UVE safePos =
        Math::IsFiniteUVE(worldPosition) ? worldPosition : Math::Vector2UVE{0.0F, 0.0F};
    const Math::Vector2UVE safeOffset =
        Math::IsFiniteUVE(camera.offset) ? camera.offset : Math::Vector2UVE{0.0F, 0.0F};
    return safePos + safeOffset;
}

Math::Vector2UVE ResolveCamera2DVisibleHalfExtentsUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& viewportSizePixels) noexcept {
    if (!Math::IsFiniteUVE(viewportSizePixels) || viewportSizePixels.x <= 0.0F ||
        viewportSizePixels.y <= 0.0F) {
        return Math::Vector2UVE{0.0F, 0.0F};
    }
    const Math::Vector2UVE safeZoom = ClampCamera2DZoomUVE(camera.zoom);
    return Math::Vector2UVE{
        (viewportSizePixels.x * 0.5F) / safeZoom.x,
        (viewportSizePixels.y * 0.5F) / safeZoom.y,
    };
}

Math::RectUVE ResolveCamera2DVisibleWorldRectUVE(
    const Camera2DComponentUVE& camera, const Math::Vector2UVE& worldPosition,
    const Math::Vector2UVE& viewportSizePixels) noexcept {
    const Math::Vector2UVE center = ResolveCamera2DViewCenterUVE(camera, worldPosition);
    const Math::Vector2UVE halfExtents =
        ResolveCamera2DVisibleHalfExtentsUVE(camera, viewportSizePixels);
    return Math::RectUVE{
        .position = center - halfExtents,
        .size = halfExtents * 2.0F,
    };
}

} // namespace UVE::Scene
