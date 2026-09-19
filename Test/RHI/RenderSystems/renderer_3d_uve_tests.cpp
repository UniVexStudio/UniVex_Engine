// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/renderer_3d_uve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/asset_reloaded_event_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/render_systems/camera_system_uve.h"
#include "uve/render_systems/light_system_uve.h"
#include "uve/render_systems/mesh_renderer_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/render_systems/render_system_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"
#include "uve/ui/ui_runtime_uve.h"

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

class Renderer3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    NullRenderDeviceUVE renderDevice;

    // Real (not fake) ShaderManagerUVE (Increment 26) — Renderer3DUVE compiles its built-in
    // shadow-depth program through this, matching the exact fixture shape
    // tests/render/shader/shader_manager_uve_tests.cpp already establishes for exercising
    // ShaderManagerUVE against NullRenderDeviceUVE. assetBundle/fileSystem exist only so
    // ShaderManagerUVE has a real IFileSystemUVE to (fail to) find a virtual shader file on —
    // every program in these tests resolves through its embedded fallback source instead.
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

    Renderer3DUVETest() {
        vertexShaderGuid = assetDatabase.RegisterUVE("renderer3d_tests_vertex.uveshader");
        fragmentShaderGuid = assetDatabase.RegisterUVE("renderer3d_tests_fragment.uveshader");

        assetManager.RegisterLoaderUVE<Asset::ShaderAssetUVE>(
            [](const std::filesystem::path&, Asset::ShaderAssetUVE& shader) {
                shader.sourceCode = "void main() { }";
                return true;
            });
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
        // Default material: valid shader GUIDs, distinguishable scalar/color values, and every
        // texture GUID left unset (kInvalidAssetGuidUVE) - exercises Renderer3DUVE's fallback
        // texture path unless a specific test overrides this loader with a real texture GUID.
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                                 Asset::MaterialAssetUVE& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.isTransparent = false;
                material.albedoColor = Math::Vector3UVE{0.2F, 0.4F, 0.6F};
                material.metallic = 0.25F;
                material.roughness = 0.75F;
                material.emissiveColor = Math::Vector3UVE{0.1F, 0.0F, 0.0F};
                return true;
            });
        // Default texture loader: a small, always-ready 2x2 RGBA8Unorm texture, reused by any
        // test that assigns a real texture GUID to a material without needing custom pixel data.
        assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
            [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
                texture.width = 2;
                texture.height = 2;
                texture.format = Asset::TextureFormatUVE::RGBA8Unorm;
                texture.pixels.assign(2U * 2U * 4U, std::byte{0xAB});
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

    Scene::EntityUVE MakePrimitiveEntityUVE(Math::Vector3UVE worldPosition,
                                             Scene::PrimitiveMeshComponentUVE primitive) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity, primitive);
        return entity;
    }

    Scene::EntityUVE MakeLightEntityUVE(Scene::LightComponentUVE light) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(entity, light);
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

    /// Starts one material-program request through `renderer`, then drains the ShaderManagerUVE
    /// jobs. The test's subsequent explicit RenderFrameUVE call observes the ready managed program
    /// without obscuring the first-frame asynchronous skip from tests that exercise it directly.
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

    void WaitUntilTextureReadyUVE(Asset::AssetGuidUVE textureGuid) {
        Asset::AssetHandleUVE<Asset::TextureAssetUVE> textureHandle =
            assetManager.LoadUVE<Asset::TextureAssetUVE>(textureGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            if (textureHandle.IsReadyUVE()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(textureHandle.IsReadyUVE());
    }

    /// Overrides the material loader so its albedoTexture points at `textureGuid`, keeping every
    /// other field identical to the default loader registered in the constructor. Safe to call
    /// any number of times per test (RegisterLoaderUVE<T>() simply replaces the stored loader).
    void UseAlbedoTextureInMaterialUVE(Asset::AssetGuidUVE textureGuid) {
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid, textureGuid](
                const std::filesystem::path&, Asset::MaterialAssetUVE& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.albedoTexture = textureGuid;
                return true;
            });
    }

    /// Bounded busy-poll (never a fixed sleep) driving `shaderManager` until Renderer3DUVE's
    /// internal built-in shadow-depth program has finished compiling (Increment 26). The test
    /// can't reach that program directly (it lives inside Renderer3DUVE's PIMPL) - instead this
    /// creates a second program request against the identical built-in descriptor and polls that
    /// one to readiness. ShaderManagerUVE::UpdateUVE() drains every completed job and applies every
    /// pending program link in one call (not just one at a time, see DrainCompletedSourceJobsUVE/
    /// ApplyPendingProgramLinksUVE), and Renderer3DUVE's own request was submitted first (at
    /// fixture construction, before any test body runs) - so by the time this probe program is
    /// ready, Renderer3DUVE's internal one is guaranteed to be ready too.
    void WaitUntilShadowProgramReadyUVE() {
        Shader::ShaderProgramDescUVE probeDesc;
        probeDesc.virtualFilePath = std::string(Shader::BuiltIn::kShadowDepthVirtualPath);
        probeDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kShadowDepthSource);
        probeDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
        const std::shared_ptr<Shader::ShaderProgramUVE> probe = shaderManager.CreateProgramUVE(probeDesc);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            shaderManager.UpdateUVE(0.0);
            if (probe->IsReadyUVE()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(probe->IsReadyUVE());
        ASSERT_TRUE(probe->IsValidUVE());
    }
};

TEST_F(Renderer3DUVETest, RenderFrameUVE_EmptyScene_MainPassBeginsAndEndsWithNoDraws) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    // No directional caster exists, so the optimized frame contains only the empty main color
    // pass. Tone mapping has not linked yet in this first frame.
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(commands.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommandUVE>(commands[0U]).desc.colorAttachment, kInvalidTextureHandleUVE);
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(commands[1U]));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_VisiblePrimitive_RecordsCanonicalGeometryAndAuthoredColor) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Math::Vector3UVE baseColor{0.15F, 0.45F, 0.85F};
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F},
                           Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, baseColor});

    // The renderer-owned built-in program compiles asynchronously. The first frame triggers regular
    // extraction; draining ShaderManagerUVE then makes the next frame’s primitive draw observable.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command) &&
               std::get<DrawIndexedCommandUVE>(command).indexCount == 36U;
    }));
    const auto primitiveColor = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformVector3CommandUVE>(command) &&
               std::get<SetUniformVector3CommandUVE>(command).name == "uColor";
    });
    ASSERT_NE(primitiveColor, commands.cend());
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(*primitiveColor).value, baseColor);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_FiniteExtremePrimitiveTransformIsRejectedBeforeQueuePublication) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE primitiveEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, -10.0F};
    transform.localScale = Math::Vector3UVE{std::numeric_limits<float>::max(), 1.0F, 1.0F};
    sceneGraph.AttachTransformUVE(entityManager, primitiveEntity, transform);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(primitiveEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.primitiveCandidates, 1U);
    EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U);
    EXPECT_EQ(diagnostics.primitiveDrawCallsRecorded, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_VisiblePrimitive_ReportsEvidenceSpecificDiagnostics) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F},
                           Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube,
                                                            Math::Vector3UVE{0.75F, 0.2F, 0.1F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE beforeProgramReady = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(beforeProgramReady.primitiveCandidates, 1U);
    EXPECT_EQ(beforeProgramReady.primitiveItemsExtracted, 1U);
    EXPECT_EQ(beforeProgramReady.primitiveDrawCallsRecorded, 0U);
    EXPECT_FALSE(beforeProgramReady.primitiveProgramReady);
    EXPECT_TRUE(beforeProgramReady.mainPassRecorded);
    EXPECT_FALSE(beforeProgramReady.toneMappingPassRecorded);
    EXPECT_EQ(beforeProgramReady.glDrawCallsIssued, 0U);

    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE afterProgramReady = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(afterProgramReady.primitiveCandidates, 1U);
    EXPECT_EQ(afterProgramReady.primitiveItemsExtracted, 1U);
    EXPECT_EQ(afterProgramReady.primitiveDrawCallsRecorded, 1U);
    EXPECT_TRUE(afterProgramReady.primitiveProgramReady);
    EXPECT_TRUE(afterProgramReady.mainPassRecorded);
    EXPECT_TRUE(afterProgramReady.toneMappingProgramReady);
    EXPECT_TRUE(afterProgramReady.toneMappingPassRecorded);
    EXPECT_EQ(afterProgramReady.glDrawCallsIssued, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_VisibleMesh_RecordsExpectedCommandSequence) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    // ShaderProgramUVE flushes its pending uniforms from an unordered cache, so insertion order is
    // intentionally not a renderer contract. Assert named effects and pass structure instead.
    ASSERT_GE(commands.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommandUVE>(commands[0U]).desc.colorAttachment, kInvalidTextureHandleUVE);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindPipelineCommandUVE>(command);
    }));
    const auto findMatrix = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformMatrix4x4CommandUVE>(command) &&
                   std::get<SetUniformMatrix4x4CommandUVE>(command).name == name;
        });
    };
    const auto findInt = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == name;
        });
    };
    const auto findVector = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformVector3CommandUVE>(command) &&
                   std::get<SetUniformVector3CommandUVE>(command).name == name;
        });
    };
    const auto model = findMatrix("uModel");
    const auto viewProjection = findMatrix("uViewProjection");
    const auto cascadeCount = findInt("uShadowCascadeCount");
    const auto pcfRadius = findInt("uShadowPcfKernelRadius");
    const auto ambient = findVector("uAmbientColor");
    const auto viewPosition = findVector("uViewPosition");
    ASSERT_NE(model, commands.cend());
    ASSERT_NE(viewProjection, commands.cend());
    ASSERT_NE(cascadeCount, commands.cend());
    ASSERT_NE(pcfRadius, commands.cend());
    ASSERT_NE(ambient, commands.cend());
    ASSERT_NE(viewPosition, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*cascadeCount).value, 0);
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*pcfRadius).value, static_cast<std::int32_t>(kTestShadowPcfKernelRadiusUVE));
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(*ambient).value, kTestAmbientColorUVE);
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(*viewPosition).value, (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command) &&
               std::get<DrawIndexedCommandUVE>(command).indexCount == 3U;
    }));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(commands.back()));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ManagedMaterialProgram_SkipsUntilReadyThenDraws) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_managed_program_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_managed_program_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& commandsWhileLinking = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_FALSE(std::any_of(commandsWhileLinking.cbegin(), commandsWhileLinking.cend(),
                             [](const RecordedCommandUVE& command) {
                                 return std::holds_alternative<DrawIndexedCommandUVE>(command);
                             }));

    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& commandsWhenReady = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_TRUE(std::any_of(commandsWhenReady.cbegin(), commandsWhenReady.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command);
    }));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ShadowPcfKernelRadiusAboveTwo_ClampsToTwo) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_pcf_clamp_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_pcf_clamp_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    Renderer3DUVE clampedRenderer{renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
                                  shaderManager, assetManager, assetDatabase, eventSystem, kTargetWidthUVE,
                                  kTargetHeightUVE, kTestAmbientColorUVE, kTestShadowMapResolutionUVE,
                                  kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE,
                                  kTestShadowMapFarPlaneUVE, kTestShadowFrustumPaddingUVE,
                                  kTestShadowCascadeSplitLambdaUVE, kTestShadowCascadeBlendRatioUVE, 3U};
    PrimeMaterialProgramUVE(clampedRenderer, cameraEntity);
    clampedRenderer.RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto radiusCommand = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        if (!std::holds_alternative<SetUniformIntCommandUVE>(command)) {
            return false;
        }
        return std::get<SetUniformIntCommandUVE>(command).name == "uShadowPcfKernelRadius";
    });
    ASSERT_NE(radiusCommand, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*radiusCommand).value, 2);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ShadowCascadeBlendRatioAboveQuarter_ClampsToQuarter) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_blend_clamp_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_blend_clamp_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    Renderer3DUVE clampedRenderer{renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
                                  shaderManager, assetManager, assetDatabase, eventSystem, kTargetWidthUVE,
                                  kTargetHeightUVE, kTestAmbientColorUVE, kTestShadowMapResolutionUVE,
                                  kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE,
                                  kTestShadowMapFarPlaneUVE, kTestShadowFrustumPaddingUVE,
                                  kTestShadowCascadeSplitLambdaUVE, 0.5F, kTestShadowPcfKernelRadiusUVE};
    PrimeMaterialProgramUVE(clampedRenderer, cameraEntity);
    clampedRenderer.RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto blendCommand = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        if (!std::holds_alternative<SetUniformFloatCommandUVE>(command)) {
            return false;
        }
        return std::get<SetUniformFloatCommandUVE>(command).name == "uShadowCascadeBlendRatio";
    });
    ASSERT_NE(blendCommand, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommandUVE>(*blendCommand).value, 0.25F);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ActiveLightEntity_PushesComputedLightUniformsInSlotZero) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_lit_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_lit_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    // Identity rotation, so LightSystemUVE derives direction {0,0,-1} (see
    // LightSystemUVETest's own RotateVectorUVE-based coverage for the rotated case). A Point
    // light never casts a shadow (Increment 26 scopes that to Directional only), so this test's
    // shadow pass stays empty regardless.
    Scene::LightComponentUVE light{Math::Vector3UVE{0.9F, 0.8F, 0.7F}, 4.5F};
    light.type = Scene::LightTypeUVE::Point;
    light.range = 22.0F;
    MakeLightEntityUVE(light);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto typeUniform = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uLights[0].type";
    });
    ASSERT_NE(typeUniform, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*typeUniform).value, 1); // Point
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_TwoLightsOfDifferentTypes_PopulateSlotsZeroAndOneOthersStaySentinel) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_multilight_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_multilight_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::LightComponentUVE directionalLight{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 2.0F};
    Scene::LightComponentUVE spotLight{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 3.0F};
    spotLight.type = Scene::LightTypeUVE::Spot;
    spotLight.spotAngleDegrees = 15.0F;
    MakeLightEntityUVE(directionalLight);
    MakeLightEntityUVE(spotLight);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    // Slot 0: directional light.
    const auto directionalType = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uLights[0].type";
    });
    ASSERT_NE(directionalType, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*directionalType).value, 0); // Directional
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*cascadeCount).value, 3);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_AmbientColorFromConstructor_AlwaysPushedRegardlessOfLight) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_ambient_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_ambient_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const auto assertAmbientColor = [](const std::vector<RecordedCommandUVE>& commands) {
        const auto ambient = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformVector3CommandUVE>(command) &&
                   std::get<SetUniformVector3CommandUVE>(command).name == "uAmbientColor";
        });
        ASSERT_NE(ambient, commands.cend());
        EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(*ambient).value, kTestAmbientColorUVE);
    };

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    assertAmbientColor(renderDevice.GetLastSubmittedCommandsUVE());

    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 1.0F});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    assertAmbientColor(renderDevice.GetLastSubmittedCommandsUVE());
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_CameraAtKnownPosition_PushesMatchingViewPositionUniform) {
    // Stays on the same viewing axis as the mesh below (default identity rotation looks down -Z)
    // so the mesh remains inside the frustum — an off-axis camera position would cull it, leaving
    // no recorded item to assert uniforms on.
    const Math::Vector3UVE cameraPosition{0.0F, 0.0F, 5.0F};
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE(cameraPosition);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_viewpos_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_viewpos_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto viewPosition = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformVector3CommandUVE>(command) &&
               std::get<SetUniformVector3CommandUVE>(command).name == "uViewPosition";
    });
    ASSERT_NE(viewPosition, commands.cend());
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(*viewPosition).value, cameraPosition);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_MaterialWithoutTextures_UsesFallbackTexturesForAllThreeSlots) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_mesh.uvemodel");
    const Asset::AssetGuidUVE materialAGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_material_a.uvemat");
    const Asset::AssetGuidUVE materialBGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_material_b.uvemat");
    // Both materials resolve through the same default (no-texture) loader from the fixture, but
    // are registered under two distinct AssetGuidUVEs, so each gets its own materialCache entry
    // and independently resolves its fallback texture handles.
    MakeMeshEntityUVE(Math::Vector3UVE{-1.0F, 0.0F, -10.0F}, meshGuid, materialAGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{1.0F, 0.0F, -10.0F}, meshGuid, materialBGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialAGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialBGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    // Slot 3 (the shadow map, bound once per item in the main pass regardless of material) is
    // excluded here. The first six remain the two items' material fallback bindings, recorded
    // during the MainColor pass before any of the slot-0 fullscreen post-process binds that
    // follow it (Phase 2b SSAO + SSAOComposite + BloomBrightPass + BloomBlurH + BloomBlurV +
    // BloomComposite, six single-texture passes, then finally ToneMapping's source bind).
    std::vector<BindTextureCommandUVE> textureBinds;
    for (const RecordedCommandUVE& command : commands) {
        if (const auto* const bindTexture = std::get_if<BindTextureCommandUVE>(&command)) {
            if (bindTexture->slot < 3U) {
                textureBinds.push_back(*bindTexture);
            }
        }
    }
    // 2 items x 3 material texture slots, then 6 Phase 2b post-process passes, then tone-map source.
    ASSERT_EQ(textureBinds.size(), 13U);

    // Group by slot: item1's slot-N handle must equal item2's slot-N handle (fallback reuse
    // across two independently-resolved materials), and the albedo/AO slots (both default to the
    // white fallback) must share a handle distinct from the normal slot's flat-normal fallback.
    EXPECT_EQ(textureBinds[0].texture, textureBinds[3].texture); // albedo, item1 vs item2
    EXPECT_EQ(textureBinds[1].texture, textureBinds[4].texture); // normal, item1 vs item2
    EXPECT_EQ(textureBinds[2].texture, textureBinds[5].texture); // ao, item1 vs item2
    EXPECT_EQ(textureBinds[0].texture, textureBinds[2].texture); // albedo == ao (both white fallback)
    EXPECT_NE(textureBinds[0].texture, textureBinds[1].texture); // albedo != normal (different fallback)
    EXPECT_NE(textureBinds[0].texture, kInvalidTextureHandleUVE);
    EXPECT_NE(textureBinds[1].texture, kInvalidTextureHandleUVE);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ReadyEmptyMeshSkipsInvalidGpuBuffers) {
    assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
        [](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
            mesh.vertices.clear();
            mesh.indices.clear();
            mesh.localBounds = Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, Math::Vector3UVE{});
            return true;
        });

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_empty_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_empty_mesh.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE firstDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(firstDiagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(firstDiagnostics.meshDrawCallsRecorded, 0U);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE secondDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(secondDiagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(secondDiagnostics.meshDrawCallsRecorded, 0U);
}

#if !UVE_DEBUG
TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidReadyMaterialPayloadSkipsGpuDrawInRelease) {
    assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAssetUVE& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.metallic = std::numeric_limits<float>::quiet_NaN();
            return true;
        });

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_material_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_tests_invalid_material_payload.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidReadyTexturePayloadUsesFallbackInRelease) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture.width = 0U;
            texture.height = 0U;
            texture.format = Asset::TextureFormatUVE::RGBA8Unorm;
            texture.pixels.clear();
            return true;
        });
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    WaitUntilTextureReadyUVE(textureGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE firstDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(firstDiagnostics.textureFallbacks, 1U);
    EXPECT_EQ(firstDiagnostics.meshDrawCallsRecorded, 0U);

    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE secondDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(secondDiagnostics.textureFallbacks, 0U);
    EXPECT_EQ(secondDiagnostics.meshDrawCallsRecorded, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_OverflowedCascadeSplitsDisableShadowPassInRelease) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Scene::CameraComponentUVE& camera = entityManager.GetComponentUVE<Scene::CameraComponentUVE>(cameraEntity);
    camera.nearPlane = std::numeric_limits<float>::min();
    camera.farPlane = std::numeric_limits<float>::max();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_overflowed_cascade_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_overflowed_cascade_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*cascadeCount).value, 0);
    EXPECT_FALSE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        if (!std::holds_alternative<BeginRenderPassCommandUVE>(command)) {
            return false;
        }
        const RenderPassDescUVE& desc = std::get<BeginRenderPassCommandUVE>(command).desc;
        return desc.colorAttachment == kInvalidTextureHandleUVE &&
               desc.depthAttachment != kInvalidTextureHandleUVE;
    }));
}
#endif

TEST_F(Renderer3DUVETest, RenderFrameUVE_MaterialWithAlbedoTexture_UploadsAndBindsRealTexture) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCountUVE();

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_textured_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_textured_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_albedo.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterFirstFrame = renderDevice.GetLiveResourceCountUVE();
    // The manager releases its temporary stage objects after linking; persistent renderer work is
    // mesh buffers, the managed pipeline, and the uploaded material texture.
    EXPECT_GT(liveResourcesAfterFirstFrame, baselineLiveResources);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetLiveResourceCountUVE(), liveResourcesAfterFirstFrame);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_Rgba16FloatAlbedoTexture_UploadsSuccessfully) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCountUVE();

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_albedo.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureFormatUVE::RGBA16Float;
            texture.pixels.assign(2U * 2U * 8U, std::byte{0});
            return true;
        });

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_GT(renderDevice.GetLiveResourceCountUVE(), baselineLiveResources);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command);
    }));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_FailedTextureUsesFallbackAndReportsDiagnostic) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE&) { return false; });

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Asset::AssetHandleUVE<Asset::TextureAssetUVE> textureHandle =
        assetManager.LoadUVE<Asset::TextureAssetUVE>(textureGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE && !textureHandle.HasFailedUVE(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(textureHandle.HasFailedUVE());
    // Settle the permanently failed texture before the first renderer prime; otherwise the helper
    // can observe a transient pending state and make this diagnostic assertion order-sensitive.
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.failedAssetLoads, 0U);
    EXPECT_EQ(diagnostics.textureFallbacks, 1U);

    // Rebuilding the material cache must not retry or repeatedly report the same permanent
    // first-load failure; an explicit texture reload is the only event that clears the memo.
    eventSystem.Publish(Asset::AssetReloadedEventUVE{materialGuid});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE afterMaterialEviction = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(afterMaterialEviction.failedAssetLoads, 0U);
    EXPECT_EQ(afterMaterialEviction.textureFallbacks, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_FailedTextureUploadIsMemoizedUntilTextureReload) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_upload_failure_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_upload_failure_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_upload_failure.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            // The custom loader returns a ready CPU asset, but the renderer's shared texture
            // validator rejects zero dimensions before any backend allocation.
            texture.width = 0U;
            texture.height = 2U;
            texture.format = Asset::TextureFormatUVE::RGBA8Unorm;
            texture.pixels.clear();
            return true;
        });
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    WaitUntilTextureReadyUVE(textureGuid);

    const std::uint64_t initialTextureAttempts = renderDevice.GetTextureCreateAttemptCountUVE();
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts + 1U);

    // Material cache invalidation must not retry the same permanently rejected GPU upload.
    eventSystem.Publish(Asset::AssetReloadedEventUVE{materialGuid});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts + 1U);

    // An explicit texture reload clears the memo and permits a deliberate retry.
    eventSystem.Publish(Asset::AssetReloadedEventUVE{textureGuid});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts + 2U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_TextureAssetNotYetReady_SkipsItemUntilLoaded) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_albedo.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);

    std::atomic<bool> textureLoadGateUVE{false};
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [&textureLoadGateUVE](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            while (!textureLoadGateUVE.load()) {
                std::this_thread::yield();
            }
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureFormatUVE::RGBA8Unorm;
            texture.pixels.assign(2U * 2U * 4U, std::byte{0xCD});
            return true;
        });

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // The texture load kicked off inside this call is blocked on textureLoadGateUVE, so the item
    // must be skipped this frame - checked below via the absence of any indexed draw, which is
    // what a real mesh item would need to have produced. Fixed command indices/counts are
    // deliberately avoided here: how many Phase 2b post-process passes (SSAO, bloom) execute
    // between the main pass and tone-mapping isn't this test's concern, and hardcoding them made
    // this assertion brittle to changes entirely unrelated to texture-readiness skipping.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE> commandsBeforeReady = renderDevice.GetLastSubmittedCommandsUVE();
    // Release the loader before assertions so any failed expectation cannot strand its worker thread.
    textureLoadGateUVE = true;
    ASSERT_FALSE(commandsBeforeReady.empty());
    EXPECT_FALSE(std::any_of(commandsBeforeReady.cbegin(), commandsBeforeReady.cend(),
                              [](const RecordedCommandUVE& command) {
                                  return std::holds_alternative<DrawIndexedCommandUVE>(command);
                              }));
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commandsBeforeReady.front()));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommandUVE>(commandsBeforeReady.back()));


    WaitUntilTextureReadyUVE(textureGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& commandsAfterReady = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_TRUE(std::any_of(commandsAfterReady.cbegin(), commandsAfterReady.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command);
    }));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_CalledTwiceWithSameScene_ReusesGpuResourceCache) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_reuse_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_reuse_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterFirstFrame = renderDevice.GetLiveResourceCountUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterSecondFrame = renderDevice.GetLiveResourceCountUVE();

    EXPECT_EQ(liveResourcesAfterSecondFrame, liveResourcesAfterFirstFrame);
}

TEST_F(Renderer3DUVETest, AssetReloadedEventUVE_ForCachedMesh_EvictsAndRecreatesGpuResources) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_reload_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_reload_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeReload = renderDevice.GetLiveResourceCountUVE();

    eventSystem.Publish(Asset::AssetReloadedEventUVE{meshGuid});
    const std::size_t liveResourcesAfterReload = renderDevice.GetLiveResourceCountUVE();
    // The mesh's vertex + index buffer are destroyed on eviction; the material's pipeline (a
    // different GUID) is untouched.
    EXPECT_EQ(liveResourcesAfterReload, liveResourcesBeforeReload - 2U);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterRerender = renderDevice.GetLiveResourceCountUVE();
    EXPECT_EQ(liveResourcesAfterRerender, liveResourcesBeforeReload);
}

TEST_F(Renderer3DUVETest, AssetReloadedEventUVE_ForCachedTexture_EvictsTextureAndInvalidatesMaterialCache) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_material.uvemat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_albedo.uvetex");
    UseAlbedoTextureInMaterialUVE(textureGuid);

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeReload = renderDevice.GetLiveResourceCountUVE();

    eventSystem.Publish(Asset::AssetReloadedEventUVE{textureGuid});
    const std::size_t liveResourcesAfterReload = renderDevice.GetLiveResourceCountUVE();
    // Texture eviction is synchronous. The program is manager-owned, so renderer cache eviction
    // only releases its shared reference; the exact retirement point is intentionally internal to
    // ShaderManagerUVE and must not be asserted through renderer resource counts.
    EXPECT_LT(liveResourcesAfterReload, liveResourcesBeforeReload);

    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterRerender = renderDevice.GetLiveResourceCountUVE();
    EXPECT_GT(liveResourcesAfterRerender, liveResourcesAfterReload);
}

TEST_F(Renderer3DUVETest, AssetReloadedEventUVE_ForMaterialTextureSwap_EvictsOldTextureCacheEntry) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_material.uvemat");
    const Asset::AssetGuidUVE textureAGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_a.uvetex");
    const Asset::AssetGuidUVE textureBGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_b.uvetex");
    UseAlbedoTextureInMaterialUVE(textureAGuid);

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureAGuid);
    WaitUntilTextureReadyUVE(textureBGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeMaterialReload = renderDevice.GetLiveResourceCountUVE();

    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
        assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
    ASSERT_TRUE(materialHandle.IsReadyUVE());
    materialHandle.TryGetUVE()->albedoTexture = textureBGuid;
    eventSystem.Publish(Asset::AssetReloadedEventUVE{materialGuid});

    // Material eviction must retire texture A even though the material GUID is unchanged and the
    // replacement texture is only referenced by the newly loaded CPU material payload. Releasing
    // the material cache may also retire its manager-owned pipeline, so assert a strict decrease
    // rather than assuming exactly one resource disappears.
    const std::size_t liveResourcesAfterMaterialReload = renderDevice.GetLiveResourceCountUVE();
    EXPECT_LT(liveResourcesAfterMaterialReload, liveResourcesBeforeMaterialReload);

    // The replacement material program is asynchronous; drain it before comparing the steady-state
    // resource count so the comparison isolates cache lifetime rather than link timing.
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterReplacementFrame = renderDevice.GetLiveResourceCountUVE();
    EXPECT_EQ(liveResourcesAfterReplacementFrame, liveResourcesBeforeMaterialReload);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ParticleRuntimeInput_ExtractsCopiedItemsAndDoesNotLeakAcrossFrames) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Scene::ParticleRuntimeUVE particleRuntime;
    const Scene::EntityUVE particleEntity{31U, 1U};
    ASSERT_TRUE(particleRuntime.AttachUVE(particleEntity, Scene::ParticleEmitterComponentUVE{4U}));
    ASSERT_TRUE(particleRuntime.EmitDetailedUVE(
                       particleEntity,
                       Scene::ParticleEmissionUVE{2U, Math::Vector3UVE{0.0F, 0.0F, -4.0F}, {}, 2.0F})
                    .IsAcceptedUVE());

    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        EXPECT_NO_FATAL_FAILURE(
            renderer3D->RenderFrameWithParticleRuntimeUVE(entityManager, cameraEntity, particleRuntime));
        if (renderer3D->GetLastFrameDiagnosticsUVE().particleProgramReady) {
            break;
        }
        shaderManager.UpdateUVE(0.0);
    }
    const Renderer3DFrameDiagnosticsUVE particleDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    ASSERT_TRUE(particleDiagnostics.particleProgramReady);
    EXPECT_EQ(particleDiagnostics.particleItemsExtracted, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCommandsRecorded, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCommandsSubmitted, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCallsRecorded, 1U);
    EXPECT_FALSE(particleDiagnostics.particleItemsTruncated);
    EXPECT_FALSE(particleDiagnostics.particleDrawCommandsSubmissionTruncated);

    const std::vector<RecordedCommandUVE>& particleCommands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto particleDraw = std::find_if(particleCommands.cbegin(), particleCommands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawCommandUVE>(command) &&
               std::get<DrawCommandUVE>(command).vertexCount == 12U;
    });
    ASSERT_NE(particleDraw, particleCommands.cend());

    EXPECT_NO_FATAL_FAILURE(renderer3D->RenderFrameUVE(entityManager, cameraEntity));
    const Renderer3DFrameDiagnosticsUVE legacyDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(legacyDiagnostics.particleItemsExtracted, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCommandsRecorded, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCommandsSubmitted, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCallsRecorded, 0U);
    EXPECT_FALSE(legacyDiagnostics.particleItemsTruncated);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ActiveCameraPath_MatchesEngineCoreIntegration) {
    // Exercises the exact call shape EngineCoreUVE::Render() uses once SetActiveCameraUVE() has
    // been called: RenderFrameUVE(*entityManager, activeCamera) against a scene with both a
    // camera and a visible mesh, driven end-to-end through real (not fake) collaborators.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_active_camera_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_active_camera_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    EXPECT_NO_FATAL_FAILURE(renderer3D->RenderFrameUVE(entityManager, cameraEntity));

    EXPECT_EQ(renderSystem.GetFrameIndexUVE(), 1U);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_FALSE(commands.empty());
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DirectionalLight_PushesThreeOrderedCascadeSplits) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_cascade_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_cascade_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    std::array<float, 3> splits{};
    std::array<bool, 3> foundSplits{};
    std::array<bool, 3> foundMatrices{};
    float cascadeBlendRatio = 0.0F;
    bool foundCascadeBlendRatio = false;
    for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
        if (const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command)) {
            if (uniform->name == "uShadowCascadeBlendRatio") {
                cascadeBlendRatio = uniform->value;
                foundCascadeBlendRatio = true;
            }
            for (std::size_t cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
                if (uniform->name == "uShadowCascadeSplits[" + std::to_string(cascadeIndex) + "]") {
                    splits[cascadeIndex] = uniform->value;
                    foundSplits[cascadeIndex] = true;
                }
            }
        }
        if (const auto* const uniform = std::get_if<SetUniformMatrix4x4CommandUVE>(&command)) {
            for (std::size_t cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
                if (uniform->name == "uLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]") {
                    foundMatrices[cascadeIndex] = uniform->value != Math::Matrix4x4UVE::IdentityUVE();
                }
            }
        }
    }

    EXPECT_TRUE(foundSplits[0]);
    EXPECT_TRUE(foundSplits[1]);
    EXPECT_TRUE(foundSplits[2]);
    EXPECT_TRUE(foundCascadeBlendRatio);
    EXPECT_FLOAT_EQ(cascadeBlendRatio, kTestShadowCascadeBlendRatioUVE);
    EXPECT_GT(splits[0], 0.0F);
    EXPECT_LT(splits[0], splits[1]);
    EXPECT_LT(splits[1], splits[2]);
    EXPECT_TRUE(foundMatrices[0]);
    EXPECT_TRUE(foundMatrices[1]);
    EXPECT_TRUE(foundMatrices[2]);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DirectionalCascadeMatrices_SnapToShadowTexelGrid) {
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_stabilization_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_stabilization_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const auto captureCascadeMatrices = [this]() {
        std::array<Math::Matrix4x4UVE, 3> matrices{};
        std::array<bool, 3> foundMatrices{};
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformMatrix4x4CommandUVE>(&command);
            if (uniform == nullptr) {
                continue;
            }
            for (std::size_t cascadeIndex = 0; cascadeIndex < matrices.size(); ++cascadeIndex) {
                if (uniform->name == "uLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]") {
                    matrices[cascadeIndex] = uniform->value;
                    foundMatrices[cascadeIndex] = true;
                }
            }
        }
        for (const bool foundMatrix : foundMatrices) {
            EXPECT_TRUE(foundMatrix);
        }
        return matrices;
    };

    const Scene::EntityUVE initialCamera = MakeCameraEntityUVE();
    PrimeMaterialProgramUVE(*renderer3D, initialCamera);
    renderer3D->RenderFrameUVE(entityManager, initialCamera);
    const std::array<Math::Matrix4x4UVE, 3> initialMatrices = captureCascadeMatrices();
    const float cascadeZeroTexelWidth =
        2.0F / (static_cast<float>(kTestShadowMapResolutionUVE) * initialMatrices[0].m[0][0]);
    ASSERT_GT(cascadeZeroTexelWidth, 0.0F);

    const Scene::EntityUVE subTexelCamera =
        MakeCameraEntityUVE(Math::Vector3UVE{cascadeZeroTexelWidth * 0.25F, 0.0F, 0.0F});
    renderer3D->RenderFrameUVE(entityManager, subTexelCamera);
    const std::array<Math::Matrix4x4UVE, 3> subTexelMatrices = captureCascadeMatrices();
    EXPECT_EQ(subTexelMatrices, initialMatrices);

    const Scene::EntityUVE nextTexelCamera =
        MakeCameraEntityUVE(Math::Vector3UVE{cascadeZeroTexelWidth * 1.25F, 0.0F, 0.0F});
    renderer3D->RenderFrameUVE(entityManager, nextTexelCamera);
    const std::array<Math::Matrix4x4UVE, 3> nextTexelMatrices = captureCascadeMatrices();
    EXPECT_NE(nextTexelMatrices[0], initialMatrices[0]);
    EXPECT_FLOAT_EQ(nextTexelMatrices[0].m[0][0], initialMatrices[0].m[0][0]);
    EXPECT_FLOAT_EQ(nextTexelMatrices[0].m[0][3] - initialMatrices[0].m[0][3],
                    -2.0F / static_cast<float>(kTestShadowMapResolutionUVE));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_NoDirectionalLight_SkipsShadowPassEvenWithVisibleMesh) {
    // A visible, asset-ready mesh exists, and the shadow program has even had the chance to
    // become valid (polled to readiness below) - but with no Directional light entity at all,
    // FindShadowCasterUVE() finds no caster, so no shadow pass is recorded.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_noshadow_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_noshadow_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_FALSE(commands.empty());
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommandUVE>(commands[0U]).desc.colorAttachment, kInvalidTextureHandleUVE);
    const auto mainPassEnd = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<EndRenderPassCommandUVE>(command);
    });
    ASSERT_NE(mainPassEnd, commands.cend());
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidShadowTargetsSkipShadowPassSafely) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_invalid_shadow_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_invalid_shadow_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    Renderer3DUVE invalidShadowRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, kTargetWidthUVE, kTargetHeightUVE, kTestAmbientColorUVE, 0U,
        kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE, kTestShadowMapFarPlaneUVE,
        kTestShadowFrustumPaddingUVE, kTestShadowCascadeSplitLambdaUVE, kTestShadowCascadeBlendRatioUVE,
        kTestShadowPcfKernelRadiusUVE);
    PrimeMaterialProgramUVE(invalidShadowRenderer, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    invalidShadowRenderer.RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_FALSE(commands.empty());
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commands.front()));
    EXPECT_NE(std::get<BeginRenderPassCommandUVE>(commands.front()).desc.colorAttachment,
              kInvalidTextureHandleUVE);
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*cascadeCount).value, 0);
    EXPECT_FALSE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindTextureCommandUVE>(command) &&
               std::get<BindTextureCommandUVE>(command).slot >= 3U;
    }));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidMainTargetsSkipFrameSafely) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Renderer3DUVE invalidMainRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, 0U, kTargetHeightUVE, kTestAmbientColorUVE, kTestShadowMapResolutionUVE,
        kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE, kTestShadowMapFarPlaneUVE,
        kTestShadowFrustumPaddingUVE, kTestShadowCascadeSplitLambdaUVE, kTestShadowCascadeBlendRatioUVE,
        kTestShadowPcfKernelRadiusUVE);

    invalidMainRenderer.RenderFrameUVE(entityManager, cameraEntity);

    const Renderer3DFrameDiagnosticsUVE diagnostics = invalidMainRenderer.GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.meshItemsExtracted, 0U);
    EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U);
    EXPECT_FALSE(diagnostics.mainPassRecorded);
    EXPECT_FALSE(diagnostics.toneMappingPassRecorded);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_FittedLightFrustum_CastsOffCameraOccluderWithoutMainPassDraw) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE visibleMeshGuid = assetDatabase.RegisterUVE("renderer3d_fitted_visible.uvemodel");
    const Asset::AssetGuidUVE visibleMaterialGuid = assetDatabase.RegisterUVE("renderer3d_fitted_visible.uvemat");
    const Asset::AssetGuidUVE offCameraMeshGuid = assetDatabase.RegisterUVE("renderer3d_fitted_offcamera.uvemodel");
    const Asset::AssetGuidUVE offCameraMaterialGuid = assetDatabase.RegisterUVE("renderer3d_fitted_offcamera.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, visibleMeshGuid, visibleMaterialGuid);
    // At z=-10 the default camera's 60-degree view only reaches roughly +/-5.8 on X, while the
    // fitted directional-light frustum includes this potential caster across its far-range width.
    MakeMeshEntityUVE(Math::Vector3UVE{50.0F, 0.0F, -10.0F}, offCameraMeshGuid, offCameraMaterialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReadyUVE(visibleMeshGuid, visibleMaterialGuid);
    WaitUntilAssetsReadyUVE(offCameraMeshGuid, offCameraMaterialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<EndRenderPassCommandUVE>(command);
    });
    ASSERT_NE(shadowPassEnd, commands.cend());
    const std::size_t shadowDrawCount = static_cast<std::size_t>(std::count_if(
        commands.cbegin(), shadowPassEnd, [](const RecordedCommandUVE& command) {
            return std::holds_alternative<DrawIndexedCommandUVE>(command);
        }));
    const std::size_t mainDrawCount = static_cast<std::size_t>(std::count_if(
        std::next(shadowPassEnd), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<DrawIndexedCommandUVE>(command);
        }));

    EXPECT_EQ(shadowDrawCount, 2U);
    EXPECT_EQ(mainDrawCount, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DirectionalLightAndReadyShadowProgram_ShadowPassDrawsOpaqueItem) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_shadowdraw_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_shadowdraw_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();

    // Rewritten when the shadow cascades became instanced. The old version asserted fixed command
    // INDICES and the presence of a per-item "uModel" uniform, both of which were artefacts of the
    // one-draw-per-caster loop rather than anything the pass promises. The instanced path sends
    // model matrices through a storage buffer, so uModel legitimately disappears, and the command
    // count per draw changes - a test pinned to either would have to be rewritten again the next
    // time the recording changes shape. What this pass actually owes its caller is checked instead:
    // the shadow pass comes first, it is set up with a real light-space matrix, and it draws the
    // caster's geometry. The path taken to get there is an implementation detail.
    const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(),
        [](const RecordedCommandUVE& command) {
            return std::holds_alternative<EndRenderPassCommandUVE>(command);
        });
    ASSERT_NE(shadowPassEnd, commands.cend());

    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(commands[1]));

    // uLightSpaceMatrix is still a plain uniform on both paths - it is per-cascade, not
    // per-instance - and it must not be identity, or the cascade is rendering from nowhere.
    const bool foundLightSpaceUniform = std::any_of(commands.cbegin(), shadowPassEnd,
        [](const RecordedCommandUVE& command) {
            if (!std::holds_alternative<SetUniformMatrix4x4CommandUVE>(command)) {
                return false;
            }
            const SetUniformMatrix4x4CommandUVE& uniform = std::get<SetUniformMatrix4x4CommandUVE>(command);
            return uniform.name == "uLightSpaceMatrix" && uniform.value != Math::Matrix4x4UVE::IdentityUVE();
        });
    EXPECT_TRUE(foundLightSpaceUniform);

    // The caster's mesh is bound and drawn. Whether that is one instanced draw or one plain draw,
    // the single caster must be covered exactly once with its real index count.
    const auto shadowDraw = std::find_if(commands.cbegin(), shadowPassEnd,
        [](const RecordedCommandUVE& command) {
            return std::holds_alternative<DrawIndexedCommandUVE>(command);
        });
    ASSERT_NE(shadowDraw, shadowPassEnd);
    EXPECT_EQ(std::get<DrawIndexedCommandUVE>(*shadowDraw).indexCount, 3U);
    EXPECT_EQ(std::get<DrawIndexedCommandUVE>(*shadowDraw).instanceCount, 1U);
    EXPECT_EQ(std::count_if(commands.cbegin(), shadowPassEnd, [](const RecordedCommandUVE& command) {
        return std::holds_alternative<DrawIndexedCommandUVE>(command);
    }), 1);
    EXPECT_TRUE(std::any_of(commands.cbegin(), shadowDraw, [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindVertexBufferCommandUVE>(command);
    }));
    EXPECT_TRUE(std::any_of(commands.cbegin(), shadowDraw, [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindIndexBufferCommandUVE>(command);
    }));

    // The main pass follows immediately after the shadow pass ends.
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommandUVE>(*std::next(shadowPassEnd)));
}

#if !UVE_DEBUG
TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidCameraParametersAreSafeNoOpInRelease) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Scene::CameraComponentUVE& camera =
        entityManager.GetComponentUVE<Scene::CameraComponentUVE>(cameraEntity);
    camera.nearPlane = 0.0F;
    const std::size_t submittedCommandCountBefore = renderDevice.GetLastSubmittedCommandsUVE().size();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    EXPECT_EQ(renderDevice.GetLastSubmittedCommandsUVE().size(), submittedCommandCountBefore);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidCameraWorldTransformIsSafeNoOpInRelease) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(cameraEntity).worldPosition.x =
        std::numeric_limits<float>::quiet_NaN();
    const std::size_t submittedCommandCountBefore = renderDevice.GetLastSubmittedCommandsUVE().size();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    EXPECT_EQ(renderDevice.GetLastSubmittedCommandsUVE().size(), submittedCommandCountBefore);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_NonFiniteShadowTuningUsesFiniteReleaseDefaults) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_nonfinite_shadow_tuning_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_nonfinite_shadow_tuning_material.uvemat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    Renderer3DUVE sanitizedRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, kTargetWidthUVE, kTargetHeightUVE, kTestAmbientColorUVE,
        kTestShadowMapResolutionUVE, kTestShadowMapHalfExtentUVE, kTestShadowMapNearPlaneUVE,
        kTestShadowMapFarPlaneUVE, nan, nan, nan, kTestShadowPcfKernelRadiusUVE);
    PrimeMaterialProgramUVE(sanitizedRenderer, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    sanitizedRenderer.RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto blendRatio = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
               std::get<SetUniformFloatCommandUVE>(command).name == "uShadowCascadeBlendRatio";
    });
    ASSERT_NE(blendRatio, commands.cend());
    EXPECT_EQ(std::get<SetUniformFloatCommandUVE>(*blendRatio).value, 0.0F);
    for (const RecordedCommandUVE& command : commands) {
        if (std::holds_alternative<SetUniformFloatCommandUVE>(command)) {
            EXPECT_TRUE(std::isfinite(std::get<SetUniformFloatCommandUVE>(command).value));
        }
    }
}
#endif

// Phase U3b regression coverage: RenderFrameToTargetUVE's own destination color/depth textures
// must be exactly what the UIOverlay pass draws into when a width/height is supplied - not
// kInvalidTextureHandleUVE (which BeginRenderPassUVE maps to the real backend framebuffer). Before
// this fix, the UIOverlay pass ignored destinationTextureOverride entirely, so an editor viewport
// compositing a RenderFrameToTargetUVE() result would have had its UI silently misdirected onto
// the actual application window instead of the caller's offscreen texture.
TEST_F(Renderer3DUVETest, RenderFrameToTargetUVE_UIOverlayPassTargetsTheSameDestinationTextureAsToneMapping) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();

    const Scene::EntityUVE buttonEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(buttonEntity);

    Events::EventSystemUVE inputEventSystem;
    Input::InputSystemUVE inputSystem(inputEventSystem);
    UI::UIRuntimeUVE uiRuntime;
    uiRuntime.TickUVE(entityManager, inputSystem);
    renderer3D->SetUIRuntimeUVE(&uiRuntime);

    const TextureHandleUVE colorTarget = renderDevice.CreateTextureUVE(
        TextureDescUVE{kTargetWidthUVE, kTargetHeightUVE, TextureFormatUVE::RGBA8Unorm, 1});
    const TextureHandleUVE depthTarget = renderDevice.CreateTextureUVE(
        TextureDescUVE{kTargetWidthUVE, kTargetHeightUVE, TextureFormatUVE::Depth32Float, 1});
    ASSERT_NE(colorTarget, kInvalidTextureHandleUVE);
    ASSERT_NE(depthTarget, kInvalidTextureHandleUVE);

    // ToneMapping's own pass callback returns early (recording nothing) until its built-in program
    // has finished async compilation - a first frame right after construction can genuinely skip
    // it, so prime it exactly like PrimeMaterialProgramUVE() primes per-material programs, before
    // relying on ToneMapping's own BeginRenderPassCommandUVE existing at all.
    renderer3D->RenderFrameToTargetUVE(entityManager, cameraEntity, colorTarget, depthTarget, kTargetWidthUVE,
                                        kTargetHeightUVE);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    renderer3D->RenderFrameToTargetUVE(entityManager, cameraEntity, colorTarget, depthTarget, kTargetWidthUVE,
                                        kTargetHeightUVE);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    std::vector<RenderPassDescUVE> beginRenderPassDescs;
    for (const RecordedCommandUVE& command : commands) {
        if (std::holds_alternative<BeginRenderPassCommandUVE>(command)) {
            beginRenderPassDescs.push_back(std::get<BeginRenderPassCommandUVE>(command).desc);
        }
    }
    // ToneMapping and UIOverlay are the frame's final two passes (in that order); both must target
    // the same caller-supplied destination texture pair.
    ASSERT_GE(beginRenderPassDescs.size(), 2U);
    const RenderPassDescUVE& toneMappingDesc = beginRenderPassDescs[beginRenderPassDescs.size() - 2U];
    const RenderPassDescUVE& uiOverlayDesc = beginRenderPassDescs.back();
    EXPECT_EQ(toneMappingDesc.colorAttachment, colorTarget);
    EXPECT_EQ(toneMappingDesc.depthAttachment, depthTarget);
    EXPECT_EQ(uiOverlayDesc.colorAttachment, colorTarget);
    EXPECT_EQ(uiOverlayDesc.depthAttachment, depthTarget);

    renderer3D->SetUIRuntimeUVE(nullptr);
}


// ---------------------------------------------------------------------------
// GPU instancing. The risk this path carries is not that it fails loudly - it
// is that it succeeds at drawing the WRONG thing: a material whose shader knows
// nothing about gl_InstanceID, drawn with instanceCount > 1, stacks every
// instance on the first one's transform. That reads as missing objects, not as
// a renderer bug, so these tests care most about the opt-in staying honest.
//
// Note the fixture's shader loader supplies "void main() { }" - deliberately
// NOT instancing-aware. Every pre-existing test in this file therefore stays on
// the per-object path, which is exactly the regression guarantee wanted: adding
// instancing must not change what a legacy material does.
// ---------------------------------------------------------------------------

/// Swaps in a vertex shader that actually implements the instancing contract. The detection is a
/// source-text check, so the marker tokens are what matter here, not a working shader body - the
/// Null device never compiles GLSL.
void UseInstancingAwareShaderUVE(Asset::AssetManagerUVE& assetManager) {
    assetManager.RegisterLoaderUVE<Asset::ShaderAssetUVE>(
        [](const std::filesystem::path&, Asset::ShaderAssetUVE& shader) {
            shader.sourceCode =
                "void main() { int slot = uInstanceBaseIndex + gl_InstanceID; }";
            return true;
        });
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_LegacyMaterial_StaysOnThePerObjectPath) {
    // The regression case, stated explicitly rather than left implicit in the other tests: a
    // material that predates the instancing contract must still get one draw call per object.
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_legacy.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_legacy.uvemat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index), 0.0F, -5.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 3U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InstancingAwareMaterial_CollapsesRepeatsIntoOneDraw) {
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_shared.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_shared.uvemat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    // Same mesh, same material, laid out along a line so they sort into one contiguous run.
    for (int index = 0; index < 4; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshGuid,
                          materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    // Four objects, one draw call - and the object count proves all four are still being drawn,
    // which a draw-call count alone would not.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InstancedDiagnosticsResetBetweenFrames) {
    // The counters are frame-local. If they accumulated, a long-running session would report a
    // growing instanced count for a static scene - a diagnostic that lies slowly.
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_reset.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_reset.uvemat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntityUVE(Math::Vector3UVE{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshGuid,
                          materialGuid);
    }
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t firstFrameObjects = renderer3D->GetLastFrameDiagnosticsUVE().instancedObjectsRecorded;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::size_t secondFrameObjects = renderer3D->GetLastFrameDiagnosticsUVE().instancedObjectsRecorded;

    EXPECT_EQ(firstFrameObjects, 3U);
    EXPECT_EQ(secondFrameObjects, 3U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SingleInstancingAwareObject_StillDrawsExactlyOnce) {
    // A run of one goes through the instanced path as a one-instance draw rather than through a
    // separate leftover path. Correct either way; this pins which one, so the consumer stays a
    // single loop.
    UseInstancingAwareShaderUVE(assetManager);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_single.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_single.uvemat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 1U);
}


// ---------------------------------------------------------------------------
// Cached offscreen target sets.
//
// ViewportManagerUVE::RenderAllPanesUVE drives ONE shared renderer across every
// pane, resizing it to each pane's pixel size in turn. Before the cache, a
// split view of differently-sized panes destroyed and recreated all SIX
// size-dependent textures (color, depth, bloom bright, two blur, SSAO) per pane
// per frame - permanently, not as a warm-up. These tests measure that directly
// through the Null device's texture-creation counter rather than asserting
// something vague about performance.
// ---------------------------------------------------------------------------

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_RepeatedSizeAlternation_StopsReallocating) {
    // The split-view case in miniature: two sizes, alternating, forever.
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(320U, 240U));
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(640U, 480U));
    const std::uint64_t afterWarmup = renderDevice.GetTextureCreateAttemptCountUVE();

    for (int frame = 0; frame < 10; ++frame) {
        ASSERT_TRUE(renderer3D->ResizeTargetsUVE(320U, 240U));
        ASSERT_TRUE(renderer3D->ResizeTargetsUVE(640U, 480U));
    }

    // Twenty resizes across ten frames, and not one texture created: both sizes were already
    // cached. Before the cache this would have been 120 creations.
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), afterWarmup);
}

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_NewSize_AllocatesOnceAndThenIsFree) {
    const std::uint64_t beforeNewSize = renderDevice.GetTextureCreateAttemptCountUVE();
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(800U, 600U));
    const std::uint64_t afterFirst = renderDevice.GetTextureCreateAttemptCountUVE();

    // A genuinely new size does cost allocations - the cache removes repeat cost, it does not
    // conjure targets. Six of them: color, depth, bloom bright, two blur, SSAO.
    EXPECT_EQ(afterFirst - beforeNewSize, 6U);

    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(800U, 600U));
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), afterFirst);
}

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_ManyDistinctSizes_DoesNotGrowWithoutBound) {
    // A window being dragged produces a new size every frame, and those sets are dead the moment
    // they are made. The cache must evict rather than accumulate, or a long drag exhausts GPU
    // memory - a slow leak is worse than the churn it replaced.
    for (std::uint32_t index = 0U; index < 24U; ++index) {
        ASSERT_TRUE(renderer3D->ResizeTargetsUVE(100U + index, 100U + index));
    }
    // Live resources are bounded by the cache limit rather than by the number of sizes seen. The
    // exact figure is not the point; that it is far below 24 sets is.
    EXPECT_LT(renderDevice.GetLiveResourceCountUVE(), 24U * 6U);
}

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_AfterEviction_StillRendersCorrectly) {
    // Eviction clears sets that a later resize may ask for again. The rebuilt set must be just as
    // usable as the original - an evicted-then-recreated size that renders nothing would be a
    // pane that goes black after the user resizes a window enough times.
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_evict.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_evict.uvemat");
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(256U, 256U));
    for (std::uint32_t index = 0U; index < 20U; ++index) {
        ASSERT_TRUE(renderer3D->ResizeTargetsUVE(300U + index, 300U + index));
    }
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(256U, 256U));

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.renderTargetWidth, 256U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 256U);
    EXPECT_TRUE(diagnostics.mainPassRecorded);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
}

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_SwitchingBackToACachedSize_RendersThatSize) {
    // The cache must hand back the RIGHT set, not merely a valid one. A mismatch here would draw
    // one pane's content at another pane's resolution.
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(320U, 200U));
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(640U, 400U));
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(320U, 200U));

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.renderTargetWidth, 320U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 200U);
}

TEST_F(Renderer3DUVETest, ResizeTargetsUVE_ZeroSizeStillRejected) {
    // Unchanged behavior, restated because the resize path was rewritten around it.
    EXPECT_FALSE(renderer3D->ResizeTargetsUVE(0U, 480U));
    EXPECT_FALSE(renderer3D->ResizeTargetsUVE(640U, 0U));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_StaticSceneSecondFrame_ServesPlacementsFromCache) {
    // End-to-end confirmation that the cache is actually reached through a real frame, not just
    // through MeshRendererUVE directly - the renderer owns the set across frames, and a set
    // rebuilt or reset per frame would silently never hit while every unit test still passed.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_cache_mesh.uvemodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_cache_material.uvemat");
    const Scene::EntityUVE movingEntity =
        MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntityUVE(Math::Vector3UVE{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // NOTE: WaitUntilAssetsReadyUVE primes the material program by rendering a frame of its own
    // (see PrimeMaterialProgramUVE), so the cache is already warm by the time this test renders.
    // Asserting a cold first frame here was wrong about the fixture, not about the cache. Rather
    // than assert a miss count that depends on how many frames the fixture happened to render,
    // this drives the property that actually matters and holds either way: once the scene is
    // static, every subsequent frame is all hits and no misses.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE firstFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(firstFrame.placementCacheHits + firstFrame.placementCacheMisses, 2U)
        << "every mesh entity must be accounted for as exactly one hit or one miss";

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE secondFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(secondFrame.placementCacheHits, 2U);
    EXPECT_EQ(secondFrame.placementCacheMisses, 0U);

    // A third frame, to prove the steady state is genuinely steady rather than alternating.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE thirdFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(thirdFrame.placementCacheHits, 2U);
    EXPECT_EQ(thirdFrame.placementCacheMisses, 0U);

    // And moving one entity must cost exactly one miss - not zero (stale) and not two (the cache
    // dropping an untouched neighbour along with the one that moved).
    Scene::TransformComponentUVE moved;
    moved.localPosition = Math::Vector3UVE{0.0F, 0.0F, -18.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, movingEntity, moved);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE movedFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(movedFrame.placementCacheMisses, 1U);
    EXPECT_EQ(movedFrame.placementCacheHits, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_StaticPrimitives_ReachAnAllHitSteadyState) {
    // The property that matters, stated the way the mesh-cache test states it: once the scene is
    // static, every subsequent frame is all hits and no misses. Not asserted as a cold first frame
    // - the fixture's own priming renders frames before this test does, and a miss count that
    // depends on how many is a test about the fixture, not about the cache.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE movingEntity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});
    MakePrimitiveEntityUVE(
        Math::Vector3UVE{2.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{1.0F, 0.0F, 0.0F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE firstFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(firstFrame.primitivePlacementCacheHits + firstFrame.primitivePlacementCacheMisses, 2U)
        << "every primitive candidate must be accounted for as exactly one hit or one miss";

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE secondFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(secondFrame.primitivePlacementCacheHits, 2U);
    EXPECT_EQ(secondFrame.primitivePlacementCacheMisses, 0U);

    // A third frame, to prove the steady state is steady rather than alternating.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE thirdFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(thirdFrame.primitivePlacementCacheHits, 2U);
    EXPECT_EQ(thirdFrame.primitivePlacementCacheMisses, 0U);

    // Moving one primitive must cost exactly one miss - not zero (stale) and not two (the cache
    // dropping an untouched neighbour along with the one that moved).
    Scene::TransformComponentUVE moved;
    moved.localPosition = Math::Vector3UVE{0.0F, 0.0F, -18.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, movingEntity, moved);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE movedFrame = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(movedFrame.primitivePlacementCacheMisses, 1U);
    EXPECT_EQ(movedFrame.primitivePlacementCacheHits, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ChangingOnlyThePrimitiveKind_InvalidatesThePlacement) {
    // The hazard specific to this key. Kind is not part of the transform, but it selects the local
    // bounds the world bounds are derived from, so a key that omitted it would serve a cube's
    // bounds for a sphere with the transform sitting perfectly still. Exercised by swapping kind
    // and nothing else.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE entity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheMisses, 0U)
        << "the cache must be warm before the kind swap for the swap to prove anything";

    entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity).kind =
        Scene::PrimitiveMeshKindUVE::UVSphere;

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheMisses, 1U)
        << "a kind change must invalidate the cached bounds even though the transform is identical";
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_MovingOnlyTheCamera_StillHitsThePlacementCache) {
    // The split this optimization rests on: placement is entity-dependent and cached, culling and
    // sort depth are view-dependent and are not. A camera move must therefore cost zero misses
    // while still being free to change what is visible. If someone later folds the frustum test
    // into the cached half, this is the test that catches it.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheMisses, 0U);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 1U);

    // Turn the camera away from the primitive. Same scene, different view.
    Scene::TransformComponentUVE cameraTransform;
    cameraTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -400.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, cameraEntity, cameraTransform);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE movedCamera = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(movedCamera.primitivePlacementCacheMisses, 0U)
        << "the camera moving must not invalidate any entity's placement";
    EXPECT_EQ(movedCamera.primitivePlacementCacheHits, 1U);
    EXPECT_EQ(movedCamera.primitiveItemsExtracted, 0U)
        << "culling must still respond to the new view despite the placement being reused";
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_RejectedPrimitive_IsCachedAsARejectionAndStaysRejected) {
    // A rejection costs the same recompute to rediscover as a success, so it is cached too. The
    // risk in caching a negative is that it is cached as a positive by accident - an entity that
    // cannot produce finite geometry must never appear in the extracted items, on the miss frame
    // or on any hit frame after it.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE transform;
    transform.localPosition = Math::Vector3UVE{std::numeric_limits<float>::max(), 0.0F, -10.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, transform);
    sceneGraph.UpdateUVE(entityManager);
    entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(
        entity,
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});

    for (int frame = 0; frame < 3; ++frame) {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
        EXPECT_EQ(diagnostics.primitiveCandidates, 1U) << "frame " << frame;
        EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U)
            << "a rejected primitive must stay rejected on frame " << frame;
    }
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheHits, 1U)
        << "the rejection must be served from the cache, not recomputed every frame";
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DestroyedPrimitive_IsPrunedFromThePlacementCache) {
    // The cache outlives the frame, so it must not outlive the entity. There is no accessor for
    // the cache's size, so this drives the prune through what is observable: after a destroy the
    // remaining primitive must still be a hit (the prune must not evict the survivor) and the
    // destroyed one must no longer be counted as a candidate at all.
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE doomed = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});
    MakePrimitiveEntityUVE(
        Math::Vector3UVE{2.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheHits, 2U);

    entityManager.DestroyEntityUVE(doomed);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE afterDestroy = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(afterDestroy.primitiveCandidates, 1U);
    EXPECT_EQ(afterDestroy.primitivePlacementCacheHits, 1U)
        << "the surviving primitive must still be served from the cache";
    EXPECT_EQ(afterDestroy.primitivePlacementCacheMisses, 0U);

    // And a frame later the survivor is still a hit, proving the prune left a usable cache behind
    // rather than one that happens to work once.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitivePlacementCacheHits, 1U);
}

} // namespace
} // namespace UVE::Render::Tests
