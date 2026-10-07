// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/decal_renderer_uve.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
#include "uve/component/mesh_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/occluder_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/render_systems/decal_draw_command_uve.h"
#include "uve/render_systems/decal_draw_data_uve.h"
#include "uve/render_systems/mesh_renderer_uve.h"
#include "uve/render_systems/mesh_visibility_set_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterationsUVE = 200000;

class DecalRendererUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    MeshRendererUVE meshRenderer;
    DecalRendererUVE decalRenderer;
    MeshVisibilitySetUVE visibilitySet;
    DecalDrawListUVE drawList;

    static constexpr const char* kDecalMaterialPathUVE = "materials/decal_renderer_tests_scorch.uvmat";
    static constexpr std::uint32_t kAllLayersUVE = 0xFFFFFFFFU;

    /// The receiving mesh's local half extents, chosen per test: a flat wall is a thin slab rather
    /// than a cube, which is what makes a patch's extent mean something.
    Math::Vector3UVE meshHalfExtents{0.5F, 0.5F, 0.5F};
    bool materialIsTransparent = false;
    Math::Vector3UVE materialAlbedo{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE materialEmissive{0.0F, 0.0F, 0.0F};

    void RegisterImmediateLoadersUVE() {
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [this](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
                mesh.localBounds = Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, meshHalfExtents);
                return true;
            });
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [this](const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.isTransparent = materialIsTransparent;
                material.albedoColor = materialAlbedo;
                material.emissiveColor = materialEmissive;
                return true;
            });
    }

    void WaitUntilAssetsReadyUVE(const std::vector<Asset::AssetGuidUVE>& materialGuids,
                                 const std::vector<Asset::AssetGuidUVE>& meshGuids = {}) {
        std::vector<Asset::AssetHandleUVE<Asset::MaterialAssetUVE>> materialHandles;
        std::vector<Asset::AssetHandleUVE<Asset::MeshAssetUVE>> meshHandles;
        for (const Asset::AssetGuidUVE guid : materialGuids) {
            materialHandles.push_back(assetManager.LoadUVE<Asset::MaterialAssetUVE>(guid, assetDatabase));
        }
        for (const Asset::AssetGuidUVE guid : meshGuids) {
            meshHandles.push_back(assetManager.LoadUVE<Asset::MeshAssetUVE>(guid, assetDatabase));
        }
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            bool ready = true;
            for (const auto& handle : materialHandles) {
                ready = ready && handle.IsReadyUVE();
            }
            for (const auto& handle : meshHandles) {
                ready = ready && handle.IsReadyUVE();
            }
            if (ready) {
                break;
            }
            std::this_thread::yield();
        }
        for (const auto& handle : materialHandles) {
            ASSERT_TRUE(handle.IsReadyUVE());
        }
        for (const auto& handle : meshHandles) {
            ASSERT_TRUE(handle.IsReadyUVE());
        }
    }

    /// A receiver entity at `position` with the frame's shared mesh half extents.
    Scene::EntityUVE MakeReceiverUVE(const Math::Vector3UVE& position, const Asset::AssetGuidUVE meshGuid,
                                     const Asset::AssetGuidUVE materialGuid,
                                     const std::uint32_t renderLayers = 1U) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entityManager.AddComponentUVE<Scene::MeshComponentUVE>(entity, Scene::MeshComponentUVE{meshGuid, materialGuid});
        entityManager.AddComponentUVE<Scene::RenderInstanceComponentUVE>(
            entity, Scene::RenderInstanceComponentUVE{renderLayers});
        return entity;
    }

    Scene::EntityUVE MakeDecalUVE(const Math::Vector3UVE& position, const Math::QuaternionUVE& rotation,
                                  Scene::Decal3DComponentUVE decal) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        entityManager.AddComponentUVE<Scene::Decal3DComponentUVE>(entity, decal);
        return entity;
    }

    /// A camera at the origin looking down -Z with a 90-degree fov, matching the mesh renderer
    /// tests' own fixture so both passes cull against the same view.
    [[nodiscard]] static Math::FrustumUVE MakeTestFrustumUVE() {
        const Math::Matrix4x4UVE view = Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(
            Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
        const Math::Matrix4x4UVE projection =
            Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
    }

    void BuildFrameUVE(const Math::Vector3UVE& cameraPosition, const std::uint32_t receiverLayerMask = kAllLayersUVE) {
        sceneGraph.UpdateUVE(entityManager);
        visibilitySet.cameraWorldPosition = cameraPosition;
        meshRenderer.BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
        decalRenderer.BuildDrawListUVE(entityManager, assetManager, assetDatabase, visibilitySet, cameraPosition,
                                       MakeTestFrustumUVE(), receiverLayerMask, drawList);
    }

    [[nodiscard]] static Scene::Decal3DComponentUVE MakeScorchDecalUVE() {
        Scene::Decal3DComponentUVE decal;
        decal.materialAssetPath = kDecalMaterialPathUVE;
        decal.size = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        decal.upperFade = 0.0F;
        decal.lowerFade = 0.0F;
        return decal;
    }

    /// A wall at z = -5, a thin slab 4x4 facing the camera, and a decal 0.2 in front of it.
    struct WallAndDecalUVE final {
        Scene::EntityUVE wall = Scene::kInvalidEntityUVE;
        Scene::EntityUVE decal = Scene::kInvalidEntityUVE;
    };

    /// A plan for whatever the last BuildFrameUVE() extracted.
    [[nodiscard]] DecalDrawPlanUVE MakePlanUVE(
        const std::size_t maximumCommands = kMaximumDecalDrawCommandsUVE,
        const std::size_t maximumVertices = kMaximumDecalVerticesUVE) {
        DecalDrawPlanUVE plan;
        BuildDecalDrawPlanUVE(drawList, plan, maximumCommands, maximumVertices);
        return plan;
    }

    /// A unit quad patch standing in for one receiving face, with the volume's own unit coordinates
    /// at its corners - the shape the projection pass hands the plan.
    [[nodiscard]] static DecalPatchUVE MakeQuadPatchUVE(const float z) {
        constexpr std::array<float, 4U> kX{-0.5F, 0.5F, 0.5F, -0.5F};
        constexpr std::array<float, 4U> kY{-0.5F, -0.5F, 0.5F, 0.5F};
        DecalPatchUVE patch{};
        patch.vertexCount = 4U;
        patch.normal = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        patch.weight = 1.0F;
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            patch.worldPositions[corner] = Math::Vector3UVE{kX[corner], kY[corner], z};
            patch.unitCoords[corner] = Math::Vector2UVE{kX[corner], kY[corner]};
        }
        return patch;
    }

    /// A draw with one quad patch and a unit box volume at the origin, for the plan's own tests -
    /// they are about what the plan does with a list, not about whether the pass builds one.
    [[nodiscard]] DecalDrawUVE MakeSyntheticDrawUVE(const Scene::EntityUVE entity,
                                                    const std::size_t materialIndex) {
        DecalDrawUVE draw{};
        draw.decal = entity;
        draw.materialIndex = materialIndex;
        draw.projection.worldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        draw.projection.worldRotation = Math::QuaternionUVE{};
        draw.projection.inverseWorldRotation = Math::QuaternionUVE{};
        draw.projection.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        draw.projection.projectionDirection = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
        draw.patches.push_back(MakeQuadPatchUVE(0.0F));
        return draw;
    }

    /// Loads one decal material through the real asset manager and hands back the handle a draw list
    /// holds - the plan resolves its colours through exactly this handle.
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> LoadDecalMaterialHandleUVE(const std::string& path) {
        const Asset::AssetGuidUVE guid = assetDatabase.RegisterUVE(path);
        RegisterImmediateLoadersUVE();
        Asset::AssetHandleUVE<Asset::MaterialAssetUVE> handle =
            assetManager.LoadUVE<Asset::MaterialAssetUVE>(guid, assetDatabase);
        WaitUntilAssetsReadyUVE({guid});
        return handle;
    }

    WallAndDecalUVE MakeWallAndDecalUVE(const Math::Vector3UVE& decalOffset = Math::Vector3UVE{}) {
        meshHalfExtents = Math::Vector3UVE{2.0F, 2.0F, 0.05F};
        const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("decal_renderer_tests_wall.uvmodel");
        const Asset::AssetGuidUVE meshMaterialGuid = assetDatabase.RegisterUVE("decal_renderer_tests_wall.uvmat");
        const Asset::AssetGuidUVE decalMaterialGuid = assetDatabase.RegisterUVE(kDecalMaterialPathUVE);
        RegisterImmediateLoadersUVE();
        WallAndDecalUVE result{};
        result.wall = MakeReceiverUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, meshMaterialGuid);
        result.decal =
            MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F} + decalOffset, Math::QuaternionUVE{}, MakeScorchDecalUVE());
        WaitUntilAssetsReadyUVE({meshMaterialGuid, decalMaterialGuid}, {meshGuid});
        return result;
    }
};

TEST_F(DecalRendererUVETest, BuildDrawListUVE_PartitionHiddenDecalIsNotConsidered) {
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    const Scene::EntityUVE partition = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, partition, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::WorldPartition3DComponentUVE>(partition);
    entityManager.AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        scene.decal, Scene::WorldPartition3DMembershipComponentUVE{partition, false});
    BuildFrameUVE(Math::Vector3UVE{});
    EXPECT_EQ(drawList.decalsConsidered, 0U);
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_OccludedDecalIsNotDrawn) {
    static_cast<void>(MakeWallAndDecalUVE());
    const Scene::EntityUVE wall = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE wallTransform;
    wallTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -2.0F};
    sceneGraph.AttachTransformUVE(entityManager, wall, wallTransform);
    Scene::Occluder3DComponentUVE wallOccluder;
    wallOccluder.halfExtents = Math::Vector3UVE{4.0F, 4.0F, 1.0F};
    entityManager.AddComponentUVE<Scene::Occluder3DComponentUVE>(wall, wallOccluder);
    BuildFrameUVE(Math::Vector3UVE{});
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_RegionHiddenDecalIsNotConsidered) {
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    const Scene::EntityUVE region = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, region, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DComponentUVE>(region);
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        scene.decal, Scene::VisibilityRegion3DMembershipComponentUVE{region, false});
    BuildFrameUVE(Math::Vector3UVE{});
    EXPECT_EQ(drawList.decalsConsidered, 0U);
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ADecalOnAFlatWallProducesAPatchWithUnitCoordinates) {
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    BuildFrameUVE(Math::Vector3UVE{});

    ASSERT_EQ(drawList.draws.size(), 1U);
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsSkippedNotPainting, 0U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 0U);
    const DecalDrawUVE& draw = drawList.draws.front();
    EXPECT_EQ(draw.decal, scene.decal);
    // The decal projects along its local -Y: with no rotation that is straight down, and the patch
    // carries it as the tangent so the shader knows which way the decal is looking.
    EXPECT_FLOAT_EQ(draw.projection.projectionDirection.y, -1.0F);

    ASSERT_EQ(draw.patches.size(), 1U) << "a thin wall crossed once, by its camera-facing face";
    const DecalPatchUVE& patch = draw.patches.front();
    ASSERT_EQ(patch.vertexCount, 4U);
    EXPECT_FLOAT_EQ(patch.normal.z, 1.0F) << "the face turned towards the camera";
    EXPECT_GT(patch.weight, 0.0F);
    EXPECT_TRUE(patch.IsValidUVE());

    for (std::size_t vertexIndex = 0U; vertexIndex < patch.vertexCount; ++vertexIndex) {
        // Half the decal's size in every direction, and the wall's front face 0.15 behind its
        // centre: the unit coordinate is that offset divided by the half extent.
        EXPECT_NEAR(patch.unitCoords[vertexIndex].x, patch.worldPositions[vertexIndex].x * 2.0F, 1.0e-4F);
        EXPECT_GT(patch.unitCoords[vertexIndex].x, -1.0F - 1.0e-4F);
        EXPECT_LT(patch.unitCoords[vertexIndex].x, 1.0F + 1.0e-4F);
        EXPECT_NEAR(patch.worldPositions[vertexIndex].z, -4.95F, 1.0e-4F);
    }
    // The material is referenced once for the frame, and the draw names it by index.
    ASSERT_EQ(drawList.materialHandles.size(), 1U);
    ASSERT_NE(drawList.TryGetMaterialHandleUVE(draw.materialIndex), nullptr);
    EXPECT_TRUE(drawList.TryGetMaterialHandleUVE(draw.materialIndex)->IsReadyUVE());
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ADecalThatCannotReachAReceiverPaintsNothing) {
    // Beside the wall rather than behind it, and still inside the view: this has to fail at the
    // volume test, not at the frustum. The wall spans x in [-2, 2]; the decal's volume starts at
    // x = 2.5.
    MakeWallAndDecalUVE(Math::Vector3UVE{3.0F, 0.0F, 0.0F});
    BuildFrameUVE(Math::Vector3UVE{});

    EXPECT_TRUE(drawList.draws.empty());
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
    EXPECT_EQ(drawList.patchesEmitted, 0U);
    // The receiver was considered and rejected on the cheap test, before any face was clipped.
    EXPECT_EQ(drawList.receiversTested, 1U);
    EXPECT_EQ(drawList.receiversOverlapped, 0U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_LayersDecideWhichReceiversAndViewsSeeTheDecal) {
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    // The wall is on layer 1, and the first decal is pinned to layer 1 rather than left at the
    // default of every layer - a decal that projects onto everything projects onto any view, which
    // would make the second half of this test say nothing.
    entityManager.GetComponentUVE<Scene::Decal3DComponentUVE>(scene.decal).cullMask = 0x1U;
    // A second decal projects only onto layer 2, so it survives the view test below and still finds
    // nothing to paint.
    Scene::Decal3DComponentUVE layerTwo = MakeScorchDecalUVE();
    layerTwo.cullMask = 0x2U;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, layerTwo);

    BuildFrameUVE(Math::Vector3UVE{}, /*receiverLayerMask=*/kAllLayersUVE);
    EXPECT_EQ(drawList.decalsCulledByLayer, 0U) << "the view renders every layer, so neither decal is culled";
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U) << "but the layer-2 decal has no receiver to land on";
    EXPECT_EQ(drawList.receiversTested, 1U) << "the layer-2 decal was dropped on its layers alone";
    EXPECT_EQ(drawList.draws.size(), 1U) << "the layer-1 decal still paints";

    // A view that renders only layer 2 cannot see the layer-1 decal at all, and the layer-2 one it
    // can see still has no receiver on its layer. The mask is the view's, tested against the decal's
    // own cull mask before any volume work happens.
    BuildFrameUVE(Math::Vector3UVE{}, /*receiverLayerMask=*/0x2U);
    EXPECT_EQ(drawList.decalsCulledByLayer, 1U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
    EXPECT_EQ(drawList.draws.size(), 0U);
    EXPECT_EQ(drawList.receiversTested, 0U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_AnUnpaintedOrDegenerateDecalCostsNoReceiverWorkAtAll) {
    MakeWallAndDecalUVE();
    Scene::Decal3DComponentUVE expired = MakeScorchDecalUVE();
    expired.expired = true;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, expired);
    Scene::Decal3DComponentUVE disabled = MakeScorchDecalUVE();
    disabled.enabled = false;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, disabled);
    Scene::Decal3DComponentUVE flat = MakeScorchDecalUVE();
    flat.size = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, flat);

    BuildFrameUVE(Math::Vector3UVE{});

    EXPECT_EQ(drawList.decalsConsidered, 4U);
    EXPECT_EQ(drawList.decalsSkippedNotPainting, 3U) << "expired, disabled, and a volume with no volume";
    EXPECT_EQ(drawList.draws.size(), 1U);
    // One receiver test per painting decal, not one per decal: the gate runs first for a reason.
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_OnlyTheCameraFacingFaceOfAThickReceiverIsPainted) {
    MakeWallAndDecalUVE();
    // A volume deep enough to swallow the whole slab, so both of its Z faces are inside it.
    Scene::Decal3DComponentUVE deep = MakeScorchDecalUVE();
    deep.size = Math::Vector3UVE{2.0F, 4.0F, 4.0F};
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::QuaternionUVE{}, deep);
    BuildFrameUVE(Math::Vector3UVE{});

    // Two decals over one wall: the shallow one paints the front face, the deep one would paint
    // both faces if back faces were not rejected.
    ASSERT_EQ(drawList.draws.size(), 2U);
    std::size_t patchCount = 0U;
    for (const DecalDrawUVE& draw : drawList.draws) {
        for (const DecalPatchUVE& patch : draw.patches) {
            ++patchCount;
            EXPECT_FLOAT_EQ(patch.normal.z, 1.0F) << "only the face turned towards the camera";
        }
    }
    EXPECT_EQ(patchCount, 2U);
    EXPECT_GT(drawList.patchesDiscardedBackFacing, 0U) << "the far face was found and rejected";
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ACylinderDecalIsRoundWhereABoxDecalIsSquare) {
    MakeWallAndDecalUVE();
    Scene::Decal3DComponentUVE cylinder = MakeScorchDecalUVE();
    cylinder.projection = Scene::DecalProjectionModeUVE::Cylinder;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, cylinder);
    BuildFrameUVE(Math::Vector3UVE{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    float boxExtent = 0.0F;
    float cylinderExtent = 0.0F;
    for (const DecalDrawUVE& draw : drawList.draws) {
        ASSERT_EQ(draw.patches.size(), 1U);
        for (std::size_t vertexIndex = 0U; vertexIndex < draw.patches.front().vertexCount; ++vertexIndex) {
            const float extent = std::fabs(draw.patches.front().worldPositions[vertexIndex].x);
            // The patch at z = -4.95 has a unit z of -0.3; the cylinder's wall is x^2 + z^2 <= 1, so
            // its half width is sqrt(1 - 0.09) * 0.5 = 0.477, not the box's 0.5. Which draw is which
            // is decided by that number rather than by extraction order.
            if (extent > 0.49F) {
                boxExtent = std::max(boxExtent, extent);
            } else {
                cylinderExtent = std::max(cylinderExtent, extent);
            }
        }
    }
    EXPECT_NEAR(boxExtent, 0.5F, 1.0e-3F);
    EXPECT_NEAR(cylinderExtent, 0.477F, 2.0e-3F);
    EXPECT_LT(cylinderExtent, boxExtent) << "a round wall cuts the corner a square one keeps";
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_DistanceFadeRemovesADecalTheCameraIsFarFrom) {
    MakeWallAndDecalUVE();
    Scene::Decal3DComponentUVE fading = MakeScorchDecalUVE();
    fading.distanceFadeEnabled = true;
    fading.distanceFadeBegin = 2.0F;
    fading.distanceFadeLength = 1.0F;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, fading);
    // The camera stands 4.8 m from the decal, past the end of a band that starts at 2 and is 1 long.
    BuildFrameUVE(Math::Vector3UVE{}, /*receiverLayerMask=*/kAllLayersUVE);

    EXPECT_EQ(drawList.decalsFadedOut, 1U);
    EXPECT_EQ(drawList.draws.size(), 1U) << "the unfaded decal beside it still paints";
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_FrustumCullingFreezeDrawsAnOffScreenDecal) {
    // The debug freeze reaches the decal pass too. A decal is painted by the same main colour view
    // whose meshes stopped being frustum-rejected, so leaving it culled would make the frozen frame
    // disagree with itself about what is on screen. Occlusion, layer, distance-fade and receiver
    // verdicts are separate systems and keep running in both modes.
    meshHalfExtents = Math::Vector3UVE{2.0F, 2.0F, 0.05F};
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("decal_renderer_tests_freeze_wall.uvmodel");
    const Asset::AssetGuidUVE wallMaterialGuid = assetDatabase.RegisterUVE("decal_renderer_tests_freeze_wall.uvmat");
    const Asset::AssetGuidUVE decalMaterialGuid = assetDatabase.RegisterUVE(kDecalMaterialPathUVE);
    RegisterImmediateLoadersUVE();
    // Both wall and decal sit at x=200 - far outside the origin, -Z, 90-degree test frustum - but
    // the decal still overlaps its own wall, so it has a receiver to paint onto once the frustum
    // stops rejecting it. The wall is a receiver even off-screen: BuildVisibilitySetUVE never
    // frustum-tests candidates, so receivers are the whole placed set, not just the visible part.
    const Math::Vector3UVE offScreen{200.0F, 0.0F, 0.0F};
    MakeReceiverUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F} + offScreen, meshGuid, wallMaterialGuid);
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F} + offScreen, Math::QuaternionUVE{}, MakeScorchDecalUVE());
    WaitUntilAssetsReadyUVE({wallMaterialGuid, decalMaterialGuid}, {meshGuid});

    BuildFrameUVE(Math::Vector3UVE{});
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsOutsideView, 1U) << "culling is on by default";
    EXPECT_EQ(drawList.draws.size(), 0U);

    // Set on the shared visibility set, exactly as the renderer sets it per cull; ClearUVE leaves it
    // alone, so the next BuildFrameUVE keeps it.
    visibilitySet.frustumTestsDisabled = true;
    BuildFrameUVE(Math::Vector3UVE{});
    EXPECT_EQ(drawList.decalsOutsideView, 0U) << "the freeze must stop rejecting the off-screen decal";
    EXPECT_EQ(drawList.draws.size(), 1U) << "and it still paints onto its own off-screen wall";
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_AReceiverOnALayerTheDecalDoesNotProjectOntoIsSkipped) {
    meshHalfExtents = Math::Vector3UVE{2.0F, 2.0F, 0.05F};
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("decal_renderer_tests_layer_wall.uvmodel");
    const Asset::AssetGuidUVE wallMaterialGuid = assetDatabase.RegisterUVE("decal_renderer_tests_layer_wall.uvmat");
    const Asset::AssetGuidUVE decalMaterialGuid = assetDatabase.RegisterUVE(kDecalMaterialPathUVE);
    RegisterImmediateLoadersUVE();
    // The receiver sits on layer 2; the decal projects only onto layer 1. The view renders
    // everything, so the decal survives the view test and is dropped per receiver instead.
    MakeReceiverUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, wallMaterialGuid, /*renderLayers=*/0x2U);
    Scene::Decal3DComponentUVE layerOne = MakeScorchDecalUVE();
    layerOne.cullMask = 0x1U;
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, layerOne);
    WaitUntilAssetsReadyUVE({wallMaterialGuid, decalMaterialGuid}, {meshGuid});

    BuildFrameUVE(Math::Vector3UVE{}, /*receiverLayerMask=*/kAllLayersUVE);

    ASSERT_EQ(drawList.draws.size(), 0U);
    EXPECT_EQ(drawList.receiversTested, 0U) << "the receiver's own layers were tested before the volume";
    EXPECT_EQ(drawList.receiversOverlapped, 0U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_TwoDecalsSharingAMaterialHoldOneReferenceAndSortBackToFront) {
    MakeWallAndDecalUVE();
    // A second decal of the same material, further along the same wall, so both land on it and the
    // two are still separable by depth.
    Scene::Decal3DComponentUVE beside = MakeScorchDecalUVE();
    beside.size = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    MakeDecalUVE(Math::Vector3UVE{1.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, beside);
    BuildFrameUVE(Math::Vector3UVE{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    EXPECT_EQ(drawList.materialHandles.size(), 1U) << "one reference per distinct material, not per decal";
    // Back to front: the decal whose patches are nearest the camera comes last, so blending paints
    // it over the one behind it.
    EXPECT_GT(drawList.draws.front().sortDepth, drawList.draws.back().sortDepth)
        << "the farther decal is first in the list, so blending paints the near one over it";
}

TEST_F(DecalRendererUVETest, AppendVertexStreamUVE_PatchesBecomeTheCanonicalMeshVertexLayout) {
    MakeWallAndDecalUVE();
    BuildFrameUVE(Math::Vector3UVE{});
    ASSERT_EQ(drawList.draws.size(), 1U);
    ASSERT_EQ(drawList.GetPatchCountUVE(), 1U);
    EXPECT_EQ(drawList.GetTriangleCountUVE(), 2U) << "a quad is two triangles";

    std::vector<Asset::MeshVertexUVE> vertices;
    std::vector<std::uint32_t> indices;
    drawList.AppendVertexStreamUVE(vertices, indices);

    EXPECT_EQ(vertices.size(), 4U);
    EXPECT_EQ(indices.size(), 6U);
    EXPECT_EQ(drawList.GetTriangleCountUVE(), indices.size() / 3U);
    const DecalDrawUVE& draw = drawList.draws.front();
    for (std::size_t vertexIndex = 0U; vertexIndex < vertices.size(); ++vertexIndex) {
        const Asset::MeshVertexUVE& vertex = vertices[vertexIndex];
        const std::size_t patchVertexIndex = vertexIndex % DecalPatchUVE::kMaximumVerticesUVE;
        // The stream is the patch, verbatim: world positions, the receiving face's normal as the
        // shading normal, the unit coordinates remapped from [-1, 1] into the [0, 1] a texture is
        // sampled with, and the projection direction as the tangent.
        EXPECT_EQ(vertex.position, draw.patches.front().worldPositions[patchVertexIndex]);
        EXPECT_EQ(vertex.normal, draw.patches.front().normal);
        EXPECT_NEAR(vertex.u, draw.patches.front().unitCoords[patchVertexIndex].x * 0.5F + 0.5F, 1.0e-6F);
        EXPECT_NEAR(vertex.v, draw.patches.front().unitCoords[patchVertexIndex].y * 0.5F + 0.5F, 1.0e-6F);
        EXPECT_EQ(vertex.tangent, draw.projection.projectionDirection);
        EXPECT_FLOAT_EQ(vertex.tangentHandedness, 1.0F);
        EXPECT_LT(indices[vertexIndex], vertices.size());
    }
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ADecalRotatedAQuarterTurnProjectsSideways) {
    MakeWallAndDecalUVE();
    // A quarter turn about Z maps the decal's local -Y onto world +X, so the volume stops standing
    // proud of the wall and lies along it: half a metre across, four metres tall, one metre deep.
    // The patch proves the volume turned with the object rather than staying world-axis-aligned.
    const Math::QuaternionUVE quarterTurnAboutZ{0.0F, 0.0F, 0.70710678F, 0.70710678F};
    Scene::Decal3DComponentUVE rotated = MakeScorchDecalUVE();
    rotated.size = Math::Vector3UVE{4.0F, 0.4F, 1.0F};
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, quarterTurnAboutZ, rotated);
    BuildFrameUVE(Math::Vector3UVE{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    bool sawSidewaysProjection = false;
    for (const DecalDrawUVE& draw : drawList.draws) {
        EXPECT_NEAR(std::fabs(draw.projection.projectionDirection.x) + std::fabs(draw.projection.projectionDirection.y), 1.0F, 1.0e-4F);
        if (std::fabs(draw.projection.projectionDirection.x) < 0.99F) {
            continue;
        }
        sawSidewaysProjection = true;
        ASSERT_FALSE(draw.patches.empty());
        float widestAcrossUVE = 0.0F;
        float tallestUVE = 0.0F;
        for (const DecalPatchUVE& patch : draw.patches) {
            for (std::size_t vertexIndex = 0U; vertexIndex < patch.vertexCount; ++vertexIndex) {
                widestAcrossUVE = std::max(widestAcrossUVE, std::fabs(patch.worldPositions[vertexIndex].x));
                tallestUVE = std::max(tallestUVE, std::fabs(patch.worldPositions[vertexIndex].y));
            }
        }
        EXPECT_LE(widestAcrossUVE, 0.25F) << "half of the 0.4 m it is wide, plus rounding";
        EXPECT_GT(tallestUVE, 1.5F) << "and it reaches far up the wall, because that is its long axis";
    }
    EXPECT_TRUE(sawSidewaysProjection) << "the rotated decal projects along world +X, not down -Y";
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ADecalWhoseMaterialIsNotRegisteredPaintsNothing) {
    MakeWallAndDecalUVE();
    Scene::Decal3DComponentUVE missing = MakeScorchDecalUVE();
    missing.materialAssetPath = "materials/decal_renderer_tests_not_registered.uvmat";
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, missing);
    BuildFrameUVE(Math::Vector3UVE{});

    EXPECT_EQ(drawList.decalsWithoutMaterial, 1U);
    EXPECT_EQ(drawList.draws.size(), 1U) << "the decal with a real material still paints";
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_ADecalWithNoMaterialPathPaintsNothing) {
    MakeWallAndDecalUVE();
    Scene::Decal3DComponentUVE unassigned = MakeScorchDecalUVE();
    unassigned.materialAssetPath.clear();
    MakeDecalUVE(Math::Vector3UVE{0.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, unassigned);
    BuildFrameUVE(Math::Vector3UVE{});

    EXPECT_EQ(drawList.decalsWithoutMaterial, 1U);
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererUVETest, BuildDrawListUVE_RebuildingTheFrameReplacesTheLastOneRatherThanAppending) {
    MakeWallAndDecalUVE();
    BuildFrameUVE(Math::Vector3UVE{});
    const std::size_t firstPatchCount = drawList.GetPatchCountUVE();
    ASSERT_GT(firstPatchCount, 0U);

    BuildFrameUVE(Math::Vector3UVE{});

    EXPECT_EQ(drawList.draws.size(), 1U) << "a second frame, not a second decal";
    EXPECT_EQ(drawList.GetPatchCountUVE(), firstPatchCount);
    EXPECT_EQ(drawList.materialHandles.size(), 1U);
    EXPECT_EQ(drawList.decalsConsidered, 1U) << "the counters describe this frame alone";
}

// ---------------------------------------------------------------------------
// The draw plan: the step between "the pass found geometry" and "the GPU is handed it".
// ---------------------------------------------------------------------------

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_WorldToUnitIsTheSpaceTheCpuClippedIn) {
    // The plan hands the fragment program one matrix instead of re-deriving the volume's frame in
    // GLSL, so this is the invariant that keeps the two halves of the decal pass in one coordinate
    // space: the matrix must map a world point to exactly what the CPU sampler's own transform
    // gives for the same point. Asserted at the patch's own vertices, which are the points the
    // clipping arithmetic actually produced.
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    BuildFrameUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_EQ(drawList.draws.size(), 1U);

    const DecalDrawPlanUVE plan = MakePlanUVE();
    ASSERT_EQ(plan.commands.size(), 1U);
    EXPECT_EQ(plan.commands.front().decal, scene.decal);

    const Scene::Decal3DProjectionUVE& projection = drawList.draws.front().projection;
    std::size_t checked = 0U;
    for (const DecalPatchUVE& patch : drawList.draws.front().patches) {
        for (std::size_t vertexIndex = 0U; vertexIndex < patch.vertexCount; ++vertexIndex) {
            const Math::Vector3UVE world = patch.worldPositions[vertexIndex];
            const Math::Vector3UVE viaMatrix = Math::TransformPointUVE(plan.commands.front().worldToUnit, world);
            const Math::Vector3UVE viaSampler = Scene::Decal3DWorldToUnitUVE(projection, world);
            EXPECT_NEAR(viaMatrix.x, viaSampler.x, 1.0e-4F);
            EXPECT_NEAR(viaMatrix.y, viaSampler.y, 1.0e-4F);
            EXPECT_NEAR(viaMatrix.z, viaSampler.z, 1.0e-4F);
            ++checked;
        }
    }
    EXPECT_GE(checked, 3U) << "a plan with no patch vertices would pass this test without testing it";
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_CarriesTheAuthoredLookIntoTheDrawUniforms) {
    // Every uniform the fragment program reads has to come from somewhere the author can reach:
    // the material's albedo and emissive, and the decal's own modulate/emissionEnergy/albedoMix and
    // fades. This pins the wiring between the two, which is the difference between an authored
    // field and a field that does nothing.
    materialAlbedo = Math::Vector3UVE{0.8F, 0.1F, 0.1F};
    materialEmissive = Math::Vector3UVE{0.2F, 0.0F, 0.0F};
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();

    Scene::Decal3DComponentUVE& authored = entityManager.GetComponentUVE<Scene::Decal3DComponentUVE>(scene.decal);
    authored.modulate = Math::Vector3UVE{0.5F, 2.0F, 1.0F};
    authored.emissionEnergy = 3.0F;
    authored.albedoMix = 0.25F;
    authored.normalFade = 0.75F;
    authored.upperFade = 0.4F;
    authored.lowerFade = 0.2F;
    authored.distanceFadeEnabled = true;
    authored.distanceFadeBegin = 12.0F;
    authored.distanceFadeLength = 8.0F;

    BuildFrameUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const DecalDrawPlanUVE plan = MakePlanUVE();
    ASSERT_EQ(plan.commands.size(), 1U);

    const DecalDrawCommandUVE& command = plan.commands.front();
    EXPECT_EQ(command.decal, scene.decal);
    EXPECT_FLOAT_EQ(command.baseColor.x, 0.8F * 0.5F);
    EXPECT_FLOAT_EQ(command.baseColor.y, 0.1F * 2.0F);
    EXPECT_FLOAT_EQ(command.baseColor.z, 0.1F * 1.0F);
    EXPECT_FLOAT_EQ(command.emissionColor.x, 0.2F * 3.0F);
    EXPECT_FLOAT_EQ(command.alphaScale, 0.25F);
    EXPECT_FLOAT_EQ(command.normalFade, 0.75F);
    EXPECT_FLOAT_EQ(command.upperFade, 0.4F);
    EXPECT_FLOAT_EQ(command.lowerFade, 0.2F);
    EXPECT_TRUE(command.distanceFadeEnabled);
    EXPECT_FLOAT_EQ(command.distanceFadeBegin, 12.0F);
    EXPECT_FLOAT_EQ(command.distanceFadeLength, 8.0F);
    EXPECT_FLOAT_EQ(command.projectionDirection.y, -1.0F);
    EXPECT_EQ(command.albedoTextureGuid, Asset::kInvalidAssetGuidUVE)
        << "this material leaves its texture unset, and the decal must say so rather than invent one";
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_EveryCommandWindowStaysInsideTheStreamsItPointsAt) {
    // The plan's streams are shared by every decal in the frame, and each command names its own
    // window into them. A window that overruns the stream - or an index that reaches outside its
    // command's own vertices - would draw one decal with another decal's geometry, which is the
    // kind of bug that shows up as a smear rather than as a crash.
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    BuildFrameUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const DecalDrawPlanUVE plan = MakePlanUVE();
    ASSERT_FALSE(plan.commands.empty());
    EXPECT_EQ(plan.commands.front().decal, scene.decal);

    for (const DecalDrawCommandUVE& command : plan.commands) {
        const std::size_t firstVertex = command.firstVertex;
        const std::size_t lastVertex = firstVertex + command.vertexCount;
        const std::size_t firstIndex = command.firstIndex;
        const std::size_t lastIndex = firstIndex + command.indexCount;
        EXPECT_LE(lastVertex, plan.vertices.size());
        EXPECT_LE(lastIndex, plan.indices.size());
        EXPECT_GE(command.vertexCount, 3U);
        EXPECT_GE(command.indexCount, 3U);
        for (std::size_t index = firstIndex; index < lastIndex; ++index) {
            EXPECT_GE(plan.indices[index], firstVertex);
            EXPECT_LT(plan.indices[index], lastVertex);
        }
    }
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_KeepsThePassesBackToFrontOrder) {
    // Blending order is the whole reason the list is sorted: a decal nearer the camera must be
    // recorded after the one behind it, and the plan is the last place that order can be lost.
    const WallAndDecalUVE scene = MakeWallAndDecalUVE();
    // A second decal of the same material beside the first, both landing on the same wall: the pass
    // sorts them back to front, and the plan must not be the step that loses that order.
    Scene::Decal3DComponentUVE beside = MakeScorchDecalUVE();
    beside.size = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    const Scene::EntityUVE second =
        MakeDecalUVE(Math::Vector3UVE{1.0F, 0.0F, -4.8F}, Math::QuaternionUVE{}, beside);

    BuildFrameUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    ASSERT_EQ(drawList.draws.size(), 2U);
    const DecalDrawPlanUVE plan = MakePlanUVE();
    ASSERT_EQ(plan.commands.size(), 2U);
    EXPECT_EQ(plan.commands[0].decal, drawList.draws[0].decal);
    EXPECT_EQ(plan.commands[1].decal, drawList.draws[1].decal);
    EXPECT_NE(plan.commands[0].decal, plan.commands[1].decal) << "two draws, not one recorded twice";
    EXPECT_TRUE((plan.commands[0].decal == scene.decal && plan.commands[1].decal == second) ||
                (plan.commands[0].decal == second && plan.commands[1].decal == scene.decal))
        << "the plan's draws are the frame's decals, and nothing else";
    EXPECT_NE(plan.commands[1].worldToUnit.m[0][3], plan.commands[0].worldToUnit.m[0][3])
        << "a command must carry its own volume, not the previous decal's";
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_ADrawWhoseMaterialIsGoneIsCountedAndTheRestStillDraw) {
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE> material =
        LoadDecalMaterialHandleUVE(kDecalMaterialPathUVE);
    DecalDrawListUVE synthetic{};
    synthetic.materialHandles.push_back(material);
    const Scene::EntityUVE painted = entityManager.CreateEntityUVE();
    synthetic.draws.push_back(MakeSyntheticDrawUVE(painted, 0U));
    synthetic.draws.push_back(MakeSyntheticDrawUVE(entityManager.CreateEntityUVE(), 7U));

    DecalDrawPlanUVE plan;
    BuildDecalDrawPlanUVE(synthetic, plan);
    ASSERT_EQ(plan.commands.size(), 1U);
    EXPECT_EQ(plan.commands.front().decal, painted);
    EXPECT_EQ(plan.drawsWithoutMaterial, 1U);
    EXPECT_EQ(plan.indices.size(), 6U) << "one quad, and nothing left behind by the draw that was "
                                          "refused for naming a material the list does not hold";
    EXPECT_EQ(plan.vertices.size(), 4U);
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_ADecalThatReplacesNothingPaintsNothing) {
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE> material =
        LoadDecalMaterialHandleUVE(kDecalMaterialPathUVE);
    DecalDrawListUVE synthetic{};
    synthetic.materialHandles.push_back(material);
    DecalDrawUVE draw = MakeSyntheticDrawUVE(entityManager.CreateEntityUVE(), 0U);
    draw.albedoMix = 0.0F;
    synthetic.draws.push_back(draw);

    DecalDrawPlanUVE plan;
    BuildDecalDrawPlanUVE(synthetic, plan);
    EXPECT_TRUE(plan.commands.empty());
    EXPECT_EQ(plan.drawsWithoutPaint, 1U);
    EXPECT_TRUE(plan.vertices.empty());
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_TheCapsRefuseAWholeDrawRatherThanHalfOfOne) {
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE> material =
        LoadDecalMaterialHandleUVE(kDecalMaterialPathUVE);
    DecalDrawListUVE synthetic{};
    synthetic.materialHandles.push_back(material);
    synthetic.draws.push_back(MakeSyntheticDrawUVE(entityManager.CreateEntityUVE(), 0U));
    synthetic.draws.push_back(MakeSyntheticDrawUVE(entityManager.CreateEntityUVE(), 0U));

    // The command cap: one draw records, the second is counted rather than half-appended.
    DecalDrawPlanUVE byCommand;
    BuildDecalDrawPlanUVE(synthetic, byCommand, 1U, kMaximumDecalVerticesUVE);
    EXPECT_EQ(byCommand.commands.size(), 1U);
    EXPECT_EQ(byCommand.drawsTruncated, 1U);
    EXPECT_EQ(byCommand.vertices.size(), 4U);
    EXPECT_EQ(byCommand.indices.size(), 6U);

    // The vertex cap: a draw whose geometry does not fit is refused whole, so the stream never
    // holds vertices no command points at.
    DecalDrawPlanUVE byVertex;
    BuildDecalDrawPlanUVE(synthetic, byVertex, kMaximumDecalDrawCommandsUVE, 6U);
    EXPECT_EQ(byVertex.commands.size(), 1U);
    EXPECT_EQ(byVertex.drawsTruncated, 1U);
    EXPECT_EQ(byVertex.vertices.size(), 4U);
}

TEST_F(DecalRendererUVETest, BuildDecalDrawPlanUVE_RebuildingAFrameReplacesTheLastOne) {
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE> material =
        LoadDecalMaterialHandleUVE(kDecalMaterialPathUVE);
    DecalDrawListUVE synthetic{};
    synthetic.materialHandles.push_back(material);
    synthetic.draws.push_back(MakeSyntheticDrawUVE(entityManager.CreateEntityUVE(), 0U));

    DecalDrawPlanUVE plan;
    BuildDecalDrawPlanUVE(synthetic, plan);
    BuildDecalDrawPlanUVE(synthetic, plan);
    EXPECT_EQ(plan.commands.size(), 1U) << "an existing plan must be cleared, not appended to";
    EXPECT_EQ(plan.vertices.size(), 4U);
    EXPECT_EQ(plan.indices.size(), 6U);
}

} // namespace
} // namespace UVE::Render::Tests
