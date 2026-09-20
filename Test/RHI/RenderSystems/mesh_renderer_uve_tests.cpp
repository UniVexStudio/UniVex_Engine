// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_renderer_uve.h"

#include "uve/render_systems/mesh_visibility_set_uve.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/nodes/3d/lod_group_3d_uve.h"
#include "uve/nodes/3d/visibility_region_3d_uve.h"
#include "uve/nodes/3d/world_partition_3d_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterationsUVE = 200000;

class MeshRendererUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    MeshRendererUVE meshRenderer;

    // Entities reach MeshRendererUVE only via SceneGraphUVE::AttachTransformUVE, matching
    // CameraSystemUVETest's fixture precedent.
    Scene::EntityUVE MakeMeshEntityUVE(Math::Vector3UVE worldPosition, Asset::AssetGuidUVE meshGuid,
                                        Asset::AssetGuidUVE materialGuid) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(entity, Scene::MeshComponentUVE{meshGuid, materialGuid});
        return entity;
    }

    // A camera at the origin looking down -Z with a 90-degree fov, matching
    // CameraSystemUVETest's ExtractFrustumUVE fixture.
    [[nodiscard]] static Math::FrustumUVE MakeTestFrustumUVE() {
        const Math::Matrix4x4UVE view =
            Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
        const Math::Matrix4x4UVE projection =
            Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
    }

    void RegisterImmediateLoadersUVE(bool materialIsTransparent) {
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
                mesh.localBounds =
                    Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
                return true;
            });
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [materialIsTransparent](const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.isTransparent = materialIsTransparent;
                return true;
            });
    }

    void WaitUntilAssetsReadyUVE(Asset::AssetGuidUVE meshGuid, Asset::AssetGuidUVE materialGuid) {
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

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_VisibleOpaqueEntity_AppearsInOpaqueBucket) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_opaque.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_opaque.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    ASSERT_EQ(queue.opaqueItems.size(), 1U);
    EXPECT_TRUE(queue.transparentItems.empty());
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_VisibleTransparentEntity_AppearsInTransparentBucket) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/true);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_transparent.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_transparent.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_TRUE(queue.opaqueItems.empty());
    ASSERT_EQ(queue.transparentItems.size(), 1U);
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_EntityOutsideFrustum_Excluded) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_offscreen.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_offscreen.uvemat");
    // Behind the camera, matching CameraSystemUVETest's own "behindCameraBox" fixture.
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
}

TEST(MeshComponentUVE, IsMeshComponentValidUVE_AllowsUnassignedPair) {
    EXPECT_TRUE(Scene::IsMeshComponentValidUVE(
        Scene::MeshComponentUVE{Asset::kInvalidAssetGuidUVE, Asset::kInvalidAssetGuidUVE}));
}

TEST(MeshComponentUVE, IsMeshComponentValidUVE_RejectsPartialAssignment) {
    EXPECT_FALSE(Scene::IsMeshComponentValidUVE(Scene::MeshComponentUVE{Asset::AssetGuidUVE{1U},
                                                                         Asset::kInvalidAssetGuidUVE}));
    EXPECT_FALSE(Scene::IsMeshComponentValidUVE(Scene::MeshComponentUVE{Asset::kInvalidAssetGuidUVE,
                                                                         Asset::AssetGuidUVE{2U}}));
    EXPECT_TRUE(Scene::IsMeshComponentValidUVE(Scene::MeshComponentUVE{Asset::AssetGuidUVE{1U},
                                                                        Asset::AssetGuidUVE{2U}}));
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_InvalidGuid_Skipped) {
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Asset::kInvalidAssetGuidUVE, Asset::kInvalidAssetGuidUVE);

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 2U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 0U);
}

#if UVE_DEBUG
TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_PartialReference_Asserts) {
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Asset::AssetGuidUVE{1U},
                      Asset::kInvalidAssetGuidUVE);

    EXPECT_DEATH(
        { static_cast<void>(meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase,
                                                                MakeTestFrustumUVE())); },
        "");
}
#endif

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_NonFiniteWorldTransform_IsCountedAndSkipped) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_nonfinite.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_nonfinite.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition.x =
        std::numeric_limits<float>::quiet_NaN();

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 0U);
    EXPECT_EQ(queue.invalidRenderEligibility, 1U);
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_FailedAssetLoad_IsCountedAndSkipped) {
    assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
        [](const std::filesystem::path&, Asset::MeshAssetUVE&) { return false; });
    assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
        [](const std::filesystem::path&, Asset::MaterialAssetUVE&) { return true; });
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_failed.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_failed.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
        assetManager.LoadUVE<Asset::MeshAssetUVE>(meshGuid, assetDatabase);
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
        assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE &&
                             (!(meshHandle.HasFailedUVE() || meshHandle.IsReadyUVE()) ||
                              !(materialHandle.HasFailedUVE() || materialHandle.IsReadyUVE()));
         ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.HasFailedUVE());
    ASSERT_TRUE(materialHandle.IsReadyUVE());

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 1U);
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_MixOfVisibleAndCulledEntities_OnlyVisibleOneIncluded) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_mixed.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_mixed.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{1000.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const RenderQueueUVE queue =
        meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, MakeTestFrustumUVE());

    EXPECT_EQ(queue.opaqueItems.size(), 1U);
}

TEST_F(MeshRendererUVETest, ExtractRenderQueueUVE_AssetNotReadyYet_ExcludedThenIncludedOnceLoaded) {
    std::atomic<bool> allowMeshLoad{false};
    assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
        [&allowMeshLoad](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
            while (!allowMeshLoad.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            mesh.localBounds =
                Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
            return true;
        });
    assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
        [](const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
            material.isTransparent = false;
            return true;
        });

    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_gate.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_gate.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
        assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE && !materialHandle.IsReadyUVE(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(materialHandle.IsReadyUVE());

    const RenderQueueUVE firstAttempt = meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, frustum);
    EXPECT_TRUE(firstAttempt.opaqueItems.empty());
    EXPECT_EQ(firstAttempt.pendingAssetLoads, 1U);
    EXPECT_EQ(firstAttempt.failedAssetLoads, 0U);

    allowMeshLoad.store(true);

    RenderQueueUVE secondAttempt;
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        secondAttempt = meshRenderer.ExtractRenderQueueUVE(entityManager, assetManager, assetDatabase, frustum);
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
// BuildVisibilitySetUVE / CullVisibilitySetIntoUVE - the frustum-independent
// split.
//
// The claim this change makes is that it is a pure performance change: the
// same scene must produce the same queue as before. So the load-bearing tests
// here are EQUIVALENCE tests against ExtractRenderQueueIntoUVE, not tests of
// the new functions in isolation - a new path that is merely self-consistent
// but disagrees with the old one is exactly the failure worth catching.
// ---------------------------------------------------------------------------

TEST_F(MeshRendererUVETest, BuildAndCull_ProducesTheSameQueueAsSingleStepExtraction) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_equiv.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_equiv.uvemat");
    // A mix of in-frustum and out-of-frustum entities, so the comparison covers the cull verdict
    // rather than just agreeing that everything is visible.
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -30.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 50.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{500.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    RenderQueueUVE singleStep;
    meshRenderer.ExtractRenderQueueIntoUVE(entityManager, assetManager, assetDatabase, frustum, singleStep);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    RenderQueueUVE twoStep;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, frustum, twoStep);

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

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_IsIndependentOfAnyFrustum) {
    // The defining property. An entity far outside the main view still belongs in the candidate
    // set, because a shadow cascade's frustum may well contain it - dropping it at build time
    // would delete shadows cast by off-screen geometry, which is the classic version of this bug.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_offscreen.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_offscreen.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 500.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        EXPECT_TRUE(candidate.placement.IsPlacedUVE());
    }

    // ...and the one behind the camera is the one the main view then rejects.
    RenderQueueUVE queue;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, MakeTestFrustumUVE(), queue);
    EXPECT_EQ(queue.opaqueItems.size(), 1U);
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_OneSetFeedsManyFrustaWithoutBeingConsumed) {
    // The whole frame depends on this: the set is built once and culled four times, so culling
    // must not move the handles out of it. A second cull returning fewer items would mean the
    // shadow cascades silently lose casters after the first one runs.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_reuse.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_reuse.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -20.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    RenderQueueUVE first;
    RenderQueueUVE second;
    RenderQueueUVE third;
    RenderQueueUVE fourth;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, frustum, first);
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, frustum, second);
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, frustum, third);
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, frustum, fourth);

    EXPECT_EQ(first.opaqueItems.size(), 2U);
    EXPECT_EQ(fourth.opaqueItems.size(), first.opaqueItems.size());
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
    // The handles must still resolve after four culls - a moved-from handle would not.
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.IsReadyUVE());
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.IsReadyUVE());
    }
    ASSERT_EQ(fourth.opaqueItems.size(), 2U);
    EXPECT_TRUE(fourth.opaqueItems[0].meshHandle.IsReadyUVE());
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_DifferentFrustaSelectDifferentSubsets) {
    // Proves the cull is genuinely per-frustum rather than baked in at build time - which is what
    // would happen if visibility leaked into the shared step.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_subset.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_subset.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // The same frustum turned around to look down +Z instead of -Z.
    Math::QuaternionUVE turnAround;
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float>,
                                          turnAround));
    const Math::Matrix4x4UVE behindView =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, turnAround);
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 2U);

    RenderQueueUVE forwardQueue;
    RenderQueueUVE backwardQueue;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, MakeTestFrustumUVE(), forwardQueue);
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet,
                                          Math::FrustumUVE::FromViewProjectionUVE(projection * behindView),
                                          backwardQueue);

    EXPECT_EQ(forwardQueue.opaqueItems.size(), 1U);
    EXPECT_EQ(backwardQueue.opaqueItems.size(), 1U);
    // Different entities, from one shared candidate set.
    EXPECT_NE(forwardQueue.opaqueItems[0].worldMatrix, backwardQueue.opaqueItems[0].worldMatrix);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_ReusedAcrossFrames_DoesNotAccumulate) {
    // Renderer3DUVE holds one set for the lifetime of the renderer, so appending instead of
    // clearing would re-render every frame the scene has ever had.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_frames.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_frames.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U);
    EXPECT_EQ(visibilitySet.failedAssetLoads, 0U);
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_CountersDescribeTheSceneNotTheView) {
    // An unassigned entity is one broken entity regardless of how many frusta ask, so every queue
    // built from the set reports the same count - not one per cascade.
    //
    // Both guids are left invalid together, matching ExtractRenderQueueUVE_InvalidGuid_Skipped:
    // MeshComponentUVE's invariant is that the two are assigned or unassigned TOGETHER, so a
    // half-assigned component is a component bug and trips UVE_ASSERT before extraction sees it.
    // Hence two counted references for the one entity.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_counters.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_counters.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -12.0F}, Asset::kInvalidAssetGuidUVE,
                      Asset::kInvalidAssetGuidUVE);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 2U);
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);

    RenderQueueUVE first;
    RenderQueueUVE second;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, MakeTestFrustumUVE(), first);
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, MakeTestFrustumUVE(), second);

    EXPECT_EQ(first.invalidAssetReferences, 2U);
    EXPECT_EQ(second.invalidAssetReferences, 2U);
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_EmptySetClearsTheQueue) {
    // The queue is caller-owned and reused, so a frame that culls nothing must leave an empty
    // queue rather than last frame's contents.
    RenderQueueUVE queue;
    queue.opaqueItems.reserve(4U);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.CullVisibilitySetIntoUVE(visibilitySet, MakeTestFrustumUVE(), queue);

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

TEST_F(MeshRendererUVETest, PlacementCache_SecondBuildOfAStaticScene_IsAllHits) {
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_static.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_static.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{4.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    // First frame can only miss - there is nothing to reuse yet.
    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 3U);

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 3U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    EXPECT_EQ(visibilitySet.candidates.size(), 3U);
}

TEST_F(MeshRendererUVETest, PlacementCache_HitProducesTheSamePlacementAsARecompute) {
    // The claim is that a hit is indistinguishable from a recompute. Anything less and the cache
    // is trading correctness for speed, which is not a trade worth making.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_same.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_same.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{1.5F, -2.5F, -12.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE cached;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cached);
    ASSERT_EQ(cached.candidates.size(), 1U);
    const Math::Matrix4x4UVE firstMatrix = cached.candidates[0].placement.worldMatrix;
    const Math::AabbUVE firstBounds = cached.candidates[0].placement.worldBounds;

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, cached);
    ASSERT_EQ(cached.placementCacheHits, 1U);

    // A completely fresh set has no cache, so its placement is necessarily recomputed.
    MeshVisibilitySetUVE fresh;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, fresh);
    ASSERT_EQ(fresh.placementCacheMisses, 1U);
    ASSERT_EQ(fresh.candidates.size(), 1U);

    EXPECT_EQ(cached.candidates[0].placement.worldMatrix, fresh.candidates[0].placement.worldMatrix);
    EXPECT_EQ(cached.candidates[0].placement.worldMatrix, firstMatrix);
    EXPECT_EQ(cached.candidates[0].placement.worldBounds.min, fresh.candidates[0].placement.worldBounds.min);
    EXPECT_EQ(cached.candidates[0].placement.worldBounds.max, fresh.candidates[0].placement.worldBounds.max);
    EXPECT_EQ(firstBounds.min, fresh.candidates[0].placement.worldBounds.min);
}

TEST_F(MeshRendererUVETest, PlacementCache_MovedEntity_IsRecomputedNotReused) {
    // THE test. A stale placement renders an object at last frame's position with no error
    // anywhere - exactly the silent failure a cache introduces if invalidation is wrong.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_moved.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_moved.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    const Math::AabbUVE beforeBounds = visibilitySet.candidates[0].placement.worldBounds;

    Scene::TransformComponentUVE moved;
    moved.localPosition = Math::Vector3UVE{0.0F, 0.0F, -25.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, moved);
    sceneGraph.UpdateUVE(entityManager);

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_NE(visibilitySet.candidates[0].placement.worldBounds.min.z, beforeBounds.min.z);
    EXPECT_FLOAT_EQ(visibilitySet.candidates[0].placement.worldBounds.GetCenterUVE().z, -25.0F);
}

TEST_F(MeshRendererUVETest, PlacementCache_TinyMovement_StillInvalidates) {
    // The key is an identity test, not a tolerance test. A sub-millimetre move must still miss -
    // a cache that rounds is a cache that drifts, and the error accumulates invisibly.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_tiny.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_tiny.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    Scene::TransformComponentUVE nudged;
    nudged.localPosition = Math::Vector3UVE{0.0001F, 0.0F, -10.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, nudged);
    sceneGraph.UpdateUVE(entityManager);

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
}

TEST_F(MeshRendererUVETest, PlacementCache_RotationAndScaleAreBothPartOfTheKey) {
    // Position is the obvious field to key on and the easy one to get right. Rotation and scale
    // change the world bounds just as much and are easy to forget.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_rs.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_rs.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    Math::QuaternionUVE turned;
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                          std::numbers::pi_v<float> / 4.0F, turned));
    Scene::TransformComponentUVE rotated;
    rotated.localPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    rotated.localRotation = turned;
    sceneGraph.SetLocalTransformUVE(entityManager, entity, rotated);
    sceneGraph.UpdateUVE(entityManager);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U) << "rotation must be part of the key";

    // Settle, so the scale check starts from a hit rather than from the rotation's miss.
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheHits, 1U);

    Scene::TransformComponentUVE scaled = rotated;
    scaled.localScale = Math::Vector3UVE{3.0F, 3.0F, 3.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, entity, scaled);
    sceneGraph.UpdateUVE(entityManager);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U) << "scale must be part of the key";
    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    // And the recomputed bounds genuinely reflect the 3x scale, rather than merely being new.
    const Math::AabbUVE bounds = visibilitySet.candidates[0].placement.worldBounds;
    EXPECT_GT(bounds.max.x - bounds.min.x, 1.4F);
}

TEST_F(MeshRendererUVETest, PlacementCache_SwappedMesh_InvalidatesEvenWhenTheTransformIsIdentical) {
    // A stationary entity whose mesh guid changes. The transform is untouched, so a transform-only
    // key would happily serve the old mesh's bounds forever.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE firstMesh = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_swap_a.uvemodel");
    const Asset::AssetGuidUVE secondMesh = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_swap_b.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_swap.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, firstMesh, materialGuid);
    WaitUntilAssetsReadyUVE(firstMesh, materialGuid);
    WaitUntilAssetsReadyUVE(secondMesh, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheMisses, 1U);

    entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity).meshGuid = secondMesh;

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 0U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 1U);
}

TEST_F(MeshRendererUVETest, PlacementCache_DestroyedEntity_IsPrunedNotRetained) {
    // Unbounded growth turns a cache into a leak. A long-running streaming world must not retain
    // an entry for every entity it has ever shown.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_prune.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_prune.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::EntityUVE doomed = MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCache.size(), 2U);

    entityManager.DestroyEntityUVE(doomed);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.placementCache.size(), 1U) << "the destroyed entity's entry must be pruned";
}

TEST_F(MeshRendererUVETest, PlacementCache_SurvivesManyFramesWithoutGrowing) {
    // The steady state a real frame loop lives in: same scene, many frames. The cache must reach a
    // fixed size and stay there, all hits.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_steady.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_steady.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    for (int frame = 0; frame < 50; ++frame) {
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    }

    EXPECT_EQ(visibilitySet.placementCache.size(), 2U);
    EXPECT_EQ(visibilitySet.placementCacheHits, 2U);
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    EXPECT_EQ(visibilitySet.candidates.size(), 2U);
}

TEST_F(MeshRendererUVETest, PlacementCache_DoesNotChangeTheQueueAnyFrameProduces) {
    // The end-to-end guarantee, stated where a reader will look for it: caching is invisible
    // downstream. A frame served entirely from cache must cull to exactly the queue an uncached
    // frame would.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_queue.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cache_queue.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -30.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 50.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    MeshVisibilitySetUVE warm;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, warm);
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, warm);
    ASSERT_EQ(warm.placementCacheMisses, 0U);
    RenderQueueUVE warmQueue;
    meshRenderer.CullVisibilitySetIntoUVE(warm, frustum, warmQueue);

    RenderQueueUVE coldQueue;
    meshRenderer.ExtractRenderQueueIntoUVE(entityManager, assetManager, assetDatabase, frustum, coldQueue);

    ASSERT_EQ(warmQueue.opaqueItems.size(), coldQueue.opaqueItems.size());
    for (std::size_t index = 0U; index < coldQueue.opaqueItems.size(); ++index) {
        EXPECT_EQ(warmQueue.opaqueItems[index].worldMatrix, coldQueue.opaqueItems[index].worldMatrix);
        EXPECT_FLOAT_EQ(warmQueue.opaqueItems[index].sortDepth, coldQueue.opaqueItems[index].sortDepth);
    }
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_EntitiesSharingAssets_EachOwnAUsableHandle) {
    // The hazard in resolving a GUID once and sharing it: the first entity to use a resolution
    // must not consume it. If the shared handle were moved from rather than copied, the second and
    // later entities would receive an empty handle - which would not fail to compile, and would
    // not fail any test that only renders one entity.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_shared.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_shared.uvemat");
    constexpr int kSharedEntityCount = 4;
    for (int index = 0; index < kSharedEntityCount; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kSharedEntityCount));
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuidUVE(), meshGuid);
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.GetGuidUVE(), materialGuid);
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.IsReadyUVE()) << "a shared resolution must not be consumed by one entity";
        EXPECT_TRUE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.IsReadyUVE());
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.TryGetUVE(), nullptr);
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.TryGetUVE(), nullptr);
    }
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_SharedFailedAsset_IsCountedOncePerEntityNotOncePerAsset) {
    // Sharing the resolution must not change what the diagnostics mean. These counters answer "how
    // many entities could not be drawn this frame", so three entities blocked on one broken mesh
    // is three, not one. Folding the count into the resolution would quietly turn every one of
    // these counters into a per-asset figure while every single-entity test kept passing.
    //
    // The mesh fails via a loader that returns false, matching the existing failed-load test:
    // an unregistered GUID stays pending rather than failing, which measures something else.
    assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
        [](const std::filesystem::path&, Asset::MeshAssetUVE&) { return false; });
    assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
        [](const std::filesystem::path&, Asset::MaterialAssetUVE&) { return true; });
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_sharedfail.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_sharedfail.uvemat");
    constexpr int kBlockedEntityCount = 3;
    for (int index = 0; index < kBlockedEntityCount; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }

    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
        assetManager.LoadUVE<Asset::MeshAssetUVE>(meshGuid, assetDatabase);
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
        assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE &&
                             (!(meshHandle.HasFailedUVE() || meshHandle.IsReadyUVE()) ||
                              !(materialHandle.HasFailedUVE() || materialHandle.IsReadyUVE()));
         ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.HasFailedUVE());
    ASSERT_TRUE(materialHandle.IsReadyUVE());

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_TRUE(visibilitySet.candidates.empty());
    EXPECT_EQ(visibilitySet.failedAssetLoads, static_cast<std::size_t>(kBlockedEntityCount))
        << "each blocked entity must be counted, even though they share one failed mesh";
    EXPECT_EQ(visibilitySet.pendingAssetLoads, 0U)
        << "the shared material resolved, so nothing should be reported as still loading";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_AssetBecomingReady_IsObservedOnTheNextBuild) {
    // The resolution memo is per walk, never across frames, because asset state is asynchronous.
    // This is the test that fails if someone later promotes it to a member to "save more work":
    // a mesh that was pending during one build must be drawable on the next without anything else
    // in the scene changing.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_becomesready.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_becomesready.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    // Built before waiting: the loads are in flight, so this build sees them pending.
    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.pendingAssetLoads, 0U)
        << "a resolution must not outlive its walk - the completed load must be seen";
    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_DistinctAssetsPerEntity_AreEachResolved) {
    // The memo keys on GUID, so the failure mode opposite to over-sharing is under-resolving:
    // every entity referencing a DIFFERENT asset must still get its own answer. A memo keyed on
    // something coarser - or one that returned the first entry it found - would collapse these.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    constexpr int kDistinctEntityCount = 3;
    std::vector<Asset::AssetGuidUVE> meshGuids;
    std::vector<Asset::AssetGuidUVE> materialGuids;
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        const std::string suffix = std::to_string(index);
        meshGuids.push_back(assetDatabase.RegisterUVE("mesh_renderer_tests_distinct" + suffix + ".uvemodel"));
        materialGuids.push_back(assetDatabase.RegisterUVE("mesh_renderer_tests_distinct" + suffix + ".uvemat"));
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -10.0F}, meshGuids.back(),
                          materialGuids.back());
    }
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        WaitUntilAssetsReadyUVE(meshGuids[static_cast<std::size_t>(index)],
                                materialGuids[static_cast<std::size_t>(index)]);
    }

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kDistinctEntityCount));
    std::vector<Asset::AssetGuidUVE> seenMeshGuids;
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        seenMeshGuids.push_back(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuidUVE());
    }
    for (const Asset::AssetGuidUVE expected : meshGuids) {
        EXPECT_NE(std::find(seenMeshGuids.cbegin(), seenMeshGuids.cend(), expected), seenMeshGuids.cend())
            << "every distinct mesh GUID must be resolved on its own, not collapsed into a neighbour's";
    }
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_ClusteredAndUnclustered_ProduceTheSameVisibleSet) {
    // The whole optimization rests on one claim: rejecting a cluster rejects only candidates that
    // were going to be rejected anyway. This is the test that checks the claim directly, by culling
    // the same scene with clusters built and with the cluster list emptied, and demanding the same
    // answer. A cluster box that is too tight silently drops visible geometry, which is the exact
    // bug no rendering test with one entity in front of the camera would ever catch.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cluster.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_cluster.uvemat");
    // Spread well past one cluster (64) and well outside the frustum, so clusters are genuinely
    // rejected rather than all trivially accepted.
    constexpr int kSpreadEntityCount = 400;
    for (int index = 0; index < kSpreadEntityCount; ++index) {
        const float x = static_cast<float>((index * 37) % 200) - 100.0F;
        const float y = static_cast<float>((index * 53) % 200) - 100.0F;
        const float z = -static_cast<float>((index * 29) % 200);
        MakeMeshEntityUVE(Math::Vector3UVE{x, y, z}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE clustered;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, clustered);
    ASSERT_FALSE(clustered.clusters.empty()) << "the scene must be large enough to actually cluster";

    RenderQueueUVE clusteredQueue;
    meshRenderer.CullVisibilitySetIntoUVE(clustered, MakeTestFrustumUVE(), clusteredQueue);

    // Same set, clusters removed: the documented fallback path, and the reference answer.
    MeshVisibilitySetUVE flat;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, flat);
    flat.clusters.clear();
    RenderQueueUVE flatQueue;
    meshRenderer.CullVisibilitySetIntoUVE(flat, MakeTestFrustumUVE(), flatQueue);

    ASSERT_EQ(clusteredQueue.opaqueItems.size(), flatQueue.opaqueItems.size())
        << "cluster rejection must never drop a visible candidate";
    EXPECT_EQ(clusteredQueue.transparentItems.size(), flatQueue.transparentItems.size());
    EXPECT_GT(clusteredQueue.opaqueItems.size(), 0U) << "a test where nothing is visible proves nothing";

    // Compared after sorting, because the queues are depth-sorted by the consumer and cluster
    // ordering legitimately changes the order equal-depth items are appended in.
    clusteredQueue.SortUVE();
    flatQueue.SortUVE();
    for (std::size_t index = 0U; index < flatQueue.opaqueItems.size(); ++index) {
        EXPECT_FLOAT_EQ(clusteredQueue.opaqueItems[index].sortDepth, flatQueue.opaqueItems[index].sortDepth)
            << "at index " << index;
    }
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_ClustersCoverEveryCandidateExactlyOnce) {
    // A permutation bug in the reorder - a cycle applied twice, an off-by-one in the offsets -
    // would duplicate one candidate and lose another. Sizes alone would still add up, so this
    // checks the ranges partition the list: contiguous, non-overlapping, covering all of it.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_partition.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_partition.uvemat");
    constexpr int kPartitionEntityCount = 150;
    for (int index = 0; index < kPartitionEntityCount; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kPartitionEntityCount));
    std::size_t expectedFirst = 0U;
    for (const MeshVisibilitySetUVE::CandidateClusterUVE& cluster : visibilitySet.clusters) {
        EXPECT_EQ(cluster.first, expectedFirst) << "clusters must be contiguous and non-overlapping";
        EXPECT_GT(cluster.count, 0U) << "an empty cluster is a plane test that can never pay for itself";
        expectedFirst += cluster.count;
    }
    EXPECT_EQ(expectedFirst, visibilitySet.candidates.size()) << "every candidate must belong to a cluster";
}

TEST_F(MeshRendererUVETest, BuildSpatialClustersUVE_ClusterBoundsEncloseEveryMember) {
    // The rejection is only sound if the enclosing box really encloses. This asserts the invariant
    // the cull depends on, rather than inferring it from a visible-count that happened to match.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_enclose.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_enclose.uvemat");
    constexpr int kEncloseEntityCount = 200;
    for (int index = 0; index < kEncloseEntityCount; ++index) {
        const float x = static_cast<float>((index * 17) % 90) - 45.0F;
        const float y = static_cast<float>((index * 31) % 90) - 45.0F;
        MakeMeshEntityUVE(Math::Vector3UVE{x, y, -20.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_FALSE(visibilitySet.clusters.empty());

    for (const MeshVisibilitySetUVE::CandidateClusterUVE& cluster : visibilitySet.clusters) {
        for (std::size_t offset = 0U; offset < cluster.count; ++offset) {
            const Math::AabbUVE& member =
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

TEST_F(MeshRendererUVETest, BuildSpatialClustersUVE_ReorderingDoesNotDisturbAssetHandles) {
    // The reorder moves candidates around, and candidates own AssetHandleUVE members. A permutation
    // that copied instead of moved, or that left a moved-from element behind, would show up as a
    // handle that no longer resolves - while sizes and bounds all still looked right.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_reorder.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_reorder.uvemat");
    constexpr int kReorderEntityCount = 100;
    for (int index = 0; index < kReorderEntityCount; ++index) {
        const float x = static_cast<float>((index * 41) % 60) - 30.0F;
        MakeMeshEntityUVE(Math::Vector3UVE{x, 0.0F, -15.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kReorderEntityCount));
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.GetGuidUVE(), meshGuid);
        EXPECT_EQ(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.GetGuidUVE(), materialGuid);
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].meshHandle.TryGetUVE(), nullptr) << "a reordered candidate must still resolve";
        EXPECT_NE(visibilitySet.assetPairs[candidate.assetPairIndex].materialHandle.TryGetUVE(), nullptr);
    }
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_EntitiesSharingAssets_ShareOneAssetPairEntry) {
    // The point of the change: references are held per distinct asset pairing, not per entity.
    // Four entities on one mesh and one material must produce exactly one entry, and every
    // candidate must point at it.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_onepair.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_onepair.uvemat");
    constexpr int kSharedEntityCount = 4;
    for (int index = 0; index < kSharedEntityCount; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -10.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kSharedEntityCount));
    EXPECT_EQ(visibilitySet.assetPairs.size(), 1U)
        << "entities sharing a mesh and material must share one reference pair, not hold one each";
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        EXPECT_EQ(candidate.assetPairIndex, 0U);
    }
    EXPECT_TRUE(visibilitySet.assetPairs[0].meshHandle.IsReadyUVE());
    EXPECT_TRUE(visibilitySet.assetPairs[0].materialHandle.IsReadyUVE());
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_SameMeshDifferentMaterials_GetSeparateAssetPairs) {
    // The failure mode opposite to over-sharing, and the reason the key is BOTH guids. Keying on
    // the mesh alone would hand the second entity the first one's material - and every visible
    // check would still pass, because both entities would still draw. The material would just be
    // silently wrong.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_twomat.uvemodel");
    const Asset::AssetGuidUVE firstMaterialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_twomat_a.uvemat");
    const Asset::AssetGuidUVE secondMaterialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_twomat_b.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, firstMaterialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, secondMaterialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, firstMaterialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, secondMaterialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 2U);
    EXPECT_EQ(visibilitySet.assetPairs.size(), 2U)
        << "one mesh with two materials is two pairings, not one";
    std::vector<Asset::AssetGuidUVE> seenMaterialGuids;
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        ASSERT_LT(candidate.assetPairIndex, visibilitySet.assetPairs.size());
        const MeshVisibilityAssetPairUVE& pair = visibilitySet.assetPairs[candidate.assetPairIndex];
        EXPECT_EQ(pair.meshHandle.GetGuidUVE(), meshGuid);
        seenMaterialGuids.push_back(pair.materialHandle.GetGuidUVE());
    }
    EXPECT_NE(std::find(seenMaterialGuids.cbegin(), seenMaterialGuids.cend(), firstMaterialGuid),
              seenMaterialGuids.cend());
    EXPECT_NE(std::find(seenMaterialGuids.cbegin(), seenMaterialGuids.cend(), secondMaterialGuid),
              seenMaterialGuids.cend());
}

TEST_F(MeshRendererUVETest, BuildSpatialClustersUVE_ReorderingKeepsEachCandidatePointingAtItsOwnAssets) {
    // The clustering reorder moves candidates around, and they now identify their assets by index.
    // An index that did not travel with its candidate would silently repaint geometry with another
    // object's material - visible only as a wrong-looking frame, never as a failure. Every entity
    // here uses a distinct mesh+material pair so a mismatch cannot hide behind shared assets.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    constexpr int kDistinctEntityCount = 80;
    std::vector<Asset::AssetGuidUVE> meshGuids;
    std::vector<Asset::AssetGuidUVE> materialGuids;
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        const std::string suffix = std::to_string(index);
        meshGuids.push_back(assetDatabase.RegisterUVE("mesh_renderer_tests_pairtravel" + suffix + ".uvemodel"));
        materialGuids.push_back(assetDatabase.RegisterUVE("mesh_renderer_tests_pairtravel" + suffix + ".uvemat"));
        // Spread widely so the Morton ordering genuinely permutes them rather than leaving the
        // creation order intact - a reorder that does nothing would not test anything.
        const float x = static_cast<float>((index * 37) % 60) - 30.0F;
        const float y = static_cast<float>((index * 53) % 60) - 30.0F;
        MakeMeshEntityUVE(Math::Vector3UVE{x, y, -20.0F}, meshGuids.back(), materialGuids.back());
    }
    for (int index = 0; index < kDistinctEntityCount; ++index) {
        WaitUntilAssetsReadyUVE(meshGuids[static_cast<std::size_t>(index)],
                                materialGuids[static_cast<std::size_t>(index)]);
    }

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), static_cast<std::size_t>(kDistinctEntityCount));
    EXPECT_EQ(visibilitySet.assetPairs.size(), static_cast<std::size_t>(kDistinctEntityCount));
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        ASSERT_LT(candidate.assetPairIndex, visibilitySet.assetPairs.size());
        const MeshVisibilityAssetPairUVE& pair = visibilitySet.assetPairs[candidate.assetPairIndex];
        // Each entity was created with matching suffixes, so a candidate whose index travelled
        // correctly has a mesh and material from the same position in the two lists.
        const auto meshPosition = std::find(meshGuids.cbegin(), meshGuids.cend(), pair.meshHandle.GetGuidUVE());
        const auto materialPosition =
            std::find(materialGuids.cbegin(), materialGuids.cend(), pair.materialHandle.GetGuidUVE());
        ASSERT_NE(meshPosition, meshGuids.cend());
        ASSERT_NE(materialPosition, materialGuids.cend());
        EXPECT_EQ(std::distance(meshGuids.cbegin(), meshPosition),
                  std::distance(materialGuids.cbegin(), materialPosition))
            << "a candidate's asset index must survive the spatial reorder";
    }
}

TEST_F(MeshRendererUVETest, CullVisibilitySetIntoUVE_QueueItemsOutliveTheVisibilitySet) {
    // Candidates borrow the set's references, but RenderQueueUVE is returned by value from the
    // public ExtractRenderQueueUVE and can outlive the set entirely - so queue items must still
    // own theirs. This drives that directly: build a queue, destroy the set, collect garbage, and
    // require the queue's handles to still resolve.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_outlive.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_outlive.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    RenderQueueUVE queue;
    {
        MeshVisibilitySetUVE scopedSet;
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, scopedSet);
        meshRenderer.CullVisibilitySetIntoUVE(scopedSet, MakeTestFrustumUVE(), queue);
        ASSERT_EQ(queue.opaqueItems.size(), 1U);
    }
    // The set is gone along with its references. Only the queue's own remain.
    assetManager.CollectGarbageUVE();

    EXPECT_TRUE(queue.opaqueItems[0].meshHandle.IsReadyUVE())
        << "a queue item must hold its own reference - it can outlive the set it came from";
    EXPECT_TRUE(queue.opaqueItems[0].materialHandle.IsReadyUVE());
    EXPECT_NE(queue.opaqueItems[0].meshHandle.TryGetUVE(), nullptr);
    EXPECT_NE(queue.opaqueItems[0].materialHandle.TryGetUVE(), nullptr);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_HiddenEntitiesAreSkippedBeforeAnyWorkIsDone) {
    // Hiding has to be worth something, not just look right. The gate sits ahead of the asset
    // resolution and the placement cache, so a hidden mesh costs a component lookup and nothing
    // else - this asserts it produces no candidate at all rather than a candidate that is later
    // culled, which would still have paid to build it.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_hidden.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_hidden.uvemat");
    const Scene::EntityUVE shown = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::EntityUVE hidden = MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // Derived field, as the scene graph would have left it after an update.
    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(
        hidden, Scene::VisibilityComponentUVE{/*visible=*/false, /*visibleInHierarchy=*/false});
    static_cast<void>(shown);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U) << "the hidden entity must not become a candidate";
    EXPECT_EQ(visibilitySet.hiddenEntities, 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U)
        << "a hidden object is not a scene fault and must not be reported as one";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_VisibleInHierarchyIsWhatCounts_NotTheAuthoredFlag) {
    // The renderer reads the DERIVED field. An entity whose own switch is on but whose parent is
    // hidden has visible == true and visibleInHierarchy == false, and reading the wrong one draws
    // exactly the objects the author just hid.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_inherited.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_inherited.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(
        entity, Scene::VisibilityComponentUVE{/*visible=*/true, /*visibleInHierarchy=*/false});

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_TRUE(visibilitySet.candidates.empty())
        << "the renderer must honour the inherited answer, not the entity's own switch";
    EXPECT_EQ(visibilitySet.hiddenEntities, 1U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_EntitiesWithoutTheComponentStillRender) {
    // The component is optional and most entities will never carry one. If its absence were read
    // as hidden, every existing scene would go blank.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_novis.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_novis.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.hiddenEntities, 0U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_InterpolatedPoseIsDrawnBetweenTheTwoSimulatedSteps) {
    // The payoff. At alpha 0.5 the candidate must sit halfway between the two recorded poses, not
    // at the newest one - which is what the renderer drew before and what produced the stutter.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponentUVE interpolation;
    interpolation.mode = Scene::PhysicsInterpolationModeUVE::On;
    interpolation.interpolatedInHierarchy = true;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3UVE{10.0F, 0.0F, -10.0F};
    entityManager.AddComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity, interpolation);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 1U);
    const Math::Vector3UVE center = visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE();
    EXPECT_NEAR(center.x, 5.0F, 1e-3F) << "halfway between the two simulated poses";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_AlphaZeroDrawsThePreviousPoseAndOneDrawsTheCurrent) {
    // The two boundaries have to be exact, or an object visibly jumps at the moment a fixed step
    // lands - which is precisely the artefact interpolation is supposed to remove.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_ends.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_ends.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponentUVE interpolation;
    interpolation.mode = Scene::PhysicsInterpolationModeUVE::On;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3UVE{-4.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3UVE{6.0F, 0.0F, -10.0F};
    entityManager.AddComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity, interpolation);

    MeshVisibilitySetUVE atStart;
    atStart.physicsInterpolationAlpha = 0.0F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, atStart);
    ASSERT_EQ(atStart.candidates.size(), 1U);
    EXPECT_NEAR(atStart.candidates[0U].placement.worldBounds.GetCenterUVE().x, -4.0F, 1e-3F);

    MeshVisibilitySetUVE atEnd;
    atEnd.physicsInterpolationAlpha = 1.0F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, atEnd);
    ASSERT_EQ(atEnd.candidates.size(), 1U);
    EXPECT_NEAR(atEnd.candidates[0U].placement.worldBounds.GetCenterUVE().x, 6.0F, 1e-3F);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_EntitiesWithoutInterpolationDrawAtTheSimulatedPose) {
    // The component is optional and most entities will never carry one. They must be unaffected by
    // a non-zero alpha, or switching interpolation on would move the whole static world.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_none.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_none.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{3.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 0U);
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE().x, 3.0F, 1e-3F);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_InterpolationDoesNotDefeatThePlacementCache) {
    // The design constraint that decided where the blend goes. The placement cache is keyed on the
    // SIMULATED transform and is worth about 29x; blending before the key would miss on every
    // frame for every moving object and hand all of that back. A second build with a different
    // alpha but the same simulated pose must still hit.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_cache.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_cache.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponentUVE interpolation;
    interpolation.mode = Scene::PhysicsInterpolationModeUVE::On;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3UVE{1.0F, 0.0F, -10.0F};
    entityManager.AddComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity, interpolation);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.25F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    ASSERT_EQ(visibilitySet.placementCacheMisses, 1U) << "first build must populate the cache";

    // Same simulated pose, different alpha - the frame after, in other words.
    visibilitySet.physicsInterpolationAlpha = 0.75F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    EXPECT_EQ(visibilitySet.placementCacheHits, 1U)
        << "a new alpha must not invalidate the cached placement";
    EXPECT_EQ(visibilitySet.placementCacheMisses, 0U);
    // And the blend still moved, so the hit is not coming from skipping the work entirely.
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE().x, 0.75F, 1e-3F);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_InterpolationOffUsesTheSimulatedPose) {
    // Off must reach all the way through to the drawn position, not merely be recorded.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_off.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_interp_off.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{8.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    Scene::PhysicsInterpolationComponentUVE interpolation;
    interpolation.mode = Scene::PhysicsInterpolationModeUVE::Off;
    interpolation.interpolatedInHierarchy = false;
    interpolation.hasPreviousPose = true;
    interpolation.previousPosition = Math::Vector3UVE{-100.0F, 0.0F, -10.0F};
    interpolation.currentPosition = Math::Vector3UVE{100.0F, 0.0F, -10.0F};
    entityManager.AddComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity, interpolation);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.physicsInterpolationAlpha = 0.5F;
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.interpolatedCandidates, 0U);
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE().x, 8.0F, 1e-3F)
        << "Off must draw the simulated pose, not a blend of the recorded ones";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_LodGroupPastItsChainIsDroppedBeforeAnyWorkIsDone) {
    // The payoff: a distance-culled entity must produce no candidate at all, not a candidate that
    // is culled later. The gate sits ahead of asset resolution and placement, so the object costs
    // a subtraction and a length and nothing else.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lod.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lod.uvemat");
    const Scene::EntityUVE near = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    const Scene::EntityUVE far = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -500.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    for (const Scene::EntityUVE entity : {near, far}) {
        entityManager.AddComponentUVE<Scene::LodGroup3DNodeComponentUVE>(
            entity, Scene::LodGroup3DNodeComponentUVE{});
    }

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U) << "the far entity must not become a candidate";
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 1U);
    EXPECT_EQ(visibilitySet.invalidAssetReferences, 0U)
        << "a distance-culled object is not a scene fault";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_LodLevelIsResolvedForVisibleEntitiesToo) {
    // currentLevel is what a future mesh swap indexes by and what the Inspector shows. Resolving
    // it only on the cull path would leave it correct exactly when nobody can see the object.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lodlevel.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lodlevel.uvemat");
    // Default chain is 10/25/60/120 - this sits in level 2.
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -40.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    entityManager.AddComponentUVE<Scene::LodGroup3DNodeComponentUVE>(
        entity, Scene::LodGroup3DNodeComponentUVE{});

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 0U);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::LodGroup3DNodeComponentUVE>(entity).currentLevel, 2U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_TheCameraPositionIsWhatDistanceIsMeasuredFrom) {
    // Distance is from the CAMERA, not from the origin. With the camera moved out to meet it, an
    // object that would otherwise be past the chain is back in range - which is the whole point
    // of a draw distance and trivially easy to get wrong by measuring from the wrong point.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lodcam.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_lodcam.uvemat");
    const Scene::EntityUVE entity = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -200.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    entityManager.AddComponentUVE<Scene::LodGroup3DNodeComponentUVE>(
        entity, Scene::LodGroup3DNodeComponentUVE{});

    MeshVisibilitySetUVE fromOrigin;
    fromOrigin.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, fromOrigin);
    EXPECT_EQ(fromOrigin.distanceCulledEntities, 1U) << "200 m away is past the default chain";

    MeshVisibilitySetUVE fromNearby;
    fromNearby.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, -195.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, fromNearby);
    EXPECT_EQ(fromNearby.distanceCulledEntities, 0U) << "5 m from the camera is level 0";
    ASSERT_EQ(fromNearby.candidates.size(), 1U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_EntitiesWithoutALodGroupAreNeverDistanceCulled) {
    // The component is opt-in. Without one, an entity draws at any distance - otherwise adding
    // LOD support to the engine would silently impose a draw distance on every existing scene.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_nolod.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_nolod.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -9000.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    EXPECT_EQ(visibilitySet.candidates.size(), 1U);
    EXPECT_EQ(visibilitySet.distanceCulledEntities, 0U);
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_PartitionCellOutsideTheBudgetIsCulledWithItsOwnCounter) {
    // The rendering half of WorldPartition3D: the membership component is the ONLY runtime state
    // the render pipeline reads (SyncWorldPartition3DNodesUVE writes it; this test simulates its
    // verdict by hand so the gate is measured in isolation). A live-flag true member renders;
    // live=false (the engine put its cell outside the budget) is culled and counted in
    // partitionCulledEntities so authored hiding and partition streaming never blur together.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_wp.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_wp.uvemat");

    const Scene::EntityUVE partition = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE partitionTransform;
    partitionTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, partition, partitionTransform);
    Scene::WorldPartition3DNodeComponentUVE partitionComponent;
    partitionComponent.cellSize = 4.0F;
    partitionComponent.cellCounts = {2U, 1U, 2U};
    partitionComponent.maximumLoadedCells = 1U;
    entityManager.AddComponentUVE<Scene::WorldPartition3DNodeComponentUVE>(partition,
                                                                           partitionComponent);

    const Scene::EntityUVE near = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F},
                                                    meshGuid, materialGuid);
    const Scene::EntityUVE far = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -6.0F},
                                                   meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    sceneGraph.SetParentUVE(entityManager, near, partition);
    sceneGraph.SetParentUVE(entityManager, far, partition);

    // What SyncWorldPartition3DNodesUVE writes after admitting only the nearest cell:
    entityManager.AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        near, Scene::WorldPartition3DMembershipComponentUVE{partition, true});
    entityManager.AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        far, Scene::WorldPartition3DMembershipComponentUVE{partition, false});

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U)
        << "only the admitted cell's mesh may render";
    EXPECT_TRUE(visibilitySet.candidates[0U].placement.IsPlacedUVE());
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE().z, -1.0F, 1e-3F)
        << "and it is the NEAR member, not the far one";
    EXPECT_EQ(visibilitySet.partitionCulledEntities, 1U)
        << "the partition gate is counted in its own counter, never conflated with authoring";

    MeshVisibilitySetUVE repeat;
    repeat.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, repeat);
    ASSERT_EQ(repeat.candidates.size(), 1U);
    EXPECT_NEAR(repeat.candidates[0U].placement.worldBounds.GetCenterUVE().z, -1.0F, 1e-3F);
    EXPECT_EQ(repeat.partitionCulledEntities, 1U)
        << "the same membership verdicts answer the same gate decision every build";
}

TEST_F(MeshRendererUVETest, BuildVisibilitySetUVE_InactiveRegionSkipsItsInteriorWithItsOwnCounter) {
    // The render half of VisibilityRegion3D: the engine-owned membership is the only runtime
    // state the pipeline reads (SyncVisibilityRegion3DNodesUVE writes it; this test stamps it by
    // hand to isolate the gate). live=false (no viewer inside the room) is culled and counted in
    // regionCulledEntities - "nobody is inside" never blurs with streaming-budget or authored
    // hiding counts.
    RegisterImmediateLoadersUVE(/*materialIsTransparent=*/false);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_vr.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("mesh_renderer_tests_vr.uvemat");

    const Scene::EntityUVE region = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE regionTransform;
    regionTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransformUVE(entityManager, region, regionTransform);
    Scene::VisibilityRegion3DNodeComponentUVE regionComponent;
    regionComponent.halfExtents = Math::Vector3UVE{5.0F, 5.0F, 5.0F};
    regionComponent.active = false; // the engine's verdict: nobody is inside this room
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DNodeComponentUVE>(region,
                                                                             regionComponent);

    const Scene::EntityUVE interior = MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -2.0F},
                                                       meshGuid, materialGuid);
    static_cast<void>(MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -9.0F},
                                        meshGuid, materialGuid)); // the corridor mesh
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // What SyncVisibilityRegion3DNodesUVE writes: only the in-room mesh is managed, with the
    // region's inactive verdict; the corridor outside every region carries no membership.
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        interior, Scene::VisibilityRegion3DMembershipComponentUVE{region, false});

    MeshVisibilitySetUVE visibilitySet;
    visibilitySet.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);

    ASSERT_EQ(visibilitySet.candidates.size(), 1U) << "only the out-of-room mesh may render";
    EXPECT_NEAR(visibilitySet.candidates[0U].placement.worldBounds.GetCenterUVE().z, -9.0F,
                1e-3F)
        << "and it is the corridor, not the interior";
    EXPECT_EQ(visibilitySet.regionCulledEntities, 1U)
        << "the region gate has its own counter - room culling is a level-design stat";
    EXPECT_EQ(visibilitySet.partitionCulledEntities, 0U);

    // A live verdict for the same room renders it: the gate is the flag, not the room.
    entityManager.GetComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(interior)
        .live = true;
    MeshVisibilitySetUVE awake;
    awake.cameraWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, awake);
    EXPECT_EQ(awake.candidates.size(), 2U) << "viewer stepped in: everything draws again";
    EXPECT_EQ(awake.regionCulledEntities, 0U);
}

} // namespace
} // namespace UVE::Render::Tests
