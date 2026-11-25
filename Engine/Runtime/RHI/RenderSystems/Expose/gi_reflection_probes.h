// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "i_entity_manager.h"
#include "i_light_system.h"
#include "vector3.h"

namespace UVE::Render {

/// L2 (3-band, 9-coefficient) RGB Spherical Harmonics for diffuse irradiance & indirect GI.
struct SphericalHarmonicsL2 final {
    std::array<Math::Vector3, 9U> coefficients{};

    [[nodiscard]] bool IsZero() const noexcept;
};

/// Projects a single radiance sample from unit direction `dir` with solid angle `weight` into `sh`.
void AccumulateShL2Sample(SphericalHarmonicsL2& sh, const Math::Vector3& dir,
                             const Math::Vector3& radiance, float weight) noexcept;

/// Projects 6 tightly-packed RGBA8 cubemap faces (face order +X,-X,+Y,-Y,+Z,-Z) into L2 SH
/// irradiance coefficients using exact solid-angle Jacobian weighting.
[[nodiscard]] SphericalHarmonicsL2 ProjectCubemapRgba8ToShL2(
    const std::array<std::span<const std::uint8_t>, 6U>& faces, std::uint32_t faceSize) noexcept;

/// Evaluates cosine-convolved diffuse irradiance at unit surface normal `normal`.
[[nodiscard]] Math::Vector3 EvaluateShL2Irradiance(const SphericalHarmonicsL2& sh,
                                                         const Math::Vector3& normal) noexcept;

[[nodiscard]] SphericalHarmonicsL2 BlendShL2(const SphericalHarmonicsL2& a,
                                                   const SphericalHarmonicsL2& b, float t) noexcept;

/// Persisted baked reflection probe payload (`.uvprobe` file).
struct BakedReflectionProbeAsset final {
    std::uint32_t faceSize = 0U;
    SphericalHarmonicsL2 diffuseSh{};
    std::array<std::vector<std::uint8_t>, 6U> faceRgba8{};
};

[[nodiscard]] bool SaveReflectionProbeAsset(const std::filesystem::path& path,
                                               const BakedReflectionProbeAsset& asset);
[[nodiscard]] bool LoadReflectionProbeAsset(const std::filesystem::path& path,
                                               BakedReflectionProbeAsset& outAsset);

/// 3D grid of L2 SH irradiance probes for trilinear dynamic-object global illumination.
struct GiProbeGrid final {
    Math::Vector3 boundsMin{-10.0F, 0.0F, -10.0F};
    Math::Vector3 boundsMax{10.0F, 10.0F, 10.0F};
    std::uint32_t countX = 1U;
    std::uint32_t countY = 1U;
    std::uint32_t countZ = 1U;
    std::vector<SphericalHarmonicsL2> probes;

    [[nodiscard]] bool IsValid() const noexcept {
        return countX > 0U && countY > 0U && countZ > 0U &&
               probes.size() == static_cast<std::size_t>(countX) * countY * countZ;
    }

    [[nodiscard]] Math::Vector3 SampleIrradiance(const Math::Vector3& worldPos,
                                                       const Math::Vector3& normal) const noexcept;
};

struct LightmapBakeSettings final {
    std::uint32_t atlasWidth = 64U;
    std::uint32_t atlasHeight = 64U;
    std::uint32_t bounceCount = 1U;
    std::uint32_t probeCountX = 4U;
    std::uint32_t probeCountY = 2U;
    std::uint32_t probeCountZ = 4U;
    float bounceIntensity = 0.65F;
    Math::Vector3 skyAmbient{0.16F, 0.18F, 0.22F};
    Math::Vector3 groundAmbient{0.05F, 0.045F, 0.04F};
};

struct LightmapBakeResult final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8;
    GiProbeGrid probeGrid{};
    std::size_t bakedTexels = 0U;
    std::size_t updatedSceneProbes = 0U;
};

/// Deterministic CPU hybrid GI baker: bakes a UV2 lightmap atlas plus a 3D L2 SH irradiance
/// probe grid (and updates any `LightmapProbe3DComponent` entities in `entityManager`).
[[nodiscard]] LightmapBakeResult BakeSceneLightmap(Scene::IEntityManager& entityManager,
                                                         std::span<const LightData> lights,
                                                         const LightmapBakeSettings& settings = {});

} // namespace UVE::Render
