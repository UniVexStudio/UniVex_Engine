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

#include "asset_bundle.h"
#include "asset_database.h"
#include "asset_manager.h"
#include "file_system.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "shader_asset.h"
#include "texture_asset.h"
#include "camera_component.h"
#include "mesh_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "aabb.h"
#include "color.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "camera_system.h"
#include "light_system.h"
#include "mesh_renderer.h"
#include "render_system.h"
#include "renderer_3d_frame_diagnostics.h"
#include "renderer_3d.h"
#include "recorded_command.h"
#include "null_render_device.h"
#include "shader_manager.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterations = 200000;
constexpr std::uint32_t kTargetWidth = 64;
constexpr std::uint32_t kTargetHeight = 64;
constexpr Math::Vector3 kTestAmbientColor{0.1F, 0.2F, 0.3F};
constexpr std::uint32_t kTestShadowMapResolution = 64;
constexpr float kTestShadowMapHalfExtent = 20.0F;
constexpr float kTestShadowMapNearPlane = 0.1F;
constexpr float kTestShadowMapFarPlane = 100.0F;
constexpr float kTestShadowFrustumPadding = 1.0F;
constexpr float kTestShadowCascadeSplitLambda = 0.5F;
constexpr float kTestShadowCascadeBlendRatio = 0.1F;
constexpr std::uint32_t kTestShadowPcfKernelRadius = 1;

/// The expected SSBO slot where FrustumCullIndirect visible indices are bound.
constexpr std::uint32_t kExpectedInstanceVisibleIndexSlot = 3U;

/// Swaps in a vertex shader that satisfies the instancing contract (reads uInstanceBaseIndex and gl_InstanceID).
void UseInstancingAwareShader(Asset::AssetManager& assetManager) {
    assetManager.RegisterLoader<Asset::ShaderAsset>(
        [](const std::filesystem::path&, Asset::ShaderAsset& shader) {
            shader.sourceCode =
                "void main() { int slot = uInstanceBaseIndex + gl_InstanceID; }";
            return true;
        });
}

/// Helper functions for inspecting commands submitted to NullRenderDevice.
[[nodiscard]] std::size_t CountDrawIndexedIndirectCommands(
    const std::vector<RecordedCommand>& commands) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (std::holds_alternative<DrawIndexedIndirectCommandRecord>(cmd)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::size_t CountDrawIndexedCommands(
    const std::vector<RecordedCommand>& commands) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (std::holds_alternative<DrawIndexedCommand>(cmd)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] std::size_t CountStorageBufferBindingsForSlot(
    const std::vector<RecordedCommand>& commands,
    const std::uint32_t targetSlot) {
    std::size_t count = 0U;
    for (const auto& cmd : commands) {
        if (const auto* const bind = std::get_if<BindStorageBufferCommand>(&cmd)) {
            if (bind->slot == targetSlot) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] const DrawIndexedIndirectCommandRecord* FindFirstDrawIndexedIndirectCommand(
    const std::vector<RecordedCommand>& commands) {
    for (const auto& cmd : commands) {
        if (const auto* const indirect = std::get_if<DrawIndexedIndirectCommandRecord>(&cmd)) {
            return indirect;
        }
    }
    return nullptr;
}

class Renderer3DIndirectInstancingTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Threading::ThreadPool threadPool{2};
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};
    NullRenderDevice renderDevice;

    Asset::AssetBundle assetBundle;
    Asset::FileSystem fileSystem{assetBundle};
    Shader::ShaderManager shaderManager{threadPool, eventSystem, renderDevice, fileSystem,
                                           Shader::ShaderManagerConfig{}};

    RenderSystem renderSystem{renderDevice};
    CameraSystem cameraSystem;
    MeshRenderer meshRenderer;
    LightSystem lightSystem;

    Asset::AssetGuid vertexShaderGuid;
    Asset::AssetGuid fragmentShaderGuid;
    Scene::Entity mostRecentCamera = Scene::kInvalidEntity;
    std::unique_ptr<Renderer3D> renderer3D;

    Renderer3DIndirectInstancingTest() {
        vertexShaderGuid = assetDatabase.Register("indirect_instancing_test_vertex.uvshader");
        fragmentShaderGuid = assetDatabase.Register("indirect_instancing_test_fragment.uvshader");

        // Default shader: non-instanced dummy program.
        assetManager.RegisterLoader<Asset::ShaderAsset>(
            [](const std::filesystem::path&, Asset::ShaderAsset& shader) {
                shader.sourceCode = "void main() { }";
                return true;
            });

        // Default mesh: 1 triangle with unit bounds.
        assetManager.RegisterLoader<Asset::MeshAsset>(
            [](const std::filesystem::path&, Asset::MeshAsset& mesh) {
                mesh.vertices = {
                    Asset::MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
                    Asset::MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
                    Asset::MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
                };
                mesh.indices = {0, 1, 2};
                mesh.localBounds =
                    Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
                return true;
            });

        // Default material.
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](
                const std::filesystem::path&, Asset::MaterialAsset& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.isTransparent = false;
                material.albedoColor = Math::Color{0.5F, 0.5F, 0.5F};
                material.metallic = 0.0F;
                material.roughness = 1.0F;
                return true;
            });

        // 2x2 fallback texture.
        assetManager.RegisterLoader<Asset::TextureAsset>(
            [](const std::filesystem::path&, Asset::TextureAsset& texture) {
                texture.width = 2;
                texture.height = 2;
                texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
                texture.pixels.assign(2U * 2U * 4U, std::byte{0xFF});
                return true;
            });

        renderer3D = std::make_unique<Renderer3D>(
            renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
            assetDatabase, eventSystem, kTargetWidth, kTargetHeight, kTestAmbientColor,
            kTestShadowMapResolution, kTestShadowMapHalfExtent, kTestShadowMapNearPlane,
            kTestShadowMapFarPlane, kTestShadowFrustumPadding, kTestShadowCascadeSplitLambda,
            kTestShadowCascadeBlendRatio, kTestShadowPcfKernelRadius);
    }

    Scene::Entity MakeCameraEntity(Math::Vector3 position = Math::Vector3{}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::CameraComponent>(entity);
        mostRecentCamera = entity;
        return entity;
    }

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

    void WaitUntilAssetsReady(Asset::AssetGuid meshGuid, Asset::AssetGuid materialGuid,
                                 bool primeDefaultRenderer = true) {
        Asset::AssetHandle<Asset::MeshAsset> meshHandle =
            assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
        Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
            assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
        Asset::AssetHandle<Asset::ShaderAsset> vertexHandle =
            assetManager.Load<Asset::ShaderAsset>(vertexShaderGuid, assetDatabase);
        Asset::AssetHandle<Asset::ShaderAsset> fragmentHandle =
            assetManager.Load<Asset::ShaderAsset>(fragmentShaderGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            if (meshHandle.IsReady() && materialHandle.IsReady() && vertexHandle.IsReady() &&
                fragmentHandle.IsReady()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(meshHandle.IsReady());
        ASSERT_TRUE(materialHandle.IsReady());
        ASSERT_TRUE(vertexHandle.IsReady());
        ASSERT_TRUE(fragmentHandle.IsReady());
        if (primeDefaultRenderer && mostRecentCamera != Scene::kInvalidEntity) {
            PrimeMaterialProgram(*renderer3D, mostRecentCamera);
        }
    }

    void PrimeMaterialProgram(Renderer3D& renderer, Scene::Entity cameraEntity) {
        renderer.RenderFrame(entityManager, cameraEntity);
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            shaderManager.Update(0.0);
            if (shaderManager.GetPendingJobCount() == 0U) {
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
    EXPECT_EQ(kExpectedInstanceVisibleIndexSlot, 3U);
}

// ---------------------------------------------------------------------------
// 2. Instanced Batch Records Indirect Draw Command and Binds Slot 3 SSBO
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_InstancedBatch_RecordsDrawIndexedIndirectAndBindsSlot3) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_test_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_test_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    // 4 identical mesh entities arranged in front of the camera.
    for (int index = 0; index < 4; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.2F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    // Verification of frame diagnostics.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);

    // Verification of command list recording on the device.
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();

    // Must record DrawIndexedIndirectCommandRecord.
    EXPECT_GE(CountDrawIndexedIndirectCommands(commands), 1U);

    const DrawIndexedIndirectCommandRecord* const indirectCmd =
        FindFirstDrawIndexedIndirectCommand(commands);
    ASSERT_NE(indirectCmd, nullptr);
    EXPECT_NE(indirectCmd->buffer, kInvalidBufferHandle);
    EXPECT_EQ(indirectCmd->offsetBytes, 0U);

    // Must bind visible index buffer to SSBO slot 3 (kExpectedInstanceVisibleIndexSlot).
    EXPECT_GE(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 1U);

    // Standard direct DrawIndexedCommand must NOT be recorded for this instanced batch.
    EXPECT_EQ(CountDrawIndexedCommands(commands), 0U);
}

// ---------------------------------------------------------------------------
// 3. Fallback: Frustum Culling Disabled (Debug Freeze) Uses Direct Instancing
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_FrustumCullingDisabled_FallsBackToDirectInstancedDraw) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_freeze_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_freeze_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    for (int index = 0; index < 4; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.2F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    // Disable frustum culling (simulate debug culling freeze).
    CullingSettings cullingSettings{};
    cullingSettings.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(cullingSettings);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    // Must fall back to direct instancing: instancedDrawCallsRecorded = 1, but indirect = 0.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);

    // Device command verification: direct DrawIndexedCommand with instanceCount = 4.
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_EQ(CountDrawIndexedIndirectCommands(commands), 0U);
    EXPECT_GE(CountDrawIndexedCommands(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 0U);
}

// ---------------------------------------------------------------------------
// 4. Fallback: Legacy Material Without Instancing Tokens Uses Unbatched Draws
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_LegacyMaterial_FallsBackToUnbatchedDraws) {
    // Default shader registered in fixture has "void main() { }" (no instancing markers).
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_legacy_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_legacy_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    // 3 separate mesh draw calls, 0 instanced, 0 indirect.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 3U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 0U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_EQ(CountDrawIndexedIndirectCommands(commands), 0U);
    EXPECT_EQ(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 0U);
}

// ---------------------------------------------------------------------------
// 5. Diagnostics Reset Between Consecutive Frames
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_DiagnosticsResetBetweenFrames) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_reset_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_reset_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    // Frame 1
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics frame1 = renderer3D->GetLastFrameDiagnostics();

    // Frame 2
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics frame2 = renderer3D->GetLastFrameDiagnostics();

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

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_MultipleInstancedBatches_RecordsMultipleIndirectDraws) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshA = assetDatabase.Register("indirect_multibatch_a.uvmodel");
    const Asset::AssetGuid matA = assetDatabase.Register("indirect_multibatch_a.uvmat");
    const Asset::AssetGuid meshB = assetDatabase.Register("indirect_multibatch_b.uvmodel");
    const Asset::AssetGuid matB = assetDatabase.Register("indirect_multibatch_b.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    // Batch A: 3 entities
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshA, matA);
    }
    // Batch B: 2 entities
    for (int index = 0; index < 2; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 2.0F, -5.0F}, meshB, matB);
    }

    WaitUntilAssetsReady(meshA, matA, false);
    WaitUntilAssetsReady(meshB, matB, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 2U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 2U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 5U);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 2U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_GE(CountDrawIndexedIndirectCommands(commands), 2U);
    EXPECT_GE(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 2U);
}

// ---------------------------------------------------------------------------
// 7. Single Object Instanced Run Uses Indirect Path
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_SingleObjectBatch_UsesIndirectInstancing) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_single_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_single_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 1U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_EQ(CountDrawIndexedIndirectCommands(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 1U);
}

// ---------------------------------------------------------------------------
// 8. Dynamic Culling Toggle Switches Between Indirect and Direct Instancing
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_DynamicCullingToggle_SwitchesBetweenIndirectAndDirect) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_toggle_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_toggle_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 0.0F, -5.0F},
                          meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    // Frame 1: Frustum culling OFF -> direct instancing.
    CullingSettings cullingSettings{};
    cullingSettings.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(cullingSettings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().instancedDrawCallsRecorded, 1U);

    // Frame 2: Frustum culling ON -> indirect instancing.
    cullingSettings.frustumCullingEnabled = true;
    renderer3D->SetCullingSettings(cullingSettings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().instancedDrawCallsRecorded, 1U);

    // Frame 3: Frustum culling OFF again -> direct instancing.
    cullingSettings.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(cullingSettings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().indirectInstancedDrawCallsRecorded, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().instancedDrawCallsRecorded, 1U);
}

// ---------------------------------------------------------------------------
// 9. Transformed Bounding Boxes Prepared for Culling Kernel
// ---------------------------------------------------------------------------

TEST_F(Renderer3DIndirectInstancingTest, RenderFrame_TransformedAabbsPassedToCompute) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("indirect_aabbs_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("indirect_aabbs_mat.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();

    // Create 5 instances positioned at varying coordinates and depths. Every one of them stays
    // inside the default 60 degree camera frustum: the CPU visibility cull runs before instancing
    // whenever frustum culling is enabled, so an off-camera instance would never reach the
    // compute kernel and the count would drop below five.
    for (int index = 0; index < 5; ++index) {
        const float x = static_cast<float>(index) * 0.4F - 0.8F;
        const float z = -3.0F - static_cast<float>(index) * 1.5F;
        MakeMeshEntity(Math::Vector3{x, 0.0F, z}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.indirectInstancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 5U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_EQ(CountDrawIndexedIndirectCommands(commands), 1U);
    EXPECT_EQ(CountStorageBufferBindingsForSlot(commands, kExpectedInstanceVisibleIndexSlot), 1U);
}

} // namespace
} // namespace UVE::Render::Tests
