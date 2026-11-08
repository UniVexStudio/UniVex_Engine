// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/terrain_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsTerrain3DComponentValidUVE(const Terrain3DComponentUVE& value) noexcept {
    if (value.resolution < kMinimumTerrainResolutionUVE || value.resolution > kMaximumTerrainResolutionUVE) {
        return false;
    }
    if (!Math::IsFiniteUVE(value.size) || value.size.x <= 0.0F || value.size.y <= 0.0F) {
        return false;
    }
    if (!std::isfinite(value.heightScale) || value.heightScale < 0.0F) {
        return false;
    }
    const std::size_t expectedHeights = static_cast<std::size_t>(value.resolution) * value.resolution;
    if (!value.heightData.empty() && value.heightData.size() != expectedHeights) {
        return false;
    }
    if (!value.splatWeights.empty() && value.splatWeights.size() != expectedHeights * 4U) {
        return false;
    }
    return true;
}

Terrain3DComponentUVE SanitizeTerrain3DComponentUVE(const Terrain3DComponentUVE& value) noexcept {
    Terrain3DComponentUVE sanitized = value;
    sanitized.resolution = std::clamp(value.resolution, kMinimumTerrainResolutionUVE, kMaximumTerrainResolutionUVE);
    if (!Math::IsFiniteUVE(value.size) || value.size.x <= 0.0F || value.size.y <= 0.0F) {
        sanitized.size = Math::Vector2UVE{64.0F, 64.0F};
    }
    if (!std::isfinite(value.heightScale) || value.heightScale < 0.0F) {
        sanitized.heightScale = 10.0F;
    }
    const std::size_t expectedHeights = static_cast<std::size_t>(sanitized.resolution) * sanitized.resolution;
    if (sanitized.heightData.size() != expectedHeights) {
        sanitized.heightData.assign(expectedHeights, 0.0F);
    }
    if (sanitized.splatWeights.size() != expectedHeights * 4U) {
        sanitized.splatWeights.assign(expectedHeights * 4U, 0U);
        for (std::size_t i = 0; i < expectedHeights; ++i) {
            sanitized.splatWeights[i * 4U] = 255U; // Default to base layer
        }
    }
    return sanitized;
}

bool IsTerrain3DObjectDefinitionValidUVE(const Terrain3DObjectDefinitionUVE& value) noexcept {
    return IsTerrain3DComponentValidUVE(value.terrain);
}

void ApplyTerrain3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const Terrain3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, Terrain3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Terrain3DComponentUVE>(entity, value.terrain);
}

void Terrain3DUVE::InitializeHeightDataUVE(Terrain3DComponentUVE& terrain) {
    terrain = SanitizeTerrain3DComponentUVE(terrain);
}

float Terrain3DUVE::SampleHeightLocalUVE(const Terrain3DComponentUVE& terrain,
                                         const float localX, const float localZ) noexcept {
    if (terrain.heightData.empty() || terrain.resolution < 2U || terrain.size.x <= 0.0F || terrain.size.y <= 0.0F) {
        return 0.0F;
    }

    const float u = std::clamp(localX / terrain.size.x + 0.5F, 0.0F, 1.0F);
    const float v = std::clamp(localZ / terrain.size.y + 0.5F, 0.0F, 1.0F);

    const float gridCoordX = u * static_cast<float>(terrain.resolution - 1U);
    const float gridCoordZ = v * static_cast<float>(terrain.resolution - 1U);

    const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(gridCoordX), terrain.resolution - 1U);
    const std::uint32_t z0 = std::min(static_cast<std::uint32_t>(gridCoordZ), terrain.resolution - 1U);
    const std::uint32_t x1 = std::min(x0 + 1U, terrain.resolution - 1U);
    const std::uint32_t z1 = std::min(z0 + 1U, terrain.resolution - 1U);

    const float fx = gridCoordX - static_cast<float>(x0);
    const float fz = gridCoordZ - static_cast<float>(z0);

    const auto getHeight = [&terrain](const std::uint32_t x, const std::uint32_t z) -> float {
        const std::size_t idx = static_cast<std::size_t>(z) * terrain.resolution + x;
        return (idx < terrain.heightData.size()) ? terrain.heightData[idx] : 0.0F;
    };

    const float h00 = getHeight(x0, z0);
    const float h10 = getHeight(x1, z0);
    const float h01 = getHeight(x0, z1);
    const float h11 = getHeight(x1, z1);

    const float h0 = h00 * (1.0F - fx) + h10 * fx;
    const float h1 = h01 * (1.0F - fx) + h11 * fx;

    return (h0 * (1.0F - fz) + h1 * fz) * terrain.heightScale;
}

Math::Vector3UVE Terrain3DUVE::SampleNormalLocalUVE(const Terrain3DComponentUVE& terrain,
                                                   const float localX, const float localZ) noexcept {
    const float stepX = terrain.size.x / static_cast<float>(std::max(terrain.resolution - 1U, 1U));
    const float stepZ = terrain.size.y / static_cast<float>(std::max(terrain.resolution - 1U, 1U));

    const float hL = SampleHeightLocalUVE(terrain, localX - stepX, localZ);
    const float hR = SampleHeightLocalUVE(terrain, localX + stepX, localZ);
    const float hD = SampleHeightLocalUVE(terrain, localX, localZ - stepZ);
    const float hU = SampleHeightLocalUVE(terrain, localX, localZ + stepZ);

    const float nx = (hL - hR) / (2.0F * stepX);
    const float ny = 1.0F;
    const float nz = (hD - hU) / (2.0F * stepZ);

    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1.0e-6F) {
        const float invLen = 1.0F / len;
        return Math::Vector3UVE{nx * invLen, ny * invLen, nz * invLen};
    }
    return Math::Vector3UVE{0.0F, 1.0F, 0.0F};
}

bool Terrain3DUVE::GenerateMeshUVE(const Terrain3DComponentUVE& terrain,
                                   const std::uint32_t lodLevel, Asset::MeshAssetUVE& outMesh) {
    if (!IsTerrain3DComponentValidUVE(terrain)) {
        return false;
    }

    const std::uint32_t stride = 1U << std::min(lodLevel, 4U);
    const std::uint32_t gridCols = (terrain.resolution - 1U) / stride + 1U;
    const std::uint32_t gridRows = (terrain.resolution - 1U) / stride + 1U;

    if (gridCols < 2U || gridRows < 2U) {
        return false;
    }

    outMesh.vertices.clear();
    outMesh.indices.clear();
    outMesh.vertices.reserve(static_cast<std::size_t>(gridCols) * gridRows);
    outMesh.indices.reserve(static_cast<std::size_t>(gridCols - 1U) * (gridRows - 1U) * 6U);

    for (std::uint32_t r = 0U; r < gridRows; ++r) {
        const std::uint32_t srcZ = std::min(r * stride, terrain.resolution - 1U);
        const float v = static_cast<float>(srcZ) / static_cast<float>(terrain.resolution - 1U);
        const float posZ = (v - 0.5F) * terrain.size.y;

        for (std::uint32_t c = 0U; c < gridCols; ++c) {
            const std::uint32_t srcX = std::min(c * stride, terrain.resolution - 1U);
            const float u = static_cast<float>(srcX) / static_cast<float>(terrain.resolution - 1U);
            const float posX = (u - 0.5F) * terrain.size.x;

            const float posY = SampleHeightLocalUVE(terrain, posX, posZ);
            const Math::Vector3UVE normal = SampleNormalLocalUVE(terrain, posX, posZ);

            Asset::MeshVertexUVE vert{};
            vert.position = Math::Vector3UVE{posX, posY, posZ};
            vert.normal = normal;
            vert.u = u;
            vert.v = v;
            vert.tangent = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
            vert.tangentHandedness = 1.0F;

            outMesh.vertices.push_back(vert);
        }
    }

    for (std::uint32_t r = 0U; r < gridRows - 1U; ++r) {
        for (std::uint32_t c = 0U; c < gridCols - 1U; ++c) {
            const std::uint32_t i00 = r * gridCols + c;
            const std::uint32_t i10 = i00 + 1U;
            const std::uint32_t i01 = (r + 1U) * gridCols + c;
            const std::uint32_t i11 = i01 + 1U;

            // First triangle
            outMesh.indices.push_back(i00);
            outMesh.indices.push_back(i01);
            outMesh.indices.push_back(i10);

            // Second triangle
            outMesh.indices.push_back(i10);
            outMesh.indices.push_back(i01);
            outMesh.indices.push_back(i11);
        }
    }

    outMesh.localBounds = LocalBoundsUVE(terrain);
    return true;
}

void Terrain3DUVE::SculptUVE(Terrain3DComponentUVE& terrain, const float localCenterX, const float localCenterZ,
                             const float radius, const float deltaHeight, const bool flatten, const float targetHeight) {
    if (radius <= 0.0F || terrain.heightData.empty() || terrain.resolution < 2U) {
        return;
    }

    const float normDelta = (terrain.heightScale > 0.0F) ? (deltaHeight / terrain.heightScale) : deltaHeight;
    const float normTarget = (terrain.heightScale > 0.0F) ? (targetHeight / terrain.heightScale) : targetHeight;

    for (std::uint32_t z = 0U; z < terrain.resolution; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(terrain.resolution - 1U);
        const float posZ = (v - 0.5F) * terrain.size.y;
        const float dz = posZ - localCenterZ;

        for (std::uint32_t x = 0U; x < terrain.resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(terrain.resolution - 1U);
            const float posX = (u - 0.5F) * terrain.size.x;
            const float dx = posX - localCenterX;

            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) {
                continue;
            }

            const float t = 1.0F - (dist / radius);
            const float falloff = t * t * (3.0F - 2.0F * t); // Smoothstep falloff

            const std::size_t idx = static_cast<std::size_t>(z) * terrain.resolution + x;
            if (flatten) {
                terrain.heightData[idx] = std::clamp(
                    terrain.heightData[idx] + (normTarget - terrain.heightData[idx]) * falloff, 0.0F, 1.0F);
            } else {
                terrain.heightData[idx] = std::clamp(terrain.heightData[idx] + normDelta * falloff, 0.0F, 1.0F);
            }
        }
    }
}

void Terrain3DUVE::PaintSplatUVE(Terrain3DComponentUVE& terrain, const float localCenterX, const float localCenterZ,
                                 const float radius, const std::uint8_t layerIndex, const float weight) {
    if (radius <= 0.0F || terrain.splatWeights.empty() || layerIndex >= 4U || weight <= 0.0F) {
        return;
    }

    for (std::uint32_t z = 0U; z < terrain.resolution; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(terrain.resolution - 1U);
        const float posZ = (v - 0.5F) * terrain.size.y;
        const float dz = posZ - localCenterZ;

        for (std::uint32_t x = 0U; x < terrain.resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(terrain.resolution - 1U);
            const float posX = (u - 0.5F) * terrain.size.x;
            const float dx = posX - localCenterX;

            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) {
                continue;
            }

            const float t = 1.0F - (dist / radius);
            const float falloff = t * t * (3.0F - 2.0F * t);
            const float addAmount = weight * falloff * 255.0F;

            const std::size_t baseIdx = (static_cast<std::size_t>(z) * terrain.resolution + x) * 4U;
            const float current = static_cast<float>(terrain.splatWeights[baseIdx + layerIndex]);
            const float updated = std::min(current + addAmount, 255.0F);
            terrain.splatWeights[baseIdx + layerIndex] = static_cast<std::uint8_t>(updated);
        }
    }
}

Math::AabbUVE Terrain3DUVE::LocalBoundsUVE(const Terrain3DComponentUVE& terrain) noexcept {
    const float halfX = std::max(terrain.size.x * 0.5F, 0.5F);
    const float halfZ = std::max(terrain.size.y * 0.5F, 0.5F);
    const float halfH = std::max(terrain.heightScale * 0.5F, 0.5F);

    return Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{0.0F, halfH, 0.0F},
        Math::Vector3UVE{halfX, halfH, halfZ}
    );
}

} // namespace UVE::Scene
