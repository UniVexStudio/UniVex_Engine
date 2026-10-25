// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/color_uve.h"

namespace UVE::Scene {

enum class LightBakeModeUVE : std::uint8_t {
    /// Real-time only; ignored by baking.
    Disabled = 0,
    /// Fully baked; costs nothing at runtime, must not change.
    Static,
    /// Baked indirect light, real-time direct light; may change colour and energy.
    Dynamic,
};

/// The shared state of LightEmitter3D, the abstract base (under RenderInstance3D) of every light.
/// No object is a LightEmitter3D on its own; its kinds - directional, point, spot - carry this
/// component and add their own shape.
struct LightEmitterComponentUVE final {
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float energy = 1.0F;
    /// Scales this light's contribution to bounced (indirect) lighting.
    float indirectEnergy = 1.0F;
    /// Scales how brightly this light shows in volumetric fog.
    float volumetricFogEnergy = 1.0F;
    /// Strength of the highlight it leaves on shiny surfaces.
    float specular = 0.5F;
    LightBakeModeUVE bakeMode = LightBakeModeUVE::Dynamic;
    /// The render layers it lights.
    std::uint32_t cullMask = 0xFFFFFFFFU;

    bool shadowEnabled = false;
    /// Negative inherits the project default; non-negative values override it per light.
    float shadowBias = -1.0F;
    /// Negative inherits the project default; non-negative values override it per light.
    float shadowNormalBias = -1.0F;
    /// 1 fully dark shadows; lower lets light through.
    float shadowOpacity = 1.0F;
    /// Softens shadow edges.
    float shadowBlur = 1.0F;

    /// Fades the light, then its shadow, out with distance from the camera.
    bool distanceFadeEnabled = false;
    float distanceFadeBegin = 40.0F;
    float distanceFadeShadow = 50.0F;
    float distanceFadeLength = 10.0F;

    [[nodiscard]] bool operator==(const LightEmitterComponentUVE&) const = default;
};

/// A known bake mode; finite values; colour channels and energies not negative; shadow opacity in
/// [0, 1]; and fade distances not negative.
[[nodiscard]] bool IsLightEmitterComponentValidUVE(const LightEmitterComponentUVE& component) noexcept;

/// Sanitizes `component` so bake mode, color, energies, shadow opacity, and fade distances are valid.
[[nodiscard]] LightEmitterComponentUVE SanitizeLightEmitterComponentUVE(
    const LightEmitterComponentUVE& component) noexcept;

/// True when `component.cullMask` shares at least one bit with `surfaceLayerMask`.
[[nodiscard]] bool DoesLightEmitterAffectSurfaceLayerMaskUVE(
    const LightEmitterComponentUVE& component, std::uint32_t surfaceLayerMask) noexcept;

/// True when the light casts runtime shadows (`shadowEnabled`, `shadowOpacity > 0`, and `cullMask != 0`).
[[nodiscard]] bool DoesLightEmitterCastActiveShadowUVE(
    const LightEmitterComponentUVE& component) noexcept;

/// Evaluates the distance fade multiplier in `[0, 1]` at `cameraDistanceMeters` (`1` when `distanceFadeEnabled` is false).
[[nodiscard]] float EvaluateLightEmitterDistanceFadeUVE(
    const LightEmitterComponentUVE& component, float cameraDistanceMeters) noexcept;

} // namespace UVE::Scene
