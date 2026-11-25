// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "render_instance_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsRenderInstanceComponentValid(const RenderInstanceComponent& component) noexcept {
    return std::isfinite(component.sortingOffset);
}

RenderInstanceComponent SanitizeRenderInstanceComponent(
    const RenderInstanceComponent& component) noexcept {
    return RenderInstanceComponent{
        .renderLayers = component.renderLayers,
        .sortingOffset = std::isfinite(component.sortingOffset) ? component.sortingOffset : 0.0F,
        .sortingUseAabbCenter = component.sortingUseAabbCenter,
    };
}

bool MatchesRenderInstanceLayerMask(const RenderInstanceComponent& component,
                                       const std::uint32_t cameraLayerMask) noexcept {
    return (component.renderLayers & cameraLayerMask) != 0U;
}

bool IsRenderInstanceOnLayer(const RenderInstanceComponent& component,
                                const std::uint32_t layerIndex) noexcept {
    if (layerIndex > kMaximumRenderInstanceLayerIndex) {
        return false;
    }
    return (component.renderLayers & (1U << layerIndex)) != 0U;
}

void SetRenderInstanceLayerBit(RenderInstanceComponent& component,
                                  const std::uint32_t layerIndex, const bool enabled) noexcept {
    if (layerIndex > kMaximumRenderInstanceLayerIndex) {
        return;
    }
    const std::uint32_t bit = (1U << layerIndex);
    if (enabled) {
        component.renderLayers |= bit;
    } else {
        component.renderLayers &= ~bit;
    }
}

Math::Vector3 ResolveRenderInstanceSortAnchor(
    const RenderInstanceComponent& component, const Math::Vector3& worldOrigin,
    const Math::Vector3& worldAabbCenter) noexcept {
    return component.sortingUseAabbCenter ? worldAabbCenter : worldOrigin;
}

float ComputeRenderInstanceSortDepth(const RenderInstanceComponent& component,
                                        const float baseCameraDistance) noexcept {
    const float safeDistance = std::isfinite(baseCameraDistance) ? baseCameraDistance : 0.0F;
    const float safeOffset = std::isfinite(component.sortingOffset) ? component.sortingOffset : 0.0F;
    return safeDistance + safeOffset;
}

} // namespace UVE::Scene
