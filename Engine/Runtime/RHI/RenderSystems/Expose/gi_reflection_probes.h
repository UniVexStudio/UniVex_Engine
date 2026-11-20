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
struct SphericalHarmonicsL2UVE final {
    std::array<Math::Vector3UVE, 9U> coefficients{};

    [[nodiscard]] bool IsZeroUVE() const noexcept;
};

/// Projects a single radiance sample from unit direction `dir` with solid angle `weight` into `sh`.
void AccumulateShL2SampleUVE(SphericalHarmonicsL2UVE& sh, const Math::Vector3UVE& dir,
                             const Math::Vector3UVE& radiance, float weight) noexcept;

/// Projects 6 tightly-packed RGBA8 cubemap faces (face order +X,-X,+Y,-Y,+Z,-Z) into L2 SH
/// irradiance coefficients using exact solid-angle Jacobian weighting.
[[nodiscard]] SphericalHarmonicsL2UVE ProjectCubemapRgba8ToShL2UVE(
    const std::array<std::span<const std::uint8_t>, 6U>& faces, std::uint32_t faceSize) noexcept;

/// Evaluates cosine-convolved diffuse irradiance at unit surface normal `normal`.
[[nodiscard]] Math::Vector3UVE EvaluateShL2IrradianceUVE(const SphericalHarmonicsL2UVE& sh,
                                                         const Math::Vector3UVE& normal) noexcept;

[[nodiscard]] SphericalHarmonicsL2UVE BlendShL2UVE(const SphericalHarmonicsL2UVE& a,
                                                   const SphericalHarmonicsL2UVE& b, float t) noexcept;

/// Persisted baked reflection probe payload (`.uvprobe` file).
struct BakedReflectionProbeAssetUVE final {
    std::uint32_t faceSize = 0U;
    SphericalHarmonicsL2UVE diffuseSh{};
    std::array<std::vector<std::uint8_t>, 6U> faceRgba8{};
};

[[nodiscard]] bool SaveReflectionProbeAssetUVE(const std::filesystem::path& path,
                                               const BakedReflectionProbeAssetUVE& asset);
[[nodiscard]] bool LoadReflectionProbeAssetUVE(const std::filesystem::path& path,
                                               BakedReflectionProbeAssetUVE& outAsset);

/// 3D grid of L2 SH irradiance probes for trilinear dynamic-object global illumination.
struct GiProbeGridUVE final {
    Math::Vector3UVE boundsMin{-10.0F, 0.0F, -10.0F};
    Math::Vector3UVE boundsMax{10.0F, 10.0F, 10.0F};
    std::uint32_t countX = 1U;
    std::uint32_t countY = 1U;
    std::uint32_t countZ = 1U;
    std::vector<SphericalHarmonicsL2UVE> probes;

    [[nodiscard]] bool IsValidUVE() const noexcept {
        return countX > 0U && countY > 0U && countZ > 0U &&
               probes.size() == static_cast<std::size_t>(countX) * countY * countZ;
    }

    [[nodiscard]] Math::Vector3UVE SampleIrradianceUVE(const Math::Vector3UVE& worldPos,
                                                       const Math::Vector3UVE& normal) const noexcept;
};

struct LightmapBakeSettingsUVE final {
    std::uint32_t atlasWidth = 64U;
    std::uint32_t atlasHeight = 64U;
    std::uint32_t bounceCount = 1U;
    std::uint32_t probeCountX = 4U;
    std::uint32_t probeCountY = 2U;
    std::uint32_t probeCountZ = 4U;
    float bounceIntensity = 0.65F;
    Math::Vector3UVE skyAmbient{0.16F, 0.18F, 0.22F};
    Math::Vector3UVE groundAmbient{0.05F, 0.045F, 0.04F};
};

struct LightmapBakeResultUVE final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8;
    GiProbeGridUVE probeGrid{};
    std::size_t bakedTexels = 0U;
    std::size_t updatedSceneProbes = 0U;
};

/// Deterministic CPU hybrid GI baker: bakes a UV2 lightmap atlas plus a 3D L2 SH irradiance
/// probe grid (and updates any `LightmapProbe3DComponentUVE` entities in `entityManager`).
[[nodiscard]] LightmapBakeResultUVE BakeSceneLightmapUVE(Scene::IEntityManagerUVE& entityManager,
                                                         std::span<const LightDataUVE> lights,
                                                         const LightmapBakeSettingsUVE& settings = {});

} // namespace UVE::Render
