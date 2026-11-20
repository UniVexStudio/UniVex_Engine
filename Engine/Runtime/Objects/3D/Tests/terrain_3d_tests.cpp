// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "terrain_3d.h"

#include <cmath>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Terrain3DUVETest, DefaultComponent_IsValid) {
    const Terrain3DComponentUVE terrain{};
    EXPECT_TRUE(IsTerrain3DComponentValidUVE(terrain));
    EXPECT_EQ(terrain.resolution, kDefaultTerrainResolutionUVE);
    EXPECT_FLOAT_EQ(terrain.size.x, 64.0F);
    EXPECT_FLOAT_EQ(terrain.size.y, 64.0F);
    EXPECT_FLOAT_EQ(terrain.heightScale, 10.0F);
}

TEST(Terrain3DUVETest, InvalidComponent_CaughtByValidator) {
    Terrain3DComponentUVE terrain{};

    terrain.resolution = 2U; // < kMinimumTerrainResolutionUVE
    EXPECT_FALSE(IsTerrain3DComponentValidUVE(terrain));
    terrain.resolution = 64U;

    terrain.size.x = -10.0F;
    EXPECT_FALSE(IsTerrain3DComponentValidUVE(terrain));
    terrain.size.x = 64.0F;

    terrain.heightScale = -5.0F;
    EXPECT_FALSE(IsTerrain3DComponentValidUVE(terrain));
    terrain.heightScale = 10.0F;

    terrain.heightData.assign(10U, 0.0F); // Mismatched size
    EXPECT_FALSE(IsTerrain3DComponentValidUVE(terrain));
}

TEST(Terrain3DUVETest, SanitizeComponent_FixesInvalidFields) {
    Terrain3DComponentUVE invalid{};
    invalid.resolution = 2U;
    invalid.size = Math::Vector2UVE{-10.0F, 0.0F};
    invalid.heightScale = -1.0F;

    const Terrain3DComponentUVE sanitized = SanitizeTerrain3DComponentUVE(invalid);
    EXPECT_TRUE(IsTerrain3DComponentValidUVE(sanitized));
    EXPECT_GE(sanitized.resolution, kMinimumTerrainResolutionUVE);
    EXPECT_GT(sanitized.size.x, 0.0F);
    EXPECT_GT(sanitized.size.y, 0.0F);
    EXPECT_GE(sanitized.heightScale, 0.0F);
    EXPECT_EQ(sanitized.heightData.size(), static_cast<std::size_t>(sanitized.resolution) * sanitized.resolution);
    EXPECT_EQ(sanitized.splatWeights.size(), static_cast<std::size_t>(sanitized.resolution) * sanitized.resolution * 4U);
}

TEST(Terrain3DUVETest, ObjectDefinition_AppliesTerrainComponent) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    const EntityUVE entity = entityManager.CreateEntityUVE();
    Terrain3DObjectDefinitionUVE definition{};
    definition.terrain.resolution = 32U;
    definition.terrain.heightScale = 20.0F;
    ApplyTerrain3DObjectDefinitionUVE(entityManager, entity, definition);

    ASSERT_TRUE(entityManager.HasComponentUVE<Terrain3DComponentUVE>(entity));
    const auto& terrain = entityManager.GetComponentUVE<Terrain3DComponentUVE>(entity);
    EXPECT_EQ(terrain.resolution, 32U);
    EXPECT_FLOAT_EQ(terrain.heightScale, 20.0F);
}

TEST(Terrain3DUVETest, SampleHeightLocal_InterpolatesBilinearly) {
    Terrain3DComponentUVE terrain{};
    terrain.resolution = 4U;
    terrain.size = Math::Vector2UVE{10.0F, 10.0F};
    terrain.heightScale = 10.0F;
    Terrain3DUVE::InitializeHeightDataUVE(terrain);

    // Set uniform height of 0.5 (representing 5.0m in world)
    std::fill(terrain.heightData.begin(), terrain.heightData.end(), 0.5F);
    const float sampled = Terrain3DUVE::SampleHeightLocalUVE(terrain, 0.0F, 0.0F);
    EXPECT_NEAR(sampled, 5.0F, 1.0e-4F);
}

TEST(Terrain3DUVETest, SampleNormalLocal_FlatTerrainProducesUpNormal) {
    Terrain3DComponentUVE terrain{};
    terrain.resolution = 8U;
    terrain.size = Math::Vector2UVE{10.0F, 10.0F};
    terrain.heightScale = 10.0F;
    Terrain3DUVE::InitializeHeightDataUVE(terrain);

    const Math::Vector3UVE normal = Terrain3DUVE::SampleNormalLocalUVE(terrain, 0.0F, 0.0F);
    EXPECT_NEAR(normal.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(normal.y, 1.0F, 1.0e-4F);
    EXPECT_NEAR(normal.z, 0.0F, 1.0e-4F);
}

TEST(Terrain3DUVETest, GenerateMesh_ConstructsValidMeshAsset) {
    Terrain3DComponentUVE terrain{};
    terrain.resolution = 16U;
    terrain.size = Math::Vector2UVE{32.0F, 32.0F};
    terrain.heightScale = 8.0F;
    Terrain3DUVE::InitializeHeightDataUVE(terrain);

    Asset::MeshAssetUVE meshLOD0{};
    EXPECT_TRUE(Terrain3DUVE::GenerateMeshUVE(terrain, 0U, meshLOD0));
    EXPECT_EQ(meshLOD0.vertices.size(), 16U * 16U);
    EXPECT_EQ(meshLOD0.indices.size(), 15U * 15U * 6U);

    Asset::MeshAssetUVE meshLOD1{};
    EXPECT_TRUE(Terrain3DUVE::GenerateMeshUVE(terrain, 1U, meshLOD1));
    // LOD 1 strides by 2
    EXPECT_LT(meshLOD1.vertices.size(), meshLOD0.vertices.size());
    EXPECT_LT(meshLOD1.indices.size(), meshLOD0.indices.size());
}

TEST(Terrain3DUVETest, Sculpt_RaisesAndFlattensHeightfield) {
    Terrain3DComponentUVE terrain{};
    terrain.resolution = 16U;
    terrain.size = Math::Vector2UVE{16.0F, 16.0F};
    terrain.heightScale = 10.0F;
    Terrain3DUVE::InitializeHeightDataUVE(terrain);

    // Sculpt a hill at center
    Terrain3DUVE::SculptUVE(terrain, 0.0F, 0.0F, 4.0F, 2.0F, false, 0.0F);
    const float hillHeight = Terrain3DUVE::SampleHeightLocalUVE(terrain, 0.0F, 0.0F);
    EXPECT_GT(hillHeight, 0.0F);

    // Flatten back to 1.0m
    Terrain3DUVE::SculptUVE(terrain, 0.0F, 0.0F, 4.0F, 0.0F, true, 1.0F);
    const float flatHeight = Terrain3DUVE::SampleHeightLocalUVE(terrain, 0.0F, 0.0F);
    EXPECT_NEAR(flatHeight, 1.0F, 0.1F);
}

TEST(Terrain3DUVETest, PaintSplat_ModulatesLayerWeights) {
    Terrain3DComponentUVE terrain{};
    terrain.resolution = 8U;
    terrain.size = Math::Vector2UVE{8.0F, 8.0F};
    Terrain3DUVE::InitializeHeightDataUVE(terrain);

    // Initially layer 1 (e.g. dirt) is 0
    const std::size_t centerIdx = (4U * terrain.resolution + 4U) * 4U + 1U;
    EXPECT_EQ(terrain.splatWeights[centerIdx], 0U);

    // Paint dirt at center
    Terrain3DUVE::PaintSplatUVE(terrain, 0.0F, 0.0F, 2.0F, 1U, 0.8F);
    EXPECT_GT(terrain.splatWeights[centerIdx], 0U);
}

TEST(Terrain3DUVETest, LocalBounds_EnclosesHeightScale) {
    Terrain3DComponentUVE terrain{};
    terrain.size = Math::Vector2UVE{50.0F, 40.0F};
    terrain.heightScale = 15.0F;

    const Math::AabbUVE bounds = Terrain3DUVE::LocalBoundsUVE(terrain);
    const Math::Vector3UVE extents = bounds.GetExtentsUVE();
    EXPECT_FLOAT_EQ(extents.x, 25.0F);
    EXPECT_FLOAT_EQ(extents.y, 7.5F);
    EXPECT_FLOAT_EQ(extents.z, 20.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
