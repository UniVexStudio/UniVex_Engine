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
#include "uve/entity/entity_manager_uve.h"
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

    void RegisterImmediateLoadersUVE() {
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [this](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
                mesh.localBounds = Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, meshHalfExtents);
                return true;
            });
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [this](const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.isTransparent = materialIsTransparent;
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
    EXPECT_FLOAT_EQ(draw.projectionDirection.y, -1.0F);

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
        EXPECT_EQ(vertex.tangent, draw.projectionDirection);
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
        EXPECT_NEAR(std::fabs(draw.projectionDirection.x) + std::fabs(draw.projectionDirection.y), 1.0F, 1.0e-4F);
        if (std::fabs(draw.projectionDirection.x) < 0.99F) {
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

} // namespace
} // namespace UVE::Render::Tests
