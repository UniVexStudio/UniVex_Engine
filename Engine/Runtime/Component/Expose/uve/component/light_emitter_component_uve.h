// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/vector3_uve.h"

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
/// No node is a LightEmitter3D on its own; its kinds - directional, point, spot - carry this
/// component and add their own shape.
struct LightEmitterComponentUVE final {
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
    float energy = 1.0F;
    /// Scales this light's contribution to bounced (indirect) lighting.
    float indirectEnergy = 1.0F;
    /// Scales how brightly this light shows in volumetric fog.
    float volumetricFogEnergy = 1.0F;
    /// Strength of the highlight it leaves on shiny surfaces.
    float specular = 0.5F;
    /// Subtracts light instead of adding it.
    bool negative = false;
    LightBakeModeUVE bakeMode = LightBakeModeUVE::Dynamic;
    /// The render layers it lights.
    std::uint32_t cullMask = 0xFFFFFFFFU;

    bool shadowEnabled = false;
    float shadowBias = 0.1F;
    float shadowNormalBias = 1.0F;
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

} // namespace UVE::Scene
