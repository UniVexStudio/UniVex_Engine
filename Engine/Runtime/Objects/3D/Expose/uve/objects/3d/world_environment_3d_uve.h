// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct WorldEnvironment3DComponentUVE final {
    std::string skyAssetPath;
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    float ambientEnergy = 1.0F;
    float exposure = 1.0F;
    float fogDensity = 0.0F;
    bool fogEnabled = false;
    bool postProcessingEnabled = true;
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};
    float skyCurve = 0.15F;
    float groundCurve = 0.02F;
    float fogSkyAffect = 0.35F;
    bool bloomEnabled = true;
    float bloomIntensity = 0.45F;
    float bloomThreshold = 1.0F;
    bool ssaoEnabled = true;
    float ssaoIntensity = 1.0F;
    float ssaoRadius = 0.5F;
    float brightness = 0.0F;
    float contrast = 1.0F;
    float saturation = 1.0F;
    Math::Vector3UVE colorFilter{1.0F, 1.0F, 1.0F};
    float fogHeight = 0.0F;
    float fogHeightFalloff = 1000.0F;
    float fogSunScatter = 0.55F;
};

[[nodiscard]] bool IsWorldEnvironment3DObjectComponentValidUVE(const WorldEnvironment3DComponentUVE& value) noexcept;

struct WorldEnvironmentFrameUVE final {
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE skyAmbient{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE groundAmbient{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    Math::Vector3UVE backgroundColor{0.050F, 0.050F, 0.050F};
    Math::Vector3UVE colorFilter{1.0F, 1.0F, 1.0F};
    float skyCurve = 0.15F;
    float groundCurve = 0.02F;
    float fogDensity = 0.0F;
    float fogSkyAffect = 0.35F;
    float fogHeight = 0.0F;
    float fogHeightFalloff = 1000.0F;
    float fogSunScatter = 0.55F;
    float exposure = 1.0F;
    float bloomIntensity = 0.45F;
    float bloomThreshold = 1.0F;
    float ssaoIntensity = 1.0F;
    float ssaoRadius = 0.5F;
    float brightness = 0.0F;
    float contrast = 1.0F;
    float saturation = 1.0F;
    bool fogEnabled = false;
    bool postProcessingEnabled = false;
    bool bloomEnabled = true;
    bool ssaoEnabled = true;
    bool hasEnvironment = false;
};

[[nodiscard]] WorldEnvironmentFrameUVE ResolveWorldEnvironmentFrameUVE(
    IEntityManagerUVE& entityManager, const Math::Vector3UVE& fallbackAmbient) noexcept;

void ApplySunToWorldEnvironmentFrameUVE(WorldEnvironmentFrameUVE& frame, const Math::Vector3UVE& sunDirection,
                                        const Math::Vector3UVE& sunColor, float sunEnergy) noexcept;

struct WorldEnvironmentObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "WorldEnvironment";
    WorldEnvironment3DComponentUVE environment{};
};

void ApplyWorldEnvironmentObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const WorldEnvironmentObjectDefinitionUVE& value);

} // namespace UVE::Scene
