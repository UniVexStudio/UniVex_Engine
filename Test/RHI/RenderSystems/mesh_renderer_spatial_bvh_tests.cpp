// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/mesh_renderer_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/render_systems/dynamic_mesh_bvh_uve.h"
#include "uve/render_systems/mesh_visibility_set_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterationsUVE = 200000;

class MeshRendererSpatialBvhTest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    MeshRendererUVE meshRenderer;

    Scene::EntityUVE MakeMeshEntityUVE(const Math::Vector3UVE worldPosition,
                                       const Asset::AssetGuidUVE meshGuid,
                                       const Asset::AssetGuidUVE materialGuid) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(
            entity, Scene::MeshComponentUVE{meshGuid, materialGuid});
        return entity;
    }

    [[nodiscard]] static Math::FrustumUVE MakeCameraFrustumUVE(
        const Math::Vector3UVE cameraPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F},
        const Math::QuaternionUVE cameraRotation = Math::QuaternionUVE{}) {
        const Math::Matrix4x4UVE view =
            Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(cameraPosition, cameraRotation);
        const Math::Matrix4x4UVE projection =
            Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
    }

    void RegisterImmediateLoadersUVE(const bool materialIsTransparent = false) {
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
                mesh.localBounds = Math::AabbUVE::FromCenterExtentsUVE(
                    Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
                return true;
            });
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [materialIsTransparent](const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.isTransparent = materialIsTransparent;
                return true;
            });
    }

    void WaitUntilAssetsReadyUVE(const Asset::AssetGuidUVE meshGuid, const Asset::AssetGuidUVE materialGuid) {
        Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
            assetManager.LoadUVE<Asset::MeshAssetUVE>(meshGuid, assetDatabase);
        Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
            assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            if (meshHandle.IsReadyUVE() && materialHandle.IsReadyUVE()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(meshHandle.IsReadyUVE());
        ASSERT_TRUE(materialHandle.IsReadyUVE());
    }
};

// ---------------------------------------------------------------------------
// 1. Off-screen entities are pruned early without evaluating placement cache
//    or layer checks
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, OffScreenEntities_PrunedEarlyWithoutPlacementCacheOrLayerChecks) {
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_prune_test.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_prune_test.uvmat");

    // Entity 1: in front of the camera (Z = -10, inside frustum near=1..far=100)
    const Scene::EntityUVE visibleEntity =
        MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Entity 2: far behind camera (Z = +500, completely outside camera frustum)
    // Configure an off-layer mask on Entity 2 that WOULD fail view layer mask check if inspected
    const Scene::EntityUVE offScreenEntity =
        MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponentUVE offScreenInstance;
    offScreenInstance.renderLayers = 0x00000004U; // Layer 3 only
    entityManager.AddComponentUVE<Scene::RenderInstanceComponentUVE>(offScreenEntity, offScreenInstance);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // Initial sync of spatial BVH
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvhUVE().GetEntityCountUVE(), 2U);

    // Configure visibility set to only look at layer 1
    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.viewLayerMask = 0x00000001U; // Layer 1 only

    const Math::FrustumUVE cameraFrustum = MakeCameraFrustumUVE();

    // Query with cull frustum
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

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
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_crowd.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_crowd.uvmat");

    // 5 visible entities
    std::vector<Scene::EntityUVE> visibleEntities;
    for (int i = 0; i < 5; ++i) {
        const auto entity = MakeMeshEntityUVE(
            Math::Vector3UVE{static_cast<float>(i - 2) * 2.0F, 0.0F, -15.0F}, meshGuid, materialGuid);
        visibleEntities.push_back(entity);
    }

    // 50 off-screen entities placed behind camera (+100 to +300 Z)
    std::vector<Scene::EntityUVE> offScreenEntities;
    for (int i = 0; i < 50; ++i) {
        const auto entity = MakeMeshEntityUVE(
            Math::Vector3UVE{static_cast<float>(i % 5) * 5.0F, 0.0F, 100.0F + static_cast<float>(i) * 4.0F},
            meshGuid, materialGuid);
        offScreenEntities.push_back(entity);
    }

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvhUVE().GetEntityCountUVE(), 55U);

    MeshVisibilitySetUVE visibilitySet;
    const Math::FrustumUVE cameraFrustum = MakeCameraFrustumUVE();

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

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
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_cluster.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_cluster.uvmat");

    // Create 12 visible entities spread in a grid in front of camera
    std::vector<Scene::EntityUVE> createdEntities;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const auto entity = MakeMeshEntityUVE(
                Math::Vector3UVE{static_cast<float>(x) * 3.0F, static_cast<float>(y) * 3.0F, -20.0F},
                meshGuid, materialGuid);
            createdEntities.push_back(entity);
        }
    }

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);

    MeshVisibilitySetUVE visibilitySet;
    const Math::FrustumUVE cameraFrustum = MakeCameraFrustumUVE();

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

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
    RenderQueueUVE queue;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, cameraFrustum, queue);
    EXPECT_EQ(queue.opaqueItems.size(), 9U);
}

// ---------------------------------------------------------------------------
// 3. Dynamic movement updates the spatial index
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, DynamicMovement_UpdatesSpatialIndexAndVisibility) {
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_move.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_move.uvmat");

    // Entity starts off-screen behind the camera (+200 Z)
    const Scene::EntityUVE movingEntity =
        MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 200.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);

    const Math::FrustumUVE cameraFrustum = MakeCameraFrustumUVE();

    // Frame 1: entity is off-screen, pruned early
    {
        MeshVisibilitySetUVE visibilitySet;
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        EXPECT_EQ(visibilitySet.candidates.size(), 0U);
        EXPECT_EQ(visibilitySet.placementCache.count(movingEntity), 0U);
    }

    // Move entity dynamically into view (-15 Z). SetLocalTransformUVE marks the world transform
    // dirty so the scene graph sweep recomputes it - direct member writes would leave it stale.
    sceneGraph.SetLocalTransformUVE(
        entityManager, movingEntity,
        Scene::TransformComponentUVE{.localPosition = Math::Vector3UVE{0.0F, 0.0F, -15.0F}});
    sceneGraph.UpdateUVE(entityManager);

    // Update spatial index with moved entity's new bounds
    meshRenderer.UpdateEntitySpatialIndexUVE(entityManager, assetManager, assetDatabase, movingEntity);

    // Frame 2: entity is now in front of camera, admitted to candidates
    {
        MeshVisibilitySetUVE visibilitySet;
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        ASSERT_EQ(visibilitySet.candidates.size(), 1U);
        EXPECT_EQ(visibilitySet.candidates[0].entity, movingEntity);
        EXPECT_EQ(visibilitySet.placementCache.count(movingEntity), 1U);
    }

    // Move entity off-screen to the right (+500 X)
    sceneGraph.SetLocalTransformUVE(
        entityManager, movingEntity,
        Scene::TransformComponentUVE{.localPosition = Math::Vector3UVE{500.0F, 0.0F, -15.0F}});
    sceneGraph.UpdateUVE(entityManager);

    // Update spatial index with explicit bounds
    const Math::AabbUVE newBounds = Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{500.0F, 0.0F, -15.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
    meshRenderer.UpdateEntitySpatialIndexUVE(movingEntity, newBounds);

    // Frame 3: entity moved out of camera FOV, pruned early again
    {
        MeshVisibilitySetUVE visibilitySet;
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);
        EXPECT_EQ(visibilitySet.candidates.size(), 0U);
    }
}

// ---------------------------------------------------------------------------
// 4. Multi-Frustum Spatial Acceleration (Main View + Shadow Cascades)
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, MultiFrustum_RetainsEntitiesInAnyActiveFrustum) {
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_multi.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_multi.uvmat");

    // Camera view points down -Z
    const Math::FrustumUVE mainCameraFrustum = MakeCameraFrustumUVE();

    // Directional light cascade looking down from +Y (a -90 degree turn about X sends the
    // camera's -Z forward to -Y; +90 would aim it at the sky and see nothing).
    Math::QuaternionUVE lightRotation{};
    static_cast<void>(Math::TryMakeAxisAngleUVE(
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, -std::numbers::pi_v<float> / 2.0F, lightRotation));
    const Math::Matrix4x4UVE lightView = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
        Math::Vector3UVE{0.0F, 50.0F, 0.0F}, lightRotation);
    const Math::Matrix4x4UVE lightProjection = Math::Matrix4x4UVE::OrthographicUVE(-20.0F, 20.0F, -20.0F, 20.0F, 0.1F, 100.0F);
    const Math::FrustumUVE shadowFrustum = Math::FrustumUVE::FromViewProjectionUVE(lightProjection * lightView);

    // Entity A: inside main camera frustum (0, 0, -10)
    const auto entityA = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Entity B: outside camera frustum, but inside shadow frustum (15, 0, 10)
    const auto entityB = MakeMeshEntityUVE(Math::Vector3UVE{15.0F, 0.0F, 10.0F}, meshGuid, materialGuid);

    // Entity C: outside BOTH frustums (0, 0, 800)
    const auto entityC = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 800.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);

    const std::vector<Math::FrustumUVE> frustums{mainCameraFrustum, shadowFrustum};
    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, frustums, visibilitySet);

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
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_region.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_region.uvmat");

    const auto entityInside =
        MakeMeshEntityUVE(Math::Vector3UVE{10.0F, 10.0F, 10.0F}, meshGuid, materialGuid);
    const auto entityOutside =
        MakeMeshEntityUVE(Math::Vector3UVE{100.0F, 100.0F, 100.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);

    const Math::AabbUVE queryRegion = Math::AabbUVE::FromCenterExtentsUVE(
        Math::Vector3UVE{10.0F, 10.0F, 10.0F}, Math::Vector3UVE{5.0F, 5.0F, 5.0F});

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, queryRegion, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, entityInside);
    EXPECT_EQ(visibilitySet.placementCache.count(entityOutside), 0U);
}

// ---------------------------------------------------------------------------
// 6. Entity Removal Cleans Up Spatial Acceleration Structure
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, EntityRemoval_UpdatesBvhTopology) {
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_remove.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_remove.uvmat");

    const auto entity1 = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const auto entity2 = MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);
    EXPECT_EQ(meshRenderer.GetSpatialBvhUVE().GetEntityCountUVE(), 2U);

    // Remove entity 1
    meshRenderer.RemoveEntitySpatialIndexUVE(entity1);
    EXPECT_EQ(meshRenderer.GetSpatialBvhUVE().GetEntityCountUVE(), 1U);

    MeshVisibilitySetUVE visibilitySet;
    const Math::FrustumUVE cameraFrustum = MakeCameraFrustumUVE();
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cameraFrustum, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, entity2);
}

// ---------------------------------------------------------------------------
// 7. Backward Compatibility: Frustum Tests Disabled retains all entities
// ---------------------------------------------------------------------------

TEST_F(MeshRendererSpatialBvhTest, FrustumTestsDisabled_PreservesAllEntities) {
    RegisterImmediateLoadersUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("bvh_compat.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("bvh_compat.uvmat");

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    meshRenderer.SynchronizeSpatialAccelerationUVE(entityManager, assetManager, assetDatabase);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.frustumTestsDisabled = true;

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    // Both entities retained when frustum tests disabled
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    EXPECT_FALSE(visibilitySet.clusters.empty());
}

} // namespace
} // namespace UVE::Render::Tests
