// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/render_systems/camera_system_uve.h"
#include "uve/render_systems/light_system_uve.h"
#include "uve/render_systems/mesh_renderer_uve.h"
#include "uve/render_systems/render_system_uve.h"
#include "uve/render_systems/renderer_3d_frame_diagnostics_uve.h"
#include "uve/render_systems/renderer_3d_uve.h"
#include "uve/rhi/recorded_command_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterationsUVE = 200000;
constexpr std::uint32_t kTargetWidthUVE = 64;
constexpr std::uint32_t kTargetHeightUVE = 64;
constexpr Math::Vector3UVE kTestAmbientColorUVE{0.1F, 0.2F, 0.3F};
constexpr std::uint32_t kTestShadowMapResolutionUVE = 64;
constexpr float kTestShadowMapHalfExtentUVE = 20.0F;
constexpr float kTestShadowMapNearPlaneUVE = 0.1F;
constexpr float kTestShadowMapFarPlaneUVE = 100.0F;
constexpr float kTestShadowFrustumPaddingUVE = 1.0F;
constexpr float kTestShadowCascadeSplitLambdaUVE = 0.5F;
constexpr float kTestShadowCascadeBlendRatioUVE = 0.1F;
constexpr std::uint32_t kTestShadowPcfKernelRadiusUVE = 1;

/// The expected SSBO slot where FrustumCullIndirectUVE visible indices are bound.
constexpr std::uint32_t kExpectedInstanceVisibleIndexSlotUVE = 3U;

/// Swaps in a vertex shader that satisfies the instancing contract (reads uInstanceBaseIndex and gl_InstanceID).
void UseInstancingAwareShaderUVE(Asset::AssetManagerUVE& assetManager) {
    assetManager.RegisterLoaderUVE<Asset::ShaderAssetUVE>(
        [](const std::filesystem::path&, Asset::ShaderAssetUVE& shader) {
            shader.sourceCode =
                "void main() { int slot = uInstanceBaseIndex + gl_InstanceID; }";
            return true;
        });
}

/// Helper functions for inspecting commands submitted to NullRenderDeviceUVE.
[[nodiscard]] std::size_t CountDrawIndexedIndirectCommandsUVE(
    const std::vector<RecordedCommandUVE>& commands) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (std::holds_alternative<DrawIndexedIndirectCommandRecordUVE>(cmd)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::size_t CountDrawIndexedCommandsUVE(
    const std::vector<RecordedCommandUVE>& commands) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (std::holds_alternative<DrawIndexedCommandUVE>(cmd)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::size_t CountStorageBufferBindingsForSlotUVE(
    const std::vector<RecordedCommandUVE>& commands,
    const std::uint32_t targetSlot) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (const auto* const bind = std::get_if<BindStorageBufferCommandUVE>(&cmd)) {
            if (bind->slot == targetSlot) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] const DrawIndexedIndirectCommandRecordUVE* FindFirstDrawIndexedIndirectCommandUVE(
    const std::vector<RecordedCommandUVE>& commands) {
    for (const auto& cmd : commands) {
        if (const auto* const indirect = std::get_if<DrawIndexedIndirectCommandRecordUVE>(&cmd)) {
            return indirect;
        }
    }
    return nullptr;
}

class Renderer3DIndirectInstancingTest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    NullRenderDeviceUVE renderDevice;

    Asset::AssetBundleUVE assetBundle;
    Asset::FileSystemUVE fileSystem{assetBundle};
    Shader::ShaderManagerUVE shaderManager{threadPool, eventSystem, renderDevice, fileSystem,
                                           Shader::ShaderManagerConfigUVE{}};

    RenderSystemUVE renderSystem{renderDevice};
    CameraSystemUVE cameraSystem;
    MeshRendererUVE meshRenderer;
    LightSystemUVE lightSystem;

    Asset::AssetGuidUVE vertexShaderGuid;
    Asset::AssetGuidUVE fragmentShaderGuid;
    Scene::EntityUVE mostRecentCamera = Scene::kInvalidEntityUVE;
    std::unique_ptr<Renderer3DUVE> renderer3D;

    Renderer3DIndirectInstancingTest() {
        vertexShaderGuid = assetDatabase.RegisterUVE("indirect_instancing_test_vertex.uvshader");
        fragmentShaderGuid = assetDatabase.RegisterUVE("indirect_instancing_test_fragment.uvshader");

        // Default shader: non-instanced dummy program.
        assetManager.RegisterLoaderUVE<Asset::ShaderAssetUVE>(
            [](const std::filesystem::path&, Asset::ShaderAssetUVE& shader) {
                shader.sourceCode = "void main() { }";
                return true;
            });

        // Default mesh: 1 triangle with unit bounds.
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
                mesh.vertices = {
                    Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
                    Asset::MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
                    Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
                };
                mesh.indices = {0, 1, 2};
                mesh.localBounds =
                    Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
                return true;
            });

        // Default material.
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](
                const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.isTransparent = false;
                material.albedoColor = Math::ColorUVE{0.5F, 0.5F, 0.5F};
                material.metallic = 0.0F;
                material.roughness = 1.0F;
                return true;
            });

        // 2x2 fallback texture.
        assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
            [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
                texture.width = 2;
                texture.height = 2;
                texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
                texture.pixels.assign(2U * 2U * 4U, std::byte{0xFF});
                return true;
            });

        renderer3D = std::make_unique<Renderer3DUVE>(
            renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
            assetDatabase, eventSystem, kTargetWidthUVE, kTargetHeightUVE, kTestAmbientColorUVE,
            kTestShadowMapResolutionUVE, kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE,
            kTestShadowMapFarPlaneUVE, kTestShadowFrustumPaddingUVE, kTestShadowCascadeSplitLambdaUVE,
            kTestShadowCascadeBlendRatioUVE, kTestShadowPcfKernelRadiusUVE);
    }

    Scene::EntityUVE MakeCameraEntityUVE(Math::Vector3UVE position = Math::Vector3UVE{}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(entity);
        mostRecentCamera = entity;
        return entity;
    }

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

    void WaitUntilAssetsReadyUVE(Asset::AssetGuidUVE meshGuid, Asset::AssetGuidUVE materialGuid,
                                 bool primeDefaultRenderer = true) {
        Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
            assetManager.LoadUVE<Asset::MeshAssetUVE>(meshGuid, assetDatabase);
        Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
            assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
        Asset::AssetHandleUVE<Asset::ShaderAssetUVE> vertexHandle =
            assetManager.LoadUVE<Asset::ShaderAssetUVE>(vertexShaderGuid, assetDatabase);
        Asset::AssetHandleUVE<Asset::ShaderAssetUVE> fragmentHandle =
            assetManager.LoadUVE<Asset::ShaderAssetUVE>(fragmentShaderGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            if (meshHandle.IsReadyUVE() && materialHandle.IsReadyUVE() && vertexHandle.IsReadyUVE() &&
                fragmentHandle.IsReadyUVE()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(meshHandle.IsReadyUVE());
        ASSERT_TRUE(materialHandle.IsReadyUVE());
        ASSERT_TRUE(vertexHandle.IsReadyUVE());
        ASSERT_TRUE(fragmentHandle.IsReadyUVE());
        if (primeDefaultRenderer && mostRecentCamera != Scene::kInvalidEntityUVE) {
            PrimeMaterialProgramUVE(*renderer3D, mostRecentCamera);
        }
    }

    void PrimeMaterialProgramUVE(Renderer3DUVE& renderer, Scene::EntityUVE cameraEntity) {
        renderer.RenderFrameUVE(entityManager, cameraEntity);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            shaderManager.UpdateUVE(0.0);
            if (shaderManager.GetPendingJobCountUVE() == 0U) {
                return;
            }
            std::this_thread::yield();
        }
        ADD_FAILURE() << "Timed out waiting for managed material program compilation";
    }
};

// ---------------------------------------------------------------------------
// 1. Constant Specification Pinning
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, SSBOSlotConstant_MatchesSpecification) {
    // The GPU shader and renderer contract dictates that visible instance index
    // compaction buffer sits at SSBO binding slot 3.
    EXPECT_EQ(kExpectedInstanceVisibleIndexSlotUVE, 3U);
}

// ---------------------------------------------------------------------------
// 2. Instanced Batch Records Indirect Draw Command and Binds Slot 3 SSBO
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_InstancedBatch_RecordsDrawIndexedIndirectAndBindsSlot3) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_test_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_test_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    // 4 identical mesh entities arranged in front of the camera.
    for (int index = 0; index < 4; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.2F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    // Verification of frame diagnostics.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);

    // Verification of command list recording on the device.
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();

    // Must record DrawIndexedIndirectCommandRecordUVE.
    EXPECT_GE(CountDrawIndexedIndirectCommandsUVE(commands), 1U);

    const DrawIndexedIndirectCommandRecordUVE* const indirectCmd =
        FindFirstDrawIndexedIndirectCommandUVE(commands);
    ASSERT_NE(indirectCmd, nullptr);
    EXPECT_NE(indirectCmd->buffer, kInvalidBufferHandleUVE);
    EXPECT_EQ(indirectCmd->offsetBytes, 0U);

    // Must bind visible index buffer to SSBO slot 3 (kExpectedInstanceVisibleIndexSlotUVE).
    EXPECT_GE(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 1U);

    // Standard direct DrawIndexedCommandUVE must NOT be recorded for this instanced batch.
    EXPECT_EQ(CountDrawIndexedCommandsUVE(commands), 0U);
}

// ---------------------------------------------------------------------------
// 3. Fallback: Frustum Culling Disabled (Debug Freeze) Uses Direct Instancing
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_FrustumCullingDisabled_FallsBackToDirectInstancedDraw) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_freeze_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_freeze_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    for (int index = 0; index < 4; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.2F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    // Disable frustum culling (simulate debug culling freeze).
    CullingSettingsUVE cullingSettings{};
    cullingSettings.frustumCullingEnabledUVE = false;
    renderer3D->SetCullingSettingsUVE(cullingSettings);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    // Must fall back to direct instancing: instancedDrawCallsRecorded = 1, but indirect = 0.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);

    // Device command verification: direct DrawIndexedCommandUVE with instanceCount = 4.
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_EQ(CountDrawIndexedIndirectCommandsUVE(commands), 0U);
    EXPECT_GE(CountDrawIndexedCommandsUVE(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 0U);
}

// ---------------------------------------------------------------------------
// 4. Fallback: Legacy Material Without Instancing Tokens Uses Unbatched Draws
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_LegacyMaterial_FallsBackToUnbatchedDraws) {
    // Default shader registered in fixture has "void main() { }" (no instancing markers).
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_legacy_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_legacy_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    // 3 separate mesh draw calls, 0 instanced, 0 indirect.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 3U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 0U);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_EQ(CountDrawIndexedIndirectCommandsUVE(commands), 0U);
    EXPECT_EQ(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 0U);
}

// ---------------------------------------------------------------------------
// 5. Diagnostics Reset Between Consecutive Frames
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_DiagnosticsResetBetweenFrames) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_reset_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_reset_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    // Frame 1
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE frame1 = renderer3D->GetLastFrameDiagnosticsUVE();

    // Frame 2
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE frame2 = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(frame1.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(frame2.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(frame1.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(frame2.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(frame1.instancedObjectsRecorded, 3U);
    EXPECT_EQ(frame2.instancedObjectsRecorded, 3U);
}

// ---------------------------------------------------------------------------
// 6. Multi-Batch Instanced Draws Record Distinct Indirect Draw Calls
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_MultipleInstancedBatches_RecordsMultipleIndirectDraws) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshA = assetDatabase.RegisterUVE("indirect_multibatch_a.uvmodel");
    const Asset::AssetGuidUVE matA = assetDatabase.RegisterUVE("indirect_multibatch_a.uvmat");
    const Asset::AssetGuidUVE meshB = assetDatabase.RegisterUVE("indirect_multibatch_b.uvmodel");
    const Asset::AssetGuidUVE matB = assetDatabase.RegisterUVE("indirect_multibatch_b.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    // Batch A: 3 entities
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshA, matA);
    }
    // Batch B: 2 entities
    for (int index = 0; index < 2; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 2.0F, -5.0F}, meshB, matB);
    }

    WaitUntilAssetsReadyUVE(meshA, matA, false);
    WaitUntilAssetsReadyUVE(meshB, matB, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 2U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 2U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 5U);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 2U);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_GE(CountDrawIndexedIndirectCommandsUVE(commands), 2U);
    EXPECT_GE(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 2U);
}

// ---------------------------------------------------------------------------
// 7. Single Object Instanced Run Uses Indirect Path
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_SingleObjectBatch_UsesIndirectInstancing) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_single_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_single_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 1U);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_EQ(CountDrawIndexedIndirectCommandsUVE(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 1U);
}

// ---------------------------------------------------------------------------
// 8. Dynamic Culling Toggle Switches Between Indirect and Direct Instancing
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_DynamicCullingToggle_SwitchesBetweenIndirectAndDirect) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_toggle_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_toggle_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    // Frame 1: Frustum culling OFF -> direct instancing.
    CullingSettingsUVE cullingSettings{};
    cullingSettings.frustumCullingEnabledUVE = false;
    renderer3D->SetCullingSettingsUVE(cullingSettings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().instancedDrawCallsRecorded, 1U);

    // Frame 2: Frustum culling ON -> indirect instancing.
    cullingSettings.frustumCullingEnabledUVE = true;
    renderer3D->SetCullingSettingsUVE(cullingSettings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().instancedDrawCallsRecorded, 1U);

    // Frame 3: Frustum culling OFF again -> direct instancing.
    cullingSettings.frustumCullingEnabledUVE = false;
    renderer3D->SetCullingSettingsUVE(cullingSettings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().instancedDrawCallsRecorded, 1U);
}

// ---------------------------------------------------------------------------
// 9. Transformed Bounding Boxes Prepared for Culling Kernel
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrameUVE_TransformedAabbsPassedToCompute) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("indirect_aabbs_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("indirect_aabbs_mat.uvmat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    // Create 5 instances positioned at varying coordinates and depths. Every one of them stays
    // inside the default 60 degree camera frustum: the CPU visibility cull runs before instancing
    // whenever frustum culling is enabled, so an off-camera instance would never reach the
    // compute kernel and the count would drop below five.
    for (int index = 0; index < 5; ++index) {
        const float x = static_cast<float>(index) * 0.4F - 0.8F;
        const float z = -3.0F - static_cast<float>(index) * 1.5F;
        MakeMeshEntityUVE(Math::Vector3UVE{x, 0.0F, z}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 5U);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_EQ(CountDrawIndexedIndirectCommandsUVE(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlotUVE(commands, kExpectedInstanceVisibleIndexSlotUVE), 1U);
}

} // namespace
} // namespace UVE::Render::Tests
