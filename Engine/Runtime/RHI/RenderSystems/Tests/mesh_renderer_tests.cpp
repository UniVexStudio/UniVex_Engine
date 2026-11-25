// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_renderer.h"

#include "mesh_visibility_set.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_manager.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "event_system.h"
#include "memory_manager.h"
#include "mesh_component.h"
#include "physics_interpolation_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "transform_component.h"
#include "render_instance_component.h"
#include "surface_instance_component.h"
#include "quaternion.h"
#include "abstract_objects_3d.h"
#include "lod_group_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"
#include "entity_manager.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterations = 200000;

class MeshRendererTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Threading::ThreadPool threadPool{2};
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};
    MeshRenderer meshRenderer;

    // Entities reach MeshRenderer only via SceneGraph::AttachTransform, matching
    // CameraSystemTest's fixture precedent.
    Scene::Entity MakeMeshEntity(Math::Vector3 worldPosition, Asset::AssetGuid meshGuid,
                                        Asset::AssetGuid materialGuid) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::MeshComponent>(entity, Scene::MeshComponent{meshGuid, materialGuid});
        return entity;
    }

    // A camera at the origin looking down -Z with a 90-degree fov, matching
    // CameraSystemTest's ExtractFrustum fixture.
    [[nodiscard]] static Math::Frustum MakeTestFrustum() {
        const Math::Matrix4x4 view =
            Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
        const Math::Matrix4x4 projection =
            Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::Frustum::FromViewProjection(projection * view);
    }

    void RegisterImmediateLoaders(bool materialIsTransparent) {
        assetManager.RegisterLoader<Asset::MeshAsset>(
            [](const std::filesystem::path&, Asset::MeshAsset& mesh) {
                mesh.localBounds =
                    Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
                return true;
            });
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [materialIsTransparent](const std::filesystem::path&, Asset::MaterialAsset& material) {
                material.isTransparent = materialIsTransparent;
                return true;
            });
    }

    void WaitUntilAssetsReady(Asset::AssetGuid meshGuid, Asset::AssetGuid materialGuid) {
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

TEST_F(MeshRendererTest, ExtractRenderQueue_VisibleOpaqueEntity_AppearsInOpaqueBucket) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_opaque.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_opaque.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    ASSERT_EQ(queue.opaqueItems.size(), 1U);
    EXPECT_TRUE(queue.transparentItems.empty());
}

TEST_F(MeshRendererTest, ExtractRenderQueue_VisibleTransparentEntity_AppearsInTransparentBucket) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/true);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_transparent.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_transparent.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    ASSERT_EQ(queue.transparentItems.size(), 1U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_EntityOutsideFrustum_Excluded) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_offscreen.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_offscreen.uvmat");
    // Behind the camera, matching CameraSystemTest's own "behindCameraBox" fixture.
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
}

// A model arrives with the materials it was authored with. The object's own material field is an
// override of that, not the only way for one to exist: without this fallback, an imported model drew
// grey until somebody assigned a material by hand, which is what every FBX and every imported
// texture looked like before.
TEST_F(MeshRendererTest, ExtractRenderQueue_ObjectWithoutItsOwnMaterial_DrawsWithTheMeshesMaterial) {
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("mesh_renderer_tests_mesh_owned_material.uvmat");
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_mesh_owned.uvmodel");
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [materialGuid](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {materialGuid};
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    ASSERT_EQ(queue.opaqueItems.size(), 1U);
    EXPECT_EQ(queue.opaqueItems.front().materialHandle.GetGuid(), materialGuid);
}

// The other half of the same rule: a mesh with no material of its own still belongs to the
// unmaterialed path, so the two paths never both draw it and never both skip it.
TEST_F(MeshRendererTest, ExtractRenderQueue_MeshWithNoMaterialOfItsOwn_StaysOffTheMaterialedPath) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_bare.uvmodel");
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                               Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
}

TEST(MeshComponent, IsMeshComponentValid_AllowsUnassignedPair) {
    EXPECT_TRUE(Scene::IsMeshComponentValid(
        Scene::MeshComponent{Asset::kInvalidAssetGuid, Asset::kInvalidAssetGuid}));
}

TEST(MeshComponent, IsMeshComponentValid_AllowsAMeshWithoutAMaterialOnly) {
    // A mesh alone is drawn with the built-in lit shader; a material alone has nothing to draw on.
    EXPECT_TRUE(Scene::IsMeshComponentValid(Scene::MeshComponent{Asset::AssetGuid{1U},
                                                                        Asset::kInvalidAssetGuid}));
    EXPECT_FALSE(Scene::IsMeshComponentValid(Scene::MeshComponent{Asset::kInvalidAssetGuid,
                                                                         Asset::AssetGuid{2U}}));
    EXPECT_TRUE(Scene::IsMeshComponentValid(Scene::MeshComponent{Asset::AssetGuid{1U},
                                                                        Asset::AssetGuid{2U}}));
}

TEST_F(MeshRendererTest, ExtractRenderQueue_MeshWithoutMaterial_IsLeftToTheBuiltInPathUncounted) {
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Asset::AssetGuid{1U}, Asset::kInvalidAssetGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    // Not a material draw, and not a broken reference either: Renderer3D draws it with the
    // built-in lit shader.
    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_InvalidGuid_Skipped) {
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Asset::kInvalidAssetGuid, Asset::kInvalidAssetGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 2U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 0U);
}

#if UVE_DEBUG
TEST_F(MeshRendererTest, ExtractRenderQueue_MaterialWithoutMesh_Asserts) {
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Asset::kInvalidAssetGuid,
                      Asset::AssetGuid{1U});

    EXPECT_DEATH(
        { static_cast<void>(meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase,
                                                                MakeTestFrustum())); },
        "");
}
#endif

TEST_F(MeshRendererTest, ExtractRenderQueue_NonFiniteWorldTransform_IsCountedAndSkipped) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_nonfinite.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_nonfinite.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(entity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(entity, invalidWorld);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 0U);
    EXPECT_EQ(queue.invalidRenderEligibility, 1U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_FailedAssetLoad_IsCountedAndSkipped) {
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [](const std::filesystem::path&, Asset::MeshAsset&) { return false; });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_failed.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_failed.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Asset::AssetHandle<Asset::MeshAsset> meshHandle =
        assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations &&
                             (!(meshHandle.HasFailed() || meshHandle.IsReady()) ||
                              !(materialHandle.HasFailed() || materialHandle.IsReady()));
         ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.HasFailed());
    ASSERT_TRUE(materialHandle.IsReady());

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 1U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_MixOfVisibleAndCulledEntities_OnlyVisibleOneIncluded) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_mixed.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_mixed.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{1000.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());

    EXPECT_EQ(queue.opaqueItems.size(), 1U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_AssetNotReadyYet_ExcludedThenIncludedOnceLoaded) {
    std::atomic<bool> allowMeshLoad{false};
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [&allowMeshLoad](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            while (!allowMeshLoad.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            mesh.localBounds =
                Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset& material) {
            material.isTransparent = false;
            return true;
        });

    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_gate.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_gate.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Math::Frustum frustum = MakeTestFrustum();
    Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !materialHandle.IsReady(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(materialHandle.IsReady());

    const RenderQueue firstAttempt = meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, frustum);
    EXPECT_TRUE(firstAttempt.opaqueItems.empty());
    EXPECT_EQ(firstAttempt.pendingAssetLoads, 1U);
    EXPECT_EQ(firstAttempt.failedAssetLoads, 0U);

    allowMeshLoad.store(true);

    RenderQueue secondAttempt;
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        secondAttempt = meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, frustum);
        if (!secondAttempt.opaqueItems.empty()) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(secondAttempt.opaqueItems.size(), 1U);
    EXPECT_EQ(secondAttempt.invalidAssetReferences, 0U);
    EXPECT_EQ(secondAttempt.pendingAssetLoads, 0U);
    EXPECT_EQ(secondAttempt.failedAssetLoads, 0U);
}

// ---------------------------------------------------------------------------
// BuildVisibilitySet / CullVisibilitySetInto - the frustum-independent
// split.
//
// The claim this change makes is that it is a pure performance change: the
// same scene must produce the same queue as before. So the load-bearing tests
// here are EQUIVALENCE tests against ExtractRenderQueueInto, not tests of
// the new functions in isolation - a new path that is merely self-consistent
// but disagrees with the old one is exactly the failure worth catching.
// ---------------------------------------------------------------------------

TEST_F(MeshRendererTest, BuildAndCull_ProducesTheSameQueueAsSingleStepExtraction) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_equiv.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_equiv.uvmat");
    // A mix of in-frustum and out-of-frustum entities, so the comparison covers the cull verdict
    // rather than just agreeing that everything is visible.
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -30.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 50.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{500.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    const Math::Frustum frustum = MakeTestFrustum();

    RenderQueue singleStep;
    meshRenderer.ExtractRenderQueueInto(entityManager, assetManager, assetDatabase, frustum, singleStep);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueue twoStep;
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, twoStep);

    ASSERT_EQ(twoStep.opaqueItems.size(), singleStep.opaqueItems.size());
    ASSERT_EQ(twoStep.transparentItems.size(), singleStep.transparentItems.size());
    for (std::size_t index = 0U; index < singleStep.opaqueItems.size(); ++index) {
        EXPECT_EQ(twoStep.opaqueItems[index].worldMatrix, singleStep.opaqueItems[index].worldMatrix);
        EXPECT_FLOAT_EQ(twoStep.opaqueItems[index].sortDepth, singleStep.opaqueItems[index].sortDepth);
    }
    EXPECT_EQ(twoStep.invalidAssetReferences, singleStep.invalidAssetReferences);
    EXPECT_EQ(twoStep.pendingAssetLoads, singleStep.pendingAssetLoads);
    EXPECT_EQ(twoStep.failedAssetLoads, singleStep.failedAssetLoads);
    EXPECT_EQ(twoStep.invalidRenderEligibility, singleStep.invalidRenderEligibility);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_IsIndependentOfAnyFrustum) {
    // The defining property. An entity far outside the main view still belongs in the candidate
    // set, because a shadow cascade's frustum may well contain it - dropping it at build time
    // would delete shadows cast by off-screen geometry, which is the classic version of this bug.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_offscreen.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_offscreen.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        EXPECT_TRUE(candidate.placement.IsPlaced());
    }

    // ...and the one behind the camera is the one the main view then rejects.
    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);
    EXPECT_EQ(queue.opaqueItems.size(), 1U);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_OneSetFeedsManyFrustaWithoutBeingConsumed) {
    // The whole frame depends on this: the set is built once and culled four times, so culling
    // must not move the handles out of it. A second cull returning fewer items would mean the
    // shadow cascades silently lose casters after the first one runs.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_reuse.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_reuse.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -20.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    const Math::Frustum frustum = MakeTestFrustum();

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    RenderQueue first;
    RenderQueue second;
    RenderQueue third;
    RenderQueue fourth;
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, first);
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, second);
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, third);
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, fourth);

    EXPECT_EQ(first.opaqueItems.size(), 2U);
    EXPECT_EQ(fourth.opaqueItems.size(), first.opaqueItems.size());
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    // The handles must still resolve after four culls - a moved-from handle would not.
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.IsReady());
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.IsReady());
    }
    ASSERT_EQ(fourth.opaqueItems.size(), 2U);
    EXPECT_TRUE(fourth.opaqueItems[0].meshHandle.IsReady());
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_DifferentFrustaSelectDifferentSubsets) {
    // Proves the cull is genuinely per-frustum rather than baked in at build time - which is what
    // would happen if visibility leaked into the shared step.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_subset.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_subset.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // The same frustum turned around to look down +Z instead of -Z.
    Math::Quaternion turnAround;
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float>,
                                          turnAround));
    const Math::Matrix4x4 behindView =
        Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{0.0F, 0.0F, 0.0F}, turnAround);
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);

    RenderQueue forwardQueue;
    RenderQueue backwardQueue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), forwardQueue);
    meshRenderer.CullVisibilitySetInto(visibilitySet,
                                          Math::Frustum::FromViewProjection(projection * behindView),
                                          backwardQueue);

    EXPECT_EQ(forwardQueue.opaqueItems.size(), 1U);
    EXPECT_EQ(backwardQueue.opaqueItems.size(), 1U);
    // Different entities, from one shared candidate set.
    EXPECT_NE(forwardQueue.opaqueItems[0].worldMatrix, backwardQueue.opaqueItems[0].worldMatrix);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_FrustumCullingFreezeKeepsOffFrustumCandidates) {
    // The per-cull freeze flag (MeshVisibilitySet::frustumTestsDisabled) is what the renderer sets
    // on the colour view when CullingSettings disables frustum culling. This proves the cull
    // honours it end to end - both the cluster-level skip and the per-candidate frustum test - so an
    // off-frustum candidate reaches the queue it would otherwise be dropped from. It is per-cull
    // state, so clearing it again must restore culling on the very next call.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_freeze.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_freeze.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    const Math::Frustum frustum = MakeTestFrustum();

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);

    RenderQueue culled;
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, culled);
    EXPECT_EQ(culled.opaqueItems.size(), 1U) << "the behind-camera mesh is frustum-rejected";

    visibilitySet.frustumTestsDisabled = true;
    RenderQueue frozen;
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, frozen);
    EXPECT_EQ(frozen.opaqueItems.size(), 2U) << "the freeze keeps the off-frustum candidate";

    visibilitySet.frustumTestsDisabled = false;
    RenderQueue restored;
    meshRenderer.CullVisibilitySetInto(visibilitySet, frustum, restored);
    EXPECT_EQ(restored.opaqueItems.size(), 1U) << "the flag is per-cull, not sticky";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_ReusedAcrossFrames_DoesNotAccumulate) {
    // Renderer3D holds one set for the lifetime of the renderer, so appending instead of
    // clearing would re-render every frame the scene has ever had.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_frames.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_frames.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U);
    EXPECT_EQ(visibilitySet.failedAssetLoads, 0U);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_CountersDescribeTheSceneNotTheView) {
    // An unassigned entity is one broken entity regardless of how many frusta ask, so every queue
    // built from the set reports the same count - not one per cascade.
    //
    // Both guids are left invalid together, matching ExtractRenderQueue_InvalidGuid_Skipped:
    // MeshComponent's invariant is that the two are assigned or unassigned TOGETHER, so a
    // half-assigned component is a component bug and trips UVE_ASSERT before extraction sees it.
    // Hence two counted references for the one entity.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_counters.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_counters.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -12.0F}, Asset::kInvalidAssetGuid,
                      Asset::kInvalidAssetGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 2U);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);

    RenderQueue first;
    RenderQueue second;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), first);
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), second);

    EXPECT_EQ(first.invalidAssetReferences, 2U);
    EXPECT_EQ(second.invalidAssetReferences, 2U);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_EmptySetClearsTheQueue) {
    // The queue is caller-owned and reused, so a frame that culls nothing must leave an empty
    // queue rather than last frame's contents.
    RenderQueue queue;
    queue.opaqueItems.reserve(4U);

    MeshVisibilitySet visibilitySet;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
}

// ---------------------------------------------------------------------------
// The placement cache.
//
// Placement dominates the frame - measured at roughly 29x the cost of a key
// comparison - and most objects do not move, so the cache skips recomputing
// answers the previous frame already had.
//
// A cache is the most dangerous kind of code here because its failures are
// silent: a stale entry renders an object at last frame's position and nothing
// crashes. So these tests care overwhelmingly about INVALIDATION - proving the
// cache notices every input that can change - rather than about hit counts.
// ---------------------------------------------------------------------------

TEST_F(MeshRendererTest, PlacementCache_SecondBuildOfAStaticScene_IsAllHits) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_static.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_static.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{4.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    // First frame can only miss - there is nothing to reuse yet.
    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 3U);

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 3U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    EXPECT_EQ(visibilitySet.candidates.size(), 3U);
}

TEST_F(MeshRendererTest, PlacementCache_HitProducesTheSamePlacementAsARecompute) {
    // The claim is that a hit is indistinguishable from a recompute. Anything less and the cache
    // is trading correctness for speed, which is not a trade worth making.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_same.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_same.uvmat");
    MakeMeshEntity(Math::Vector3{1.5F, -2.5F, -12.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet cached;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cached);
    ASSERT_EQ(cached.candidates.size(), 1U);
    const Math::Matrix4x4 firstMatrix = cached.candidates[0].placement.worldMatrix;
    const Math::Aabb firstBounds = cached.candidates[0].placement.worldBounds;

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, cached);
    ASSERT_EQ(cached.placementCacheHits, 1U);

    // A completely fresh set has no cache, so its placement is necessarily recomputed.
    MeshVisibilitySet fresh;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, fresh);
    ASSERT_EQ(fresh.placementCacheMisses, 1U);
    ASSERT_EQ(fresh.candidates.size(), 1U);

    EXPECT_EQ(cached.candidates[0].placement.worldMatrix, fresh.candidates[0].placement.worldMatrix);
    EXPECT_EQ(cached.candidates[0].placement.worldMatrix, firstMatrix);
    EXPECT_EQ(cached.candidates[0].placement.worldBounds.min, fresh.candidates[0].placement.worldBounds.min);
    EXPECT_EQ(cached.candidates[0].placement.worldBounds.max, fresh.candidates[0].placement.worldBounds.max);
    EXPECT_EQ(firstBounds.min, fresh.candidates[0].placement.worldBounds.min);
}

TEST_F(MeshRendererTest, PlacementCache_MovedEntity_IsRecomputedNotReused) {
    // THE test. A stale placement renders an object at last frame's position with no error
    // anywhere - exactly the silent failure a cache introduces if invalidation is wrong.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_moved.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_moved.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    const Math::Aabb beforeBounds = visibilitySet.candidates[0].placement.worldBounds;

    Scene::TransformComponent moved;
    moved.localPosition = Math::Vector3{0.0F, 0.0F, -25.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, moved);
    sceneGraph.Update(entityManager);

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_NE(visibilitySet.candidates[0].placement.worldBounds.min.z, beforeBounds.min.z);
    EXPECT_FLOAT_EQ(visibilitySet.candidates[0].placement.worldBounds.GetCenter().z, -25.0F);
}

TEST_F(MeshRendererTest, PlacementCache_TinyMovement_StillInvalidates) {
    // The key is an identity test, not a tolerance test. A sub-millimetre move must still miss -
    // a cache that rounds is a cache that drifts, and the error accumulates invisibly.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_tiny.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_tiny.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    Scene::TransformComponent nudged;
    nudged.localPosition = Math::Vector3{0.0001F, 0.0F, -10.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, nudged);
    sceneGraph.Update(entityManager);

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
}

TEST_F(MeshRendererTest, PlacementCache_RotationAndScaleAreBothPartOfTheKey) {
    // Position is the obvious field to key on and the easy one to get right. Rotation and scale
    // change the world bounds just as much and are easy to forget.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_rs.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_rs.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    Math::Quaternion turned;
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                          std::numbers::pi_v<float> / 4.0F, turned));
    Scene::TransformComponent rotated;
    rotated.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    rotated.localRotation = turned;
    sceneGraph.SetLocalTransform(entityManager, entity, rotated);
    sceneGraph.Update(entityManager);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U) << "rotation must be part of the key";

    // Settle, so the scale check starts from a hit rather than from the rotation's miss.
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheHits, 1U);

    Scene::TransformComponent scaled = rotated;
    scaled.localScale = Math::Vector3{3.0F, 3.0F, 3.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, scaled);
    sceneGraph.Update(entityManager);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U) << "scale must be part of the key";
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    // And the recomputed bounds genuinely reflect the 3x scale, rather than merely being new.
    const Math::Aabb bounds = visibilitySet.candidates[0].placement.worldBounds;
    EXPECT_GT(bounds.max.x - bounds.min.x, 1.4F);
}

TEST_F(MeshRendererTest, PlacementCache_SwappedMesh_InvalidatesEvenWhenTheTransformIsIdentical) {
    // A stationary entity whose mesh guid changes. The transform is untouched, so a transform-only
    // key would happily serve the old mesh's bounds forever.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid firstMesh = assetDatabase.Register("mesh_renderer_tests_cache_swap_a.uvmodel");
    const Asset::AssetGuid secondMesh = assetDatabase.Register("mesh_renderer_tests_cache_swap_b.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_swap.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, firstMesh, materialGuid);
    WaitUntilAssetsReady(firstMesh, materialGuid);
    WaitUntilAssetsReady(secondMesh, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheMisses, 1U);

    entityManager.GetComponent<Scene::MeshComponent>(entity).meshGuid = secondMesh;

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
}

TEST_F(MeshRendererTest, PlacementCache_DestroyedEntity_IsPrunedNotRetained) {
    // Unbounded growth turns a cache into a leak. A long-running streaming world must not retain
    // an entry for every entity it has ever shown.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_prune.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_prune.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity doomed = MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCache.size(), 2U);

    entityManager.DestroyEntity(doomed);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.placementCache.size(), 1U) << "the destroyed entity's entry must be pruned";
}

TEST_F(MeshRendererTest, PlacementCache_SurvivesManyFramesWithoutGrowing) {
    // The steady state a real frame loop lives in: same scene, many frames. The cache must reach a
    // fixed size and stay there, all hits.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_steady.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_steady.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    for (int frame = 0; frame < 50; ++frame) {
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    }

    EXPECT_EQ(visibilitySet.placementCache.size(), 2U);
    EXPECT_EQ(visibilitySet.placementCacheHits, 2U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
}

TEST_F(MeshRendererTest, PlacementCache_DoesNotChangeTheQueueAnyFrameProduces) {
    // The end-to-end guarantee, stated where a reader will look for it: caching is invisible
    // downstream. A frame served entirely from cache must cull to exactly the queue an uncached
    // frame would.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cache_queue.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cache_queue.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -30.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, 50.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    const Math::Frustum frustum = MakeTestFrustum();

    MeshVisibilitySet warm;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, warm);
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, warm);
    ASSERT_EQ(warm.placementCacheMisses, 0U);
    RenderQueue warmQueue;
    meshRenderer.CullVisibilitySetInto(warm, frustum, warmQueue);

    RenderQueue coldQueue;
    meshRenderer.ExtractRenderQueueInto(entityManager, assetManager, assetDatabase, frustum, coldQueue);

    ASSERT_EQ(warmQueue.opaqueItems.size(), coldQueue.opaqueItems.size());
    for (std::size_t index = 0U; index < coldQueue.opaqueItems.size(); ++index) {
        EXPECT_EQ(warmQueue.opaqueItems[index].worldMatrix, coldQueue.opaqueItems[index].worldMatrix);
        EXPECT_FLOAT_EQ(warmQueue.opaqueItems[index].sortDepth, coldQueue.opaqueItems[index].sortDepth);
    }
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EntitiesSharingAssets_EachOwnAUsableHandle) {
    // The hazard in resolving a GUID once and sharing it: the first entity to use a resolution
    // must not consume it. If the shared handle were moved from rather than copied, the second and
    // later entities would receive an empty handle - which would not fail to compile, and would
    // not fail any test that only renders one entity.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_shared.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_shared.uvmat");
    constexpr int kSharedEntityCount = 4;
    for (int index = 0; index < kSharedEntityCount; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kSharedEntityCount));
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuid(), meshGuid);
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.GetGuid(), materialGuid);
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.IsReady()) << "a shared resolution must not be consumed by one entity";
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.IsReady());
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.TryGet(), nullptr);
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.TryGet(), nullptr);
    }
}

TEST_F(MeshRendererTest, BuildVisibilitySet_SharedFailedAsset_IsCountedOncePerEntityNotOncePerAsset) {
    // Sharing the resolution must not change what the diagnostics mean. These counters answer "how
    // many entities could not be drawn this frame", so three entities blocked on one broken mesh
    // is three, not one. Folding the count into the resolution would quietly turn every one of
    // these counters into a per-asset figure while every single-entity test kept passing.
    //
    // The mesh fails via a loader that returns false, matching the existing failed-load test:
    // an unregistered GUID stays pending rather than failing, which measures something else.
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [](const std::filesystem::path&, Asset::MeshAsset&) { return false; });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_sharedfail.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_sharedfail.uvmat");
    constexpr int kBlockedEntityCount = 3;
    for (int index = 0; index < kBlockedEntityCount; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }

    Asset::AssetHandle<Asset::MeshAsset> meshHandle =
        assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations &&
                             (!(meshHandle.HasFailed() || meshHandle.IsReady()) ||
                              !(materialHandle.HasFailed() || materialHandle.IsReady()));
         ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.HasFailed());
    ASSERT_TRUE(materialHandle.IsReady());

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_EQ(visibilitySet.failedAssetLoads, static_cast<std::size_t>(kBlockedEntityCount))
        << "each blocked entity must be counted, even though they share one failed mesh";
    EXPECT_EQ(visibilitySet.pendingAssetLoads, 0U)
        << "the shared material resolved, so nothing should be reported as still loading";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_AssetBecomingReady_IsObservedOnTheNextBuild) {
    // The resolution memo is per walk, never across frames, because asset state is asynchronous.
    // This is the test that fails if someone later promotes it to a member to "save more work":
    // a mesh that was pending during one build must be drawable on the next without anything else
    // in the scene changing.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_becomesready.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_becomesready.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Built before waiting: the loads are in flight, so this build sees them pending.
    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    WaitUntilAssetsReady(meshGuid, materialGuid);

    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.pendingAssetLoads, 0U)
        << "a resolution must not outlive its walk - the completed load must be seen";
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DistinctAssetsPerEntity_AreEachResolved) {
    // The memo keys on GUID, so the failure mode opposite to over-sharing is under-resolving:
    // every entity referencing a DIFFERENT asset must still get its own answer. A memo keyed on
    // something coarser - or one that returned the first entry it found - would collapse these.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    constexpr int kDistinctEntityCount = 3;
    std::vector<Asset::AssetGuid> meshGuids;
    std::vector<Asset::AssetGuid> materialGuids;
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        const std::string suffix = std::to_string(index);
        meshGuids.push_back(assetDatabase.Register("mesh_renderer_tests_distinct" + suffix + ".uvmodel"));
        materialGuids.push_back(assetDatabase.Register("mesh_renderer_tests_distinct" + suffix + ".uvmat"));
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -10.0F}, meshGuids.back(),
                          materialGuids.back());
    }
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        WaitUntilAssetsReady(meshGuids[static_cast<std::size_t>(index)],
                                materialGuids[static_cast<std::size_t>(index)]);
    }

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kDistinctEntityCount));
    std::vector<Asset::AssetGuid> seenMeshGuids;
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        seenMeshGuids.push_back(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuid());
    }
    for (const Asset::AssetGuid expected : meshGuids) {
        EXPECT_NE(std::find(seenMeshGuids.cbegin(), seenMeshGuids.cend(), expected), seenMeshGuids.cend())
            << "every distinct mesh GUID must be resolved on its own, not collapsed into a neighbour's";
    }
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_ClusteredAndUnclustered_ProduceTheSameVisibleSet) {
    // The whole optimization rests on one claim: rejecting a cluster rejects only candidates that
    // were going to be rejected anyway. This is the test that checks the claim directly, by culling
    // the same scene with clusters built and with the cluster list emptied, and demanding the same
    // answer. A cluster box that is too tight silently drops visible geometry, which is the exact
    // bug no rendering test with one entity in front of the camera would ever catch.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_cluster.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_cluster.uvmat");
    // Spread well past one cluster (64) and well outside the frustum, so clusters are genuinely
    // rejected rather than all trivially accepted.
    constexpr int kSpreadEntityCount = 400;
    for (int index = 0; index < kSpreadEntityCount; ++index) {
        const float x = static_cast<float>((index * 37) % 200) - 100.0F;
        const float y = static_cast<float>((index * 53) % 200) - 100.0F;
        const float z = -static_cast<float>((index * 29) % 200);
        MakeMeshEntity(Math::Vector3{x, y, z}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet clustered;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, clustered);
    ASSERT_FALSE(clustered.clusters.empty()) << "the scene must be large enough to actually cluster";

    RenderQueue clusteredQueue;
    meshRenderer.CullVisibilitySetInto(clustered, MakeTestFrustum(), clusteredQueue);

    // Same set, clusters removed: the documented fallback path, and the reference answer.
    MeshVisibilitySet flat;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, flat);
    flat.clusters.clear();
    RenderQueue flatQueue;
    meshRenderer.CullVisibilitySetInto(flat, MakeTestFrustum(), flatQueue);

    ASSERT_EQ(clusteredQueue.opaqueItems.size(), flatQueue.opaqueItems.size())
        << "cluster rejection must never drop a visible candidate";
    EXPECT_EQ(clusteredQueue.transparentItems.size(), flatQueue.transparentItems.size());
    EXPECT_GT(clusteredQueue.opaqueItems.size(), 0U) << "a test where nothing is visible proves nothing";

    // Compared after sorting, because the queues are depth-sorted by the consumer and cluster
    // ordering legitimately changes the order equal-depth items are appended in.
    clusteredQueue.Sort();
    flatQueue.Sort();
    for (std::size_t index = 0U; index < flatQueue.opaqueItems.size(); ++index) {
        EXPECT_FLOAT_EQ(clusteredQueue.opaqueItems[index].sortDepth, flatQueue.opaqueItems[index].sortDepth)
            << "at index " << index;
    }
}

TEST_F(MeshRendererTest, BuildVisibilitySet_ClustersCoverEveryCandidateExactlyOnce) {
    // A permutation bug in the reorder - a cycle applied twice, an off-by-one in the offsets -
    // would duplicate one candidate and lose another. Sizes alone would still add up, so this
    // checks the ranges partition the list: contiguous, non-overlapping, covering all of it.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_partition.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_partition.uvmat");
    constexpr int kPartitionEntityCount = 150;
    for (int index = 0; index < kPartitionEntityCount; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kPartitionEntityCount));
    std::size_t expectedFirst = 0U;
    for (const MeshVisibilitySet::CandidateCluster& cluster : visibilitySet.clusters) {
        EXPECT_EQ(cluster.first, expectedFirst) << "clusters must be contiguous and non-overlapping";
        EXPECT_GT(cluster.count, 0U) << "an empty cluster is a plane test that can never pay for itself";
        expectedFirst += cluster.count;
    }
    EXPECT_EQ(expectedFirst, visibilitySet.candidates.size()) << "every candidate must belong to a cluster";
}

TEST_F(MeshRendererTest, BuildSpatialClusters_ClusterBoundsEncloseEveryMember) {
    // The rejection is only sound if the enclosing box really encloses. This asserts the invariant
    // the cull depends on, rather than inferring it from a visible-count that happened to match.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_enclose.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_enclose.uvmat");
    constexpr int kEncloseEntityCount = 200;
    for (int index = 0; index < kEncloseEntityCount; ++index) {
        const float x = static_cast<float>((index * 17) % 90) - 45.0F;
        const float y = static_cast<float>((index * 31) % 90) - 45.0F;
        MakeMeshEntity(Math::Vector3{x, y, -20.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_FALSE(visibilitySet.clusters.empty());

    for (const MeshVisibilitySet::CandidateCluster& cluster : visibilitySet.clusters) {
        for (std::size_t offset = 0U; offset < cluster.count; ++offset) {
            const Math::Aabb& member =
                visibilitySet.candidates[cluster.first + offset].placement.worldBounds;
            EXPECT_LE(cluster.bounds.min.x, member.min.x);
            EXPECT_LE(cluster.bounds.min.y, member.min.y);
            EXPECT_LE(cluster.bounds.min.z, member.min.z);
            EXPECT_GE(cluster.bounds.max.x, member.max.x);
            EXPECT_GE(cluster.bounds.max.y, member.max.y);
            EXPECT_GE(cluster.bounds.max.z, member.max.z);
        }
    }
}

TEST_F(MeshRendererTest, BuildSpatialClusters_ReorderingDoesNotDisturbAssetHandles) {
    // The reorder moves candidates around, and candidates own AssetHandle members. A permutation
    // that copied instead of moved, or that left a moved-from element behind, would show up as a
    // handle that no longer resolves - while sizes and bounds all still looked right.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_reorder.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_reorder.uvmat");
    constexpr int kReorderEntityCount = 100;
    for (int index = 0; index < kReorderEntityCount; ++index) {
        const float x = static_cast<float>((index * 41) % 60) - 30.0F;
        MakeMeshEntity(Math::Vector3{x, 0.0F, -15.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kReorderEntityCount));
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuid(), meshGuid);
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.GetGuid(), materialGuid);
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.TryGet(), nullptr) << "a reordered candidate must still resolve";
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.TryGet(), nullptr);
    }
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EntitiesSharingAssets_ShareOneAssetPairEntry) {
    // The point of the change: references are held per distinct asset pairing, not per entity.
    // Four entities on one mesh and one material must produce exactly one entry, and every
    // candidate must point at it.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_onepair.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_onepair.uvmat");
    constexpr int kSharedEntityCount = 4;
    for (int index = 0; index < kSharedEntityCount; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kSharedEntityCount));
    EXPECT_EQ(visibilitySet.assetPairs.size(), 1U)
        << "entities sharing a mesh and material must share one reference pair, not hold one each";
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        EXPECT_EQ(candidate.assetPairIndex, 0U);
    }
    EXPECT_TRUE(visibilitySet.assetPairs[0].meshHandle.IsReady());
    EXPECT_TRUE(visibilitySet.assetPairs[0].materialHandle.IsReady());
}

TEST_F(MeshRendererTest, BuildVisibilitySet_SameMeshDifferentMaterials_GetSeparateAssetPairs) {
    // The failure mode opposite to over-sharing, and the reason the key is BOTH guids. Keying on
    // the mesh alone would hand the second entity the first one's material - and every visible
    // check would still pass, because both entities would still draw. The material would just be
    // silently wrong.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_twomat.uvmodel");
    const Asset::AssetGuid firstMaterialGuid = assetDatabase.Register("mesh_renderer_tests_twomat_a.uvmat");
    const Asset::AssetGuid secondMaterialGuid = assetDatabase.Register("mesh_renderer_tests_twomat_b.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, firstMaterialGuid);
    MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, secondMaterialGuid);
    WaitUntilAssetsReady(meshGuid, firstMaterialGuid);
    WaitUntilAssetsReady(meshGuid, secondMaterialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    EXPECT_EQ(visibilitySet.assetPairs.size(), 2U)
        << "one mesh with two materials is two pairings, not one";
    std::vector<Asset::AssetGuid> seenMaterialGuids;
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        ASSERT_LT(candidate.assetPairIndex, visibilitySet.assetPairs.size());
        const MeshVisibilityAssetPair& pair = visibilitySet.assetPairs[candidate.assetPairIndex];
        EXPECT_EQ(pair.meshHandle.GetGuid(), meshGuid);
        seenMaterialGuids.push_back(pair.materialHandle.GetGuid());
    }
    EXPECT_NE(std::find(seenMaterialGuids.cbegin(), seenMaterialGuids.cend(), firstMaterialGuid),
              seenMaterialGuids.cend());
    EXPECT_NE(std::find(seenMaterialGuids.cbegin(), seenMaterialGuids.cend(), secondMaterialGuid),
              seenMaterialGuids.cend());
}

TEST_F(MeshRendererTest, BuildSpatialClusters_ReorderingKeepsEachCandidatePointingAtItsOwnAssets) {
    // The clustering reorder moves candidates around, and they now identify their assets by index.
    // An index that did not travel with its candidate would silently repaint geometry with another
    // object's material - visible only as a wrong-looking frame, never as a failure. Every entity
    // here uses a distinct mesh+material pair so a mismatch cannot hide behind shared assets.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    constexpr int kDistinctEntityCount = 80;
    std::vector<Asset::AssetGuid> meshGuids;
    std::vector<Asset::AssetGuid> materialGuids;
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        const std::string suffix = std::to_string(index);
        meshGuids.push_back(assetDatabase.Register("mesh_renderer_tests_pairtravel" + suffix + ".uvmodel"));
        materialGuids.push_back(assetDatabase.Register("mesh_renderer_tests_pairtravel" + suffix + ".uvmat"));
        // Spread widely so the Morton ordering genuinely permutes them rather than leaving the
        // creation order intact - a reorder that does nothing would not test anything.
        const float x = static_cast<float>((index * 37) % 60) - 30.0F;
        const float y = static_cast<float>((index * 53) % 60) - 30.0F;
        MakeMeshEntity(Math::Vector3{x, y, -20.0F}, meshGuids.back(), materialGuids.back());
    }
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        WaitUntilAssetsReady(meshGuids[static_cast<std::size_t>(index)],
                                materialGuids[static_cast<std::size_t>(index)]);
    }

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kDistinctEntityCount));
    EXPECT_EQ(visibilitySet.assetPairs.size(), static_cast<std::size_t>(kDistinctEntityCount));
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        ASSERT_LT(candidate.assetPairIndex, visibilitySet.assetPairs.size());
        const MeshVisibilityAssetPair& pair = visibilitySet.assetPairs[candidate.assetPairIndex];
        // Each entity was created with matching suffixes, so a candidate whose index travelled
        // correctly has a mesh and material from the same position in the two lists.
        const auto meshPosition = std::find(meshGuids.cbegin(), meshGuids.cend(), pair.meshHandle.GetGuid());
        const auto materialPosition =
            std::find(materialGuids.cbegin(), materialGuids.cend(), pair.materialHandle.GetGuid());
        ASSERT_NE(meshPosition, meshGuids.cend());
        ASSERT_NE(materialPosition, materialGuids.cend());
        EXPECT_EQ(std::distance(meshGuids.cbegin(), meshPosition),
                  std::distance(materialGuids.cbegin(), materialPosition))
            << "a candidate's asset index must survive the spatial reorder";
    }
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_QueueItemsOutliveTheVisibilitySet) {
    // Candidates borrow the set's references, but RenderQueue is returned by value from the
    // public ExtractRenderQueue and can outlive the set entirely - so queue items must still
    // own theirs. This drives that directly: build a queue, destroy the set, collect garbage, and
    // require the queue's handles to still resolve.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_outlive.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_outlive.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    RenderQueue queue;
    {
        MeshVisibilitySet scopedSet;
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, scopedSet);
        meshRenderer.CullVisibilitySetInto(scopedSet, MakeTestFrustum(), queue);
        ASSERT_EQ(queue.opaqueItems.size(), 1U);
    }
    // The set is gone along with its references. Only the queue's own remain.
    assetManager.CollectGarbage();

    EXPECT_TRUE(queue.opaqueItems[0].meshHandle.IsReady())
        << "a queue item must hold its own reference - it can outlive the set it came from";
    EXPECT_TRUE(queue.opaqueItems[0].materialHandle.IsReady());
    EXPECT_NE(queue.opaqueItems[0].meshHandle.TryGet(), nullptr);
    EXPECT_NE(queue.opaqueItems[0].materialHandle.TryGet(), nullptr);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_HiddenEntitiesAreSkippedBeforeAnyWorkIsDone) {
    // Hiding has to be worth something, not just look right. The gate sits ahead of the asset
    // resolution and the placement cache, so a hidden mesh costs a component lookup and nothing
    // else - this asserts it produces no candidate at all rather than a candidate that is later
    // culled, which would still have paid to build it.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_hidden.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_hidden.uvmat");
    const Scene::Entity shown = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity hidden = MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // Derived field, as the scene graph would have left it after an update.
    entityManager.AddComponent<Scene::VisibilityComponent>(
        hidden, Scene::VisibilityComponent{/*visible=*/false, /*visibleInHierarchy=*/false});
    static_cast<void>(shown);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U) << "the hidden entity must not become a candidate";
    EXPECT_EQ(visibilitySet.hiddenEntities, 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U)
        << "a hidden object is not a scene fault and must not be reported as one";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_VisibleInHierarchyIsWhatCounts_NotTheAuthoredFlag) {
    // The renderer reads the DERIVED field. An entity whose own switch is on but whose parent is
    // hidden has visible == true and visibleInHierarchy == false, and reading the wrong one draws
    // exactly the objects the author just hid.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_inherited.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_inherited.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    entityManager.AddComponent<Scene::VisibilityComponent>(
        entity, Scene::VisibilityComponent{/*visible=*/true, /*visibleInHierarchy=*/false});

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_TRUE(visibilitySet.candidates.empty())
        << "the renderer must honour the inherited answer, not the entity's own switch";
    EXPECT_EQ(visibilitySet.hiddenEntities, 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EntitiesWithoutTheComponentStillRender) {
    // The component is optional and most entities will never carry one. If its absence were read
    // as hidden, every existing scene would go blank.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_novis.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_novis.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.hiddenEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_InterpolatedPoseIsDrawnBetweenTheTwoSimulatedSteps) {
    // The payoff. At alpha 0.5 the candidate must sit halfway between the two recorded poses, not
    // at the newest one - which is what the renderer drew before and what produced the stutter.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_interp.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_interp.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponent interpolation;
    interpolation.mode = Scene::PoseSmoothing::Blended;
    interpolation.interpolatedInHierarchy = true;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3{10.0F, 0.0F, -10.0F};
    entityManager.AddComponent<Scene::PhysicsInterpolationComponent>(entity, interpolation);

    MeshVisibilitySet visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 1U);
    const Math::Vector3 center = visibilitySet.candidates[0U].placement.worldBounds.GetCenter();
    EXPECT_NEAR(center.x, 5.0F, 1e-3F) << "halfway between the two simulated poses";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_AlphaZeroDrawsThePreviousPoseAndOneDrawsTheCurrent) {
    // The two boundaries have to be exact, or an object visibly jumps at the moment a fixed step
    // lands - which is precisely the artefact interpolation is supposed to remove.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_interp_ends.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_interp_ends.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponent interpolation;
    interpolation.mode = Scene::PoseSmoothing::Blended;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3{-4.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3{6.0F, 0.0F, -10.0F};
    entityManager.AddComponent<Scene::PhysicsInterpolationComponent>(entity, interpolation);

    MeshVisibilitySet atStart;
    atStart.physicsInterpolationAlpha = 0.0F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, atStart);
    ASSERT_EQ(atStart.candidates.size(), 1U);
    EXPECT_NEAR(atStart.candidates[0U].placement.worldBounds.GetCenter().x, -4.0F, 1e-3F);

    MeshVisibilitySet atEnd;
    atEnd.physicsInterpolationAlpha = 1.0F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, atEnd);
    ASSERT_EQ(atEnd.candidates.size(), 1U);
    EXPECT_NEAR(atEnd.candidates[0U].placement.worldBounds.GetCenter().x, 6.0F, 1e-3F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EntitiesWithoutInterpolationDrawAtTheSimulatedPose) {
    // The component is optional and most entities will never carry one. They must be unaffected by
    // a non-zero alpha, or switching interpolation on would move the whole static world.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_interp_none.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_interp_none.uvmat");
    MakeMeshEntity(Math::Vector3{3.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 0U);
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().x, 3.0F, 1e-3F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_InterpolationDoesNotDefeatThePlacementCache) {
    // The design constraint that decided where the blend goes. The placement cache is keyed on the
    // SIMULATED transform and is worth about 29x; blending before the key would miss on every
    // frame for every moving object and hand all of that back. A second build with a different
    // alpha but the same simulated pose must still hit.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_interp_cache.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_interp_cache.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponent interpolation;
    interpolation.mode = Scene::PoseSmoothing::Blended;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3{1.0F, 0.0F, -10.0F};
    entityManager.AddComponent<Scene::PhysicsInterpolationComponent>(entity, interpolation);

    MeshVisibilitySet visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.25F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheMisses, 1U) << "first build must populate the cache";

    // Same simulated pose, different alpha - the frame after, in other words.
    visibilitySet.physicsInterpolationAlpha = 0.75F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 1U)
        << "a new alpha must not invalidate the cached placement";
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    // And the blend still moved, so the hit is not coming from skipping the work entirely.
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().x, 0.75F, 1e-3F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_InterpolationOffUsesTheSimulatedPose) {
    // Off must reach all the way through to the drawn position, not merely be recorded.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_interp_off.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_interp_off.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{8.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponent interpolation;
    interpolation.mode = Scene::PoseSmoothing::Exact;
    interpolation.interpolatedInHierarchy = false;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3{-100.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3{100.0F, 0.0F, -10.0F};
    entityManager.AddComponent<Scene::PhysicsInterpolationComponent>(entity, interpolation);

    MeshVisibilitySet visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 0U);
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().x, 8.0F, 1e-3F)
        << "Off must draw the simulated pose, not a blend of the recorded ones";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_LodGroupPastItsChainIsDroppedBeforeAnyWorkIsDone) {
    // The payoff: a distance-culled entity must produce no candidate at all, not a candidate that
    // is culled later. The gate sits ahead of asset resolution and placement, so the object costs
    // a subtraction and a length and nothing else.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_lod.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_lod.uvmat");
    const Scene::Entity near = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    const Scene::Entity far = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -500.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    for (const Scene::Entity entity : {near, far}) {
        entityManager.AddComponent<Scene::LodGroup3DComponent>(
            entity, Scene::LodGroup3DComponent{});
    }

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U) << "the far entity must not become a candidate";
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U)
        << "a distance-culled object is not a scene fault";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_LodLevelIsResolvedForVisibleEntitiesToo) {
    // currentLevel is what a future mesh swap indexes by and what the Inspector shows. Resolving
    // it only on the cull path would leave it correct exactly when nobody can see the object.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_lodlevel.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_lodlevel.uvmat");
    // Default chain is 10/25/60/120 - this sits in level 2.
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -40.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    entityManager.AddComponent<Scene::LodGroup3DComponent>(
        entity, Scene::LodGroup3DComponent{});

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 0U);
    EXPECT_EQ(entityManager.GetComponent<Scene::LodGroup3DComponent>(entity).currentLevel, 2U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_LodGroupDrawsTheMeshOfTheResolvedLevel) {
    // The swap itself, end to end. Two meshes: the component's own full-detail mesh and a level 1
    // override. The entity starts close enough to draw level 0 - its own mesh - then moves past the
    // first threshold, and the set must resolve, place and bucket the LEVEL 1 mesh instead. Without
    // this, a resolved level would be a number nothing draws with.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid fullDetailMesh = assetDatabase.Register("mesh_renderer_tests_lodswap_full.uvmodel");
    const Asset::AssetGuid levelOneMesh = assetDatabase.Register("mesh_renderer_tests_lodswap_level1.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_lodswap.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, fullDetailMesh, materialGuid);
    WaitUntilAssetsReady(fullDetailMesh, materialGuid);
    WaitUntilAssetsReady(levelOneMesh, materialGuid);

    Scene::LodGroup3DComponent lod;
    lod.levelCount = 2U;
    lod.distanceThresholds[0] = 10.0F;
    lod.distanceThresholds[1] = 100.0F;
    lod.lodMeshGuids[1] = levelOneMesh; // level 0 stays on the component's own mesh
    entityManager.AddComponent<Scene::LodGroup3DComponent>(entity, lod);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(entityManager.GetComponent<Scene::LodGroup3DComponent>(entity).currentLevel, 0U);
    ASSERT_LT(visibilitySet.candidates[0].assetPairIndex, visibilitySet.assetPairs.size());
    EXPECT_EQ(visibilitySet.assetPairs[visibilitySet.candidates[0].assetPairIndex].meshHandle.GetGuid(),
              fullDetailMesh)
        << "level 0 overrides nothing, so it draws the component's own mesh";

    // Past the first threshold, with the transform moved the way any script would move it.
    Scene::TransformComponent moved;
    moved.localPosition = Math::Vector3{0.0F, 0.0F, -40.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, moved);
    sceneGraph.Update(entityManager);

    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(entityManager.GetComponent<Scene::LodGroup3DComponent>(entity).currentLevel, 1U);
    ASSERT_LT(visibilitySet.candidates[0].assetPairIndex, visibilitySet.assetPairs.size());
    EXPECT_EQ(visibilitySet.assetPairs[visibilitySet.candidates[0].assetPairIndex].meshHandle.GetGuid(),
              levelOneMesh)
        << "level 1's override is the mesh that gets drawn";
    EXPECT_EQ(visibilitySet.placementCacheHits, 0U)
        << "a level change is a different mesh with different bounds: the cached placement must not "
           "be reused";
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_LodBiasKeepsDetailFurtherAndDropsItSooner) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_lodbias.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_lodbias.uvmat");
    // Default chain last threshold is 120 m. 200 m is past it; bias 2 makes it look like 100 m.
    const Scene::Entity keep = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -200.0F}, meshGuid, materialGuid);
    const Scene::Entity drop = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -80.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    entityManager.AddComponent<Scene::LodGroup3DComponent>(keep, Scene::LodGroup3DComponent{});
    entityManager.AddComponent<Scene::LodGroup3DComponent>(drop, Scene::LodGroup3DComponent{});
    Scene::SurfaceInstanceComponent keepSurface{};
    keepSurface.lodBias = 2.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(keep, keepSurface);
    Scene::SurfaceInstanceComponent dropSurface{};
    dropSurface.lodBias = 0.5F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(drop, dropSurface);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.distanceCulledEntities, 1U);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0U].entity, keep);
    EXPECT_EQ(entityManager.GetComponent<Scene::LodGroup3DComponent>(keep).currentLevel, 3U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_TheCameraPositionIsWhatDistanceIsMeasuredFrom) {
    // Distance is from the CAMERA, not from the origin. With the camera moved out to meet it, an
    // object that would otherwise be past the chain is back in range - which is the whole point
    // of a draw distance and trivially easy to get wrong by measuring from the wrong point.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_lodcam.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_lodcam.uvmat");
    const Scene::Entity entity = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -200.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    entityManager.AddComponent<Scene::LodGroup3DComponent>(
        entity, Scene::LodGroup3DComponent{});

    MeshVisibilitySet fromOrigin;
    fromOrigin.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, fromOrigin);
    EXPECT_EQ(fromOrigin.distanceCulledEntities, 1U) << "200 m away is past the default chain";

    MeshVisibilitySet fromNearby;
    fromNearby.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, -195.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, fromNearby);
    EXPECT_EQ(fromNearby.distanceCulledEntities, 0U) << "5 m from the camera is level 0";
    ASSERT_EQ(fromNearby.candidates.size(), 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EntitiesWithoutALodGroupAreNeverDistanceCulled) {
    // The component is opt-in. Without one, an entity draws at any distance - otherwise adding
    // LOD support to the engine would silently impose a draw distance on every existing scene.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_nolod.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_nolod.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9000.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_PartitionCellOutsideTheBudgetIsCulledWithItsOwnCounter) {
    // The rendering half of WorldPartition3D: the membership component is the ONLY runtime state
    // the render pipeline reads (SyncWorldPartition3DObjects writes it; this test simulates its
    // verdict by hand so the gate is measured in isolation). A live-flag true member renders;
    // live=false (the engine put its cell outside the budget) is culled and counted in
    // partitionCulledEntities so authored hiding and the vis-budget never blur together.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_wp.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_wp.uvmat");

    const Scene::Entity partition = entityManager.CreateEntity();
    Scene::TransformComponent partitionTransform;
    partitionTransform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, partition, partitionTransform);
    Scene::WorldPartition3DComponent partitionComponent;
    partitionComponent.cellSize = 4.0F;
    partitionComponent.cellCounts = {2U, 1U, 2U};
    partitionComponent.maximumLoadedCells = 1U;
    entityManager.AddComponent<Scene::WorldPartition3DComponent>(partition,
                                                                           partitionComponent);

    const Scene::Entity near = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -1.0F},
                                                    meshGuid, materialGuid);
    const Scene::Entity far = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -6.0F},
                                                   meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    sceneGraph.SetParent(entityManager, near, partition);
    sceneGraph.SetParent(entityManager, far, partition);

    // What SyncWorldPartition3DObjects writes after admitting only the nearest cell:
    entityManager.AddComponent<Scene::WorldPartition3DMembershipComponent>(
        near, Scene::WorldPartition3DMembershipComponent{partition, true});
    entityManager.AddComponent<Scene::WorldPartition3DMembershipComponent>(
        far, Scene::WorldPartition3DMembershipComponent{partition, false});

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U)
        << "only the admitted cell's mesh may render";
    EXPECT_TRUE(visibilitySet.candidates[0U].placement.IsPlaced());
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().z, -1.0F, 1e-3F)
        << "and it is the NEAR member, not the far one";
    EXPECT_EQ(visibilitySet.partitionCulledEntities, 1U)
        << "the partition gate is counted in its own counter, never conflated with authoring";

    MeshVisibilitySet repeat;
    repeat.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, repeat);
    ASSERT_EQ(repeat.candidates.size(), 1U);
    EXPECT_NEAR(repeat.candidates[0U].placement.worldBounds.GetCenter().z, -1.0F, 1e-3F);
    EXPECT_EQ(repeat.partitionCulledEntities, 1U)
        << "the same membership verdicts answer the same gate decision every build";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_InactiveRegionSkipsItsInteriorWithItsOwnCounter) {
    // The render half of VisibilityRegion3D: the engine-owned membership is the only runtime
    // state the pipeline reads (SyncVisibilityRegion3DObjects writes it; this test stamps it by
    // hand to isolate the gate). live=false (no viewer inside the room) is culled and counted in
    // regionCulledEntities - "nobody is inside" never blurs with streaming-budget or authored
    // hiding counts.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_vr.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_vr.uvmat");

    const Scene::Entity region = entityManager.CreateEntity();
    Scene::TransformComponent regionTransform;
    regionTransform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, region, regionTransform);
    Scene::VisibilityRegion3DComponent regionComponent;
    regionComponent.halfExtents = Math::Vector3{5.0F, 5.0F, 5.0F};
    regionComponent.active = false; // the engine's verdict: nobody is inside this room
    entityManager.AddComponent<Scene::VisibilityRegion3DComponent>(region,
                                                                             regionComponent);

    const Scene::Entity interior = MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -2.0F},
                                                       meshGuid, materialGuid);
    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F},
                                        meshGuid, materialGuid)); // the corridor mesh
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // What SyncVisibilityRegion3DObjects writes: only the in-room mesh is managed, with the
    // region's inactive verdict; the corridor outside every region carries no membership.
    entityManager.AddComponent<Scene::VisibilityRegion3DMembershipComponent>(
        interior, Scene::VisibilityRegion3DMembershipComponent{region, false});

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U) << "only the out-of-room mesh may render";
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().z, -9.0F,
                1e-3F)
        << "and it is the corridor, not the interior";
    EXPECT_EQ(visibilitySet.regionCulledEntities, 1U)
        << "the region gate has its own counter - room culling is a level-design stat";
    EXPECT_EQ(visibilitySet.partitionCulledEntities, 0U);

    // A live verdict for the same room renders it: the gate is the flag, not the room.
    entityManager.GetComponent<Scene::VisibilityRegion3DMembershipComponent>(interior)
        .live = true;
    MeshVisibilitySet awake;
    awake.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, awake);
    EXPECT_EQ(awake.candidates.size(), 2U) << "viewer stepped in: everything draws again";
    EXPECT_EQ(awake.regionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_AMeshThatPeeksAroundTheWallStillDraws) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{8.0F, 0.5F, 0.5F});
        return true;
    });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_occ_peek.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_occ_peek.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);

    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid, materialGuid));
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U)
        << "the origin sits behind the wall but a corner peeks: conservative cover must not cull";
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_MeshBehindAnOccluderIsCulledInItsOwnCounter) {
    // The wall claim, end to end: a mesh dead-center behind the wall costs one slab test and
    // lands in occlusionCulledEntities, while its twin off the silhouette still draws - hiding
    // never spills past the box the author painted.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_occ.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_occ.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);

    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid,
                                        materialGuid)); // behind the wall
    static_cast<void>(MakeMeshEntity(Math::Vector3{8.0F, 0.0F, -9.0F}, meshGuid,
                                        materialGuid)); // off the silhouette: the segment's cone
                                                        // from this camera stays clear of the box
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U) << "only the off-silhouette mesh may render";
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenter().x, 8.0F, 1e-3F);
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 1U)
        << "wall cover is a per-frame render stat, never conflated with the ownership gates";
    EXPECT_EQ(visibilitySet.partitionCulledEntities, 0U);
    EXPECT_EQ(visibilitySet.regionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_CameraMoveReanswersOcclusionWithNoStateAtAll) {
    // No persisted verdicts exist, so moving the camera behind the wall line re-answers the
    // whole scene this very build: what was hidden shows, what was free-space hides - with no
    // teleport-stale possibility by construction.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_occ2.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_occ2.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    entityManager.AddComponent<Scene::Occluder3DComponent>(
        wall, Scene::Occluder3DComponent{});

    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid,
                                        materialGuid));
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet before;
    before.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, before);
    ASSERT_TRUE(before.candidates.empty());
    ASSERT_EQ(before.occlusionCulledEntities, 1U);

    MeshVisibilitySet behindWall;
    behindWall.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, -7.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, behindWall);
    EXPECT_EQ(behindWall.candidates.size(), 1U)
        << "from behind the wall the cover sits behind the mesh: drawn, this very frame";
    EXPECT_EQ(behindWall.occlusionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DisabledOccluderCoversNothing) {
    // The eye-toggle on the wall: disabled means the box paints nothing into the frame, the
    // same way an enabled=false on the region releases its members - fail open, measured.
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_occ3.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_occ3.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    wallOccluder.enabled = false;
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);

    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid,
                                        materialGuid));
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U) << "disabled: the cover paints nothing";
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_AnyOfSeveralWallsHidesOnce) {
    // Composition is a plain OR: a corridor of two walls hides the mesh exactly once in the
    // counter (not once per wall - the stats would otherwise lie about scene content).
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_occ4.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_occ4.uvmat");

    for (const float wallZ : {-4.0F, -6.0F}) {
        const Scene::Entity wall = entityManager.CreateEntity();
        Scene::TransformComponent wallTransform;
        wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, wallZ};
        sceneGraph.AttachTransform(entityManager, wall, wallTransform);
        entityManager.AddComponent<Scene::Occluder3DComponent>(
            wall, Scene::Occluder3DComponent{});
    }

    static_cast<void>(MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid,
                                        materialGuid));
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 1U)
        << "two walls, one hidden mesh, one stat - no double-booking";
}

TEST_F(MeshRendererTest, BuildVisibilitySet_SurfaceVisibilityRangeCullsInItsOwnCounter) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_range.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_surf_range.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.visibilityRangeEnd = 5.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet far;
    far.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, far);
    EXPECT_TRUE(far.candidates.empty());
    EXPECT_EQ(far.rangeCulledEntities, 1U);
    EXPECT_EQ(far.distanceCulledEntities, 0U);

    MeshVisibilitySet near;
    near.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, -8.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, near);
    EXPECT_EQ(near.candidates.size(), 1U);
    EXPECT_EQ(near.rangeCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DefaultSurfaceInstanceDoesNotImposeADrawDistance) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_default.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_surf_default.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9000.0F}, meshGuid, materialGuid);
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, Scene::SurfaceInstanceComponent{});
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.rangeCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_IgnoreOcclusionKeepsAMeshBehindTheWall) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_occ.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_surf_occ.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);

    const Scene::Entity hidden =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.ignoreOcclusionCulling = true;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(hidden, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 0U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_ExtraCullMarginLetsACoveredMeshPeek) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_margin.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_surf_margin.uvmat");

    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -9.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.extraCullMargin = 20.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.occlusionCulledEntities, 0U);
    EXPECT_NEAR(visibilitySet.candidates[0].placement.worldBounds.min.x, -20.5F, 1.0e-3F);
    EXPECT_NEAR(visibilitySet.candidates[0].placement.worldBounds.max.x, 20.5F, 1.0e-3F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_MaterialOverrideReplacesTheMeshMaterial) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path& path,
                                                              Asset::MaterialAsset& material) {
        material.isTransparent = path.generic_string().find("override") != std::string::npos;
        return true;
    });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_ov.uvmodel");
    const Asset::AssetGuid baseMaterial = assetDatabase.Register("mesh_renderer_tests_surf_ov_base.uvmat");
    const Asset::AssetGuid overrideMaterial = assetDatabase.Register("mesh_renderer_tests_surf_ov_override.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, baseMaterial);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverridePath = "mesh_renderer_tests_surf_ov_override.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, baseMaterial);
    WaitUntilAssetsReady(meshGuid, overrideMaterial);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());
    EXPECT_TRUE(queue.opaqueItems.empty());
    ASSERT_EQ(queue.transparentItems.size(), 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_MissingMaterialOverrideDoesNotFallBackToTheMeshMaterial) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_surf_miss.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_surf_miss.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverridePath = "does_not_exist.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_GE(visibilitySet.invalidAssetReferences, 1U);
}

// Per-surface overrides are the instance's own words about the mesh's surfaces, so they belong to
// the object rather than to the model: two MeshInstance3Ds sharing one imported mesh can shade the
// same surface differently. Only the overridden surface changes; the rest keep the mesh's material.
TEST_F(MeshRendererTest, BuildVisibilitySet_PerSurfaceOverrideReplacesOnlyThatSurface) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ps_mesh.uvmodel");
    const Asset::AssetGuid slotZero = assetDatabase.Register("mesh_renderer_tests_ps_zero.uvmat");
    const Asset::AssetGuid slotOne = assetDatabase.Register("mesh_renderer_tests_ps_one.uvmat");
    const Asset::AssetGuid override = assetDatabase.Register("mesh_renderer_tests_ps_override.uvmat");
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [slotZero, slotOne](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {slotZero, slotOne};
            mesh.indices = {0U, 1U, 2U, 2U, 1U, 3U};
            mesh.surfaces = {Asset::MeshSurface{0U, 3U, 0U}, Asset::MeshSurface{3U, 3U, 1U}};
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.surfaceMaterialOverridePaths = {"", "mesh_renderer_tests_ps_override.uvmat"};
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, slotZero);
    WaitUntilAssetsReady(meshGuid, slotOne);
    WaitUntilAssetsReady(meshGuid, override);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    // The set groups candidates spatially, so order is not part of the contract: each surface is
    // found by the range it draws.
    const auto findSurface = [&visibilitySet](const std::uint32_t firstIndex) {
        return std::find_if(visibilitySet.candidates.begin(), visibilitySet.candidates.end(),
                            [firstIndex](const MeshVisibilityCandidate& candidate) {
                                return candidate.surface.firstIndex == firstIndex;
                            });
    };
    const auto first = findSurface(0U);
    const auto second = findSurface(3U);
    ASSERT_NE(first, visibilitySet.candidates.end());
    ASSERT_NE(second, visibilitySet.candidates.end());
    EXPECT_EQ(first->surface.indexCount, 3U);
    EXPECT_EQ(visibilitySet.assetPairs[first->assetPairIndex].materialHandle.GetGuid(), slotZero);
    EXPECT_EQ(second->surface.indexCount, 3U);
    EXPECT_EQ(visibilitySet.assetPairs[second->assetPairIndex].materialHandle.GetGuid(), override);
}

// The two levels are not alternatives: a whole-object override is one material for every surface,
// so it outranks the per-surface list exactly the way it outranks the mesh's own materials.
TEST_F(MeshRendererTest, BuildVisibilitySet_WholeObjectOverrideOutranksPerSurfaceOverrides) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ps2_mesh.uvmodel");
    const Asset::AssetGuid slotZero = assetDatabase.Register("mesh_renderer_tests_ps2_zero.uvmat");
    const Asset::AssetGuid slotOne = assetDatabase.Register("mesh_renderer_tests_ps2_one.uvmat");
    const Asset::AssetGuid whole = assetDatabase.Register("mesh_renderer_tests_ps2_whole.uvmat");
    const Asset::AssetGuid perSurface = assetDatabase.Register("mesh_renderer_tests_ps2_surface.uvmat");
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [slotZero, slotOne](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {slotZero, slotOne};
            mesh.indices = {0U, 1U, 2U, 2U, 1U, 3U};
            mesh.surfaces = {Asset::MeshSurface{0U, 3U, 0U}, Asset::MeshSurface{3U, 3U, 1U}};
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverridePath = "mesh_renderer_tests_ps2_whole.uvmat";
    surface.surfaceMaterialOverridePaths = {"mesh_renderer_tests_ps2_surface.uvmat", ""};
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    for (const Asset::AssetGuid guid : {slotZero, slotOne, whole, perSurface}) {
        WaitUntilAssetsReady(meshGuid, guid);
    }

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    // One draw for the whole buffer, not one per surface.
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].surface.indexCount, 6U);
    EXPECT_EQ(visibilitySet.assetPairs[visibilitySet.candidates[0].assetPairIndex]
                  .materialHandle.GetGuid(),
              whole);
}

// A per-surface override that no longer resolves must not quietly fall back to the mesh material:
// the author asked for a specific material, and drawing something else makes a moved .uvmaterial
// look like it still applies. The other surface still draws.
TEST_F(MeshRendererTest, BuildVisibilitySet_MissingPerSurfaceOverrideDropsOnlyThatSurface) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ps3_mesh.uvmodel");
    const Asset::AssetGuid slotZero = assetDatabase.Register("mesh_renderer_tests_ps3_zero.uvmat");
    const Asset::AssetGuid slotOne = assetDatabase.Register("mesh_renderer_tests_ps3_one.uvmat");
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [slotZero, slotOne](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {slotZero, slotOne};
            mesh.indices = {0U, 1U, 2U, 2U, 1U, 3U};
            mesh.surfaces = {Asset::MeshSurface{0U, 3U, 0U}, Asset::MeshSurface{3U, 3U, 1U}};
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.surfaceMaterialOverridePaths = {"does_not_exist_surface.uvmat", ""};
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, slotZero);
    WaitUntilAssetsReady(meshGuid, slotOne);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    // Only the second surface survives; the first one's override names nothing.
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].surface.firstIndex, 3U);
    EXPECT_GE(visibilitySet.invalidAssetReferences, 1U);
}

// A mesh with ONE implicit surface still has surface 0 to override: a single-material model does
// not need an explicit split for the instance to shade it differently.
TEST_F(MeshRendererTest, BuildVisibilitySet_PerSurfaceOverrideWorksOnAnImplicitSingleSurface) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ps4_mesh.uvmodel");
    const Asset::AssetGuid slotZero = assetDatabase.Register("mesh_renderer_tests_ps4_zero.uvmat");
    const Asset::AssetGuid override = assetDatabase.Register("mesh_renderer_tests_ps4_override.uvmat");
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [slotZero](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {slotZero};
            mesh.indices = {0U, 1U, 2U};
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.surfaceMaterialOverridePaths = {"mesh_renderer_tests_ps4_override.uvmat"};
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, slotZero);
    WaitUntilAssetsReady(meshGuid, override);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].surface.firstIndex, 0U);
    EXPECT_EQ(visibilitySet.candidates[0].surface.indexCount, 3U);
    EXPECT_EQ(visibilitySet.assetPairs[visibilitySet.candidates[0].assetPairIndex]
                  .materialHandle.GetGuid(),
              override);
}

// The override path the Inspector writes is relative to the content root ("Materials/M.uvmaterial"),
// while the asset database stores whatever path it was registered with ("assets/Materials/..."). A
// lookup that only compares whole strings never matches, which is exactly why an override authored
// in the editor used to do nothing. The suffix match closes that gap.
TEST_F(MeshRendererTest, BuildVisibilitySet_OverridePathResolvesAgainstAContentRootRelativeSpelling) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F},
                                                               Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("assets/Models/uve_ps5.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("assets/Models/uve_ps5.uvmat");
    const Asset::AssetGuid overrideGuid =
        assetDatabase.Register("assets/Materials/uve_ps5_override.uvmat");

    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverridePath = "Materials/uve_ps5_override.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, overrideGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.assetPairs[visibilitySet.candidates[0].assetPairIndex]
                  .materialHandle.GetGuid(),
              overrideGuid);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_OffLayerMeshesAreCulledInTheirOwnCounter) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ri_layer.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_ri_layer.uvmat");
    const Scene::Entity onLayer =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity offLayer =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponent layerTwo{};
    layerTwo.renderLayers = 0x2U;
    entityManager.AddComponent<Scene::RenderInstanceComponent>(onLayer, layerTwo);
    Scene::RenderInstanceComponent layerOne{};
    layerOne.renderLayers = 0x1U;
    entityManager.AddComponent<Scene::RenderInstanceComponent>(offLayer, layerOne);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.viewLayerMask = 0x2U;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].entity, onLayer);
    EXPECT_EQ(visibilitySet.candidates[0].renderLayers, 0x2U);
    EXPECT_EQ(visibilitySet.layerCulledEntities, 1U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DefaultLayerOneDrawsOnAnUnfilteredView) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ri_default.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_ri_default.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.candidates[0].renderLayers, 1U);
    EXPECT_EQ(visibilitySet.layerCulledEntities, 0U);

    visibilitySet.viewLayerMask = 0x2U;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_EQ(visibilitySet.layerCulledEntities, 1U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_SortingOffsetPushesTransparentDepthBack) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/true);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ri_sort.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_ri_sort.uvmat");
    const Scene::Entity plain =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity pushed =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponent offset{};
    offset.sortingOffset = 4.0F;
    entityManager.AddComponent<Scene::RenderInstanceComponent>(pushed, offset);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);
    ASSERT_EQ(queue.transparentItems.size(), 2U);
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        if (candidate.entity == plain) {
            EXPECT_FLOAT_EQ(candidate.sortingOffset, 0.0F);
        }
        if (candidate.entity == pushed) {
            EXPECT_FLOAT_EQ(candidate.sortingOffset, 4.0F);
        }
    }
    EXPECT_NEAR(std::fabs(queue.transparentItems[0].sortDepth - queue.transparentItems[1].sortDepth), 4.0F,
                1.0e-3F);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_SortByOriginUsesTheObjectOriginNotTheBoundsCenter) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 4.0F}};
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path&,
                                                              Asset::MaterialAsset& material) {
        material.isTransparent = false;
        return true;
    });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ri_origin.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_ri_origin.uvmat");
    const Scene::Entity byCenter =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity byOrigin =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponent originSort{};
    originSort.sortingUseAabbCenter = false;
    entityManager.AddComponent<Scene::RenderInstanceComponent>(byOrigin, originSort);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);
    ASSERT_EQ(queue.opaqueItems.size(), 2U);
    EXPECT_NE(queue.opaqueItems[0].sortDepth, queue.opaqueItems[1].sortDepth)
        << "bounds centre and origin must produce different depths on an offset AABB";
    static_cast<void>(byCenter);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_RenderItemCarriesTheInstanceLayers) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_ri_item_layers.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_ri_item_layers.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::RenderInstanceComponent instance{};
    instance.renderLayers = 0x8U;
    entityManager.AddComponent<Scene::RenderInstanceComponent>(entity, instance);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());
    ASSERT_EQ(queue.opaqueItems.size(), 1U);
    EXPECT_EQ(queue.opaqueItems[0].renderLayers, 0x8U);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_ShadowModeIsCarriedOnTheCandidate) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow.uvmat");
    const Scene::Entity off =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity only =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity none =
        MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent offSurface{};
    offSurface.castShadow = Scene::SurfaceShadowMode::Off;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(off, offSurface);
    Scene::SurfaceInstanceComponent onlySurface{};
    onlySurface.castShadow = Scene::SurfaceShadowMode::ShadowsOnly;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(only, onlySurface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 3U);
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        if (candidate.entity == off) {
            EXPECT_FALSE(candidate.castsShadow);
            EXPECT_TRUE(candidate.drawsInView);
        } else if (candidate.entity == only) {
            EXPECT_TRUE(candidate.castsShadow);
            EXPECT_FALSE(candidate.drawsInView);
        } else if (candidate.entity == none) {
            EXPECT_TRUE(candidate.castsShadow);
            EXPECT_TRUE(candidate.drawsInView);
        }
    }
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_OffIsDroppedFromTheShadowPass) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow_off.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow_off.uvmat");
    const Scene::Entity off =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity on =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent offSurface{};
    offSurface.castShadow = Scene::SurfaceShadowMode::Off;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(off, offSurface);
    Scene::SurfaceInstanceComponent onSurface{};
    onSurface.castShadow = Scene::SurfaceShadowMode::On;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(on, onSurface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueue colour;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), colour);
    EXPECT_EQ(colour.opaqueItems.size(), 2U);

    visibilitySet.shadowPass = true;
    RenderQueue shadow;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), shadow);
    ASSERT_EQ(shadow.opaqueItems.size(), 1U);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_ShadowsOnlyIsDroppedFromTheColourView) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow_only.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_shadow_only.uvmat");
    const Scene::Entity only =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity on =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent onlySurface{};
    onlySurface.castShadow = Scene::SurfaceShadowMode::ShadowsOnly;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(only, onlySurface);
    Scene::SurfaceInstanceComponent onSurface{};
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(on, onSurface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueue colour;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), colour);
    ASSERT_EQ(colour.opaqueItems.size(), 1U);

    visibilitySet.shadowPass = true;
    RenderQueue shadow;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), shadow);
    EXPECT_EQ(shadow.opaqueItems.size(), 2U);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_SurfaceTransparencyMovesAnOpaqueMaterialToTheTransparentBucket) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_fade.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_fade.uvmat");
    const Scene::Entity faded =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.transparency = 0.4F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(faded, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const RenderQueue queue =
        meshRenderer.ExtractRenderQueue(entityManager, assetManager, assetDatabase, MakeTestFrustum());
    EXPECT_TRUE(queue.opaqueItems.empty());
    ASSERT_EQ(queue.transparentItems.size(), 1U);
    EXPECT_FLOAT_EQ(queue.transparentItems[0].opacity, 0.6F);
}

TEST_F(MeshRendererTest, ExtractRenderQueue_FullyTransparentSurfaceIsDroppedFromTheColourView) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_invisible.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_invisible.uvmat");
    const Scene::Entity invisible =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity solid =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.transparency = 1.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(invisible, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    RenderQueue colour;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), colour);
    ASSERT_EQ(colour.opaqueItems.size(), 1U);
    EXPECT_FLOAT_EQ(colour.opaqueItems[0].opacity, 1.0F);

    visibilitySet.shadowPass = true;
    RenderQueue shadow;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), shadow);
    EXPECT_EQ(shadow.opaqueItems.size(), 2U);
    static_cast<void>(solid);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_OverlayAddsASecondColourPassThatDoesNotCast) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path&,
                                                              Asset::MaterialAsset&) { return true; });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_overlay.uvmodel");
    const Asset::AssetGuid baseMaterial = assetDatabase.Register("mesh_renderer_tests_si_overlay_base.uvmat");
    const Asset::AssetGuid overlayMaterial =
        assetDatabase.Register("mesh_renderer_tests_si_overlay_flash.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, baseMaterial);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverlayPath = "mesh_renderer_tests_si_overlay_flash.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, baseMaterial);
    WaitUntilAssetsReady(meshGuid, overlayMaterial);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    std::size_t overlays = 0U;
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        if (candidate.overlay) {
            ++overlays;
            EXPECT_FALSE(candidate.castsShadow);
            EXPECT_TRUE(candidate.isTransparent);
            EXPECT_TRUE(candidate.drawsInView);
        }
    }
    EXPECT_EQ(overlays, 1U);

    RenderQueue colour;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), colour);
    ASSERT_EQ(colour.opaqueItems.size(), 1U);
    ASSERT_EQ(colour.transparentItems.size(), 1U);
    EXPECT_FALSE(colour.opaqueItems[0].overlay);
    EXPECT_TRUE(colour.transparentItems[0].overlay);
    EXPECT_EQ(colour.transparentItems[0].materialHandle.GetGuid(), overlayMaterial);

    visibilitySet.shadowPass = true;
    RenderQueue shadow;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), shadow);
    ASSERT_EQ(shadow.opaqueItems.size(), 1U);
    EXPECT_FALSE(shadow.opaqueItems[0].overlay);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_MissingOverlayKeepsTheBaseMesh) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_overlay_miss.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_overlay_miss.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverlayPath = "does_not_exist_overlay.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_FALSE(visibilitySet.candidates[0].overlay);
    EXPECT_GE(visibilitySet.invalidAssetReferences, 1U);
}

TEST_F(MeshRendererTest, CullVisibilitySetInto_OverlaySortsCloserThanItsHost) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/true);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_overlay_sort.uvmodel");
    const Asset::AssetGuid baseMaterial = assetDatabase.Register("mesh_renderer_tests_si_overlay_sort_base.uvmat");
    const Asset::AssetGuid overlayMaterial =
        assetDatabase.Register("mesh_renderer_tests_si_overlay_sort_flash.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, baseMaterial);
    Scene::SurfaceInstanceComponent surface{};
    surface.materialOverlayPath = "mesh_renderer_tests_si_overlay_sort_flash.uvmat";
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, baseMaterial);
    WaitUntilAssetsReady(meshGuid, overlayMaterial);

    MeshVisibilitySet visibilitySet;
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);
    ASSERT_EQ(queue.transparentItems.size(), 2U);
    float hostDepth = 0.0F;
    float overlayDepth = 0.0F;
    for (const RenderItem& item : queue.transparentItems) {
        if (item.overlay) {
            overlayDepth = item.sortDepth;
        } else {
            hostDepth = item.sortDepth;
        }
    }
    EXPECT_LT(overlayDepth, hostDepth);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_SelfFadeKeepsAMeshInTheMarginAndScalesOpacity) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_fade_range.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_fade_range.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.visibilityRangeEnd = 8.0F;
    surface.visibilityRangeEndMargin = 4.0F;
    surface.visibilityRangeFadeMode = Scene::SurfaceFadeMode::Self;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.rangeCulledEntities, 0U);
    EXPECT_FLOAT_EQ(visibilitySet.candidates[0].opacity, 0.5F);
    EXPECT_TRUE(visibilitySet.candidates[0].isTransparent);

    RenderQueue queue;
    meshRenderer.CullVisibilitySetInto(visibilitySet, MakeTestFrustum(), queue);
    ASSERT_EQ(queue.transparentItems.size(), 1U);
    EXPECT_FLOAT_EQ(queue.transparentItems[0].opacity, 0.5F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DisabledFadeStillHardCutsAtTheRangeEnd) {
    RegisterImmediateLoaders(/*materialIsTransparent=*/false);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_si_fade_off.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_si_fade_off.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.visibilityRangeEnd = 8.0F;
    surface.visibilityRangeEndMargin = 4.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_EQ(visibilitySet.rangeCulledEntities, 1U);
}

[[nodiscard]] Math::Vector3 MatrixAxisZ(const Math::Matrix4x4& matrix) noexcept {
    const Math::Vector3 axis{matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]};
    return Math::Normalize(axis);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_DisabledBillboardKeepsTheAuthoredPose) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path&,
                                                              Asset::MaterialAsset&) { return true; });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_billboard_off.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_billboard_off.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Math::Quaternion yaw{};
    ASSERT_TRUE(Math::TryMakeEuler(Math::Vector3{0.0F, std::numbers::pi_v<float> * 0.5F, 0.0F}, yaw));
    Scene::TransformComponent turned;
    turned.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    turned.localRotation = yaw;
    sceneGraph.SetLocalTransform(entityManager, entity, turned);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    const Math::Vector3 axisZ = MatrixAxisZ(visibilitySet.candidates[0].placement.worldMatrix);
    EXPECT_NEAR(std::fabs(axisZ.x), 1.0F, 1.0e-3F);
    EXPECT_NEAR(axisZ.z, 0.0F, 1.0e-3F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_EnabledBillboardPointsLocalZAtTheCamera) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path&,
                                                              Asset::MaterialAsset& material) {
        material.billboardMode = Asset::MaterialBillboardMode::Enabled;
        return true;
    });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_billboard_on.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_billboard_on.uvmat");
    const Scene::Entity entity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Math::Quaternion yaw{};
    ASSERT_TRUE(Math::TryMakeEuler(Math::Vector3{0.0F, std::numbers::pi_v<float> * 0.5F, 0.0F}, yaw));
    Scene::TransformComponent turned;
    turned.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    turned.localRotation = yaw;
    sceneGraph.SetLocalTransform(entityManager, entity, turned);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    const Math::Vector3 axisZ = MatrixAxisZ(visibilitySet.candidates[0].placement.worldMatrix);
    EXPECT_NEAR(axisZ.x, 0.0F, 1.0e-3F);
    EXPECT_NEAR(axisZ.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(axisZ.z, 1.0F, 1.0e-3F);
    EXPECT_NEAR(visibilitySet.candidates[0].placement.worldMatrix.m[0][3], 0.0F, 1.0e-4F);
    EXPECT_NEAR(visibilitySet.candidates[0].placement.worldMatrix.m[1][3], 0.0F, 1.0e-4F);
    EXPECT_NEAR(visibilitySet.candidates[0].placement.worldMatrix.m[2][3], -10.0F, 1.0e-4F);
}

TEST_F(MeshRendererTest, BuildVisibilitySet_YBillboardIgnoresCameraHeight) {
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.localBounds =
            Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
        return true;
    });
    assetManager.RegisterLoader<Asset::MaterialAsset>([](const std::filesystem::path&,
                                                              Asset::MaterialAsset& material) {
        material.billboardMode = Asset::MaterialBillboardMode::Y;
        return true;
    });
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mesh_renderer_tests_billboard_y.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mesh_renderer_tests_billboard_y.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    MeshVisibilitySet visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3{10.0F, 40.0F, -10.0F};
    meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    const Math::Vector3 axisZ = MatrixAxisZ(visibilitySet.candidates[0].placement.worldMatrix);
    EXPECT_NEAR(axisZ.x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(axisZ.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(axisZ.z, 0.0F, 1.0e-3F);
}

} // namespace
} // namespace UVE::Render::Tests
