// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/renderer_3d_uve.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/visibility_component_uve.h"

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/asset_reloaded_event_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_compression_uve.h"
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
#include "uve/component/canvas_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/occluder_3d_uve.h"
#include "uve/objects/3d/reflection_probe_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
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
        vertexShaderGuid = assetDatabase.RegisterUVE("renderer3d_tests_vertex.uvshader");
        fragmentShaderGuid = assetDatabase.RegisterUVE("renderer3d_tests_fragment.uvshader");

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
                material.albedoColor = Math::ColorUVE{0.2F, 0.4F, 0.6F};
                material.metallic = 0.25F;
                material.roughness = 0.75F;
                material.emissiveColor = Math::ColorUVE{0.1F, 0.0F, 0.0F};
                return true;
            });
        // Default texture loader: a small, always-ready 2x2 RGBA8Unorm texture, reused by any
        // test that assigns a real texture GUID to a material without needing custom pixel data.
        assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
            [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
                texture.width = 2;
                texture.height = 2;
                texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
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
        // Renderer3DUVE also requests tone-mapping and other built-ins at fixture construction.
        // Drain those jobs too so command-buffer tests do not race asynchronous shader compilation.
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            shaderManager.UpdateUVE(0.0);
            if (shaderManager.GetPendingJobCountUVE() == 0U) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);
    }
};

TEST_F(Renderer3DUVETest, ReflectionProbeResolutionControlsCubeTargetAndRecapturesOnTierChange) {
    const Scene::EntityUVE camera = MakeCameraEntityUVE();
    const Scene::EntityUVE probeEntity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, probeEntity, Scene::TransformComponentUVE{});
    Scene::ReflectionProbe3DComponentUVE probe{};
    probe.resolution = Scene::ReflectionProbeResolutionUVE::High;
    probe.capturedOnce = true;
    probe.captureGeneration = 7U;
    entityManager.AddComponentUVE<Scene::ReflectionProbe3DComponentUVE>(probeEntity, probe);
    const Scene::EntityUVE mediumProbeEntity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, mediumProbeEntity, Scene::TransformComponentUVE{});
    Scene::ReflectionProbe3DComponentUVE mediumProbe{};
    mediumProbe.resolution = Scene::ReflectionProbeResolutionUVE::Medium;
    mediumProbe.capturedOnce = true;
    mediumProbe.captureGeneration = 3U;
    entityManager.AddComponentUVE<Scene::ReflectionProbe3DComponentUVE>(mediumProbeEntity, mediumProbe);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, camera);
    std::vector<TextureDescUVE> liveTextureDescs = renderDevice.GetLiveTextureDescsUVE();
    // Tier 2.3: each probe owns one six-layer cubemap (not six per-face 2D textures).
    const auto countProbeCubesAt = [&liveTextureDescs](const std::uint32_t resolution) {
        return std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [resolution](const auto& desc) {
            return desc.width == resolution && desc.height == resolution &&
                   desc.format == TextureFormatUVE::RGBA8Unorm &&
                   desc.type == TextureTypeUVE::Cubemap && desc.arrayLayers == 6U;
        });
    };
    EXPECT_EQ(countProbeCubesAt(256U), 1) << "a reflection probe owns one six-layer cubemap";
    EXPECT_EQ(countProbeCubesAt(128U), 1) << "probes in one frame retain their own resolution tier";
    EXPECT_GE(std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
                  return desc.width == 128U && desc.height == 128U &&
                         desc.format == TextureFormatUVE::Depth32Float;
              }), 2)
        << "a cached per-tier capture depth target survives another probe's capture";
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
        return desc.width == 256U && desc.height == 256U && desc.format == TextureFormatUVE::Depth32Float;
    })) << "the capture depth target must match the selected face resolution";

    // The capture generation is intentionally unchanged: changing only the authored tier must
    // invalidate/reallocate the probe cubemap and capture depth, then render the new size.
    entityManager.GetComponentUVE<Scene::ReflectionProbe3DComponentUVE>(probeEntity).resolution =
        Scene::ReflectionProbeResolutionUVE::Ultra;
    renderer3D->RenderFrameUVE(entityManager, camera);
    liveTextureDescs = renderDevice.GetLiveTextureDescsUVE();
    EXPECT_EQ(countProbeCubesAt(512U), 1) << "resolution edits recapture instead of leaving stale probe data";
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
        return desc.width == 512U && desc.height == 512U && desc.format == TextureFormatUVE::Depth32Float;
    }));
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ADecalOnAMeshIsProjectedAndReportedInTheFrameDiagnostics) {
    // The decal pass end to end through the frame the renderer actually builds: the wall's assets
    // are registered the way every other mesh in these tests registers them, the decal authors the
    // same material by PATH (which is what a Decal3D stores), and the numbers below are read from
    // the frame diagnostics the renderer publishes rather than from the pass directly. If the pass
    // were not wired into RenderFrameUVE(), every one of these would be zero.
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_decal_wall.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_decal_scorch.uvmat");
    const Scene::EntityUVE camera = MakeCameraEntityUVE();
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);

    const Scene::EntityUVE decalEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE decalLocal;
    // The mesh is a unit cube; a 1 m decal 0.4 in front of it reaches its camera-facing face.
    decalLocal.localPosition = Math::Vector3UVE{0.0F, 0.0F, -4.4F};
    sceneGraph.AttachTransformUVE(entityManager, decalEntity, decalLocal);
    Scene::Decal3DComponentUVE decal;
    decal.materialAssetPath = "renderer3d_tests_decal_scorch.uvmat";
    decal.size = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    entityManager.AddComponentUVE<Scene::Decal3DComponentUVE>(decalEntity, decal);
    sceneGraph.UpdateUVE(entityManager);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, camera);

    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.decalsConsidered, 1U);
    EXPECT_EQ(diagnostics.decalDrawsExtracted, 1U) << "the decal reached the mesh standing in front of it";
    EXPECT_EQ(diagnostics.decalPatchesExtracted, 1U) << "one face of the cube is inside the volume";
    EXPECT_EQ(diagnostics.decalTrianglesExtracted, 2U);
    EXPECT_EQ(diagnostics.decalsWithoutReceivers, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_ADecalIsHandedToTheGpuRatherThanOnlyCounted) {
    // The diagnostics above prove the projection pass found geometry. This proves the other half of
    // the item: that the geometry reaches the GPU - a bind of the decal's own vertex and index
    // buffers, the volume's units matrix as a uniform, and one indexed draw of the patch's two
    // triangles. Until this existed a decal was extracted, counted, and never seen.
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_decal_wall.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_decal_scorch.uvmat");
    const Scene::EntityUVE camera = MakeCameraEntityUVE();
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);

    const Scene::EntityUVE decalEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE decalLocal;
    decalLocal.localPosition = Math::Vector3UVE{0.0F, 0.0F, -4.4F};
    sceneGraph.AttachTransformUVE(entityManager, decalEntity, decalLocal);
    Scene::Decal3DComponentUVE decal;
    decal.materialAssetPath = "renderer3d_tests_decal_scorch.uvmat";
    decal.size = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    entityManager.AddComponentUVE<Scene::Decal3DComponentUVE>(decalEntity, decal);
    sceneGraph.UpdateUVE(entityManager);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    // The built-in decal program links asynchronously, like every other built-in: the first frame
    // extracts and queues it, the drain finishes the link, the next frame can draw with it.
    renderer3D->RenderFrameUVE(entityManager, camera);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        shaderManager.UpdateUVE(0.0);
        if (shaderManager.GetPendingJobCountUVE() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCountUVE(), 0U);

    renderer3D->RenderFrameUVE(entityManager, camera);

    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.decalDrawsExtracted, 1U);
    EXPECT_EQ(diagnostics.decalDrawCallsRecorded, 1U) << "an extracted decal must reach the GPU";
    EXPECT_EQ(diagnostics.decalDrawsDropped, 0U);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto decalDraw = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        // A quad patch fans into two triangles, so six indices - and the meshes in this scene draw
        // with their own index counts, which is what makes six the decal's alone.
        return std::holds_alternative<DrawIndexedCommandUVE>(command) &&
               std::get<DrawIndexedCommandUVE>(command).indexCount == 6U;
    });
    ASSERT_NE(decalDraw, commands.cend()) << "no six-index draw was recorded";
    EXPECT_TRUE(std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
                    return std::holds_alternative<BindVertexBufferCommandUVE>(command);
                }) != commands.cend())
        << "the decal's own vertex buffer was never bound";
    const auto worldToUnit = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformMatrix4x4CommandUVE>(command) &&
               std::get<SetUniformMatrix4x4CommandUVE>(command).name == "uWorldToUnit";
    });
    ASSERT_NE(worldToUnit, commands.cend())
        << "the volume's coordinate space must reach the shader, or the fades are evaluated in the "
           "wrong space";
    // The decal program samples its material's albedo from the renderer's albedo slot (0, the same
    // one the mesh materials bind to); this material has no texture, so what is bound there is the
    // white fallback, and the flat colour and full alpha it leaves behind are the point - an
    // untextured decal still paints.
    const auto albedoSampler = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uAlbedoTexture" &&
               std::get<SetUniformIntCommandUVE>(command).value == 0;
    });
    EXPECT_NE(albedoSampler, commands.cend()) << "the decal program must be told where its texture is";
}

TEST_F(Renderer3DUVETest, FastApproximateAAIsAppliedByToneMappingWithClampedQualityUniforms) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    PostProcessSettingsUVE settings;
    settings.fastApproximateAAEnabledUVE = true;
    settings.fastApproximateAAQualityUVE = 99U; // Renderer clamps untrusted API callers to High.
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    ASSERT_TRUE(diagnostics.toneMappingPassRecorded);
    EXPECT_TRUE(diagnostics.fastApproximateAAPassApplied);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto aaEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uFastApproximateAA";
    });
    ASSERT_NE(aaEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*aaEnabled).value, 1);
    const auto aaQuality = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uScreenSpaceAAQuality";
    });
    ASSERT_NE(aaQuality, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*aaQuality).value, 2);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
               std::get<SetUniformFloatCommandUVE>(command).name == "uFxaaTexelX" &&
               std::get<SetUniformFloatCommandUVE>(command).value > 0.0F;
    }));
}

TEST_F(Renderer3DUVETest, SharpeningAmountIsClampedAndSentToTheFinalPass) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    const auto captureSharpeningAmount = [this, cameraEntity]() -> std::optional<float> {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
            if (uniform != nullptr && uniform->name == "uSharpeningAmount") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    PostProcessSettingsUVE settings;
    settings.sharpeningAmountUVE = 0.6F;
    renderer3D->SetPostProcessSettingsUVE(settings);
    const std::optional<float> configuredAmount = captureSharpeningAmount();
    ASSERT_TRUE(configuredAmount.has_value());
    EXPECT_FLOAT_EQ(*configuredAmount, 0.6F);

    settings.sharpeningAmountUVE = 1.5F; // Direct API callers are clamped to the supported range.
    renderer3D->SetPostProcessSettingsUVE(settings);
    const std::optional<float> clampedAmount = captureSharpeningAmount();
    ASSERT_TRUE(clampedAmount.has_value());
    EXPECT_FLOAT_EQ(*clampedAmount, 1.0F);
}

TEST_F(Renderer3DUVETest, DitheringToggleReachesTheFinalPass) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    const auto captureDitheringToggle = [this, cameraEntity]() -> std::optional<std::int32_t> {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformIntCommandUVE>(&command);
            if (uniform != nullptr && uniform->name == "uDitheringEnabled") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    PostProcessSettingsUVE settings;
    settings.ditheringEnabledUVE = true;
    renderer3D->SetPostProcessSettingsUVE(settings);
    const std::optional<std::int32_t> enabled = captureDitheringToggle();
    ASSERT_TRUE(enabled.has_value());
    EXPECT_EQ(*enabled, 1);

    settings.ditheringEnabledUVE = false;
    renderer3D->SetPostProcessSettingsUVE(settings);
    const std::optional<std::int32_t> disabled = captureDitheringToggle();
    ASSERT_TRUE(disabled.has_value());
    EXPECT_EQ(*disabled, 0);
}

TEST_F(Renderer3DUVETest, NearestUpscalingIsSelectedByTheToneMappingShader) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    PostProcessSettingsUVE settings;
    settings.nearestUpscalingEnabledUVE = true;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto upscalingMethod = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uNearestUpscaling";
    });
    ASSERT_NE(upscalingMethod, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*upscalingMethod).value, 1);
}

TEST_F(Renderer3DUVETest, LinearClampToneMappingIsSelectedByTheToneMappingShader) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    PostProcessSettingsUVE settings;
    settings.acesToneMappingEnabledUVE = false;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto toneMappingMethod = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uToneMappingMethod";
    });
    ASSERT_NE(toneMappingMethod, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*toneMappingMethod).value, 0);
}

TEST_F(Renderer3DUVETest, SSAORadiusIntensityPowerQualityAndBlurReachTheirShaderPasses) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    WaitUntilShadowProgramReadyUVE();

    PostProcessSettingsUVE settings;
    settings.ssaoRadiusUVE = 0.8F;
    settings.ssaoIntensityUVE = 1.7F;
    settings.ssaoPowerUVE = 2.0F;
    settings.ssaoQualityUVE = 0U; // Low maps to four kernel samples.
    settings.ssaoBlurEnabledUVE = true;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    EXPECT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().ssaoPassRecorded);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto floatUniform = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
                   std::get<SetUniformFloatCommandUVE>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommandUVE>(*found).value};
    };
    const auto intUniform = [&commands](const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<std::int32_t>{std::get<SetUniformIntCommandUVE>(*found).value};
    };
    ASSERT_TRUE(floatUniform("uRadius").has_value());
    ASSERT_TRUE(floatUniform("uIntensity").has_value());
    ASSERT_TRUE(floatUniform("uPower").has_value());
    ASSERT_TRUE(intUniform("uSampleCount").has_value());
    ASSERT_TRUE(intUniform("uSsaoBlurEnabled").has_value());
    EXPECT_FLOAT_EQ(*floatUniform("uRadius"), 0.8F);
    EXPECT_FLOAT_EQ(*floatUniform("uIntensity"), 1.7F);
    EXPECT_FLOAT_EQ(*floatUniform("uPower"), 2.0F);
    EXPECT_EQ(*intUniform("uSampleCount"), 4);
    EXPECT_EQ(*intUniform("uSsaoBlurEnabled"), 1);

    settings.ssaoQualityUVE = 99U; // Untrusted API quality is clamped to High (12 samples).
    settings.ssaoBlurEnabledUVE = false;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& highQualityCommands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto highSampleCount = std::find_if(highQualityCommands.cbegin(), highQualityCommands.cend(),
                                               [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uSampleCount";
    });
    ASSERT_NE(highSampleCount, highQualityCommands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*highSampleCount).value, 12);
    const auto disabledBlur = std::find_if(highQualityCommands.cbegin(), highQualityCommands.cend(),
                                            [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uSsaoBlurEnabled";
    });
    ASSERT_NE(disabledBlur, highQualityCommands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*disabledBlur).value, 0);
}

TEST_F(Renderer3DUVETest, WorldEnvironmentBloomSoftKneeReachesTheBrightPassShader) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.ssaoEnabled = false;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);

    const auto readBloomSoftKnee = [this]() -> std::optional<float> {
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
            if (uniform != nullptr && uniform->name == "uBloomSoftKnee") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    // Bloom's built-in shader is compiled asynchronously. Render and service shader jobs until the
    // bright-pass is recorded, then verify that the default still selects the former hard threshold.
    bool bloomPassRecorded = false;
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        if (renderer3D->GetLastFrameDiagnosticsUVE().bloomPassRecorded) {
            bloomPassRecorded = true;
            break;
        }
        shaderManager.UpdateUVE(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(bloomPassRecorded);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().bloomMipCountUsed, 1U);
    const std::optional<float> defaultSoftKnee = readBloomSoftKnee();
    ASSERT_TRUE(defaultSoftKnee.has_value());
    EXPECT_FLOAT_EQ(*defaultSoftKnee, 0.0F);

    // An edit to the serialized environment setting must change what the same bright-pass shader
    // receives on the next frame, not just alter inspector metadata.
    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).bloomSoftKnee = 0.65F;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::optional<float> configuredSoftKnee = readBloomSoftKnee();
    ASSERT_TRUE(configuredSoftKnee.has_value());
    EXPECT_FLOAT_EQ(*configuredSoftKnee, 0.65F);
}

TEST_F(Renderer3DUVETest, WorldEnvironmentBloomMipCountCreatesAndRunsDistinctScaleTargets) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.bloomMipCount = 3U;
    environment.ssaoEnabled = false;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);

    // The downsample program is asynchronous too. Require the frame to report three used levels,
    // rather than treating a merely authored or allocated count as evidence that the pyramid ran.
    bool requestedMipCountUsed = false;
    for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        if (renderer3D->GetLastFrameDiagnosticsUVE().bloomMipCountUsed == 3U) {
            requestedMipCountUsed = true;
            break;
        }
        shaderManager.UpdateUVE(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(requestedMipCountUsed);
    EXPECT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().bloomPassRecorded);

    const std::vector<TextureDescUVE> liveTextureDescs = renderDevice.GetLiveTextureDescsUVE();
    const auto countHdrTargetsAt = [&liveTextureDescs](const std::uint32_t width, const std::uint32_t height) {
        return std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [width, height](const auto& desc) {
            return desc.width == width && desc.height == height &&
                   (desc.format == TextureFormatUVE::RGBA16Float || desc.format == TextureFormatUVE::RGBA8Unorm);
        });
    };
    EXPECT_GE(countHdrTargetsAt(16U, 16U), 3)
        << "the first downsample level owns bright and two separable-blur targets";
    EXPECT_GE(countHdrTargetsAt(8U, 8U), 3)
        << "the second downsample level owns its own bright and blur targets";

    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).bloomMipCount = 1U;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().bloomMipCountUsed, 1U)
        << "lowering the authored count immediately switches back to one-scale bloom";
}

TEST_F(Renderer3DUVETest, WorldEnvironmentSSAOEnableRadiusAndIntensityOverridesRemainActive) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.ssaoEnabled = true;
    environment.ssaoRadius = 0.65F;
    environment.ssaoIntensity = 1.4F;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);
    WaitUntilShadowProgramReadyUVE();

    PostProcessSettingsUVE settings;
    settings.ssaoRadiusUVE = 1.8F;
    settings.ssaoIntensityUVE = 2.6F;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().ssaoPassRecorded);

    std::optional<float> radius;
    std::optional<float> intensity;
    for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
        const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
        if (uniform == nullptr) {
            continue;
        }
        if (uniform->name == "uRadius") {
            radius = uniform->value;
        } else if (uniform->name == "uIntensity") {
            intensity = uniform->value;
        }
    }
    ASSERT_TRUE(radius.has_value());
    ASSERT_TRUE(intensity.has_value());
    EXPECT_FLOAT_EQ(*radius, environment.ssaoRadius);
    EXPECT_FLOAT_EQ(*intensity, environment.ssaoIntensity);

    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).ssaoEnabled = false;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnosticsUVE().ssaoPassRecorded);

    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).ssaoEnabled = true;
    settings.ssaoEnabledUVE = false;
    renderer3D->SetPostProcessSettingsUVE(settings);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnosticsUVE().ssaoPassRecorded);
}

TEST_F(Renderer3DUVETest, WorldEnvironmentFogControlsAndInverseProjectionReachToneMapping) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.fogEnabled = true;
    environment.fogMode = Scene::WorldEnvironmentFogModeUVE::Linear;
    environment.fogStart = 17.0F;
    environment.fogEnd = 275.0F;
    environment.ssaoEnabled = false;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto fogEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uFogEnabled";
    });
    ASSERT_NE(fogEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*fogEnabled).value, 1);
    const auto fogMode = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uFogMode";
    });
    ASSERT_NE(fogMode, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*fogMode).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentFogModeUVE::Linear));
    const auto uniformValue = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
                   std::get<SetUniformFloatCommandUVE>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommandUVE>(*found).value};
    };
    ASSERT_TRUE(uniformValue("uFogStart").has_value());
    ASSERT_TRUE(uniformValue("uFogEnd").has_value());
    EXPECT_FLOAT_EQ(*uniformValue("uFogStart"), 17.0F);
    EXPECT_FLOAT_EQ(*uniformValue("uFogEnd"), 275.0F);

    const auto inverseProjectionUniform =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformMatrix4x4CommandUVE>(command) &&
                   std::get<SetUniformMatrix4x4CommandUVE>(command).name == "uInverseProjection";
        });
    ASSERT_NE(inverseProjectionUniform, commands.cend());
    const Math::Matrix4x4UVE projection =
        cameraSystem.ComputeProjectionMatrixUVE(entityManager, cameraEntity, 1.0F);
    Math::Matrix4x4UVE expectedInverseProjection{};
    ASSERT_TRUE(Math::TryInverseUVE(projection, expectedInverseProjection));
    EXPECT_EQ(std::get<SetUniformMatrix4x4CommandUVE>(*inverseProjectionUniform).value,
              expectedInverseProjection);
}

TEST_F(Renderer3DUVETest, WorldEnvironmentLensControlsReachToneMappingAndRespectPostProcessingToggle) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.bloomEnabled = false;
    environment.ssaoEnabled = false;
    environment.vignetteIntensity = 0.7F;
    environment.vignetteRadius = 0.55F;
    environment.chromaticAberrationIntensity = 0.4F;
    environment.filmGrainIntensity = 0.6F;
    environment.lensDistortionIntensity = 0.5F;
    environment.depthOfFieldEnabled = true;
    environment.depthOfFieldFocusMode = Scene::WorldEnvironmentDepthOfFieldFocusModeUVE::ScreenCenter;
    environment.depthOfFieldBokehShape = Scene::WorldEnvironmentDepthOfFieldBokehShapeUVE::Hexagonal;
    environment.depthOfFieldFocusDistance = 24.0F;
    environment.depthOfFieldAperture = 0.8F;
    environment.depthOfFieldQuality = 2U;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);
    WaitUntilShadowProgramReadyUVE();

    const auto floatUniformValue = [](const std::vector<RecordedCommandUVE>& commands,
                                      const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
                   std::get<SetUniformFloatCommandUVE>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommandUVE>(*found).value};
    };
    const auto intUniformValue = [](const std::vector<RecordedCommandUVE>& commands,
                                    const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<std::int32_t>{std::get<SetUniformIntCommandUVE>(*found).value};
    };

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& enabledCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uVignetteIntensity").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uVignetteRadius").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uChromaticAberrationIntensity").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uFilmGrainIntensity").has_value());
    ASSERT_TRUE(intUniformValue(enabledCommands, "uFilmGrainFrame").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uLensDistortionIntensity").has_value());
    ASSERT_TRUE(intUniformValue(enabledCommands, "uDepthOfFieldEnabled").has_value());
    ASSERT_TRUE(intUniformValue(enabledCommands, "uDepthOfFieldFocusMode").has_value());
    ASSERT_TRUE(intUniformValue(enabledCommands, "uDepthOfFieldBokehShape").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uDepthOfFieldFocusDistance").has_value());
    ASSERT_TRUE(floatUniformValue(enabledCommands, "uDepthOfFieldAperture").has_value());
    ASSERT_TRUE(intUniformValue(enabledCommands, "uDepthOfFieldQuality").has_value());
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uVignetteIntensity"), 0.7F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uVignetteRadius"), 0.55F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uChromaticAberrationIntensity"), 0.4F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uFilmGrainIntensity"), 0.6F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uLensDistortionIntensity"), 0.5F);
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldEnabled"), 1);
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldFocusMode"),
              static_cast<std::int32_t>(Scene::WorldEnvironmentDepthOfFieldFocusModeUVE::ScreenCenter));
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldBokehShape"),
              static_cast<std::int32_t>(Scene::WorldEnvironmentDepthOfFieldBokehShapeUVE::Hexagonal));
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uDepthOfFieldFocusDistance"), 24.0F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uDepthOfFieldAperture"), 0.8F);
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldQuality"), 2);
    const std::int32_t enabledGrainFrame = *intUniformValue(enabledCommands, "uFilmGrainFrame");

    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).postProcessingEnabled =
        false;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& disabledCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uVignetteIntensity").has_value());
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uChromaticAberrationIntensity").has_value());
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uFilmGrainIntensity").has_value());
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uLensDistortionIntensity").has_value());
    ASSERT_TRUE(intUniformValue(disabledCommands, "uDepthOfFieldEnabled").has_value());
    ASSERT_TRUE(intUniformValue(disabledCommands, "uFilmGrainFrame").has_value());
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uVignetteIntensity"), 0.0F);
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uChromaticAberrationIntensity"), 0.0F);
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uFilmGrainIntensity"), 0.0F);
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uLensDistortionIntensity"), 0.0F);
    EXPECT_EQ(*intUniformValue(disabledCommands, "uDepthOfFieldEnabled"), 0);
    const std::int32_t disabledGrainFrame = *intUniformValue(disabledCommands, "uFilmGrainFrame");
    EXPECT_EQ((disabledGrainFrame - enabledGrainFrame + 4096) % 4096, 1);
}

TEST_F(Renderer3DUVETest, WorldEnvironmentMotionBlurUsesOnlyValidCameraHistory) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.motionBlurEnabled = true;
    environment.motionBlurStrength = 0.75F;
    environment.motionBlurSampleCount = 12U;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);

    const auto intUniformValue = [](const std::vector<RecordedCommandUVE>& commands,
                                    const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<std::int32_t>{std::get<SetUniformIntCommandUVE>(*found).value};
    };
    const auto floatUniformValue = [](const std::vector<RecordedCommandUVE>& commands,
                                      const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
                   std::get<SetUniformFloatCommandUVE>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommandUVE>(*found).value};
    };
    const auto matrixUniformValue = [](const std::vector<RecordedCommandUVE>& commands,
                                       const std::string_view name) -> std::optional<Math::Matrix4x4UVE> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformMatrix4x4CommandUVE>(command) &&
                   std::get<SetUniformMatrix4x4CommandUVE>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<Math::Matrix4x4UVE>{std::get<SetUniformMatrix4x4CommandUVE>(*found).value};
    };
    const auto currentViewProjection = [this, cameraEntity]() -> Math::Matrix4x4UVE {
        constexpr std::uint32_t width = kTargetWidthUVE;
        constexpr std::uint32_t height = kTargetHeightUVE;
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        return cameraSystem.ComputeViewProjectionUVE(entityManager, cameraEntity, aspectRatio);
    };

    WaitUntilShadowProgramReadyUVE();

    // The first tone-mapped frame has no prior camera, so no motion vector is trustworthy yet.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& firstCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(intUniformValue(firstCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(firstCommands, "uMotionBlurStrength").has_value());
    ASSERT_TRUE(intUniformValue(firstCommands, "uMotionBlurSampleCount").has_value());
    ASSERT_TRUE(matrixUniformValue(firstCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(firstCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(firstCommands, "uMotionBlurStrength"), 0.0F);
    EXPECT_EQ(*intUniformValue(firstCommands, "uMotionBlurSampleCount"), 12);
    const Math::Matrix4x4UVE staticViewProjection = currentViewProjection();
    EXPECT_EQ(*matrixUniformValue(firstCommands, "uPreviousViewProjection"), staticViewProjection);

    // A second frame from the same camera has history, so the authored blur reaches the shader.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& staticCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(intUniformValue(staticCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(staticCommands, "uMotionBlurStrength").has_value());
    ASSERT_TRUE(matrixUniformValue(staticCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(staticCommands, "uMotionBlurEnabled"), 1);
    EXPECT_FLOAT_EQ(*floatUniformValue(staticCommands, "uMotionBlurStrength"), 0.75F);
    EXPECT_EQ(*matrixUniformValue(staticCommands, "uPreviousViewProjection"), staticViewProjection);

    // Moving the camera keeps the prior frame's matrix, which is what makes blur possible.
    Math::QuaternionUVE rotatedCamera{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.35F, rotatedCamera));
    Scene::TransformComponentUVE movedTransform =
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(cameraEntity);
    movedTransform.localRotation = rotatedCamera;
    sceneGraph.SetLocalTransformUVE(entityManager, cameraEntity, movedTransform);
    sceneGraph.UpdateUVE(entityManager);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& movedCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(intUniformValue(movedCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(matrixUniformValue(movedCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(movedCommands, "uMotionBlurEnabled"), 1);
    EXPECT_EQ(*matrixUniformValue(movedCommands, "uPreviousViewProjection"), staticViewProjection);
    EXPECT_NE(*matrixUniformValue(movedCommands, "uPreviousViewProjection"), currentViewProjection());

    // A target aspect change invalidates the old reprojection rather than stretching stale vectors.
    ASSERT_TRUE(renderer3D->ResizeTargetsUVE(kTargetWidthUVE * 2U, kTargetHeightUVE + 32U));
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& resizedCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(intUniformValue(resizedCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(resizedCommands, "uMotionBlurStrength").has_value());
    EXPECT_EQ(*intUniformValue(resizedCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(resizedCommands, "uMotionBlurStrength"), 0.0F);

    // Disabling the post-processing master switch suppresses blur even with fresh history.
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).postProcessingEnabled =
        false;
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnosticsUVE().toneMappingPassRecorded);
    const std::vector<RecordedCommandUVE>& disabledCommands = renderDevice.GetLastSubmittedCommandsUVE();
    ASSERT_TRUE(intUniformValue(disabledCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uMotionBlurStrength").has_value());
    EXPECT_EQ(*intUniformValue(disabledCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uMotionBlurStrength"), 0.0F);
}

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

TEST_F(Renderer3DUVETest, RenderFrameUVE_HiddenPrimitiveIsNotDrawnAndReturnsWhenShown) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE placed;
    placed.localPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransformUVE(entityManager, parent, placed);
    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(parent, Scene::VisibilityComponentUVE{});
    const Scene::EntityUVE primitiveEntity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, primitiveEntity, Scene::TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, primitiveEntity, parent);
    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(primitiveEntity, Scene::VisibilityComponentUVE{});
    entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(primitiveEntity);
    sceneGraph.UpdateUVE(entityManager);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);

    // Its own switch off: not even a candidate.
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(primitiveEntity).visible = false;
    sceneGraph.UpdateUVE(entityManager);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveDrawCallsRecorded, 0U);

    // Shown again, but its parent hidden: still not drawn - hiding a parent hides the subtree.
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(primitiveEntity).visible = true;
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(parent).visible = false;
    sceneGraph.UpdateUVE(entityManager);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 0U);

    // Parent shown again: the primitive comes back.
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(parent).visible = true;
    sceneGraph.UpdateUVE(entityManager);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);
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

TEST_F(Renderer3DUVETest, RenderFrameUVE_PartitionHiddenPrimitiveIsNotACandidate) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE partition = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, partition, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::WorldPartition3DComponentUVE>(partition);
    sceneGraph.UpdateUVE(entityManager);

    const Scene::EntityUVE hidden = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});
    const Scene::EntityUVE shown = MakePrimitiveEntityUVE(
        Math::Vector3UVE{1.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.2F, 0.8F, 0.1F}});
    entityManager.AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        hidden, Scene::WorldPartition3DMembershipComponentUVE{partition, false});
    entityManager.AddComponentUVE<Scene::WorldPartition3DMembershipComponentUVE>(
        shown, Scene::WorldPartition3DMembershipComponentUVE{partition, true});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_OccludedPrimitiveIsNotExtracted) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE wall = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE wallTransform;
    wallTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransformUVE(entityManager, wall, wallTransform);
    Scene::Occluder3DComponentUVE wallOccluder;
    wallOccluder.halfExtents = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    entityManager.AddComponentUVE<Scene::Occluder3DComponentUVE>(wall, wallOccluder);
    sceneGraph.UpdateUVE(entityManager);

    MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SurfaceInstanceRangeHidesAPrimitive) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE entity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponentUVE surface{};
    surface.visibilityRangeEnd = 5.0F;
    entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, surface);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SurfaceInstanceShadowsOnlyDoesNotDrawAPrimitive) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE entity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponentUVE surface{};
    surface.castShadow = Scene::SurfaceShadowModeUVE::ShadowsOnly;
    entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, surface);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SurfaceInstanceIgnoreOcclusionKeepsACoveredPrimitive) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE wall = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE wallTransform;
    wallTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransformUVE(entityManager, wall, wallTransform);
    Scene::Occluder3DComponentUVE wallOccluder;
    wallOccluder.halfExtents = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    entityManager.AddComponentUVE<Scene::Occluder3DComponentUVE>(wall, wallOccluder);
    sceneGraph.UpdateUVE(entityManager);

    const Scene::EntityUVE entity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponentUVE surface{};
    surface.ignoreOcclusionCulling = true;
    entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, surface);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SurfaceInstanceTransparencyPushesPrimitiveOpacity) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE entity = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.15F, 0.45F, 0.85F}});
    Scene::SurfaceInstanceComponentUVE surface{};
    surface.transparency = 0.25F;
    entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(entity, surface);

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
    const auto opacity = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformFloatCommandUVE>(command) &&
               std::get<SetUniformFloatCommandUVE>(command).name == "uSurfaceOpacity";
    });
    ASSERT_NE(opacity, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommandUVE>(*opacity).value, 0.75F);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SurfaceInstanceTransparencyBindsABlendedMeshPipeline) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_si_blend_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_si_blend_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{-1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::EntityUVE faded =
        MakeMeshEntityUVE(Math::Vector3UVE{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponentUVE surface{};
    surface.transparency = 0.25F;
    entityManager.AddComponentUVE<Scene::SurfaceInstanceComponentUVE>(faded, surface);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const auto pipelineBoundForOpacity = [](const std::vector<RecordedCommandUVE>& commands,
                                            const float opacity) -> PipelineHandleUVE {
        for (std::size_t index = 0U; index < commands.size(); ++index) {
            if (!std::holds_alternative<SetUniformFloatCommandUVE>(commands[index])) {
                continue;
            }
            const SetUniformFloatCommandUVE& uniform = std::get<SetUniformFloatCommandUVE>(commands[index]);
            if (uniform.name != "uSurfaceOpacity" || uniform.value != opacity) {
                continue;
            }
            for (std::size_t lookback = index; lookback > 0U; --lookback) {
                const RecordedCommandUVE& previous = commands[lookback - 1U];
                if (std::holds_alternative<BindPipelineCommandUVE>(previous)) {
                    return std::get<BindPipelineCommandUVE>(previous).pipeline;
                }
            }
        }
        return kInvalidPipelineHandleUVE;
    };

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const PipelineHandleUVE opaque = pipelineBoundForOpacity(commands, 1.0F);
    const PipelineHandleUVE blended = pipelineBoundForOpacity(commands, 0.75F);
    ASSERT_NE(opaque, kInvalidPipelineHandleUVE);
    ASSERT_NE(blended, kInvalidPipelineHandleUVE);
    EXPECT_NE(opaque, blended);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_RegionHiddenPrimitiveIsNotACandidate) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE region = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, region, Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DComponentUVE>(region);
    sceneGraph.UpdateUVE(entityManager);

    const Scene::EntityUVE hidden = MakePrimitiveEntityUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.8F, 0.2F, 0.1F}});
    const Scene::EntityUVE shown = MakePrimitiveEntityUVE(
        Math::Vector3UVE{1.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponentUVE{Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.2F, 0.8F, 0.1F}});
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        hidden, Scene::VisibilityRegion3DMembershipComponentUVE{region, false});
    entityManager.AddComponentUVE<Scene::VisibilityRegion3DMembershipComponentUVE>(
        shown, Scene::VisibilityRegion3DMembershipComponentUVE{region, true});

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().primitiveItemsExtracted, 1U);
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_managed_program_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_managed_program_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_pcf_clamp_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_pcf_clamp_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_blend_clamp_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_blend_clamp_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_lit_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_lit_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    // Identity rotation, so LightSystemUVE derives direction {0,0,-1} (see
    // LightSystemUVETest's own RotateVectorUVE-based coverage for the rotated case). A Point
    // light never casts a shadow (Increment 26 scopes that to Directional only), so this test's
    // shadow pass stays empty regardless.
    Scene::LightComponentUVE light{Math::ColorUVE{0.9F, 0.8F, 0.7F}, 4.5F};
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_multilight_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_multilight_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::LightComponentUVE directionalLight{Math::ColorUVE{1.0F, 0.0F, 0.0F}, 2.0F};
    Scene::LightComponentUVE spotLight{Math::ColorUVE{0.0F, 1.0F, 0.0F}, 3.0F};
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_ambient_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_ambient_material.uvmat");
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

    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 1.0F});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    assertAmbientColor(renderDevice.GetLastSubmittedCommandsUVE());
}

TEST_F(Renderer3DUVETest, WorldEnvironmentAmbientSourceReachesPrimitiveShaderUniforms) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponentUVE{});
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.ambientColor = Math::Vector3UVE{0.4F, 0.3F, 0.2F};
    environment.ambientEnergy = 2.0F;
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);

    const auto captureAmbient = [this, cameraEntity, environmentEntity](
                                    const Scene::WorldEnvironmentAmbientSourceUVE source) {
        entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity).ambientSource = source;
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
        std::optional<std::int32_t> selectedSource;
        std::optional<Math::Vector3UVE> ambientColor;
        std::optional<Math::Vector3UVE> skyAmbient;
        std::optional<Math::Vector3UVE> groundAmbient;
        for (const RecordedCommandUVE& command : commands) {
            if (const auto* const uniform = std::get_if<SetUniformIntCommandUVE>(&command);
                uniform != nullptr && uniform->name == "uAmbientSource") {
                selectedSource = uniform->value;
            } else if (const auto* const vectorUniform = std::get_if<SetUniformVector3CommandUVE>(&command);
                       vectorUniform != nullptr) {
                if (vectorUniform->name == "uAmbientColor") {
                    ambientColor = vectorUniform->value;
                } else if (vectorUniform->name == "uSkyAmbient") {
                    skyAmbient = vectorUniform->value;
                } else if (vectorUniform->name == "uGroundAmbient") {
                    groundAmbient = vectorUniform->value;
                }
            }
        }
        return std::tuple{selectedSource, ambientColor, skyAmbient, groundAmbient};
    };

    const auto [noneSource, noneColor, noneSky, noneGround] =
        captureAmbient(Scene::WorldEnvironmentAmbientSourceUVE::None);
    ASSERT_TRUE(noneSource.has_value());
    ASSERT_TRUE(noneColor.has_value());
    ASSERT_TRUE(noneSky.has_value());
    ASSERT_TRUE(noneGround.has_value());
    EXPECT_EQ(*noneSource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSourceUVE::None));
    EXPECT_EQ(*noneColor, Math::Vector3UVE{});
    EXPECT_EQ(*noneSky, Math::Vector3UVE{});
    EXPECT_EQ(*noneGround, Math::Vector3UVE{});

    const auto [flatSource, flatColor, flatSky, flatGround] =
        captureAmbient(Scene::WorldEnvironmentAmbientSourceUVE::FlatColor);
    ASSERT_TRUE(flatSource.has_value());
    ASSERT_TRUE(flatColor.has_value());
    ASSERT_TRUE(flatSky.has_value());
    ASSERT_TRUE(flatGround.has_value());
    EXPECT_EQ(*flatSource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSourceUVE::FlatColor));
    EXPECT_EQ(*flatColor, (Math::Vector3UVE{0.8F, 0.6F, 0.4F}));
    EXPECT_EQ(*flatSky, Math::Vector3UVE{});
    EXPECT_EQ(*flatGround, Math::Vector3UVE{});

    const auto [skySource, skyColor, skyAmbient, groundAmbient] =
        captureAmbient(Scene::WorldEnvironmentAmbientSourceUVE::Sky);
    ASSERT_TRUE(skySource.has_value());
    ASSERT_TRUE(skyColor.has_value());
    ASSERT_TRUE(skyAmbient.has_value());
    ASSERT_TRUE(groundAmbient.has_value());
    EXPECT_EQ(*skySource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSourceUVE::Sky));
    EXPECT_EQ(*skyColor, (Math::Vector3UVE{0.8F, 0.6F, 0.4F}));
    EXPECT_GT(skyAmbient->z, groundAmbient->z);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_CameraAtKnownPosition_PushesMatchingViewPositionUniform) {
    // Stays on the same viewing axis as the mesh below (default identity rotation looks down -Z)
    // so the mesh remains inside the frustum — an off-axis camera position would cull it, leaving
    // no recorded item to assert uniforms on.
    const Math::Vector3UVE cameraPosition{0.0F, 0.0F, 5.0F};
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE(cameraPosition);
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_viewpos_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_viewpos_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_mesh.uvmodel");
    const Asset::AssetGuidUVE materialAGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_material_a.uvmat");
    const Asset::AssetGuidUVE materialBGuid = assetDatabase.RegisterUVE("renderer3d_tests_fallback_material_b.uvmat");
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
    // 2 items x 3 material texture slots, then 6 Phase 2b post-process passes, then ToneMapping's
    // two binds - its scene-colour source on slot 0 and the scene depth it reads on slot 1 to
    // report per-pixel coverage in the destination's alpha (see fullscreen_quad.glsl).
    ASSERT_EQ(textureBinds.size(), 14U);

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

TEST_F(Renderer3DUVETest, RenderFrameUVE_MaterialSettings_PushUvEmissiveNormalAoUnshadedCutoffAndPackedMaps) {
    assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAssetUVE& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.albedoColor = Math::ColorUVE{0.2F, 0.4F, 0.6F};
            material.metallic = 0.25F;
            material.roughness = 0.75F;
            material.emissiveColor = Math::ColorUVE{0.1F, 0.0F, 0.0F};
            material.emissiveEnergy = 3.0F;
            material.normalScale = 0.5F;
            material.occlusionStrength = 0.25F;
            material.uvScale = Math::Vector2UVE{2.0F, 4.0F};
            material.uvOffset = Math::Vector2UVE{0.25F, 0.5F};
            material.unshaded = true;
            material.alphaCutoff = 0.4F;
            return true;
        });

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_settings_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_tests_material_settings.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    bool foundEmissiveEnergy = false;
    bool foundNormalScale = false;
    bool foundOcclusionStrength = false;
    bool foundAlphaCutoff = false;
    bool foundUnshaded = false;
    bool foundUvScale = false;
    bool foundUvOffset = false;
    bool foundMetallicRoughnessSlot = false;
    bool foundEmissiveSlot = false;
    bool boundMetallicRoughness = false;
    bool boundEmissive = false;
    for (const RecordedCommandUVE& command : commands) {
        if (const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command)) {
            if (uniform->name == "uEmissiveEnergy") {
                EXPECT_FLOAT_EQ(uniform->value, 3.0F);
                foundEmissiveEnergy = true;
            }
            if (uniform->name == "uNormalScale") {
                EXPECT_FLOAT_EQ(uniform->value, 0.5F);
                foundNormalScale = true;
            }
            if (uniform->name == "uOcclusionStrength") {
                EXPECT_FLOAT_EQ(uniform->value, 0.25F);
                foundOcclusionStrength = true;
            }
            if (uniform->name == "uAlphaCutoff") {
                EXPECT_FLOAT_EQ(uniform->value, 0.4F);
                foundAlphaCutoff = true;
            }
        }
        if (const auto* const uniform = std::get_if<SetUniformIntCommandUVE>(&command)) {
            if (uniform->name == "uUnshaded") {
                EXPECT_EQ(uniform->value, 1);
                foundUnshaded = true;
            }
            if (uniform->name == "uMetallicRoughnessTexture") {
                EXPECT_EQ(uniform->value, 12);
                foundMetallicRoughnessSlot = true;
            }
            if (uniform->name == "uEmissiveTexture") {
                EXPECT_EQ(uniform->value, 13);
                foundEmissiveSlot = true;
            }
        }
        if (const auto* const uniform = std::get_if<SetUniformVector3CommandUVE>(&command)) {
            if (uniform->name == "uUvScale") {
                EXPECT_FLOAT_EQ(uniform->value.x, 2.0F);
                EXPECT_FLOAT_EQ(uniform->value.y, 4.0F);
                foundUvScale = true;
            }
            if (uniform->name == "uUvOffset") {
                EXPECT_FLOAT_EQ(uniform->value.x, 0.25F);
                EXPECT_FLOAT_EQ(uniform->value.y, 0.5F);
                foundUvOffset = true;
            }
        }
        if (const auto* const bind = std::get_if<BindTextureCommandUVE>(&command)) {
            if (bind->slot == 12U) {
                EXPECT_NE(bind->texture, kInvalidTextureHandleUVE);
                boundMetallicRoughness = true;
            }
            if (bind->slot == 13U) {
                EXPECT_NE(bind->texture, kInvalidTextureHandleUVE);
                boundEmissive = true;
            }
        }
    }
    EXPECT_TRUE(foundEmissiveEnergy);
    EXPECT_TRUE(foundNormalScale);
    EXPECT_TRUE(foundOcclusionStrength);
    EXPECT_TRUE(foundAlphaCutoff);
    EXPECT_TRUE(foundUnshaded);
    EXPECT_TRUE(foundUvScale);
    EXPECT_TRUE(foundUvOffset);
    EXPECT_TRUE(foundMetallicRoughnessSlot);
    EXPECT_TRUE(foundEmissiveSlot);
    EXPECT_TRUE(boundMetallicRoughness);
    EXPECT_TRUE(boundEmissive);
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_empty_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_empty_mesh.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_material_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_tests_invalid_material_payload.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE diagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidReadyTexturePayloadUsesFallbackInRelease) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture.width = 0U;
            texture.height = 0U;
            texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_overflowed_cascade_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_overflowed_cascade_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 2.0F});
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

TEST_F(Renderer3DUVETest, RenderFrameUVE_UIImages_BindTheirResolvedTextureInsteadOfSolidFallback) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Input::InputSystemUVE inputSystem{eventSystem};
    UI::UIRuntimeUVE uiRuntime;
    renderer3D->SetUIRuntimeUVE(&uiRuntime);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    struct ResetUIRuntimeBindingUVE final {
        Render::Renderer3DUVE& renderer;
        ~ResetUIRuntimeBindingUVE() { renderer.SetUIRuntimeUVE(nullptr); }
    } resetUIRuntimeBinding{*renderer3D};

    const Scene::EntityUVE solidImageEntity = entityManager.CreateEntityUVE();
    Scene::UIImageComponentUVE solidImage;
    solidImage.rect.size = Math::Vector2UVE{32.0F, 32.0F};
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(solidImageEntity, solidImage);
    inputSystem.UpdateUVE();
    uiRuntime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatchUVE().quads.size(), 1U);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const auto slotZeroTextures = [](const std::vector<RecordedCommandUVE>& commands) {
        std::vector<TextureHandleUVE> result;
        for (const RecordedCommandUVE& command : commands) {
            if (const auto* const bind = std::get_if<BindTextureCommandUVE>(&command);
                bind != nullptr && bind->slot == 0U) {
                result.push_back(bind->texture);
            }
        }
        return result;
    };
    const std::vector<TextureHandleUVE> solidTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommandsUVE());

    static_cast<void>(entityManager.DestroyEntityUVE(solidImageEntity));
    const Asset::AssetGuidUVE splashTextureGuid = assetDatabase.RegisterUVE("renderer3d_tests_ui_splash.uvtex");
    const Scene::EntityUVE imageEntity = entityManager.CreateEntityUVE();
    Scene::UIImageComponentUVE texturedImage;
    texturedImage.textureAssetGuid = splashTextureGuid;
    texturedImage.rect.size = Math::Vector2UVE{64.0F, 32.0F};
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(imageEntity, texturedImage);
    WaitUntilTextureReadyUVE(splashTextureGuid);
    inputSystem.UpdateUVE();
    uiRuntime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatchUVE().quads.size(), 1U);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<TextureHandleUVE> imageTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommandsUVE());
    ASSERT_FALSE(solidTextures.empty());
    ASSERT_EQ(imageTextures.size(), solidTextures.size());
    EXPECT_NE(imageTextures.back(), kInvalidTextureHandleUVE);
    EXPECT_NE(imageTextures.back(), solidTextures.back());
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_UIOverlay_PreservesCanvasOrderAcrossGlyphAndImageTextures) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    Input::InputSystemUVE inputSystem{eventSystem};
    UI::UIRuntimeUVE uiRuntime;
    renderer3D->SetUIRuntimeUVE(&uiRuntime);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    struct ResetUIRuntimeBindingUVE final {
        Render::Renderer3DUVE& renderer;
        ~ResetUIRuntimeBindingUVE() { renderer.SetUIRuntimeUVE(nullptr); }
    } resetUIRuntimeBinding{*renderer3D};

    const Asset::AssetGuidUVE splashTextureGuid = assetDatabase.RegisterUVE("renderer3d_tests_ui_canvas_order.uvtex");
    const Scene::EntityUVE imageCanvasEntity = entityManager.CreateEntityUVE();
    Scene::CanvasComponentUVE imageCanvas;
    imageCanvas.sortOrder = 1;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(imageCanvasEntity, imageCanvas);
    Scene::UIImageComponentUVE image;
    image.textureAssetGuid = splashTextureGuid;
    image.rect.position = Math::Vector2UVE{20.0F, 20.0F};
    image.rect.size = Math::Vector2UVE{48.0F, 48.0F};
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(imageCanvasEntity, image);
    WaitUntilTextureReadyUVE(splashTextureGuid);
    inputSystem.UpdateUVE();
    uiRuntime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatchUVE().quads.size(), 1U);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const auto slotZeroTextures = [](const std::vector<RecordedCommandUVE>& commands) {
        std::vector<TextureHandleUVE> result;
        for (const RecordedCommandUVE& command : commands) {
            if (const auto* const bind = std::get_if<BindTextureCommandUVE>(&command);
                bind != nullptr && bind->slot == 0U) {
                result.push_back(bind->texture);
            }
        }
        return result;
    };
    const std::vector<TextureHandleUVE> imageOnlyTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommandsUVE());
    ASSERT_FALSE(imageOnlyTextures.empty());

    const Scene::EntityUVE textCanvasEntity = entityManager.CreateEntityUVE();
    Scene::CanvasComponentUVE textCanvas;
    textCanvas.sortOrder = 0;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(textCanvasEntity, textCanvas);
    Scene::UITextComponentUVE text;
    text.text = "A";
    text.positionPixels = Math::Vector2UVE{8.0F, 8.0F};
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(textCanvasEntity, text);
    inputSystem.UpdateUVE();
    uiRuntime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatchUVE().quads.size(), 2U);
    EXPECT_EQ(uiRuntime.GetDrawBatchUVE().quads[0].kind, UI::UIDrawItemKindUVE::Glyph);
    EXPECT_EQ(uiRuntime.GetDrawBatchUVE().quads[1].kind, UI::UIDrawItemKindUVE::Image);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<TextureHandleUVE> mixedTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommandsUVE());
    ASSERT_GE(mixedTextures.size(), 2U);
    EXPECT_EQ(mixedTextures.back(), imageOnlyTextures.back());
    EXPECT_NE(mixedTextures[mixedTextures.size() - 2U], mixedTextures.back());
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_MaterialWithAlbedoTexture_UploadsAndBindsRealTexture) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCountUVE();

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_textured_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_textured_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_albedo.uvtex");
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

TEST_F(Renderer3DUVETest, RenderFrameUVE_SrgbAlbedoMetadataReachesRhiDescriptor) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_srgb_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_srgb_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_srgb_albedo.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture.width = 2U;
            texture.height = 2U;
            texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
            texture.colorSpace = Asset::TextureAssetColorSpaceUVE::Srgb;
            texture.usage = Asset::TextureUsageUVE::Color;
            texture.pixels.assign(2U * 2U * 4U, std::byte{0x7F});
            texture.mipLevels.push_back(Asset::TextureMipLevelUVE{
                1U, 1U, std::vector<std::byte>(4U, std::byte{0x3F})});
            return true;
        });

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<TextureDescUVE> liveTextureDescs = renderDevice.GetLiveTextureDescsUVE();
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const TextureDescUVE& desc) {
        return desc.format == TextureFormatUVE::RGBA8Unorm && desc.colorSpace == TextureColorSpaceUVE::Srgb &&
               desc.mipLevels == 2U;
    }));
}

#if defined(UVE_HAS_BASIS_ENCODER) && UVE_HAS_BASIS_ENCODER
TEST_F(Renderer3DUVETest, RenderFrameUVE_BasisTextureFallsBackToRgbaOnNullDevice) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_basis_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_basis_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_basis_albedo.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);

    Asset::TextureAssetUVE basisTexture;
    basisTexture.width = 4U;
    basisTexture.height = 4U;
    basisTexture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
    basisTexture.colorSpace = Asset::TextureAssetColorSpaceUVE::Srgb;
    basisTexture.usage = Asset::TextureUsageUVE::Color;
    basisTexture.pixels.resize(4U * 4U * 4U, std::byte{0xFF});
    ASSERT_TRUE(Asset::CompressTextureAssetWithBasisUVE(
        basisTexture, Asset::TextureCompressionModeUVE::BasisUASTC, 60U, 1U));
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [basisTexture](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture = basisTexture;
            return true;
        });

    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilTextureReadyUVE(textureGuid);
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    EXPECT_FALSE(renderDevice.SupportsTextureFormatUVE(TextureFormatUVE::BC7RGBA));
    const std::vector<TextureDescUVE> liveTextureDescs = renderDevice.GetLiveTextureDescsUVE();
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const TextureDescUVE& desc) {
        return desc.format == TextureFormatUVE::RGBA8Unorm && desc.colorSpace == TextureColorSpaceUVE::Srgb &&
               desc.mipLevels == 1U;
    }));
}
#endif

TEST_F(Renderer3DUVETest, RenderFrameUVE_Rgba16FloatAlbedoTexture_UploadsSuccessfully) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCountUVE();

    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_f16_albedo.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureAssetFormatUVE::RGBA16Float;
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_failed_texture.uvtex");
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

TEST_F(Renderer3DUVETest, RenderFrameUVE_InvalidTexturePayloadIsRejectedAndMemoizedUntilReload) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_invalid_texture.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            // The custom loader returns a ready CPU asset with invalid dimensions. The renderer
            // must reject it before making a backend allocation attempt.
            texture.width = 0U;
            texture.height = 2U;
            texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
            texture.pixels.clear();
            return true;
        });
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);
    WaitUntilTextureReadyUVE(textureGuid);

    const std::uint64_t initialTextureAttempts = renderDevice.GetTextureCreateAttemptCountUVE();
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().textureFallbacks, 1U);

    // Material cache invalidation must not send the same invalid asset to the backend.
    eventSystem.Publish(Asset::AssetReloadedEventUVE{materialGuid});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().textureFallbacks, 1U);

    // An explicit texture reload clears the failure memo. The asset is still invalid in this test,
    // so it is revalidated and safely falls back without reaching the render device.
    eventSystem.Publish(Asset::AssetReloadedEventUVE{textureGuid});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCountUVE(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().textureFallbacks, 1U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_TextureAssetNotYetReady_SkipsItemUntilLoaded) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_pending_albedo.uvtex");
    UseAlbedoTextureInMaterialUVE(textureGuid);

    std::atomic<bool> textureLoadGateUVE{false};
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [&textureLoadGateUVE](const std::filesystem::path&, Asset::TextureAssetUVE& texture) {
            while (!textureLoadGateUVE.load()) {
                std::this_thread::yield();
            }
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_reuse_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_reuse_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_reload_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_reload_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_material.uvmat");
    const Asset::AssetGuidUVE textureGuid = assetDatabase.RegisterUVE("renderer3d_tests_texreload_albedo.uvtex");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_material.uvmat");
    const Asset::AssetGuidUVE textureAGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_a.uvtex");
    const Asset::AssetGuidUVE textureBGuid = assetDatabase.RegisterUVE("renderer3d_tests_material_swap_b.uvtex");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_active_camera_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_active_camera_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid, false);

    EXPECT_NO_FATAL_FAILURE(renderer3D->RenderFrameUVE(entityManager, cameraEntity));

    EXPECT_EQ(renderSystem.GetFrameIndexUVE(), 1U);
    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    EXPECT_FALSE(commands.empty());
}

TEST_F(Renderer3DUVETest, DirectionalLightInheritShadowBiasUsesProjectDefaultsAndHonorsOverrides) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_shadow_bias_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_shadow_bias_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    const Scene::EntityUVE sun = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, sun, Scene::TransformComponentUVE{});
    Scene::DirectionalLight3DObjectDefinitionUVE definition;
    definition.light.shadowMaxDistance = 0.0F;
    definition.emitter.shadowEnabled = true;
    Scene::ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, definition);
    sceneGraph.UpdateUVE(entityManager);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->SetShadowBiasDefaultsUVE(0.4F, 2.0F);
    const auto captureBiasUniforms = [this, cameraEntity]() {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        std::optional<float> depthBias;
        std::optional<float> normalBias;
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
            if (uniform == nullptr) {
                continue;
            }
            if (uniform->name == "uShadowBias") {
                depthBias = uniform->value;
            } else if (uniform->name == "uShadowNormalBias") {
                normalBias = uniform->value;
            }
        }
        return std::pair{depthBias, normalBias};
    };

    auto [depthBias, normalBias] = captureBiasUniforms();
    ASSERT_TRUE(depthBias.has_value());
    ASSERT_TRUE(normalBias.has_value());
    EXPECT_NEAR(*depthBias, 0.4F * 0.025F, 1.0e-6F);
    EXPECT_FLOAT_EQ(*normalBias, 2.0F);

    Scene::LightEmitterComponentUVE& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sun);
    emitter.shadowBias = 0.2F;
    emitter.shadowNormalBias = 0.5F;
    const auto [overriddenDepthBias, overriddenNormalBias] = captureBiasUniforms();
    ASSERT_TRUE(overriddenDepthBias.has_value());
    ASSERT_TRUE(overriddenNormalBias.has_value());
    EXPECT_NEAR(*overriddenDepthBias, 0.2F * 0.025F, 1.0e-6F);
    EXPECT_FLOAT_EQ(*overriddenNormalBias, 0.5F);
}

TEST_F(Renderer3DUVETest, DirectionalLightDistanceFadeRangeReachesShaderAndZeroDisablesIt) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_shadow_fade_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_shadow_fade_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    const Scene::EntityUVE sun = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, sun, Scene::TransformComponentUVE{});
    Scene::DirectionalLight3DObjectDefinitionUVE definition;
    definition.light.shadowMaxDistance = 0.0F;
    definition.light.shadowDistanceFadeRange = 6.5F;
    Scene::ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, definition);
    sceneGraph.UpdateUVE(entityManager);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    const auto captureDistanceFadeRange = [this, cameraEntity]() -> std::optional<float> {
        renderer3D->RenderFrameUVE(entityManager, cameraEntity);
        std::optional<float> fadeRange;
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
            if (uniform != nullptr && uniform->name == "uShadowMaxDistanceFadeRange") {
                fadeRange = uniform->value;
            }
        }
        return fadeRange;
    };

    const std::optional<float> fadeRange = captureDistanceFadeRange();
    ASSERT_TRUE(fadeRange.has_value());
    EXPECT_FLOAT_EQ(*fadeRange, 6.5F);
    bool cascadesEnabled = false;
    for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
        const auto* const uniform = std::get_if<SetUniformIntCommandUVE>(&command);
        cascadesEnabled = cascadesEnabled ||
                          (uniform != nullptr && uniform->name == "uShadowCascadeCount" && uniform->value == 3);
    }
    EXPECT_TRUE(cascadesEnabled);

    entityManager.GetComponentUVE<Scene::DirectionalLight3DComponentUVE>(sun).shadowDistanceFadeRange = 0.0F;
    const std::optional<float> disabledFadeRange = captureDistanceFadeRange();
    ASSERT_TRUE(disabledFadeRange.has_value());
    EXPECT_FLOAT_EQ(*disabledFadeRange, 0.0F);
}

TEST_F(Renderer3DUVETest, DirectionalLightInheritSplitBlendUsesRendererDefault) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_split_blend_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_split_blend_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::EntityUVE sun = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, sun, Scene::TransformComponentUVE{});
    Scene::DirectionalLight3DObjectDefinitionUVE definition;
    definition.light.shadowMaxDistance = 0.0F; // Follow this camera's far plane.
    definition.light.shadowSplitBlend = -1.0F; // Inherit kTestShadowCascadeSplitLambdaUVE from Renderer3D.
    Scene::ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, definition);
    sceneGraph.UpdateUVE(entityManager);
    WaitUntilAssetsReadyUVE(meshGuid, materialGuid);
    WaitUntilShadowProgramReadyUVE();

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const Scene::CameraComponentUVE& camera = entityManager.GetComponentUVE<Scene::CameraComponentUVE>(cameraEntity);
    std::array<float, 3U> splits{};
    std::array<bool, 3U> found{};
    for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
        const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command);
        if (uniform == nullptr) {
            continue;
        }
        for (std::size_t index = 0U; index < splits.size(); ++index) {
            if (uniform->name == "uShadowCascadeSplits[" + std::to_string(index) + "]") {
                splits[index] = uniform->value;
                found[index] = true;
            }
        }
    }
    for (std::size_t index = 0U; index < splits.size(); ++index) {
        ASSERT_TRUE(found[index]);
        const float progress = static_cast<float>(index + 1U) / static_cast<float>(splits.size());
        const float uniformSplit = camera.nearPlane + (camera.farPlane - camera.nearPlane) * progress;
        const float logarithmicSplit = camera.nearPlane *
                                       std::pow(camera.farPlane / camera.nearPlane, progress);
        const float expected = uniformSplit * (1.0F - kTestShadowCascadeSplitLambdaUVE) +
                               logarithmicSplit * kTestShadowCascadeSplitLambdaUVE;
        EXPECT_NEAR(splits[index], expected, 0.001F);
    }
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DirectionalLight_PushesThreeOrderedCascadeSplits) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_cascade_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_cascade_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 2.0F});
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_stabilization_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_stabilization_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 2.0F});
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_noshadow_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_noshadow_material.uvmat");
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
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_invalid_shadow_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_invalid_shadow_material.uvmat");
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
               std::get<BindTextureCommandUVE>(command).slot >= 3U &&
               std::get<BindTextureCommandUVE>(command).slot < 12U;
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
    const Asset::AssetGuidUVE visibleMeshGuid = assetDatabase.RegisterUVE("renderer3d_fitted_visible.uvmodel");
    const Asset::AssetGuidUVE visibleMaterialGuid = assetDatabase.RegisterUVE("renderer3d_fitted_visible.uvmat");
    const Asset::AssetGuidUVE offCameraMeshGuid = assetDatabase.RegisterUVE("renderer3d_fitted_offcamera.uvmodel");
    const Asset::AssetGuidUVE offCameraMaterialGuid = assetDatabase.RegisterUVE("renderer3d_fitted_offcamera.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, visibleMeshGuid, visibleMaterialGuid);
    // At z=-10 the default camera's 60-degree view only reaches roughly +/-5.8 on X, while the
    // fitted directional-light frustum includes this potential caster across its far-range width.
    MakeMeshEntityUVE(Math::Vector3UVE{50.0F, 0.0F, -10.0F}, offCameraMeshGuid, offCameraMaterialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 3.0F});
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

TEST_F(Renderer3DUVETest, FrustumCullingFreezeDrawsOffCameraMeshesAndLeavesShadowCascadesCulled) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE visibleMeshGuid = assetDatabase.RegisterUVE("renderer3d_freeze_visible.uvmodel");
    const Asset::AssetGuidUVE visibleMaterialGuid = assetDatabase.RegisterUVE("renderer3d_freeze_visible.uvmat");
    const Asset::AssetGuidUVE offCameraMeshGuid = assetDatabase.RegisterUVE("renderer3d_freeze_offcamera.uvmodel");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, visibleMeshGuid, visibleMaterialGuid);
    // Far outside the default camera's 60-degree view at z=-10 (which reaches roughly +/-5.8 on X),
    // but inside the fitted directional-light frustum. It shares the visible mesh's material so
    // the freeze assertion isolates culling from asynchronous compilation of a second material shader.
    MakeMeshEntityUVE(Math::Vector3UVE{50.0F, 0.0F, -10.0F}, offCameraMeshGuid, visibleMaterialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReadyUVE(visibleMeshGuid, visibleMaterialGuid);
    WaitUntilAssetsReadyUVE(offCameraMeshGuid, visibleMaterialGuid);
    WaitUntilShadowProgramReadyUVE();

    const auto countDraws = [](const std::vector<RecordedCommandUVE>& commands) {
        const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(),
                                                [](const RecordedCommandUVE& command) {
                                                    return std::holds_alternative<EndRenderPassCommandUVE>(command);
                                                });
        const bool hasShadowPass = shadowPassEnd != commands.cend();
        const auto draws = [&commands](auto first, auto last) {
            std::size_t instances = 0U;
            for (auto command = first; command != last; ++command) {
                if (const auto* const draw = std::get_if<DrawIndexedCommandUVE>(&*command); draw != nullptr) {
                    instances += draw->instanceCount;
                }
            }
            return instances;
        };
        return std::pair<std::size_t, std::size_t>{
            hasShadowPass ? draws(commands.cbegin(), shadowPassEnd) : 0U,
            hasShadowPass ? draws(std::next(shadowPassEnd), commands.cend()) : draws(commands.cbegin(), commands.cend())};
    };

    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const auto [culledShadowDraws, culledMainDraws] = countDraws(renderDevice.GetLastSubmittedCommandsUVE());
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnosticsUVE().frustumCullingFrozen)
        << "culling is on unless a host asks for the freeze";
    EXPECT_EQ(culledMainDraws, 1U);
    ASSERT_GT(culledShadowDraws, 0U) << "this test needs a shadow pass to prove the freeze skips it";

    Render::CullingSettingsUVE frozen;
    frozen.frustumCullingEnabledUVE = false;
    renderer3D->SetCullingSettingsUVE(frozen);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const auto [frozenShadowDraws, frozenMainDraws] = countDraws(renderDevice.GetLastSubmittedCommandsUVE());
    const Renderer3DFrameDiagnosticsUVE frozenDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_TRUE(frozenDiagnostics.frustumCullingFrozen);
    EXPECT_EQ(frozenMainDraws, 2U) << "the off-camera mesh must reach the colour view when culling is frozen";
    EXPECT_EQ(frozenDiagnostics.meshItemsExtracted, 2U);
    EXPECT_EQ(frozenShadowDraws, culledShadowDraws)
        << "shadow cascades keep culling against their own light frusta";

    renderer3D->SetCullingSettingsUVE(Render::CullingSettingsUVE{});
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const auto [restoredShadowDraws, restoredMainDraws] = countDraws(renderDevice.GetLastSubmittedCommandsUVE());
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnosticsUVE().frustumCullingFrozen);
    EXPECT_EQ(restoredMainDraws, culledMainDraws);
    EXPECT_EQ(restoredShadowDraws, culledShadowDraws);
}

TEST_F(Renderer3DUVETest, FrustumCullingFreezeDrawsOffCameraPrimitives) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponentUVE{});
    // The primitive path culls in its own extraction, not through the mesh visibility set, so it
    // needs its own proof that the freeze reaches it.
    MakePrimitiveEntityUVE(Math::Vector3UVE{80.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponentUVE{});

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
    const Renderer3DFrameDiagnosticsUVE culled = renderer3D->GetLastFrameDiagnosticsUVE();
    ASSERT_TRUE(culled.primitiveProgramReady);
    EXPECT_FALSE(culled.frustumCullingFrozen);
    EXPECT_EQ(culled.primitiveCandidates, 2U);
    EXPECT_EQ(culled.primitiveItemsExtracted, 1U);

    Render::CullingSettingsUVE frozen;
    frozen.frustumCullingEnabledUVE = false;
    renderer3D->SetCullingSettingsUVE(frozen);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    const Renderer3DFrameDiagnosticsUVE frozenDiagnostics = renderer3D->GetLastFrameDiagnosticsUVE();
    EXPECT_TRUE(frozenDiagnostics.frustumCullingFrozen);
    EXPECT_EQ(frozenDiagnostics.primitiveItemsExtracted, 2U)
        << "the off-camera primitive must be submitted when culling is frozen";
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_DirectionalLightAndReadyShadowProgram_ShadowPassDrawsOpaqueItem) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_shadowdraw_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_shadowdraw_material.uvmat");
    MakeMeshEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 3.0F});
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
    MakeLightEntityUVE(Scene::LightComponentUVE{Math::ColorUVE{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_nonfinite_shadow_tuning_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid =
        assetDatabase.RegisterUVE("renderer3d_nonfinite_shadow_tuning_material.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_legacy.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_legacy.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_shared.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_shared.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_reset.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_reset.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_instancing_single.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_instancing_single.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_evict.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_evict.uvmat");
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
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_cache_mesh.uvmodel");
    const Asset::AssetGuidUVE materialGuid = assetDatabase.RegisterUVE("renderer3d_tests_cache_material.uvmat");
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
TEST_F(Renderer3DUVETest, RenderFrameUVE_ASkinnedMeshUnderASkeletonIsDrawnInTheSkeletonsPose) {
    // One triangle bound wholly to joint "Hips"; no material, so it takes the unmaterialed path.
    assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>([](const std::filesystem::path&, Asset::MeshAssetUVE& mesh) {
        mesh.vertices = {
            Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
            Asset::MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
            Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        };
        mesh.indices = {0, 1, 2};
        mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}};
        Asset::MeshJointUVE hips;
        hips.name = "Hips";
        mesh.joints = {hips};
        Asset::MeshSkinningInfluenceUVE influence;
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences = {influence, influence, influence};
        return true;
    });
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Asset::AssetGuidUVE meshGuid = assetDatabase.RegisterUVE("renderer3d_tests_skinned_mesh.uvmodel");
    const Scene::EntityUVE skeletonEntity = entityManager.CreateEntityUVE();
    Scene::TransformComponentUVE skeletonTransform;
    skeletonTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransformUVE(entityManager, skeletonEntity, skeletonTransform);
    Scene::Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBoneUVE bone;
    bone.name = "Hips";
    skeleton.bones = {bone};
    entityManager.AddComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity, skeleton);
    const Scene::EntityUVE meshEntity = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, meshEntity, Scene::TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, meshEntity, skeletonEntity);
    entityManager.AddComponentUVE<Scene::MeshComponentUVE>(meshEntity, Scene::MeshComponentUVE{meshGuid, Asset::kInvalidAssetGuidUVE});
    sceneGraph.UpdateUVE(entityManager);
    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle = assetManager.LoadUVE<Asset::MeshAssetUVE>(meshGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterationsUVE && !meshHandle.IsReadyUVE(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.IsReadyUVE());

    // The vertex buffer bound for the last indexed draw of three indices, read back.
    const auto drawnVertices = [this]() {
        std::vector<Asset::MeshVertexUVE> vertices;
        BufferHandleUVE bound{};
        for (const RecordedCommandUVE& command : renderDevice.GetLastSubmittedCommandsUVE()) {
            if (std::holds_alternative<BindVertexBufferCommandUVE>(command)) {
                bound = std::get<BindVertexBufferCommandUVE>(command).buffer;
            } else if (std::holds_alternative<DrawIndexedCommandUVE>(command) &&
                       std::get<DrawIndexedCommandUVE>(command).indexCount == 3U) {
                vertices.assign(3U, Asset::MeshVertexUVE{});
                if (!renderDevice.ReadbackBufferUVE(bound, std::as_writable_bytes(std::span(vertices)))) {
                    vertices.clear();
                }
            }
        }
        return vertices;
    };

    // The skeleton at rest: the triangle as authored. (The first frame starts the built-in
    // programs compiling; the drawn one comes after.)
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().skinnedMeshesDrawn, 1U);
    std::vector<Asset::MeshVertexUVE> vertices = drawnVertices();
    ASSERT_EQ(vertices.size(), 3U);
    EXPECT_NEAR(vertices[2].position.y, 1.0F, 1e-5F);

    // Posing the bone moves the triangle with it.
    entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity).pose = {
        Scene::SkeletonBonePoseUVE{Math::Vector3UVE{0.0F, 2.0F, 0.0F}, Math::QuaternionUVE{},
                                   Math::Vector3UVE{1.0F, 1.0F, 1.0F}}};
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    vertices = drawnVertices();
    ASSERT_EQ(vertices.size(), 3U);
    EXPECT_NEAR(vertices[0].position.y, 2.0F, 1e-5F);
    EXPECT_NEAR(vertices[2].position.y, 3.0F, 1e-5F);

    // Without its skeleton the mesh is drawn in bind pose from the shared buffer.
    sceneGraph.SetParentUVE(entityManager, meshEntity, Scene::kInvalidEntityUVE);
    sceneGraph.UpdateUVE(entityManager);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnosticsUVE().skinnedMeshesDrawn, 0U);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_EmptySkyAssetKeepsProceduralSky) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    const Scene::EntityUVE environment = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environment,
                                                                        Scene::WorldEnvironment3DComponentUVE{});
    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*enabled).value, 0);
    bool skyPassBoundSlot1 = false;
    for (auto it = enabled; it != commands.cend(); ++it) {
        if (std::holds_alternative<EndRenderPassCommandUVE>(*it)) {
            break;
        }
        if (std::holds_alternative<BindTextureCommandUVE>(*it) &&
            std::get<BindTextureCommandUVE>(*it).slot == 1U) {
            skyPassBoundSlot1 = true;
        }
    }
    EXPECT_FALSE(skyPassBoundSlot1);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_SkyAssetDrivesEnvironmentMapAmbientAndSky) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponentUVE{});
    const std::filesystem::path skyPath{"environment/day.hdr"};
    const Asset::AssetGuidUVE skyGuid = assetDatabase.RegisterUVE(skyPath);
    WaitUntilTextureReadyUVE(skyGuid);

    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.skyAssetPath = skyPath.string();
    environment.ambientSource = Scene::WorldEnvironmentAmbientSourceUVE::EnvironmentMap;
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);

    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*enabled).value, 1);
    const auto ambientSource = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uAmbientSource";
    });
    const auto ambientMapEnabled =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == "uAmbientEnvironmentMapEnabled";
        });
    const auto ambientMapSampler =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == "uAmbientEnvironmentMap";
        });
    ASSERT_NE(ambientSource, commands.cend());
    ASSERT_NE(ambientMapEnabled, commands.cend());
    ASSERT_NE(ambientMapSampler, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*ambientSource).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSourceUVE::EnvironmentMap));
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*ambientMapEnabled).value, 1);
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*ambientMapSampler).value, 14);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindTextureCommandUVE>(command) &&
               std::get<BindTextureCommandUVE>(command).slot == 14U &&
               std::get<BindTextureCommandUVE>(command).texture != kInvalidTextureHandleUVE;
    }));
    bool skyPassBoundSlot1 = false;
    for (auto it = enabled; it != commands.cend(); ++it) {
        if (std::holds_alternative<EndRenderPassCommandUVE>(*it)) {
            break;
        }
        if (std::holds_alternative<BindTextureCommandUVE>(*it) &&
            std::get<BindTextureCommandUVE>(*it).slot == 1U &&
            std::get<BindTextureCommandUVE>(*it).texture != kInvalidTextureHandleUVE) {
            skyPassBoundSlot1 = true;
        }
    }
    EXPECT_TRUE(skyPassBoundSlot1);
}

TEST_F(Renderer3DUVETest, RenderFrameUVE_MissingSkyAssetFallsBackForSkyAndAmbientLighting) {
    const Scene::EntityUVE cameraEntity = MakeCameraEntityUVE();
    MakePrimitiveEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponentUVE{});
    assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
        [](const std::filesystem::path&, Asset::TextureAssetUVE&) { return false; });
    Scene::WorldEnvironment3DComponentUVE environment{};
    environment.skyAssetPath = "environment/missing.hdr";
    environment.ambientSource = Scene::WorldEnvironmentAmbientSourceUVE::EnvironmentMap;
    const Scene::EntityUVE environmentEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(environmentEntity, environment);

    PrimeMaterialProgramUVE(*renderer3D, cameraEntity);
    renderer3D->RenderFrameUVE(entityManager, cameraEntity);

    const std::vector<RecordedCommandUVE>& commands = renderDevice.GetLastSubmittedCommandsUVE();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*enabled).value, 0);
    const auto ambientSource = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
               std::get<SetUniformIntCommandUVE>(command).name == "uAmbientSource";
    });
    const auto ambientMapEnabled =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
            return std::holds_alternative<SetUniformIntCommandUVE>(command) &&
                   std::get<SetUniformIntCommandUVE>(command).name == "uAmbientEnvironmentMapEnabled";
        });
    ASSERT_NE(ambientSource, commands.cend());
    ASSERT_NE(ambientMapEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*ambientSource).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSourceUVE::EnvironmentMap));
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(*ambientMapEnabled).value, 0);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommandUVE& command) {
        return std::holds_alternative<BindTextureCommandUVE>(command) &&
               std::get<BindTextureCommandUVE>(command).slot == 14U &&
               std::get<BindTextureCommandUVE>(command).texture != kInvalidTextureHandleUVE;
    })); // A valid fallback descriptor remains bound even though the map source is disabled.
}

} // namespace UVE::Render::Tests
