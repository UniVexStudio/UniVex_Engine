// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gi_reflection_probes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "all_objects_3d.h"
#include "primitive_mesh_component.h"
#include "scalar.h"
#include "world_transform_component.h"

namespace UVE::Render {
namespace {

constexpr char kProbeMagic[4] = {'U', 'V', 'P', 'B'};
constexpr std::uint32_t kProbeVersion = 1U;

[[nodiscard]] std::array<float, 9U> EvaluateShL2Basis(const Math::Vector3& d) noexcept {
    constexpr float c0 = 0.28209479F;
    constexpr float c1 = 0.48860251F;
    constexpr float c2 = 1.09254843F;
    constexpr float c3 = 0.31539157F;
    constexpr float c4 = 0.54627421F;
    return {
        c0,
        c1 * d.y,
        c1 * d.z,
        c1 * d.x,
        c2 * d.x * d.y,
        c2 * d.y * d.z,
        c3 * (3.0F * d.z * d.z - 1.0F),
        c2 * d.x * d.z,
        c4 * (d.x * d.x - d.y * d.y),
    };
}

[[nodiscard]] Math::Vector3 CubemapFaceDirection(const std::size_t face, const float u,
                                                       const float v) noexcept {
    Math::Vector3 dir{};
    switch (face) {
        case 0U: dir = {1.0F, -v, -u}; break;   // +X
        case 1U: dir = {-1.0F, -v, u}; break;   // -X
        case 2U: dir = {u, 1.0F, v}; break;     // +Y
        case 3U: dir = {u, -1.0F, -v}; break;   // -Y
        case 4U: dir = {u, -v, 1.0F}; break;    // +Z
        default: dir = {-u, -v, -1.0F}; break;  // -Z
    }
    return Math::Normalize(dir);
}

[[nodiscard]] float SrgbByteToLinear(const std::uint8_t byte) noexcept {
    const float c = static_cast<float>(byte) / 255.0F;
    return c <= 0.04045F ? (c / 12.92F) : std::pow((c + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] std::uint8_t LinearToSrgbByte(const float value) noexcept {
    const float c = std::clamp(value, 0.0F, 1.0F);
    const float srgb = c <= 0.0031308F ? (c * 12.92F) : (1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F);
    return static_cast<std::uint8_t>(std::clamp(std::lround(srgb * 255.0F), 0L, 255L));
}

[[nodiscard]] Math::Vector3 EvaluateDirectAtPoint(const Math::Vector3& worldPos,
                                                        const Math::Vector3& normal,
                                                        const std::span<const LightData> lights) noexcept {
    Math::Vector3 sum{0.0F, 0.0F, 0.0F};
    for (const LightData& light : lights) {
        if (light.intensity <= 0.0F) {
            continue;
        }
        Math::Vector3 dir{};
        float atten = 1.0F;
        if (light.type == Scene::LightType::Directional) {
            dir = Math::Normalize(light.direction * -1.0F);
        } else {
            const Math::Vector3 delta = light.position - worldPos;
            const float dist = Math::Length(delta);
            if (dist <= 1.0e-4F) {
                continue;
            }
            dir = delta * (1.0F / dist);
            atten = 1.0F / std::max(dist * dist, 0.25F);
            if (light.range > 0.0F) {
                if (dist >= light.range) {
                    continue;
                }
                const float r = dist / light.range;
                const float w = std::clamp(1.0F - r * r * r * r, 0.0F, 1.0F);
                atten *= w * w;
            }
        }
        const float ndl = std::max(Math::Dot(normal, dir), 0.0F);
        if (ndl > 0.0F) {
            sum.x += light.color.r * light.intensity * atten * ndl;
            sum.y += light.color.g * light.intensity * atten * ndl;
            sum.z += light.color.b * light.intensity * atten * ndl;
        }
    }
    return sum;
}

} // namespace

bool SphericalHarmonicsL2::IsZero() const noexcept {
    for (const Math::Vector3& c : coefficients) {
        if (std::abs(c.x) > 1.0e-7F || std::abs(c.y) > 1.0e-7F || std::abs(c.z) > 1.0e-7F) {
            return false;
        }
    }
    return true;
}

void AccumulateShL2Sample(SphericalHarmonicsL2& sh, const Math::Vector3& dir,
                             const Math::Vector3& radiance, const float weight) noexcept {
    const std::array<float, 9U> basis = EvaluateShL2Basis(dir);
    for (std::size_t i = 0U; i < 9U; ++i) {
        const float w = basis[i] * weight;
        sh.coefficients[i].x += radiance.x * w;
        sh.coefficients[i].y += radiance.y * w;
        sh.coefficients[i].z += radiance.z * w;
    }
}

SphericalHarmonicsL2 ProjectCubemapRgba8ToShL2(
    const std::array<std::span<const std::uint8_t>, 6U>& faces, const std::uint32_t faceSize) noexcept {
    SphericalHarmonicsL2 sh{};
    if (faceSize == 0U) {
        return sh;
    }
    const std::size_t expectedBytes = static_cast<std::size_t>(faceSize) * faceSize * 4U;
    float weightSum = 0.0F;
    const float invSize = 1.0F / static_cast<float>(faceSize);
    for (std::size_t face = 0U; face < 6U; ++face) {
        if (faces[face].size() < expectedBytes) {
            continue;
        }
        for (std::uint32_t y = 0U; y < faceSize; ++y) {
            const float v = ((static_cast<float>(y) + 0.5F) * invSize) * 2.0F - 1.0F;
            for (std::uint32_t x = 0U; x < faceSize; ++x) {
                const float u = ((static_cast<float>(x) + 0.5F) * invSize) * 2.0F - 1.0F;
                const float temp = 1.0F + u * u + v * v;
                const float solidAngle = 4.0F / (std::sqrt(temp) * temp);
                const std::size_t px = (static_cast<std::size_t>(y) * faceSize + x) * 4U;
                const Math::Vector3 radiance{
                    SrgbByteToLinear(faces[face][px + 0U]),
                    SrgbByteToLinear(faces[face][px + 1U]),
                    SrgbByteToLinear(faces[face][px + 2U]),
                };
                AccumulateShL2Sample(sh, CubemapFaceDirection(face, u, v), radiance, solidAngle);
                weightSum += solidAngle;
            }
        }
    }
    if (weightSum > 1.0e-5F) {
        const float norm = (4.0F * Math::kPi) / weightSum;
        for (Math::Vector3& c : sh.coefficients) {
            c = c * norm;
        }
    }
    return sh;
}

Math::Vector3 EvaluateShL2Irradiance(const SphericalHarmonicsL2& sh,
                                           const Math::Vector3& normal) noexcept {
    const Math::Vector3 n = Math::Normalize(normal);
    const std::array<float, 9U> basis = EvaluateShL2Basis(n);
    // Cosine-lobe convolution weights (Ramamoorthi-Hanrahan): A0 = pi, A1 = 2*pi/3, A2 = pi/4, divided by pi.
    constexpr std::array<float, 9U> kConv{1.0F, 0.6666667F, 0.6666667F, 0.6666667F,
                                          0.25F, 0.25F, 0.25F, 0.25F, 0.25F};
    Math::Vector3 out{0.0F, 0.0F, 0.0F};
    for (std::size_t i = 0U; i < 9U; ++i) {
        const float w = basis[i] * kConv[i];
        out.x += sh.coefficients[i].x * w;
        out.y += sh.coefficients[i].y * w;
        out.z += sh.coefficients[i].z * w;
    }
    return Math::Vector3{std::max(out.x, 0.0F), std::max(out.y, 0.0F), std::max(out.z, 0.0F)};
}

SphericalHarmonicsL2 BlendShL2(const SphericalHarmonicsL2& a,
                                     const SphericalHarmonicsL2& b, const float t) noexcept {
    const float w = std::clamp(t, 0.0F, 1.0F);
    SphericalHarmonicsL2 out{};
    for (std::size_t i = 0U; i < 9U; ++i) {
        out.coefficients[i] = a.coefficients[i] * (1.0F - w) + b.coefficients[i] * w;
    }
    return out;
}

bool SaveReflectionProbeAsset(const std::filesystem::path& path, const BakedReflectionProbeAsset& asset) {
    if (asset.faceSize == 0U || asset.faceSize > 2048U) {
        return false;
    }
    const std::size_t faceBytes = static_cast<std::size_t>(asset.faceSize) * asset.faceSize * 4U;
    for (std::size_t f = 0U; f < 6U; ++f) {
        if (asset.faceRgba8[f].size() != faceBytes) {
            return false;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(kProbeMagic, 4);
    out.write(reinterpret_cast<const char*>(&kProbeVersion), sizeof(kProbeVersion));
    out.write(reinterpret_cast<const char*>(&asset.faceSize), sizeof(asset.faceSize));
    for (const Math::Vector3& c : asset.diffuseSh.coefficients) {
        const float rgb[3] = {c.x, c.y, c.z};
        out.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }
    for (std::size_t f = 0U; f < 6U; ++f) {
        out.write(reinterpret_cast<const char*>(asset.faceRgba8[f].data()),
                  static_cast<std::streamsize>(faceBytes));
    }
    return out.good();
}

bool LoadReflectionProbeAsset(const std::filesystem::path& path, BakedReflectionProbeAsset& outAsset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    char magic[4]{};
    std::uint32_t version = 0U;
    std::uint32_t faceSize = 0U;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&faceSize), sizeof(faceSize));
    if (!in || std::memcmp(magic, kProbeMagic, 4) != 0 || version != kProbeVersion ||
        faceSize == 0U || faceSize > 2048U) {
        return false;
    }
    BakedReflectionProbeAsset loaded{};
    loaded.faceSize = faceSize;
    for (Math::Vector3& c : loaded.diffuseSh.coefficients) {
        float rgb[3]{};
        in.read(reinterpret_cast<char*>(rgb), sizeof(rgb));
        c = Math::Vector3{rgb[0], rgb[1], rgb[2]};
    }
    const std::size_t faceBytes = static_cast<std::size_t>(faceSize) * faceSize * 4U;
    for (std::size_t f = 0U; f < 6U; ++f) {
        loaded.faceRgba8[f].resize(faceBytes);
        in.read(reinterpret_cast<char*>(loaded.faceRgba8[f].data()), static_cast<std::streamsize>(faceBytes));
    }
    if (!in) {
        return false;
    }
    outAsset = std::move(loaded);
    return true;
}

Math::Vector3 GiProbeGrid::SampleIrradiance(const Math::Vector3& worldPos,
                                                     const Math::Vector3& normal) const noexcept {
    if (!IsValid()) {
        return Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    const Math::Vector3 extent = boundsMax - boundsMin;
    const float fx = extent.x > 1.0e-4F
                         ? std::clamp((worldPos.x - boundsMin.x) / extent.x, 0.0F, 1.0F) *
                               static_cast<float>(countX - 1U)
                         : 0.0F;
    const float fy = extent.y > 1.0e-4F
                         ? std::clamp((worldPos.y - boundsMin.y) / extent.y, 0.0F, 1.0F) *
                               static_cast<float>(countY - 1U)
                         : 0.0F;
    const float fz = extent.z > 1.0e-4F
                         ? std::clamp((worldPos.z - boundsMin.z) / extent.z, 0.0F, 1.0F) *
                               static_cast<float>(countZ - 1U)
                         : 0.0F;

    const auto x0 = static_cast<std::uint32_t>(fx);
    const auto y0 = static_cast<std::uint32_t>(fy);
    const auto z0 = static_cast<std::uint32_t>(fz);
    const std::uint32_t x1 = std::min(x0 + 1U, countX - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, countY - 1U);
    const std::uint32_t z1 = std::min(z0 + 1U, countZ - 1U);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);

    const auto at = [&](const std::uint32_t x, const std::uint32_t y, const std::uint32_t z) -> const SphericalHarmonicsL2& {
        return probes[(static_cast<std::size_t>(z) * countY + y) * countX + x];
    };
    const SphericalHarmonicsL2 c00 = BlendShL2(at(x0, y0, z0), at(x1, y0, z0), tx);
    const SphericalHarmonicsL2 c10 = BlendShL2(at(x0, y1, z0), at(x1, y1, z0), tx);
    const SphericalHarmonicsL2 c01 = BlendShL2(at(x0, y0, z1), at(x1, y0, z1), tx);
    const SphericalHarmonicsL2 c11 = BlendShL2(at(x0, y1, z1), at(x1, y1, z1), tx);
    const SphericalHarmonicsL2 c0 = BlendShL2(c00, c10, ty);
    const SphericalHarmonicsL2 c1 = BlendShL2(c01, c11, ty);
    return EvaluateShL2Irradiance(BlendShL2(c0, c1, tz), normal);
}

LightmapBakeResult BakeSceneLightmap(Scene::IEntityManager& entityManager,
                                           const std::span<const LightData> lights,
                                           const LightmapBakeSettings& settings) {
    LightmapBakeResult result{};
    result.width = std::clamp(settings.atlasWidth, 4U, 1024U);
    result.height = std::clamp(settings.atlasHeight, 4U, 1024U);
    result.rgba8.assign(static_cast<std::size_t>(result.width) * result.height * 4U, 0U);

    // Derive GI bake bounds from LightmapGI3D volumes if present, else scene bounds.
    Math::Vector3 bMin{-10.0F, 0.0F, -10.0F};
    Math::Vector3 bMax{10.0F, 10.0F, 10.0F};
    bool foundVolume = false;
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::LightmapGI3DComponent>(
        [&](const Scene::Entity, const Scene::WorldTransformComponent& wt,
            const Scene::LightmapGI3DComponent& gi) {
            if (!gi.enabled || wt.dirty) {
                return;
            }
            const Math::Vector3 lo = wt.worldPosition - gi.halfExtents;
            const Math::Vector3 hi = wt.worldPosition + gi.halfExtents;
            if (!foundVolume) {
                bMin = lo;
                bMax = hi;
                foundVolume = true;
            } else {
                bMin = Math::Vector3{std::min(bMin.x, lo.x), std::min(bMin.y, lo.y), std::min(bMin.z, lo.z)};
                bMax = Math::Vector3{std::max(bMax.x, hi.x), std::max(bMax.y, hi.y), std::max(bMax.z, hi.z)};
            }
        });

    GiProbeGrid& grid = result.probeGrid;
    grid.boundsMin = bMin;
    grid.boundsMax = bMax;
    grid.countX = std::clamp(settings.probeCountX, 1U, 16U);
    grid.countY = std::clamp(settings.probeCountY, 1U, 8U);
    grid.countZ = std::clamp(settings.probeCountZ, 1U, 16U);
    grid.probes.assign(static_cast<std::size_t>(grid.countX) * grid.countY * grid.countZ, {});

    constexpr std::array<Math::Vector3, 6U> kSixDirs{{
        {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -1.0F},
    }};
    const float dirWeight = (4.0F * Math::kPi) / static_cast<float>(kSixDirs.size());

    for (std::uint32_t z = 0U; z < grid.countZ; ++z) {
        const float tz = grid.countZ > 1U ? static_cast<float>(z) / static_cast<float>(grid.countZ - 1U) : 0.5F;
        for (std::uint32_t y = 0U; y < grid.countY; ++y) {
            const float ty = grid.countY > 1U ? static_cast<float>(y) / static_cast<float>(grid.countY - 1U) : 0.5F;
            for (std::uint32_t x = 0U; x < grid.countX; ++x) {
                const float tx = grid.countX > 1U ? static_cast<float>(x) / static_cast<float>(grid.countX - 1U) : 0.5F;
                const Math::Vector3 pos{
                    bMin.x + (bMax.x - bMin.x) * tx,
                    bMin.y + (bMax.y - bMin.y) * ty,
                    bMin.z + (bMax.z - bMin.z) * tz,
                };
                SphericalHarmonicsL2& sh =
                    grid.probes[(static_cast<std::size_t>(z) * grid.countY + y) * grid.countX + x];
                for (const Math::Vector3& dir : kSixDirs) {
                    const float hemi = dir.y * 0.5F + 0.5F;
                    const Math::Vector3 env =
                        settings.groundAmbient * (1.0F - hemi) + settings.skyAmbient * hemi;
                    const Math::Vector3 direct = EvaluateDirectAtPoint(pos, dir, lights);
                    const Math::Vector3 bounce = direct * (settings.bounceIntensity / Math::kPi);
                    AccumulateShL2Sample(sh, dir, env + bounce, dirWeight);
                }
            }
        }
    }

    // Update any authored LightmapProbe3DComponent entities with their baked irradiance.
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::LightmapProbe3DComponent>(
        [&](const Scene::Entity, const Scene::WorldTransformComponent& wt,
            Scene::LightmapProbe3DComponent& probe) {
            if (!probe.enabled || wt.dirty) {
                return;
            }
            const Math::Vector3 irr = grid.SampleIrradiance(wt.worldPosition, Math::Vector3{0.0F, 1.0F, 0.0F});
            probe.sampledIrradiance = Math::Color{irr.x, irr.y, irr.z};
            ++result.updatedSceneProbes;
        });

    // Bake planar UV2 irradiance atlas across boundsMin..boundsMax XZ footprint.
    const float midY = (bMin.y + bMax.y) * 0.5F;
    const Math::Vector3 up{0.0F, 1.0F, 0.0F};
    for (std::uint32_t y = 0U; y < result.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(result.height);
        for (std::uint32_t x = 0U; x < result.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(result.width);
            const Math::Vector3 worldPos{
                bMin.x + (bMax.x - bMin.x) * u,
                midY,
                bMin.z + (bMax.z - bMin.z) * v,
            };
            const Math::Vector3 irr = grid.SampleIrradiance(worldPos, up);
            const std::size_t idx = (static_cast<std::size_t>(y) * result.width + x) * 4U;
            result.rgba8[idx + 0U] = LinearToSrgbByte(irr.x);
            result.rgba8[idx + 1U] = LinearToSrgbByte(irr.y);
            result.rgba8[idx + 2U] = LinearToSrgbByte(irr.z);
            result.rgba8[idx + 3U] = 255U;
            ++result.bakedTexels;
        }
    }
    return result;
}

} // namespace UVE::Render
