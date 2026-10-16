// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/render_instance_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsRenderInstanceComponentValidUVE(const RenderInstanceComponentUVE& component) noexcept {
    return std::isfinite(component.sortingOffset);
}

RenderInstanceComponentUVE SanitizeRenderInstanceComponentUVE(
    const RenderInstanceComponentUVE& component) noexcept {
    return RenderInstanceComponentUVE{
        .renderLayers = component.renderLayers,
        .sortingOffset = std::isfinite(component.sortingOffset) ? component.sortingOffset : 0.0F,
        .sortingUseAabbCenter = component.sortingUseAabbCenter,
    };
}

bool MatchesRenderInstanceLayerMaskUVE(const RenderInstanceComponentUVE& component,
                                       const std::uint32_t cameraLayerMask) noexcept {
    return (component.renderLayers & cameraLayerMask) != 0U;
}

bool IsRenderInstanceOnLayerUVE(const RenderInstanceComponentUVE& component,
                                const std::uint32_t layerIndex) noexcept {
    if (layerIndex > kMaximumRenderInstanceLayerIndexUVE) {
        return false;
    }
    return (component.renderLayers & (1U << layerIndex)) != 0U;
}

void SetRenderInstanceLayerBitUVE(RenderInstanceComponentUVE& component,
                                  const std::uint32_t layerIndex, const bool enabled) noexcept {
    if (layerIndex > kMaximumRenderInstanceLayerIndexUVE) {
        return;
    }
    const std::uint32_t bit = (1U << layerIndex);
    if (enabled) {
        component.renderLayers |= bit;
    } else {
        component.renderLayers &= ~bit;
    }
}

Math::Vector3UVE ResolveRenderInstanceSortAnchorUVE(
    const RenderInstanceComponentUVE& component, const Math::Vector3UVE& worldOrigin,
    const Math::Vector3UVE& worldAabbCenter) noexcept {
    return component.sortingUseAabbCenter ? worldAabbCenter : worldOrigin;
}

float ComputeRenderInstanceSortDepthUVE(const RenderInstanceComponentUVE& component,
                                        const float baseCameraDistance) noexcept {
    const float safeDistance = std::isfinite(baseCameraDistance) ? baseCameraDistance : 0.0F;
    const float safeOffset = std::isfinite(component.sortingOffset) ? component.sortingOffset : 0.0F;
    return safeDistance + safeOffset;
}

} // namespace UVE::Scene
