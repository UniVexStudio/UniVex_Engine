// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "surface_instance_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float SanitizeNonNegative(const float value, const float fallback = 0.0F) noexcept {
    return IsNonNegative(value) ? value : fallback;
}

} // namespace

bool IsSurfaceShadowModeValid(const SurfaceShadowMode mode) noexcept {
    return mode <= SurfaceShadowMode::ShadowsOnly;
}

bool IsSurfaceLightingModeValid(const SurfaceLightingMode mode) noexcept {
    return mode <= SurfaceLightingMode::Dynamic;
}

bool IsSurfaceFadeModeValid(const SurfaceFadeMode mode) noexcept {
    return mode <= SurfaceFadeMode::Dependencies;
}

bool IsSurfaceInstanceComponentValid(const SurfaceInstanceComponent& component) noexcept {
    return IsSurfaceShadowModeValid(component.castShadow) &&
           IsSurfaceLightingModeValid(component.lightingMode) &&
           IsSurfaceFadeModeValid(component.visibilityRangeFadeMode) &&
           std::isfinite(component.transparency) && component.transparency >= 0.0F &&
           component.transparency <= 1.0F && IsNonNegative(component.extraCullMargin) &&
           std::isfinite(component.lodBias) && component.lodBias > 0.0F &&
           IsNonNegative(component.visibilityRangeBegin) &&
           IsNonNegative(component.visibilityRangeBeginMargin) &&
           IsNonNegative(component.visibilityRangeEnd) &&
           IsNonNegative(component.visibilityRangeEndMargin) &&
           (component.visibilityRangeEnd == 0.0F ||
            component.visibilityRangeEnd >= component.visibilityRangeBegin) &&
           component.surfaceMaterialOverridePaths.size() <= kMaximumSurfaceMaterialOverrides;
}

SurfaceInstanceComponent SanitizeSurfaceInstanceComponent(
    const SurfaceInstanceComponent& component) {
    SurfaceInstanceComponent out = component;
    out.transparency =
        std::isfinite(out.transparency) ? std::clamp(out.transparency, 0.0F, 1.0F) : 0.0F;
    if (!IsSurfaceShadowModeValid(out.castShadow)) {
        out.castShadow = SurfaceShadowMode::On;
    }
    out.extraCullMargin = SanitizeNonNegative(out.extraCullMargin, 0.0F);
    if (!std::isfinite(out.lodBias) || out.lodBias <= 0.0F) {
        out.lodBias = 1.0F;
    }
    if (!IsSurfaceLightingModeValid(out.lightingMode)) {
        out.lightingMode = SurfaceLightingMode::Static;
    }
    out.visibilityRangeBegin = SanitizeNonNegative(out.visibilityRangeBegin, 0.0F);
    out.visibilityRangeBeginMargin = SanitizeNonNegative(out.visibilityRangeBeginMargin, 0.0F);
    out.visibilityRangeEnd = SanitizeNonNegative(out.visibilityRangeEnd, 0.0F);
    out.visibilityRangeEndMargin = SanitizeNonNegative(out.visibilityRangeEndMargin, 0.0F);
    if (out.visibilityRangeEnd > 0.0F && out.visibilityRangeEnd < out.visibilityRangeBegin) {
        out.visibilityRangeEnd = out.visibilityRangeBegin;
    }
    if (!IsSurfaceFadeModeValid(out.visibilityRangeFadeMode)) {
        out.visibilityRangeFadeMode = SurfaceFadeMode::Disabled;
    }
    if (out.surfaceMaterialOverridePaths.size() > kMaximumSurfaceMaterialOverrides) {
        out.surfaceMaterialOverridePaths.resize(kMaximumSurfaceMaterialOverrides);
    }
    // An empty entry means "this surface keeps its own material". At the END of the list that is
    // the same statement an absent entry makes, so trimming them keeps a list that grew and then
    // shrank from saving a run of empties that mean nothing.
    while (!out.surfaceMaterialOverridePaths.empty() && out.surfaceMaterialOverridePaths.back().empty()) {
        out.surfaceMaterialOverridePaths.pop_back();
    }
    return out;
}

float ResolveSurfaceInstanceOpacity(const SurfaceInstanceComponent& component) noexcept {
    if (!std::isfinite(component.transparency)) {
        return 1.0F;
    }
    return 1.0F - std::clamp(component.transparency, 0.0F, 1.0F);
}

bool CastsSurfaceShadow(const SurfaceInstanceComponent& component) noexcept {
    return component.castShadow == SurfaceShadowMode::On ||
           component.castShadow == SurfaceShadowMode::DoubleSided ||
           component.castShadow == SurfaceShadowMode::ShadowsOnly;
}

bool DrawsMainPassSurfaceGeometry(const SurfaceInstanceComponent& component) noexcept {
    return component.castShadow != SurfaceShadowMode::ShadowsOnly &&
           ResolveSurfaceInstanceOpacity(component) > 0.0F;
}

bool IsDistanceWithinSurfaceVisibilityRange(const SurfaceInstanceComponent& component,
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

float ComputeSurfaceVisibilityRangeFadeFactor(const SurfaceInstanceComponent& component,
                                                 const float cameraDistance) noexcept {
    if (!IsDistanceWithinSurfaceVisibilityRange(component, cameraDistance)) {
        return 0.0F;
    }
    if (component.visibilityRangeFadeMode == SurfaceFadeMode::Disabled) {
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
