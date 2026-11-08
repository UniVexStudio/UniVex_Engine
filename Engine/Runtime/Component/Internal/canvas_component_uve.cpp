// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/canvas_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsCanvasComponentValidUVE(const CanvasComponentUVE& component) noexcept {
    if (!IsCanvasSortOrderInRangeUVE(component.sortOrder)) {
        return false;
    }
    if (component.renderMode == CanvasRenderModeUVE::WorldSpace) {
        if (!Math::IsFiniteUVE(component.worldSize) || component.worldSize.x <= 0.0F || component.worldSize.y <= 0.0F) {
            return false;
        }
        if (!Math::IsFiniteUVE(component.referencePixels) || component.referencePixels.x <= 0.0F || component.referencePixels.y <= 0.0F) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsCanvasSortOrderInRangeUVE(const std::int32_t sortOrder) noexcept {
    return sortOrder >= kMinimumCanvasSortOrderUVE && sortOrder <= kMaximumCanvasSortOrderUVE;
}

[[nodiscard]] std::int32_t ClampCanvasSortOrderUVE(const std::int32_t sortOrder) noexcept {
    return std::clamp(sortOrder, kMinimumCanvasSortOrderUVE, kMaximumCanvasSortOrderUVE);
}

[[nodiscard]] CanvasComponentUVE SanitizeCanvasComponentUVE(
    const CanvasComponentUVE& component) noexcept {
    CanvasComponentUVE sanitized = component;
    sanitized.sortOrder = ClampCanvasSortOrderUVE(component.sortOrder);
    if (!Math::IsFiniteUVE(component.worldSize) || component.worldSize.x <= 0.0F || component.worldSize.y <= 0.0F) {
        sanitized.worldSize = Math::Vector2UVE{1.0F, 1.0F};
    }
    if (!Math::IsFiniteUVE(component.referencePixels) || component.referencePixels.x <= 0.0F || component.referencePixels.y <= 0.0F) {
        sanitized.referencePixels = Math::Vector2UVE{100.0F, 100.0F};
    }
    return sanitized;
}

[[nodiscard]] bool ResolveInheritedCanvasVisibilityUVE(const CanvasComponentUVE* const canvas,
                                                       const bool parentVisible) noexcept {
    if (!parentVisible) {
        return false;
    }
    return canvas == nullptr ? true : canvas->visible;
}

[[nodiscard]] std::int32_t ResolveEffectiveCanvasSortOrderUVE(
    const CanvasComponentUVE* const canvas, const std::int32_t fallbackSortOrder) noexcept {
    return canvas == nullptr ? fallbackSortOrder : canvas->sortOrder;
}

[[nodiscard]] bool CompareCanvasDrawOrderUVE(const CanvasComponentUVE& lhs,
                                             const std::uint32_t lhsTieBreak,
                                             const CanvasComponentUVE& rhs,
                                             const std::uint32_t rhsTieBreak) noexcept {
    if (lhs.sortOrder != rhs.sortOrder) {
        return lhs.sortOrder < rhs.sortOrder;
    }
    return lhsTieBreak < rhsTieBreak;
}

[[nodiscard]] Math::Matrix4x4UVE ComputeWorldSpaceCanvasTransformUVE(
    const CanvasComponentUVE& canvas, const Math::Vector3UVE& worldPosition,
    const Math::QuaternionUVE& worldRotation, const Math::Vector3UVE& worldScale) noexcept {
    const float effScaleX = (std::isfinite(worldScale.x) && worldScale.x != 0.0F) ? worldScale.x : 1.0F;
    const float effScaleY = (std::isfinite(worldScale.y) && worldScale.y != 0.0F) ? worldScale.y : 1.0F;
    const float effScaleZ = (std::isfinite(worldScale.z) && worldScale.z != 0.0F) ? worldScale.z : 1.0F;

    const float wsX = std::max(canvas.worldSize.x, 0.001F);
    const float wsY = std::max(canvas.worldSize.y, 0.001F);
    const float refX = std::max(canvas.referencePixels.x, 1.0F);
    const float refY = std::max(canvas.referencePixels.y, 1.0F);

    // Pixel-to-metric ratio: map (0,0) to top-left of the world quad (-wsX*0.5, wsY*0.5, 0)
    const float pixelScaleX = wsX / refX;
    const float pixelScaleY = -wsY / refY; // UI Y goes down, 3D world Y goes up
    const float offsetX = -0.5F * wsX;
    const float offsetY = 0.5F * wsY;

    // Local canvas matrix mapping (px, py, 0, 1) -> (lx, ly, 0, 1)
    Math::Matrix4x4UVE localMapping{};
    localMapping.m[0][0] = pixelScaleX;
    localMapping.m[0][3] = offsetX;
    localMapping.m[1][1] = pixelScaleY;
    localMapping.m[1][3] = offsetY;
    localMapping.m[2][2] = 1.0F;
    localMapping.m[3][3] = 1.0F;

    const Math::Matrix4x4UVE worldTrs = Math::Matrix4x4UVE::ComposeTrsUVE(
        worldPosition, worldRotation, Math::Vector3UVE{effScaleX, effScaleY, effScaleZ});

    return worldTrs * localMapping;
}

[[nodiscard]] Math::Matrix4x4UVE ComputeWorldSpaceCanvasBillboardTransformUVE(
    const CanvasComponentUVE& canvas, const Math::Vector3UVE& worldPosition,
    const Math::Vector3UVE& cameraPosition, const Math::Vector3UVE& worldScale) noexcept {
    Math::Vector3UVE toCamera = cameraPosition - worldPosition;
    toCamera.y = 0.0F; // Yaw-only billboard
    const float len = std::sqrt(toCamera.x * toCamera.x + toCamera.z * toCamera.z);
    Math::QuaternionUVE billboardRotation{};
    if (len > 1.0e-5F) {
        const float angle = std::atan2(toCamera.x, toCamera.z);
        static_cast<void>(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, angle, billboardRotation));
    }
    return ComputeWorldSpaceCanvasTransformUVE(canvas, worldPosition, billboardRotation, worldScale);
}

} // namespace UVE::Scene
