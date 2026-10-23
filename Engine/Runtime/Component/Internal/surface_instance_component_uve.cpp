// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/surface_instance_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float SanitizeNonNegativeUVE(const float value, const float fallback = 0.0F) noexcept {
    return IsNonNegativeUVE(value) ? value : fallback;
}

} // namespace

bool IsSurfaceShadowModeValidUVE(const SurfaceShadowModeUVE mode) noexcept {
    return mode <= SurfaceShadowModeUVE::ShadowsOnly;
}

bool IsSurfaceLightingModeValidUVE(const SurfaceLightingModeUVE mode) noexcept {
    return mode <= SurfaceLightingModeUVE::Dynamic;
}

bool IsSurfaceFadeModeValidUVE(const SurfaceFadeModeUVE mode) noexcept {
    return mode <= SurfaceFadeModeUVE::Dependencies;
}

bool IsSurfaceInstanceComponentValidUVE(const SurfaceInstanceComponentUVE& component) noexcept {
    return IsSurfaceShadowModeValidUVE(component.castShadow) &&
           IsSurfaceLightingModeValidUVE(component.lightingMode) &&
           IsSurfaceFadeModeValidUVE(component.visibilityRangeFadeMode) &&
           std::isfinite(component.transparency) && component.transparency >= 0.0F &&
           component.transparency <= 1.0F && IsNonNegativeUVE(component.extraCullMargin) &&
           std::isfinite(component.lodBias) && component.lodBias > 0.0F &&
           IsNonNegativeUVE(component.visibilityRangeBegin) &&
           IsNonNegativeUVE(component.visibilityRangeBeginMargin) &&
           IsNonNegativeUVE(component.visibilityRangeEnd) &&
           IsNonNegativeUVE(component.visibilityRangeEndMargin) &&
           (component.visibilityRangeEnd == 0.0F ||
            component.visibilityRangeEnd >= component.visibilityRangeBegin) &&
           component.surfaceMaterialOverridePaths.size() <= kMaximumSurfaceMaterialOverridesUVE;
}

SurfaceInstanceComponentUVE SanitizeSurfaceInstanceComponentUVE(
    const SurfaceInstanceComponentUVE& component) {
    SurfaceInstanceComponentUVE out = component;
    out.transparency =
        std::isfinite(out.transparency) ? std::clamp(out.transparency, 0.0F, 1.0F) : 0.0F;
    if (!IsSurfaceShadowModeValidUVE(out.castShadow)) {
        out.castShadow = SurfaceShadowModeUVE::On;
    }
    out.extraCullMargin = SanitizeNonNegativeUVE(out.extraCullMargin, 0.0F);
    if (!std::isfinite(out.lodBias) || out.lodBias <= 0.0F) {
        out.lodBias = 1.0F;
    }
    if (!IsSurfaceLightingModeValidUVE(out.lightingMode)) {
        out.lightingMode = SurfaceLightingModeUVE::Static;
    }
    out.visibilityRangeBegin = SanitizeNonNegativeUVE(out.visibilityRangeBegin, 0.0F);
    out.visibilityRangeBeginMargin = SanitizeNonNegativeUVE(out.visibilityRangeBeginMargin, 0.0F);
    out.visibilityRangeEnd = SanitizeNonNegativeUVE(out.visibilityRangeEnd, 0.0F);
    out.visibilityRangeEndMargin = SanitizeNonNegativeUVE(out.visibilityRangeEndMargin, 0.0F);
    if (out.visibilityRangeEnd > 0.0F && out.visibilityRangeEnd < out.visibilityRangeBegin) {
        out.visibilityRangeEnd = out.visibilityRangeBegin;
    }
    if (!IsSurfaceFadeModeValidUVE(out.visibilityRangeFadeMode)) {
        out.visibilityRangeFadeMode = SurfaceFadeModeUVE::Disabled;
    }
    if (out.surfaceMaterialOverridePaths.size() > kMaximumSurfaceMaterialOverridesUVE) {
        out.surfaceMaterialOverridePaths.resize(kMaximumSurfaceMaterialOverridesUVE);
    }
    // An empty entry means "this surface keeps its own material". At the END of the list that is
    // the same statement an absent entry makes, so trimming them keeps a list that grew and then
    // shrank from saving a run of empties that mean nothing.
    while (!out.surfaceMaterialOverridePaths.empty() && out.surfaceMaterialOverridePaths.back().empty()) {
        out.surfaceMaterialOverridePaths.pop_back();
    }
    return out;
}

float ResolveSurfaceInstanceOpacityUVE(const SurfaceInstanceComponentUVE& component) noexcept {
    if (!std::isfinite(component.transparency)) {
        return 1.0F;
    }
    return 1.0F - std::clamp(component.transparency, 0.0F, 1.0F);
}

bool CastsSurfaceShadowUVE(const SurfaceInstanceComponentUVE& component) noexcept {
    return component.castShadow == SurfaceShadowModeUVE::On ||
           component.castShadow == SurfaceShadowModeUVE::DoubleSided ||
           component.castShadow == SurfaceShadowModeUVE::ShadowsOnly;
}

bool DrawsMainPassSurfaceGeometryUVE(const SurfaceInstanceComponentUVE& component) noexcept {
    return component.castShadow != SurfaceShadowModeUVE::ShadowsOnly &&
           ResolveSurfaceInstanceOpacityUVE(component) > 0.0F;
}

bool IsDistanceWithinSurfaceVisibilityRangeUVE(const SurfaceInstanceComponentUVE& component,
                                               const float cameraDistance) noexcept {
    if (!std::isfinite(cameraDistance) || cameraDistance < 0.0F) {
        return false;
    }
    if (cameraDistance < component.visibilityRangeBegin) {
        return false;
    }
    if (component.visibilityRangeEnd > 0.0F && cameraDistance > component.visibilityRangeEnd) {
        return false;
    }
    return true;
}

float ComputeSurfaceVisibilityRangeFadeFactorUVE(const SurfaceInstanceComponentUVE& component,
                                                 const float cameraDistance) noexcept {
    if (!IsDistanceWithinSurfaceVisibilityRangeUVE(component, cameraDistance)) {
        return 0.0F;
    }
    if (component.visibilityRangeFadeMode == SurfaceFadeModeUVE::Disabled) {
        return 1.0F;
    }
    float factor = 1.0F;
    if (component.visibilityRangeBegin > 0.0F && component.visibilityRangeBeginMargin > 0.0F) {
        const float fadeInSpan =
            std::min(1.0F, (cameraDistance - component.visibilityRangeBegin) /
                               component.visibilityRangeBeginMargin);
        factor = std::min(factor, std::clamp(fadeInSpan, 0.0F, 1.0F));
    }
    if (component.visibilityRangeEnd > 0.0F && component.visibilityRangeEndMargin > 0.0F) {
        const float fadeOutSpan =
            std::min(1.0F, (component.visibilityRangeEnd - cameraDistance) /
                               component.visibilityRangeEndMargin);
        factor = std::min(factor, std::clamp(fadeOutSpan, 0.0F, 1.0F));
    }
    return factor;
}

} // namespace UVE::Scene
