// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "touch_coordinate_transform.h"
#include <algorithm>
#include <cmath>
namespace UVE::Input {
bool ApplyTouchOrientation(const Math::Vector2 normalizedPosition,
                              const TouchOrientation orientation,
                              Math::Vector2& outOriented) noexcept {
    if (!std::isfinite(normalizedPosition.x) || !std::isfinite(normalizedPosition.y) ||
        normalizedPosition.x < 0.0F || normalizedPosition.x > 1.0F || normalizedPosition.y < 0.0F ||
        normalizedPosition.y > 1.0F) {
        return false;
    }
    Math::Vector2 oriented;
    switch (orientation) {
        case TouchOrientation::Deg0:
            oriented = normalizedPosition;
            break;
        case TouchOrientation::Deg90:
            oriented = Math::Vector2{1.0F - normalizedPosition.y, normalizedPosition.x};
            break;
        case TouchOrientation::Deg180:
            oriented = Math::Vector2{1.0F - normalizedPosition.x, 1.0F - normalizedPosition.y};
            break;
        case TouchOrientation::Deg270:
            oriented = Math::Vector2{normalizedPosition.y, 1.0F - normalizedPosition.x};
            break;
        default:
            return false;
    }
    outOriented = oriented;
    return true;
}

bool NormalizeTouchCoordinate(const Math::Vector2 pixelPosition,
                                 const TouchCoordinateViewport& viewport,
                                 Math::Vector2& outNormalized) noexcept {
    if (!std::isfinite(pixelPosition.x) || !std::isfinite(pixelPosition.y) ||
        !ValidateTouchCoordinateViewport(viewport)) {
        return false;
    }
    const float usableWidth = viewport.width - viewport.safeLeft - viewport.safeRight;
    const float usableHeight = viewport.height - viewport.safeTop - viewport.safeBottom;
    outNormalized.x = std::clamp((pixelPosition.x - viewport.safeLeft) / usableWidth, 0.0F, 1.0F);
    outNormalized.y = std::clamp((pixelPosition.y - viewport.safeTop) / usableHeight, 0.0F, 1.0F);
    return true;
}
} // namespace UVE::Input
