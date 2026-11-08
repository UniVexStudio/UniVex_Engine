// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::uint32_t kMinimumTerrainResolutionUVE = 4U;
inline constexpr std::uint32_t kMaximumTerrainResolutionUVE = 1024U;
inline constexpr std::uint32_t kDefaultTerrainResolutionUVE = 64U;

/// Real-time 3D Terrain component: heightfield grid, sculpting, multi-texture splatting,
/// and procedural mesh generation with multi-level LOD.
struct Terrain3DComponentUVE final {
    /// Number of heightfield grid vertices along each axis (e.g. 64x64).
    std::uint32_t resolution = kDefaultTerrainResolutionUVE;
    /// Horizontal dimensions in meters (XZ plane).
    Math::Vector2UVE size{64.0F, 64.0F};
    /// Maximum vertical height extent (Y axis).
    float heightScale = 10.0F;

    /// Normalized heightfield data in [0, 1] range, sized resolution * resolution.
    std::vector<float> heightData;

    /// 4-channel texture splat weights (RGBA) for multi-layer terrain shading (e.g. grass, dirt, rock, snow),
    /// sized resolution * resolution * 4 bytes.
    std::vector<std::uint8_t> splatWeights;

    /// Surface material for terrain rendering.
    Asset::AssetGuidUVE materialGuid{};

    [[nodiscard]] bool operator==(const Terrain3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsTerrain3DComponentValidUVE(const Terrain3DComponentUVE& value) noexcept;

[[nodiscard]] Terrain3DComponentUVE SanitizeTerrain3DComponentUVE(const Terrain3DComponentUVE& value) noexcept;

struct Terrain3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Terrain3D";
    Terrain3DComponentUVE terrain{};
};

[[nodiscard]] bool IsTerrain3DObjectDefinitionValidUVE(const Terrain3DObjectDefinitionUVE& value) noexcept;

void ApplyTerrain3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Terrain3DObjectDefinitionUVE& value);

class Terrain3DUVE final {
public:
    Terrain3DUVE() = delete;

    /// Allocates and initializes flat heightfield and default splat weights for the configured resolution.
    static void InitializeHeightDataUVE(Terrain3DComponentUVE& terrain);

    /// Samples the heightfield at local XZ coordinates using bilinear filtering.
    [[nodiscard]] static float SampleHeightLocalUVE(const Terrain3DComponentUVE& terrain,
                                                    float localX, float localZ) noexcept;

    /// Samples the surface normal at local XZ coordinates via central finite differences.
    [[nodiscard]] static Math::Vector3UVE SampleNormalLocalUVE(const Terrain3DComponentUVE& terrain,
                                                               float localX, float localZ) noexcept;

    /// Generates a triangulated 3D mesh for the terrain at the specified LOD stride.
    [[nodiscard]] static bool GenerateMeshUVE(const Terrain3DComponentUVE& terrain,
                                             std::uint32_t lodLevel, Asset::MeshAssetUVE& outMesh);

    /// Sculpt tool: applies a circular brush raising/lowering or flattening heightfield values.
    static void SculptUVE(Terrain3DComponentUVE& terrain, float localCenterX, float localCenterZ,
                          float radius, float deltaHeight, bool flatten = false, float targetHeight = 0.0F);

    /// Paint tool: paints splat weights for a specific texture layer (0..3) with a circular brush.
    static void PaintSplatUVE(Terrain3DComponentUVE& terrain, float localCenterX, float localCenterZ,
                              float radius, std::uint8_t layerIndex, float weight);

    /// Computes local-space axis-aligned bounding box enclosing the terrain.
    [[nodiscard]] static Math::AabbUVE LocalBoundsUVE(const Terrain3DComponentUVE& terrain) noexcept;
};

} // namespace UVE::Scene
