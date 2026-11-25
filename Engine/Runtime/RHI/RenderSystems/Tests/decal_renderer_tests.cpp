// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "decal_renderer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
#include "mesh_component.h"
#include "render_instance_component.h"
#include "world_transform_component.h"
#include "event_system.h"
#include "memory_manager.h"
#include "decal_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"
#include "entity_manager.h"
#include "decal_draw_command.h"
#include "decal_draw_data.h"
#include "mesh_renderer.h"
#include "mesh_visibility_set.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterations = 200000;

class DecalRendererTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Threading::ThreadPool threadPool{2};
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};
    MeshRenderer meshRenderer;
    DecalRenderer decalRenderer;
    MeshVisibilitySet visibilitySet;
    DecalDrawList drawList;

    static constexpr const char* kDecalMaterialPath = "materials/decal_renderer_tests_scorch.uvmat";
    static constexpr std::uint32_t kAllLayers = 0xFFFFFFFFU;

    /// The receiving mesh's local half extents, chosen per test: a flat wall is a thin slab rather
    /// than a cube, which is what makes a patch's extent mean something.
    Math::Vector3 meshHalfExtents{0.5F, 0.5F, 0.5F};
    bool materialIsTransparent = false;
    Math::Color materialAlbedo{1.0F, 1.0F, 1.0F};
    Math::Color materialEmissive{0.0F, 0.0F, 0.0F};

    void RegisterImmediateLoaders() {
        assetManager.RegisterLoader<Asset::MeshAsset>(
            [this](const std::filesystem::path&, Asset::MeshAsset& mesh) {
                mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{}, meshHalfExtents);
                return true;
            });
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [this](const std::filesystem::path&, Asset::MaterialAsset& material) {
                material.isTransparent = materialIsTransparent;
                material.albedoColor = materialAlbedo;
                material.emissiveColor = materialEmissive;
                return true;
            });
    }

    void WaitUntilAssetsReady(const std::vector<Asset::AssetGuid>& materialGuids,
                                 const std::vector<Asset::AssetGuid>& meshGuids = {}) {
        std::vector<Asset::AssetHandle<Asset::MaterialAsset>> materialHandles;
        std::vector<Asset::AssetHandle<Asset::MeshAsset>> meshHandles;
        for (const Asset::AssetGuid guid : materialGuids) {
            materialHandles.push_back(assetManager.Load<Asset::MaterialAsset>(guid, assetDatabase));
        }
        for (const Asset::AssetGuid guid : meshGuids) {
            meshHandles.push_back(assetManager.Load<Asset::MeshAsset>(guid, assetDatabase));
        }
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            bool ready = true;
            for (const auto& handle : materialHandles) {
                ready = ready && handle.IsReady();
            }
            for (const auto& handle : meshHandles) {
                ready = ready && handle.IsReady();
            }
            if (ready) {
                break;
            }
            std::this_thread::yield();
        }
        for (const auto& handle : materialHandles) {
            ASSERT_TRUE(handle.IsReady());
        }
        for (const auto& handle : meshHandles) {
            ASSERT_TRUE(handle.IsReady());
        }
    }

    /// A receiver entity at `position` with the frame's shared mesh half extents.
    Scene::Entity MakeReceiver(const Math::Vector3& position, const Asset::AssetGuid meshGuid,
                                     const Asset::AssetGuid materialGuid,
                                     const std::uint32_t renderLayers = 1U) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        entityManager.AddComponent<Scene::MeshComponent>(entity, Scene::MeshComponent{meshGuid, materialGuid});
        entityManager.AddComponent<Scene::RenderInstanceComponent>(
            entity, Scene::RenderInstanceComponent{renderLayers});
        return entity;
    }

    Scene::Entity MakeDecal(const Math::Vector3& position, const Math::Quaternion& rotation,
                                  Scene::Decal3DComponent decal) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, local);
        entityManager.AddComponent<Scene::Decal3DComponent>(entity, decal);
        return entity;
    }

    /// A camera at the origin looking down -Z with a 90-degree fov, matching the mesh renderer
    /// tests' own fixture so both passes cull against the same view.
    [[nodiscard]] static Math::Frustum MakeTestFrustum() {
        const Math::Matrix4x4 view = Math::Matrix4x4::ViewFromPositionAndRotation(
            Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
        const Math::Matrix4x4 projection =
            Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
        return Math::Frustum::FromViewProjection(projection * view);
    }

    void BuildFrame(const Math::Vector3& cameraPosition, const std::uint32_t receiverLayerMask = kAllLayers) {
        sceneGraph.Update(entityManager);
        visibilitySet.cameraWorldPosition = cameraPosition;
        meshRenderer.BuildVisibilitySet(entityManager, assetManager, assetDatabase, visibilitySet);
        decalRenderer.BuildDrawList(entityManager, assetManager, assetDatabase, visibilitySet, cameraPosition,
                                       MakeTestFrustum(), receiverLayerMask, drawList);
    }

    [[nodiscard]] static Scene::Decal3DComponent MakeScorchDecal() {
        Scene::Decal3DComponent decal;
        decal.materialAssetPath = kDecalMaterialPath;
        decal.size = Math::Vector3{1.0F, 1.0F, 1.0F};
        decal.upperFade = 0.0F;
        decal.lowerFade = 0.0F;
        return decal;
    }

    /// A wall at z = -5, a thin slab 4x4 facing the camera, and a decal 0.2 in front of it.
    struct WallAndDecal final {
        Scene::Entity wall = Scene::kInvalidEntity;
        Scene::Entity decal = Scene::kInvalidEntity;
    };

    /// A plan for whatever the last BuildFrame() extracted.
    [[nodiscard]] DecalDrawPlan MakePlan(
        const std::size_t maximumCommands = kMaximumDecalDrawCommands,
        const std::size_t maximumVertices = kMaximumDecalVertices) {
        DecalDrawPlan plan;
        BuildDecalDrawPlan(drawList, plan, maximumCommands, maximumVertices);
        return plan;
    }

    /// A unit quad patch standing in for one receiving face, with the volume's own unit coordinates
    /// at its corners - the shape the projection pass hands the plan.
    [[nodiscard]] static DecalPatch MakeQuadPatch(const float z) {
        constexpr std::array<float, 4U> kX{-0.5F, 0.5F, 0.5F, -0.5F};
        constexpr std::array<float, 4U> kY{-0.5F, -0.5F, 0.5F, 0.5F};
        DecalPatch patch{};
        patch.vertexCount = 4U;
        patch.normal = Math::Vector3{0.0F, 0.0F, 1.0F};
        patch.weight = 1.0F;
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            patch.worldPositions[corner] = Math::Vector3{kX[corner], kY[corner], z};
            patch.unitCoords[corner] = Math::Vector2{kX[corner], kY[corner]};
        }
        return patch;
    }

    /// A draw with one quad patch and a unit box volume at the origin, for the plan's own tests -
    /// they are about what the plan does with a list, not about whether the pass builds one.
    [[nodiscard]] DecalDraw MakeSyntheticDraw(const Scene::Entity entity,
                                                    const std::size_t materialIndex) {
        DecalDraw draw{};
        draw.decal = entity;
        draw.materialIndex = materialIndex;
        draw.projection.worldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
        draw.projection.worldRotation = Math::Quaternion{};
        draw.projection.inverseWorldRotation = Math::Quaternion{};
        draw.projection.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
        draw.projection.projectionDirection = Math::Vector3{0.0F, 0.0F, -1.0F};
        draw.patches.push_back(MakeQuadPatch(0.0F));
        return draw;
    }

    /// Loads one decal material through the real asset manager and hands back the handle a draw list
    /// holds - the plan resolves its colours through exactly this handle.
    Asset::AssetHandle<Asset::MaterialAsset> LoadDecalMaterialHandle(const std::string& path) {
        const Asset::AssetGuid guid = assetDatabase.Register(path);
        RegisterImmediateLoaders();
        Asset::AssetHandle<Asset::MaterialAsset> handle =
            assetManager.Load<Asset::MaterialAsset>(guid, assetDatabase);
        WaitUntilAssetsReady({guid});
        return handle;
    }

    WallAndDecal MakeWallAndDecal(const Math::Vector3& decalOffset = Math::Vector3{}) {
        meshHalfExtents = Math::Vector3{2.0F, 2.0F, 0.05F};
        const Asset::AssetGuid meshGuid = assetDatabase.Register("decal_renderer_tests_wall.uvmodel");
        const Asset::AssetGuid meshMaterialGuid = assetDatabase.Register("decal_renderer_tests_wall.uvmat");
        const Asset::AssetGuid decalMaterialGuid = assetDatabase.Register(kDecalMaterialPath);
        RegisterImmediateLoaders();
        WallAndDecal result{};
        result.wall = MakeReceiver(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, meshMaterialGuid);
        result.decal =
            MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F} + decalOffset, Math::Quaternion{}, MakeScorchDecal());
        WaitUntilAssetsReady({meshMaterialGuid, decalMaterialGuid}, {meshGuid});
        return result;
    }
};

TEST_F(DecalRendererTest, BuildDrawList_PartitionHiddenDecalIsNotConsidered) {
    const WallAndDecal scene = MakeWallAndDecal();
    const Scene::Entity partition = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, partition, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::WorldPartition3DComponent>(partition);
    entityManager.AddComponent<Scene::WorldPartition3DMembershipComponent>(
        scene.decal, Scene::WorldPartition3DMembershipComponent{partition, false});
    BuildFrame(Math::Vector3{});
    EXPECT_EQ(drawList.decalsConsidered, 0U);
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererTest, BuildDrawList_OccludedDecalIsNotDrawn) {
    static_cast<void>(MakeWallAndDecal());
    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -2.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{4.0F, 4.0F, 1.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);
    BuildFrame(Math::Vector3{});
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererTest, BuildDrawList_RegionHiddenDecalIsNotConsidered) {
    const WallAndDecal scene = MakeWallAndDecal();
    const Scene::Entity region = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, region, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::VisibilityRegion3DComponent>(region);
    entityManager.AddComponent<Scene::VisibilityRegion3DMembershipComponent>(
        scene.decal, Scene::VisibilityRegion3DMembershipComponent{region, false});
    BuildFrame(Math::Vector3{});
    EXPECT_EQ(drawList.decalsConsidered, 0U);
    EXPECT_TRUE(drawList.draws.empty());
}

TEST_F(DecalRendererTest, BuildDrawList_ADecalOnAFlatWallProducesAPatchWithUnitCoordinates) {
    const WallAndDecal scene = MakeWallAndDecal();
    BuildFrame(Math::Vector3{});

    ASSERT_EQ(drawList.draws.size(), 1U);
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsSkippedNotPainting, 0U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 0U);
    const DecalDraw& draw = drawList.draws.front();
    EXPECT_EQ(draw.decal, scene.decal);
    // The decal projects along its local -Y: with no rotation that is straight down, and the patch
    // carries it as the tangent so the shader knows which way the decal is looking.
    EXPECT_FLOAT_EQ(draw.projection.projectionDirection.y, -1.0F);

    ASSERT_EQ(draw.patches.size(), 1U) << "a thin wall crossed once, by its camera-facing face";
    const DecalPatch& patch = draw.patches.front();
    ASSERT_EQ(patch.vertexCount, 4U);
    EXPECT_FLOAT_EQ(patch.normal.z, 1.0F) << "the face turned towards the camera";
    EXPECT_GT(patch.weight, 0.0F);
    EXPECT_TRUE(patch.IsValid());

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
    ASSERT_NE(drawList.TryGetMaterialHandle(draw.materialIndex), nullptr);
    EXPECT_TRUE(drawList.TryGetMaterialHandle(draw.materialIndex)->IsReady());
}

TEST_F(DecalRendererTest, BuildDrawList_ADecalThatCannotReachAReceiverPaintsNothing) {
    // Beside the wall rather than behind it, and still inside the view: this has to fail at the
    // volume test, not at the frustum. The wall spans x in [-2, 2]; the decal's volume starts at
    // x = 2.5.
    MakeWallAndDecal(Math::Vector3{3.0F, 0.0F, 0.0F});
    BuildFrame(Math::Vector3{});

    EXPECT_TRUE(drawList.draws.empty());
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
    EXPECT_EQ(drawList.patchesEmitted, 0U);
    // The receiver was considered and rejected on the cheap test, before any face was clipped.
    EXPECT_EQ(drawList.receiversTested, 1U);
    EXPECT_EQ(drawList.receiversOverlapped, 0U);
}

TEST_F(DecalRendererTest, BuildDrawList_LayersDecideWhichReceiversAndViewsSeeTheDecal) {
    const WallAndDecal scene = MakeWallAndDecal();
    // The wall is on layer 1, and the first decal is pinned to layer 1 rather than left at the
    // default of every layer - a decal that projects onto everything projects onto any view, which
    // would make the second half of this test say nothing.
    entityManager.GetComponent<Scene::Decal3DComponent>(scene.decal).cullMask = 0x1U;
    // A second decal projects only onto layer 2, so it survives the view test below and still finds
    // nothing to paint.
    Scene::Decal3DComponent layerTwo = MakeScorchDecal();
    layerTwo.cullMask = 0x2U;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, layerTwo);

    BuildFrame(Math::Vector3{}, /*receiverLayerMask=*/kAllLayers);
    EXPECT_EQ(drawList.decalsCulledByLayer, 0U) << "the view renders every layer, so neither decal is culled";
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U) << "but the layer-2 decal has no receiver to land on";
    EXPECT_EQ(drawList.receiversTested, 1U) << "the layer-2 decal was dropped on its layers alone";
    EXPECT_EQ(drawList.draws.size(), 1U) << "the layer-1 decal still paints";

    // A view that renders only layer 2 cannot see the layer-1 decal at all, and the layer-2 one it
    // can see still has no receiver on its layer. The mask is the view's, tested against the decal's
    // own cull mask before any volume work happens.
    BuildFrame(Math::Vector3{}, /*receiverLayerMask=*/0x2U);
    EXPECT_EQ(drawList.decalsCulledByLayer, 1U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
    EXPECT_EQ(drawList.draws.size(), 0U);
    EXPECT_EQ(drawList.receiversTested, 0U);
}

TEST_F(DecalRendererTest, BuildDrawList_AnUnpaintedOrDegenerateDecalCostsNoReceiverWorkAtAll) {
    MakeWallAndDecal();
    Scene::Decal3DComponent expired = MakeScorchDecal();
    expired.expired = true;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, expired);
    Scene::Decal3DComponent disabled = MakeScorchDecal();
    disabled.enabled = false;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, disabled);
    Scene::Decal3DComponent flat = MakeScorchDecal();
    flat.size = Math::Vector3{0.0F, 1.0F, 1.0F};
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, flat);

    BuildFrame(Math::Vector3{});

    EXPECT_EQ(drawList.decalsConsidered, 4U);
    EXPECT_EQ(drawList.decalsSkippedNotPainting, 3U) << "expired, disabled, and a volume with no volume";
    EXPECT_EQ(drawList.draws.size(), 1U);
    // One receiver test per painting decal, not one per decal: the gate runs first for a reason.
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererTest, BuildDrawList_OnlyTheCameraFacingFaceOfAThickReceiverIsPainted) {
    MakeWallAndDecal();
    // A volume deep enough to swallow the whole slab, so both of its Z faces are inside it.
    Scene::Decal3DComponent deep = MakeScorchDecal();
    deep.size = Math::Vector3{2.0F, 4.0F, 4.0F};
    MakeDecal(Math::Vector3{0.0F, 0.0F, -5.0F}, Math::Quaternion{}, deep);
    BuildFrame(Math::Vector3{});

    // Two decals over one wall: the shallow one paints the front face, the deep one would paint
    // both faces if back faces were not rejected.
    ASSERT_EQ(drawList.draws.size(), 2U);
    std::size_t patchCount = 0U;
    for (const DecalDraw& draw : drawList.draws) {
        for (const DecalPatch& patch : draw.patches) {
            ++patchCount;
            EXPECT_FLOAT_EQ(patch.normal.z, 1.0F) << "only the face turned towards the camera";
        }
    }
    EXPECT_EQ(patchCount, 2U);
    EXPECT_GT(drawList.patchesDiscardedBackFacing, 0U) << "the far face was found and rejected";
}

TEST_F(DecalRendererTest, BuildDrawList_ACylinderDecalIsRoundWhereABoxDecalIsSquare) {
    MakeWallAndDecal();
    Scene::Decal3DComponent cylinder = MakeScorchDecal();
    cylinder.projection = Scene::DecalProjectionMode::Cylinder;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, cylinder);
    BuildFrame(Math::Vector3{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    float boxExtent = 0.0F;
    float cylinderExtent = 0.0F;
    for (const DecalDraw& draw : drawList.draws) {
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

TEST_F(DecalRendererTest, BuildDrawList_DistanceFadeRemovesADecalTheCameraIsFarFrom) {
    MakeWallAndDecal();
    Scene::Decal3DComponent fading = MakeScorchDecal();
    fading.distanceFadeEnabled = true;
    fading.distanceFadeBegin = 2.0F;
    fading.distanceFadeLength = 1.0F;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, fading);
    // The camera stands 4.8 m from the decal, past the end of a band that starts at 2 and is 1 long.
    BuildFrame(Math::Vector3{}, /*receiverLayerMask=*/kAllLayers);

    EXPECT_EQ(drawList.decalsFadedOut, 1U);
    EXPECT_EQ(drawList.draws.size(), 1U) << "the unfaded decal beside it still paints";
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererTest, BuildDrawList_FrustumCullingFreezeDrawsAnOffScreenDecal) {
    // The debug freeze reaches the decal pass too. A decal is painted by the same main colour view
    // whose meshes stopped being frustum-rejected, so leaving it culled would make the frozen frame
    // disagree with itself about what is on screen. Occlusion, layer, distance-fade and receiver
    // verdicts are separate systems and keep running in both modes.
    meshHalfExtents = Math::Vector3{2.0F, 2.0F, 0.05F};
    const Asset::AssetGuid meshGuid = assetDatabase.Register("decal_renderer_tests_freeze_wall.uvmodel");
    const Asset::AssetGuid wallMaterialGuid = assetDatabase.Register("decal_renderer_tests_freeze_wall.uvmat");
    const Asset::AssetGuid decalMaterialGuid = assetDatabase.Register(kDecalMaterialPath);
    RegisterImmediateLoaders();
    // Both wall and decal sit at x=200 - far outside the origin, -Z, 90-degree test frustum - but
    // the decal still overlaps its own wall, so it has a receiver to paint onto once the frustum
    // stops rejecting it. The wall is a receiver even off-screen: BuildVisibilitySet never
    // frustum-tests candidates, so receivers are the whole placed set, not just the visible part.
    const Math::Vector3 offScreen{200.0F, 0.0F, 0.0F};
    MakeReceiver(Math::Vector3{0.0F, 0.0F, -5.0F} + offScreen, meshGuid, wallMaterialGuid);
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F} + offScreen, Math::Quaternion{}, MakeScorchDecal());
    WaitUntilAssetsReady({wallMaterialGuid, decalMaterialGuid}, {meshGuid});

    BuildFrame(Math::Vector3{});
    EXPECT_EQ(drawList.decalsConsidered, 1U);
    EXPECT_EQ(drawList.decalsOutsideView, 1U) << "culling is on by default";
    EXPECT_EQ(drawList.draws.size(), 0U);

    // Set on the shared visibility set, exactly as the renderer sets it per cull; Clear leaves it
    // alone, so the next BuildFrame keeps it.
    visibilitySet.frustumTestsDisabled = true;
    BuildFrame(Math::Vector3{});
    EXPECT_EQ(drawList.decalsOutsideView, 0U) << "the freeze must stop rejecting the off-screen decal";
    EXPECT_EQ(drawList.draws.size(), 1U) << "and it still paints onto its own off-screen wall";
}

TEST_F(DecalRendererTest, BuildDrawList_AReceiverOnALayerTheDecalDoesNotProjectOntoIsSkipped) {
    meshHalfExtents = Math::Vector3{2.0F, 2.0F, 0.05F};
    const Asset::AssetGuid meshGuid = assetDatabase.Register("decal_renderer_tests_layer_wall.uvmodel");
    const Asset::AssetGuid wallMaterialGuid = assetDatabase.Register("decal_renderer_tests_layer_wall.uvmat");
    const Asset::AssetGuid decalMaterialGuid = assetDatabase.Register(kDecalMaterialPath);
    RegisterImmediateLoaders();
    // The receiver sits on layer 2; the decal projects only onto layer 1. The view renders
    // everything, so the decal survives the view test and is dropped per receiver instead.
    MakeReceiver(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, wallMaterialGuid, /*renderLayers=*/0x2U);
    Scene::Decal3DComponent layerOne = MakeScorchDecal();
    layerOne.cullMask = 0x1U;
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, layerOne);
    WaitUntilAssetsReady({wallMaterialGuid, decalMaterialGuid}, {meshGuid});

    BuildFrame(Math::Vector3{}, /*receiverLayerMask=*/kAllLayers);

    ASSERT_EQ(drawList.draws.size(), 0U);
    EXPECT_EQ(drawList.receiversTested, 0U) << "the receiver's own layers were tested before the volume";
    EXPECT_EQ(drawList.receiversOverlapped, 0U);
    EXPECT_EQ(drawList.decalsWithoutReceivers, 1U);
}

TEST_F(DecalRendererTest, BuildDrawList_TwoDecalsSharingAMaterialHoldOneReferenceAndSortBackToFront) {
    MakeWallAndDecal();
    // A second decal of the same material, further along the same wall, so both land on it and the
    // two are still separable by depth.
    Scene::Decal3DComponent beside = MakeScorchDecal();
    beside.size = Math::Vector3{0.5F, 0.5F, 0.5F};
    MakeDecal(Math::Vector3{1.0F, 0.0F, -4.8F}, Math::Quaternion{}, beside);
    BuildFrame(Math::Vector3{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    EXPECT_EQ(drawList.materialHandles.size(), 1U) << "one reference per distinct material, not per decal";
    // Back to front: the decal whose patches are nearest the camera comes last, so blending paints
    // it over the one behind it.
    EXPECT_GT(drawList.draws.front().sortDepth, drawList.draws.back().sortDepth)
        << "the farther decal is first in the list, so blending paints the near one over it";
}

TEST_F(DecalRendererTest, AppendVertexStream_PatchesBecomeTheCanonicalMeshVertexLayout) {
    MakeWallAndDecal();
    BuildFrame(Math::Vector3{});
    ASSERT_EQ(drawList.draws.size(), 1U);
    ASSERT_EQ(drawList.GetPatchCount(), 1U);
    EXPECT_EQ(drawList.GetTriangleCount(), 2U) << "a quad is two triangles";

    std::vector<Asset::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    drawList.AppendVertexStream(vertices, indices);

    EXPECT_EQ(vertices.size(), 4U);
    EXPECT_EQ(indices.size(), 6U);
    EXPECT_EQ(drawList.GetTriangleCount(), indices.size() / 3U);
    const DecalDraw& draw = drawList.draws.front();
    for (std::size_t vertexIndex = 0U; vertexIndex < vertices.size(); ++vertexIndex) {
        const Asset::MeshVertex& vertex = vertices[vertexIndex];
        const std::size_t patchVertexIndex = vertexIndex % DecalPatch::kMaximumVertices;
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

TEST_F(DecalRendererTest, BuildDrawList_ADecalRotatedAQuarterTurnProjectsSideways) {
    MakeWallAndDecal();
    // A quarter turn about Z maps the decal's local -Y onto world +X, so the volume stops standing
    // proud of the wall and lies along it: half a metre across, four metres tall, one metre deep.
    // The patch proves the volume turned with the object rather than staying world-axis-aligned.
    const Math::Quaternion quarterTurnAboutZ{0.0F, 0.0F, 0.70710678F, 0.70710678F};
    Scene::Decal3DComponent rotated = MakeScorchDecal();
    rotated.size = Math::Vector3{4.0F, 0.4F, 1.0F};
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, quarterTurnAboutZ, rotated);
    BuildFrame(Math::Vector3{});

    ASSERT_EQ(drawList.draws.size(), 2U);
    bool sawSidewaysProjection = false;
    for (const DecalDraw& draw : drawList.draws) {
        EXPECT_NEAR(std::fabs(draw.projection.projectionDirection.x) + std::fabs(draw.projection.projectionDirection.y), 1.0F, 1.0e-4F);
        if (std::fabs(draw.projection.projectionDirection.x) < 0.99F) {
            continue;
        }
        sawSidewaysProjection = true;
        ASSERT_FALSE(draw.patches.empty());
        float widestAcross = 0.0F;
        float tallest = 0.0F;
        for (const DecalPatch& patch : draw.patches) {
            for (std::size_t vertexIndex = 0U; vertexIndex < patch.vertexCount; ++vertexIndex) {
                widestAcross = std::max(widestAcross, std::fabs(patch.worldPositions[vertexIndex].x));
                tallest = std::max(tallest, std::fabs(patch.worldPositions[vertexIndex].y));
            }
        }
        EXPECT_LE(widestAcross, 0.25F) << "half of the 0.4 m it is wide, plus rounding";
        EXPECT_GT(tallest, 1.5F) << "and it reaches far up the wall, because that is its long axis";
    }
    EXPECT_TRUE(sawSidewaysProjection) << "the rotated decal projects along world +X, not down -Y";
}

TEST_F(DecalRendererTest, BuildDrawList_ADecalWhoseMaterialIsNotRegisteredPaintsNothing) {
    MakeWallAndDecal();
    Scene::Decal3DComponent missing = MakeScorchDecal();
    missing.materialAssetPath = "materials/decal_renderer_tests_not_registered.uvmat";
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, missing);
    BuildFrame(Math::Vector3{});

    EXPECT_EQ(drawList.decalsWithoutMaterial, 1U);
    EXPECT_EQ(drawList.draws.size(), 1U) << "the decal with a real material still paints";
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererTest, BuildDrawList_ADecalWithNoMaterialPathPaintsNothing) {
    MakeWallAndDecal();
    Scene::Decal3DComponent unassigned = MakeScorchDecal();
    unassigned.materialAssetPath.clear();
    MakeDecal(Math::Vector3{0.0F, 0.0F, -4.8F}, Math::Quaternion{}, unassigned);
    BuildFrame(Math::Vector3{});

    EXPECT_EQ(drawList.decalsWithoutMaterial, 1U);
    EXPECT_EQ(drawList.receiversTested, 1U);
}

TEST_F(DecalRendererTest, BuildDrawList_RebuildingTheFrameReplacesTheLastOneRatherThanAppending) {
    MakeWallAndDecal();
    BuildFrame(Math::Vector3{});
    const std::size_t firstPatchCount = drawList.GetPatchCount();
    ASSERT_GT(firstPatchCount, 0U);

    BuildFrame(Math::Vector3{});

    EXPECT_EQ(drawList.draws.size(), 1U) << "a second frame, not a second decal";
    EXPECT_EQ(drawList.GetPatchCount(), firstPatchCount);
    EXPECT_EQ(drawList.materialHandles.size(), 1U);
    EXPECT_EQ(drawList.decalsConsidered, 1U) << "the counters describe this frame alone";
}

// ---------------------------------------------------------------------------
// The draw plan: the step between "the pass found geometry" and "the GPU is handed it".
// ---------------------------------------------------------------------------

TEST_F(DecalRendererTest, BuildDecalDrawPlan_WorldToUnitIsTheSpaceTheCpuClippedIn) {
    // The plan hands the fragment program one matrix instead of re-deriving the volume's frame in
    // GLSL, so this is the invariant that keeps the two halves of the decal pass in one coordinate
    // space: the matrix must map a world point to exactly what the CPU sampler's own transform
    // gives for the same point. Asserted at the patch's own vertices, which are the points the
    // clipping arithmetic actually produced.
    const WallAndDecal scene = MakeWallAndDecal();
    BuildFrame(Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_EQ(drawList.draws.size(), 1U);

    const DecalDrawPlan plan = MakePlan();
    ASSERT_EQ(plan.commands.size(), 1U);
    EXPECT_EQ(plan.commands.front().decal, scene.decal);

    const Scene::Decal3DProjection& projection = drawList.draws.front().projection;
    std::size_t checked = 0U;
    for (const DecalPatch& patch : drawList.draws.front().patches) {
        for (std::size_t vertexIndex = 0U; vertexIndex < patch.vertexCount; ++vertexIndex) {
            const Math::Vector3 world = patch.worldPositions[vertexIndex];
            const Math::Vector3 viaMatrix = Math::TransformPoint(plan.commands.front().worldToUnit, world);
            const Math::Vector3 viaSampler = Scene::Decal3DWorldToUnit(projection, world);
            EXPECT_NEAR(viaMatrix.x, viaSampler.x, 1.0e-4F);
            EXPECT_NEAR(viaMatrix.y, viaSampler.y, 1.0e-4F);
            EXPECT_NEAR(viaMatrix.z, viaSampler.z, 1.0e-4F);
            ++checked;
        }
    }
    EXPECT_GE(checked, 3U) << "a plan with no patch vertices would pass this test without testing it";
}

TEST_F(DecalRendererTest, BuildDecalDrawPlan_CarriesTheAuthoredLookIntoTheDrawUniforms) {
    // Every uniform the fragment program reads has to come from somewhere the author can reach:
    // the material's albedo and emissive, and the decal's own modulate/emissionEnergy/albedoMix and
    // fades. This pins the wiring between the two, which is the difference between an authored
    // field and a field that does nothing.
    materialAlbedo = Math::Color{0.8F, 0.1F, 0.1F};
    materialEmissive = Math::Color{0.2F, 0.0F, 0.0F};
    const WallAndDecal scene = MakeWallAndDecal();

    Scene::Decal3DComponent& authored = entityManager.GetComponent<Scene::Decal3DComponent>(scene.decal);
    authored.modulate = Math::Vector3{0.5F, 2.0F, 1.0F};
    authored.emissionEnergy = 3.0F;
    authored.albedoMix = 0.25F;
    authored.normalFade = 0.75F;
    authored.upperFade = 0.4F;
    authored.lowerFade = 0.2F;
    authored.distanceFadeEnabled = true;
    authored.distanceFadeBegin = 12.0F;
    authored.distanceFadeLength = 8.0F;

    BuildFrame(Math::Vector3{0.0F, 0.0F, 0.0F});
    const DecalDrawPlan plan = MakePlan();
    ASSERT_EQ(plan.commands.size(), 1U);

    const DecalDrawCommand& command = plan.commands.front();
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
    EXPECT_EQ(command.albedoTextureGuid, Asset::kInvalidAssetGuid)
        << "this material leaves its texture unset, and the decal must say so rather than invent one";
}

TEST_F(DecalRendererTest, BuildDecalDrawPlan_EveryCommandWindowStaysInsideTheStreamsItPointsAt) {
    // The plan's streams are shared by every decal in the frame, and each command names its own
    // window into them. A window that overruns the stream - or an index that reaches outside its
    // command's own vertices - would draw one decal with another decal's geometry, which is the
    // kind of bug that shows up as a smear rather than as a crash.
    const WallAndDecal scene = MakeWallAndDecal();
    BuildFrame(Math::Vector3{0.0F, 0.0F, 0.0F});
    const DecalDrawPlan plan = MakePlan();
    ASSERT_FALSE(plan.commands.empty());
    EXPECT_EQ(plan.commands.front().decal, scene.decal);

    for (const DecalDrawCommand& command : plan.commands) {
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

TEST_F(DecalRendererTest, BuildDecalDrawPlan_KeepsThePassesBackToFrontOrder) {
    // Blending order is the whole reason the list is sorted: a decal nearer the camera must be
    // recorded after the one behind it, and the plan is the last place that order can be lost.
    const WallAndDecal scene = MakeWallAndDecal();
    // A second decal of the same material beside the first, both landing on the same wall: the pass
    // sorts them back to front, and the plan must not be the step that loses that order.
    Scene::Decal3DComponent beside = MakeScorchDecal();
    beside.size = Math::Vector3{0.5F, 0.5F, 0.5F};
    const Scene::Entity second =
        MakeDecal(Math::Vector3{1.0F, 0.0F, -4.8F}, Math::Quaternion{}, beside);

    BuildFrame(Math::Vector3{0.0F, 0.0F, 0.0F});
    ASSERT_EQ(drawList.draws.size(), 2U);
    const DecalDrawPlan plan = MakePlan();
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

TEST_F(DecalRendererTest, BuildDecalDrawPlan_ADrawWhoseMaterialIsGoneIsCountedAndTheRestStillDraw) {
    const Asset::AssetHandle<Asset::MaterialAsset> material =
        LoadDecalMaterialHandle(kDecalMaterialPath);
    DecalDrawList synthetic{};
    synthetic.materialHandles.push_back(material);
    const Scene::Entity painted = entityManager.CreateEntity();
    synthetic.draws.push_back(MakeSyntheticDraw(painted, 0U));
    synthetic.draws.push_back(MakeSyntheticDraw(entityManager.CreateEntity(), 7U));

    DecalDrawPlan plan;
    BuildDecalDrawPlan(synthetic, plan);
    ASSERT_EQ(plan.commands.size(), 1U);
    EXPECT_EQ(plan.commands.front().decal, painted);
    EXPECT_EQ(plan.drawsWithoutMaterial, 1U);
    EXPECT_EQ(plan.indices.size(), 6U) << "one quad, and nothing left behind by the draw that was "
                                          "refused for naming a material the list does not hold";
    EXPECT_EQ(plan.vertices.size(), 4U);
}

TEST_F(DecalRendererTest, BuildDecalDrawPlan_ADecalThatReplacesNothingPaintsNothing) {
    const Asset::AssetHandle<Asset::MaterialAsset> material =
        LoadDecalMaterialHandle(kDecalMaterialPath);
    DecalDrawList synthetic{};
    synthetic.materialHandles.push_back(material);
    DecalDraw draw = MakeSyntheticDraw(entityManager.CreateEntity(), 0U);
    draw.albedoMix = 0.0F;
    synthetic.draws.push_back(draw);

    DecalDrawPlan plan;
    BuildDecalDrawPlan(synthetic, plan);
    EXPECT_TRUE(plan.commands.empty());
    EXPECT_EQ(plan.drawsWithoutPaint, 1U);
    EXPECT_TRUE(plan.vertices.empty());
}

TEST_F(DecalRendererTest, BuildDecalDrawPlan_TheCapsRefuseAWholeDrawRatherThanHalfOfOne) {
    const Asset::AssetHandle<Asset::MaterialAsset> material =
        LoadDecalMaterialHandle(kDecalMaterialPath);
    DecalDrawList synthetic{};
    synthetic.materialHandles.push_back(material);
    synthetic.draws.push_back(MakeSyntheticDraw(entityManager.CreateEntity(), 0U));
    synthetic.draws.push_back(MakeSyntheticDraw(entityManager.CreateEntity(), 0U));

    // The command cap: one draw records, the second is counted rather than half-appended.
    DecalDrawPlan byCommand;
    BuildDecalDrawPlan(synthetic, byCommand, 1U, kMaximumDecalVertices);
    EXPECT_EQ(byCommand.commands.size(), 1U);
    EXPECT_EQ(byCommand.drawsTruncated, 1U);
    EXPECT_EQ(byCommand.vertices.size(), 4U);
    EXPECT_EQ(byCommand.indices.size(), 6U);

    // The vertex cap: a draw whose geometry does not fit is refused whole, so the stream never
    // holds vertices no command points at.
    DecalDrawPlan byVertex;
    BuildDecalDrawPlan(synthetic, byVertex, kMaximumDecalDrawCommands, 6U);
    EXPECT_EQ(byVertex.commands.size(), 1U);
    EXPECT_EQ(byVertex.drawsTruncated, 1U);
    EXPECT_EQ(byVertex.vertices.size(), 4U);
}

TEST_F(DecalRendererTest, BuildDecalDrawPlan_RebuildingAFrameReplacesTheLastOne) {
    const Asset::AssetHandle<Asset::MaterialAsset> material =
        LoadDecalMaterialHandle(kDecalMaterialPath);
    DecalDrawList synthetic{};
    synthetic.materialHandles.push_back(material);
    synthetic.draws.push_back(MakeSyntheticDraw(entityManager.CreateEntity(), 0U));

    DecalDrawPlan plan;
    BuildDecalDrawPlan(synthetic, plan);
    BuildDecalDrawPlan(synthetic, plan);
    EXPECT_EQ(plan.commands.size(), 1U) << "an existing plan must be cleared, not appended to";
    EXPECT_EQ(plan.vertices.size(), 4U);
    EXPECT_EQ(plan.indices.size(), 6U);
}

} // namespace
} // namespace UVE::Render::Tests
