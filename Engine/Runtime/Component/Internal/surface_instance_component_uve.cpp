// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/surface_instance_component_uve.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsSurfaceInstanceComponentValidUVE(const SurfaceInstanceComponentUVE& component) noexcept {
    return component.castShadow <= SurfaceShadowModeUVE::ShadowsOnly &&
           component.lightingMode <= SurfaceLightingModeUVE::Dynamic &&
           component.visibilityRangeFadeMode <= SurfaceFadeModeUVE::Dependencies &&
           std::isfinite(component.transparency) && component.transparency >= 0.0F && component.transparency <= 1.0F &&
           IsNonNegativeUVE(component.extraCullMargin) && std::isfinite(component.lodBias) && component.lodBias > 0.0F &&
           IsNonNegativeUVE(component.visibilityRangeBegin) && IsNonNegativeUVE(component.visibilityRangeBeginMargin) &&
           IsNonNegativeUVE(component.visibilityRangeEnd) && IsNonNegativeUVE(component.visibilityRangeEndMargin) &&
           (component.visibilityRangeEnd == 0.0F || component.visibilityRangeEnd >= component.visibilityRangeBegin);
}

} // namespace UVE::Scene
