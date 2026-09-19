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
#include "uve/component/world_transform_component_uve.h"
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
        EXPECT_TRUE(candidate.meshHandle.IsReadyUVE());
        EXPECT_TRUE(candidate.materialHandle.IsReadyUVE());
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
        EXPECT_EQ(candidate.meshHandle.GetGuidUVE(), meshGuid);
        EXPECT_EQ(candidate.materialHandle.GetGuidUVE(), materialGuid);
        EXPECT_TRUE(candidate.meshHandle.IsReadyUVE()) << "a shared resolution must not be consumed by one entity";
        EXPECT_TRUE(candidate.materialHandle.IsReadyUVE());
        EXPECT_NE(candidate.meshHandle.TryGetUVE(), nullptr);
        EXPECT_NE(candidate.materialHandle.TryGetUVE(), nullptr);
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
        seenMeshGuids.push_back(candidate.meshHandle.GetGuidUVE());
    }
    for (const Asset::AssetGuidUVE expected : meshGuids) {
        EXPECT_NE(std::find(seenMeshGuids.cbegin(), seenMeshGuids.cend(), expected), seenMeshGuids.cend())
            << "every distinct mesh GUID must be resolved on its own, not collapsed into a neighbour's";
    }
}

} // namespace
} // namespace UVE::Render::Tests
