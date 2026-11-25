// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "asset_guid.h"
#include "mesh_asset.h"
#include "entity.h"
#include "aabb.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::uint32_t kMinimumTerrainResolution = 4U;
inline constexpr std::uint32_t kMaximumTerrainResolution = 1024U;
inline constexpr std::uint32_t kDefaultTerrainResolution = 64U;

/// Real-time 3D Terrain component: heightfield grid, sculpting, multi-texture splatting,
/// and procedural mesh generation with multi-level LOD.
struct Terrain3DComponent final {
    /// Number of heightfield grid vertices along each axis (e.g. 64x64).
    std::uint32_t resolution = kDefaultTerrainResolution;
    /// Horizontal dimensions in meters (XZ plane).
    Math::Vector2 size{64.0F, 64.0F};
    /// Maximum vertical height extent (Y axis).
    float heightScale = 10.0F;

    /// Normalized heightfield data in [0, 1] range, sized resolution * resolution.
    std::vector<float> heightData;

    /// 4-channel texture splat weights (RGBA) for multi-layer terrain shading (e.g. grass, dirt, rock, snow),
    /// sized resolution * resolution * 4 bytes.
    std::vector<std::uint8_t> splatWeights;

    /// Surface material for terrain rendering.
    Asset::AssetGuid materialGuid{};

    [[nodiscard]] bool operator==(const Terrain3DComponent&) const = default;
};

[[nodiscard]] bool IsTerrain3DComponentValid(const Terrain3DComponent& value) noexcept;

[[nodiscard]] Terrain3DComponent SanitizeTerrain3DComponent(const Terrain3DComponent& value) noexcept;

struct Terrain3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Terrain3D";
    Terrain3DComponent terrain{};
};

[[nodiscard]] bool IsTerrain3DObjectDefinitionValid(const Terrain3DObjectDefinition& value) noexcept;

void ApplyTerrain3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const Terrain3DObjectDefinition& value);

class Terrain3D final {
public:
    Terrain3D() = delete;

    /// Allocates and initializes flat heightfield and default splat weights for the configured resolution.
    static void InitializeHeightData(Terrain3DComponent& terrain);

    /// Samples the heightfield at local XZ coordinates using bilinear filtering.
    [[nodiscard]] static float SampleHeightLocal(const Terrain3DComponent& terrain,
                                                    float localX, float localZ) noexcept;

    /// Samples the surface normal at local XZ coordinates via central finite differences.
    [[nodiscard]] static Math::Vector3 SampleNormalLocal(const Terrain3DComponent& terrain,
                                                               float localX, float localZ) noexcept;

    /// Generates a triangulated 3D mesh for the terrain at the specified LOD stride.
    [[nodiscard]] static bool GenerateMesh(const Terrain3DComponent& terrain,
                                             std::uint32_t lodLevel, Asset::MeshAsset& outMesh);

    /// Sculpt tool: applies a circular brush raising/lowering or flattening heightfield values.
    static void Sculpt(Terrain3DComponent& terrain, float localCenterX, float localCenterZ,
                          float radius, float deltaHeight, bool flatten = false, float targetHeight = 0.0F);

    /// Paint tool: paints splat weights for a specific texture layer (0..3) with a circular brush.
    static void PaintSplat(Terrain3DComponent& terrain, float localCenterX, float localCenterZ,
                              float radius, std::uint8_t layerIndex, float weight);

    /// Computes local-space axis-aligned bounding box enclosing the terrain.
    [[nodiscard]] static Math::Aabb LocalBounds(const Terrain3DComponent& terrain) noexcept;
};

} // namespace UVE::Scene
