// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mesh_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_manager.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "mesh_component.h"
#include "render_instance_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "dynamic_mesh_bvh.h"
#include "mesh_visibility_set.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterations = 200000;

class MeshRendererSpatialBvhTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Threading::ThreadPool threadPool{2};
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};
    MeshRenderer meshRenderer;

    Scene::Entity MakeMeshEntity(const Math::Vector3 worldPosition,
                                       const Asset::AssetGuid meshGuid,
                                       const Asset::AssetGuid materialGuid) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::MeshComponent>(
            entity, Scene::MeshComponent{meshGuid, materialGuid});
        return entity;
    }

    [[nodiscard]] static Math::Frustum MakeCameraFrustum(
        const Math::Vector3 cameraPosition = Math::Vector3{0.0F, 0.0F, 0.0F},
        const Math::Quaternion cameraRotation = Math::Quaternion{}) {
        const Math::Matrix4x4 view =
            Math::Matrix4x4::ViewFromPositionAndRotation(cameraPosition, cameraRotation);
        const Math::Matrix4x4 projection =
            Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::Frustum::FromViewProjection(projection * view);
    }

    void RegisterImmediateLoaders(const bool materialIsTransparent = false) {
        assetManager.RegisterLoader<Asset::MeshAsset>(
            [](const std::filesystem::path&, Asset::MeshAsset& mesh) {
                mesh.localBounds = Math::Aabb::FromCenterExtents(
                    Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
                return true;
            });
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [materialIsTransparent](const std::filesystem::path&, Asset::MaterialAsset& material) {
                material.isTransparent = materialIsTransparent;
                return true;
            });
    }

    void WaitUntilAssetsReady(const Asset::AssetGuid meshGuid, const Asset::AssetGuid materialGuid) {
        Asset::AssetHandle<Asset::MeshAsset> meshHandle =
            assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
        Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
            assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            if (meshHandle.IsReady() && materialHandle.IsReady()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(meshHandle.IsReady());
        ASSERT_TRUE(materialHandle.IsReady());
    }
};

// ---------------------------------------------------------------------------
// 1. Off-screen entities are pruned early without evaluating placement cache
//    or layer checks
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, OffScreenEntities_PrunedEarlyWithoutPlacementCacheOrLayerChecks) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_prune_test.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_prune_test.uvmat");

    // Entity 1: in front of the camera (Z = -10, inside frustum near=1..far=100)
    const Scene::Entity visibleEntity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Entity 2: far behind camera (Z = +500, completely outside camera frustum)
    // Configure an off-layer mask on Entity 2 that WOULD fail view layer mask check if inspected
    const Scene::Entity offScreenEntity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponent offScreenInstance;
    offScreenInstance.renderLayers = 0x00000004U; // Layer 3 only
    entityManager.AddComponent<Scene::RenderInstanceComponent>(offScreenEntity, offScreenInstance);

    WaitUntilAssetsReady(meshGuid, materialGuid);

    // Initial sync of spatial BVH
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvh().GetEntityCount(), 2U);

    // Configure visibility set to only look at layer 1
    MeshVisibilitySet visibilitySet;
    visibilitySet.viewLayerMask = 0x00000001U; // Layer 1 only

    const Math::Frustum cameraFrustum = MakeCameraFrustum();

    // Query with cull frustum
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

    // Verification 1: Only the visible entity is admitted to candidates
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, visibleEntity);

    // Verification 2: The placement cache was evaluated ONLY for the visible entity.
    // The off-screen entity's placement was never computed, cached, or keyed.
    EXPECT_EQ(visibilitySet.placementCache.count(visibleEntity), 1U);
    EXPECT_EQ(visibilitySet.placementCache.count(offScreenEntity), 0U);
    EXPECT_EQ(visibilitySet.placementCacheHits + visibilitySet.placementCacheMisses, 1U);

    // Verification 3: The off-screen entity's render layers were NEVER checked!
    // If it had been visited by the entity walk, layerCulledEntities would have incremented to 1.
    // Because the BVH pruned its subtree early, layerCulledEntities remains 0.
    EXPECT_EQ(visibilitySet.layerCulledEntities, 0U);
}

TEST_F(MeshRendererSpatialBvhTest, ManyOffScreenEntities_ZeroPlacementCachePollution) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_crowd.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_crowd.uvmat");

    // 5 visible entities
    std::vector<Scene::Entity> visibleEntities;
    for (int i = 0; i < 5; ++i) {
        const auto entity = MakeMeshEntity(
            Math::Vector3{static_cast<float>(i - 2) * 2.0F, 0.0F, -15.0F}, meshGuid, materialGuid);
        visibleEntities.push_back(entity);
    }

    // 50 off-screen entities placed behind camera (+100 to +300 Z)
    std::vector<Scene::Entity> offScreenEntities;
    for (int i = 0; i < 50; ++i) {
        const auto entity = MakeMeshEntity(
            Math::Vector3{static_cast<float>(i % 5) * 5.0F, 0.0F, 100.0F + static_cast<float>(i) * 4.0F},
            meshGuid, materialGuid);
        offScreenEntities.push_back(entity);
    }

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvh().GetEntityCount(), 55U);

    MeshVisibilitySet visibilitySet;
    const Math::Frustum cameraFrustum = MakeCameraFrustum();

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

    // Exactly 5 candidates survive
    EXPECT_EQ(visibilitySet.candidates.size(), 5U);

    // Placement cache has exactly 5 entries, zero entries for the 50 off-screen entities
    EXPECT_EQ(visibilitySet.placementCache.size(), 5U);
    EXPECT_EQ(visibilitySet.placementCacheHits + visibilitySet.placementCacheMisses, 5U);
    for (const auto entity : offScreenEntities) {
        EXPECT_EQ(visibilitySet.placementCache.find(entity), visibilitySet.placementCache.end());
    }
}

// ---------------------------------------------------------------------------
// 2. Visible entities are preserved and clustered correctly
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, VisibleEntities_PreservedAndClusteredCorrectly) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_cluster.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_cluster.uvmat");

    // Create 12 visible entities spread in a grid in front of camera
    std::vector<Scene::Entity> createdEntities;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const auto entity = MakeMeshEntity(
                Math::Vector3{static_cast<float>(x) * 3.0F, static_cast<float>(y) * 3.0F, -20.0F},
                meshGuid, materialGuid);
            createdEntities.push_back(entity);
        }
    }

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);

    MeshVisibilitySet visibilitySet;
    const Math::Frustum cameraFrustum = MakeCameraFrustum();

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

    // All 9 visible entities survive
    EXPECT_EQ(visibilitySet.candidates.size(), 9U);

    // Spatial clustering generated clusters covering all candidates
    EXPECT_FALSE(visibilitySet.clusters.empty());
    std::size_t totalClusteredCandidates = 0U;
    for (const auto& cluster : visibilitySet.clusters) {
        EXPECT_GT(cluster.count, 0U);
        EXPECT_TRUE(cluster.bounds.min.x <= cluster.bounds.max.x);
        EXPECT_TRUE(cluster.bounds.min.y <= cluster.bounds.max.y);
        EXPECT_TRUE(cluster.bounds.min.z <= cluster.bounds.max.z);
        totalClusteredCandidates += cluster.count;
    }
    EXPECT_EQ(totalClusteredCandidates, visibilitySet.candidates.size());

    // Cull into RenderQueue and verify queue contains all items
    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, cameraFrustum, queue);
    EXPECT_EQ(queue.opaqueItems.size(), 9U);
}

// ---------------------------------------------------------------------------
// 3. Dynamic movement updates the spatial index
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, DynamicMovement_UpdatesSpatialIndexAndVisibility) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_move.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_move.uvmat");

    // Entity starts off-screen behind the camera (+200 Z)
    const Scene::Entity movingEntity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 200.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);

    const Math::Frustum cameraFrustum = MakeCameraFrustum();

    // Frame 1: entity is off-screen, pruned early
    {
        MeshVisibilitySet visibilitySet;
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        EXPECT_EQ(visibilitySet.candidates.size(), 0U);
        EXPECT_EQ(visibilitySet.placementCache.count(movingEntity), 0U);
    }

    // Move entity dynamically into view (-15 Z). SetLocalTransform marks the world transform
    // dirty so the scene graph sweep recomputes it - direct member writes would leave it stale.
    sceneGraph.SetLocalTransform(
        entityManager, movingEntity,
        Scene::TransformComponent{.localPosition = Math::Vector3{0.0F, 0.0F, -15.0F}});
    sceneGraph.Update(entityManager);

    // Update spatial index with moved entity's new bounds
    meshRenderer.UpdateEntitySpatialIndex(entityManager, assetManager, assetDatabase, movingEntity);

    // Frame 2: entity is now in front of camera, admitted to candidates
    {
        MeshVisibilitySet visibilitySet;
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        ASSERT_EQ(visibilitySet.candidates.size(), 1U);
        EXPECT_EQ(visibilitySet.candidates[0].entity, movingEntity);
        EXPECT_EQ(visibilitySet.placementCache.count(movingEntity), 1U);
    }

    // Move entity off-screen to the right (+500 X)
    sceneGraph.SetLocalTransform(
        entityManager, movingEntity,
        Scene::TransformComponent{.localPosition = Math::Vector3{500.0F, 0.0F, -15.0F}});
    sceneGraph.Update(entityManager);

    // Update spatial index with explicit bounds
    const Math::Aabb newBounds = Math::Aabb::FromCenterExtents(
        Math::Vector3{500.0F, 0.0F, -15.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    meshRenderer.UpdateEntitySpatialIndex(movingEntity, newBounds);

    // Frame 3: entity moved out of camera FOV, pruned early again
    {
        MeshVisibilitySet visibilitySet;
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        EXPECT_EQ(visibilitySet.candidates.size(), 0U);
    }
}

// ---------------------------------------------------------------------------
// 4. Multi-Frustum Spatial Acceleration (Main View + Shadow Cascades)
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, MultiFrustum_RetainsEntitiesInAnyActiveFrustum) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_multi.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_multi.uvmat");

    // Camera view points down -Z
    const Math::Frustum mainCameraFrustum = MakeCameraFrustum();

    // Directional light cascade looking down from +Y (a -90 degree turn about X sends the
    // camera's -Z forward to -Y; +90 would aim it at the sky and see nothing).
    Math::Quaternion lightRotation{};
    static_cast<void>(Math::TryMakeAxisAngle(
        Math::Vector3{1.0F, 0.0F, 0.0F}, -std::numbers::pi_v<float> / 2.0F, lightRotation));
    const Math::Matrix4x4 lightView = Math::Matrix4x4::ViewFromPositionAndRotation(
        Math::Vector3{0.0F, 50.0F, 0.0F}, lightRotation);
    const Math::Matrix4x4 lightProjection = Math::Matrix4x4::Orthographic(-20.0F, 20.0F, -20.0F, 20.0F, 0.1F, 100.0F);
    const Math::Frustum shadowFrustum = Math::Frustum::FromViewProjection(lightProjection * lightView);

    // Entity A: inside main camera frustum (0, 0, -10)
    const auto entityA = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Entity B: outside camera frustum, but inside shadow frustum (15, 0, 10)
    const auto entityB = MakeMeshEntity(Math::Vector3{15.0F, 0.0F, 10.0F}, meshGuid, materialGuid);

    // Entity C: outside BOTH frustums (0, 0, 800)
    const auto entityC = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 800.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);

    const std::vector<Math::Frustum> frustums{mainCameraFrustum, shadowFrustum};
    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, frustums, visibilitySet);

    // Entity A and Entity B survive; Entity C is pruned
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    const bool hasA = std::any_of(visibilitySet.candidates.begin(), visibilitySet.candidates.end(),
                                  [entityA](const auto& c) { return c.entity == entityA; });
    const bool hasB = std::any_of(visibilitySet.candidates.begin(), visibilitySet.candidates.end(),
                                  [entityB](const auto& c) { return c.entity == entityB; });
    const bool hasC = std::any_of(visibilitySet.candidates.begin(), visibilitySet.candidates.end(),
                                  [entityC](const auto& c) { return c.entity == entityC; });

    EXPECT_TRUE(hasA);
    EXPECT_TRUE(hasB);
    EXPECT_FALSE(hasC);
    EXPECT_EQ(visibilitySet.placementCache.count(entityC), 0U);
}

// ---------------------------------------------------------------------------
// 5. Scene Bounding Region Spatial Query
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, BoundingRegion_QueriesOnlyEntitiesInSpatialBox) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_region.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_region.uvmat");

    const auto entityInside =
        MakeMeshEntity(Math::Vector3{10.0F, 10.0F, 10.0F}, meshGuid, materialGuid);
    const auto entityOutside =
        MakeMeshEntity(Math::Vector3{100.0F, 100.0F, 100.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);

    const Math::Aabb queryRegion = Math::Aabb::FromCenterExtents(
        Math::Vector3{10.0F, 10.0F, 10.0F}, Math::Vector3{5.0F, 5.0F, 5.0F});

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, queryRegion, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, entityInside);
    EXPECT_EQ(visibilitySet.placementCache.count(entityOutside), 0U);
}

// ---------------------------------------------------------------------------
// 6. Entity Removal Cleans Up Spatial Acceleration Structure
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, EntityRemoval_UpdatesBvhTopology) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_remove.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_remove.uvmat");

    const auto entity1 = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const auto entity2 = MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvh().GetEntityCount(), 2U);

    // Remove entity 1
    meshRenderer.RemoveEntitySpatialIndex(entity1);
    EXPECT_EQ(meshRenderer.GetSpatialBvh().GetEntityCount(), 1U);

    MeshVisibilitySet visibilitySet;
    const Math::Frustum cameraFrustum = MakeCameraFrustum();
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, entity2);
}

// ---------------------------------------------------------------------------
// 7. Backward Compatibility: Frustum Tests Disabled retains all entities
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, FrustumTestsDisabled_PreservesAllEntities) {
    RegisterImmediateLoaders();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("bvh_compat.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("bvh_compat.uvmat");

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReady(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAcceleration(entityManager, assetManager, assetDatabase);

    MeshVisibilitySet visibilitySet;
    visibilitySet.frustumTestsDisabled = true;

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    // Both entities retained when frustum tests disabled
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    EXPECT_FALSE(visibilitySet.clusters.empty());
}

} // namespace
} // namespace UVE::Render::Tests
