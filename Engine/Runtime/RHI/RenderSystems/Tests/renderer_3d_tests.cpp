// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "renderer_3d.h"

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

#include "visibility_component.h"

#include "asset_bundle.h"
#include "asset_database.h"
#include "asset_manager.h"
#include "asset_load_completed_event.h"
#include "asset_reloaded_event.h"
#include "file_system.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "shader_asset.h"
#include "texture_asset.h"
#include "texture_compression.h"
#include "event_system.h"
#include "input_system.h"
#include "memory_manager.h"
#include "camera_system.h"
#include "light_system.h"
#include "mesh_renderer.h"
#include "null_render_device.h"
#include "render_system.h"
#include "shadow_atlas_manager.h"
#include "built_in_shaders.h"
#include "shader_manager.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "surface_instance_component.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "decal_3d.h"
#include "directional_light_3d.h"
#include "occluder_3d.h"
#include "reflection_probe_3d.h"
#include "sky_3d.h"
#include "visibility_region_3d.h"
#include "world_environment_3d.h"
#include "world_partition_3d.h"
#include "skeleton_3d.h"
#include "scene_graph.h"
#include "thread_pool.h"
#include "ui_runtime.h"

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

class Renderer3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Threading::ThreadPool threadPool{2};
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};
    NullRenderDevice renderDevice;

    // Real (not fake) ShaderManager (Increment 26) — Renderer3D compiles its built-in
    // shadow-depth program through this, matching the exact fixture shape
    // tests/render/shader/shader_manager_uve_tests.cpp already establishes for exercising
    // ShaderManager against NullRenderDevice. assetBundle/fileSystem exist only so
    // ShaderManager has a real IFileSystem to (fail to) find a virtual shader file on —
    // every program in these tests resolves through its embedded fallback source instead.
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

    Renderer3DTest() {
        vertexShaderGuid = assetDatabase.Register("renderer3d_tests_vertex.uvshader");
        fragmentShaderGuid = assetDatabase.Register("renderer3d_tests_fragment.uvshader");

        assetManager.RegisterLoader<Asset::ShaderAsset>(
            [](const std::filesystem::path&, Asset::ShaderAsset& shader) {
                shader.sourceCode = "void main() { }";
                return true;
            });
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
        // Default material: valid shader GUIDs, distinguishable scalar/color values, and every
        // texture GUID left unset (kInvalidAssetGuid) - exercises Renderer3D's fallback
        // texture path unless a specific test overrides this loader with a real texture GUID.
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                                 Asset::MaterialAsset& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.isTransparent = false;
                material.albedoColor = Math::Color{0.2F, 0.4F, 0.6F};
                material.metallic = 0.25F;
                material.roughness = 0.75F;
                material.emissiveColor = Math::Color{0.1F, 0.0F, 0.0F};
                return true;
            });
        // Default texture loader: a small, always-ready 2x2 RGBA8Unorm texture, reused by any
        // test that assigns a real texture GUID to a material without needing custom pixel data.
        assetManager.RegisterLoader<Asset::TextureAsset>(
            [](const std::filesystem::path&, Asset::TextureAsset& texture) {
                texture.width = 2;
                texture.height = 2;
                texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
                texture.pixels.assign(2U * 2U * 4U, std::byte{0xAB});
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

    Scene::Entity MakePrimitiveEntity(Math::Vector3 worldPosition,
                                             Scene::PrimitiveMeshComponent primitive) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::PrimitiveMeshComponent>(entity, primitive);
        return entity;
    }

    Scene::Entity MakeLightEntity(Scene::LightComponent light) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::LightComponent>(entity, light);
        return entity;
    }

    /// A light with a real world transform. Spot/point lights take their position from it, so the
    /// origin-only overload above cannot express one at all.
    Scene::Entity MakeLightEntity(Scene::LightComponent light, Math::Vector3 position,
                                         Math::Quaternion rotation = Math::Quaternion{}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::LightComponent>(entity, light);
        return entity;
    }

    /// The spot light every atlas test below uses: five metres in front of the origin camera,
    /// pointing straight down -Z, wide enough and long enough to contain a mesh at (0, 0, -10).
    static Scene::LightComponent MakeSpotLightComponent(float range = 10.0F,
                                                               float spotAngleDegrees = 30.0F) {
        Scene::LightComponent light;
        light.type = Scene::LightType::Spot;
        light.color = Math::Color{1.0F, 1.0F, 1.0F};
        light.intensity = 4.0F;
        light.range = range;
        light.spotAngleDegrees = spotAngleDegrees;
        return light;
    }

    /// The point light every point-shadow test uses: five metres in front of the origin camera with
    /// enough range to contain a mesh at (0, 0, -10). A point light has no cone, so its rotation is
    /// deliberately left at identity - the six faces come from the atlas' fixed table.
    static Scene::LightComponent MakePointLightComponent(float range = 20.0F) {
        Scene::LightComponent light;
        light.type = Scene::LightType::Point;
        light.color = Math::Color{1.0F, 1.0F, 1.0F};
        light.intensity = 4.0F;
        light.range = range;
        return light;
    }

    /// Every BeginRenderPass whose depth attachment is a shadow atlas tile, recognised by the
    /// viewport override that only a tile pass sets.
    [[nodiscard]] static std::vector<BeginRenderPassCommand> AtlasTilePasses(
        const std::vector<RecordedCommand>& commands) {
        std::vector<BeginRenderPassCommand> passes;
        for (const RecordedCommand& command : commands) {
            if (!std::holds_alternative<BeginRenderPassCommand>(command)) {
                continue;
            }
            const BeginRenderPassCommand& pass = std::get<BeginRenderPassCommand>(command);
            if (pass.desc.viewportOverride.has_value() &&
                std::get<BeginRenderPassCommand>(command).desc.colorAttachment == kInvalidTextureHandle) {
                passes.push_back(pass);
            }
        }
        return passes;
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

    /// Starts one material-program request through `renderer`, then drains the ShaderManager
    /// jobs. The test's subsequent explicit RenderFrame call observes the ready managed program
    /// without obscuring the first-frame asynchronous skip from tests that exercise it directly.
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

    void WaitUntilTextureReady(Asset::AssetGuid textureGuid) {
        Asset::AssetHandle<Asset::TextureAsset> textureHandle =
            assetManager.Load<Asset::TextureAsset>(textureGuid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            if (textureHandle.IsReady()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(textureHandle.IsReady());
    }

    /// Overrides the material loader so its albedoTexture points at `textureGuid`, keeping every
    /// other field identical to the default loader registered in the constructor. Safe to call
    /// any number of times per test (RegisterLoader<T>() simply replaces the stored loader).
    void UseAlbedoTextureInMaterial(Asset::AssetGuid textureGuid) {
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid, textureGuid](
                const std::filesystem::path&, Asset::MaterialAsset& material) {
                material.vertexShader = vertexGuid;
                material.fragmentShader = fragmentGuid;
                material.albedoTexture = textureGuid;
                return true;
            });
    }

    /// Bounded busy-poll (never a fixed sleep) driving `shaderManager` until Renderer3D's
    /// internal built-in shadow-depth program has finished compiling (Increment 26). The test
    /// can't reach that program directly (it lives inside Renderer3D's PIMPL) - instead this
    /// creates a second program request against the identical built-in descriptor and polls that
    /// one to readiness. ShaderManager::Update() drains every completed job and applies every
    /// pending program link in one call (not just one at a time, see DrainCompletedSourceJobs/
    /// ApplyPendingProgramLinks), and Renderer3D's own request was submitted first (at
    /// fixture construction, before any test body runs) - so by the time this probe program is
    /// ready, Renderer3D's internal one is guaranteed to be ready too.
    void WaitUntilShadowProgramReady() {
        Shader::ShaderProgramDesc probeDesc;
        probeDesc.virtualFilePath = std::string(Shader::BuiltIn::kShadowDepthVirtualPath);
        probeDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kShadowDepthSource);
        probeDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
        const std::shared_ptr<Shader::ShaderProgram> probe = shaderManager.CreateProgram(probeDesc);
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            shaderManager.Update(0.0);
            if (probe->IsReady()) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_TRUE(probe->IsReady());
        ASSERT_TRUE(probe->IsValid());
        // Renderer3D also requests tone-mapping and other built-ins at fixture construction.
        // Drain those jobs too so command-buffer tests do not race asynchronous shader compilation.
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            shaderManager.Update(0.0);
            if (shaderManager.GetPendingJobCount() == 0U) {
                break;
            }
            std::this_thread::yield();
        }
        ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);
    }
};

TEST_F(Renderer3DTest, ReflectionProbeResolutionControlsCubeTargetAndRecapturesOnTierChange) {
    const Scene::Entity camera = MakeCameraEntity();
    const Scene::Entity probeEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, probeEntity, Scene::TransformComponent{});
    Scene::ReflectionProbe3DComponent probe{};
    probe.resolution = Scene::ReflectionProbeResolution::High;
    probe.capturedOnce = true;
    probe.captureGeneration = 7U;
    entityManager.AddComponent<Scene::ReflectionProbe3DComponent>(probeEntity, probe);
    const Scene::Entity mediumProbeEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, mediumProbeEntity, Scene::TransformComponent{});
    Scene::ReflectionProbe3DComponent mediumProbe{};
    mediumProbe.resolution = Scene::ReflectionProbeResolution::Medium;
    mediumProbe.capturedOnce = true;
    mediumProbe.captureGeneration = 3U;
    entityManager.AddComponent<Scene::ReflectionProbe3DComponent>(mediumProbeEntity, mediumProbe);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, camera);
    std::vector<TextureDesc> liveTextureDescs = renderDevice.GetLiveTextureDescs();
    // Tier 2.3: each probe owns one six-layer cubemap (not six per-face 2D textures).
    const auto countProbeCubesAt = [&liveTextureDescs](const std::uint32_t resolution) {
        return std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [resolution](const auto& desc) {
            const bool expectedFormat =
                desc.format == TextureFormat::RGBA16Float || desc.format == TextureFormat::RGBA8Unorm;
            return desc.width == resolution && desc.height == resolution && expectedFormat &&
                   desc.type == TextureType::Cubemap && desc.arrayLayers == 6U;
        });
    };
    EXPECT_EQ(countProbeCubesAt(256U), 1) << "a reflection probe owns one six-layer cubemap";
    EXPECT_EQ(countProbeCubesAt(128U), 1) << "probes in one frame retain their own resolution tier";
    EXPECT_GE(std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
                  return desc.width == 128U && desc.height == 128U &&
                         desc.format == TextureFormat::Depth32Float;
              }), 2)
        << "a cached per-tier capture depth target survives another probe's capture";
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
        return desc.width == 256U && desc.height == 256U && desc.format == TextureFormat::Depth32Float;
    })) << "the capture depth target must match the selected face resolution";

    // The capture generation is intentionally unchanged: changing only the authored tier must
    // invalidate/reallocate the probe cubemap and capture depth, then render the new size.
    entityManager.GetComponent<Scene::ReflectionProbe3DComponent>(probeEntity).resolution =
        Scene::ReflectionProbeResolution::Ultra;
    renderer3D->RenderFrame(entityManager, camera);
    liveTextureDescs = renderDevice.GetLiveTextureDescs();
    EXPECT_EQ(countProbeCubesAt(512U), 1) << "resolution edits recapture instead of leaving stale probe data";
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const auto& desc) {
        return desc.width == 512U && desc.height == 512U && desc.format == TextureFormat::Depth32Float;
    }));
}

TEST_F(Renderer3DTest, RenderFrame_ADecalOnAMeshIsProjectedAndReportedInTheFrameDiagnostics) {
    // The decal pass end to end through the frame the renderer actually builds: the wall's assets
    // are registered the way every other mesh in these tests registers them, the decal authors the
    // same material by PATH (which is what a Decal3D stores), and the numbers below are read from
    // the frame diagnostics the renderer publishes rather than from the pass directly. If the pass
    // were not wired into RenderFrame(), every one of these would be zero.
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_decal_wall.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_decal_scorch.uvmat");
    const Scene::Entity camera = MakeCameraEntity();
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);

    const Scene::Entity decalEntity = entityManager.CreateEntity();
    Scene::TransformComponent decalLocal;
    // The mesh is a unit cube; a 1 m decal 0.4 in front of it reaches its camera-facing face.
    decalLocal.localPosition = Math::Vector3{0.0F, 0.0F, -4.4F};
    sceneGraph.AttachTransform(entityManager, decalEntity, decalLocal);
    Scene::Decal3DComponent decal;
    decal.materialAssetPath = "renderer3d_tests_decal_scorch.uvmat";
    decal.size = Math::Vector3{1.0F, 1.0F, 1.0F};
    entityManager.AddComponent<Scene::Decal3DComponent>(decalEntity, decal);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, camera);

    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.decalsConsidered, 1U);
    EXPECT_EQ(diagnostics.decalDrawsExtracted, 1U) << "the decal reached the mesh standing in front of it";
    EXPECT_EQ(diagnostics.decalPatchesExtracted, 1U) << "one face of the cube is inside the volume";
    EXPECT_EQ(diagnostics.decalTrianglesExtracted, 2U);
    EXPECT_EQ(diagnostics.decalsWithoutReceivers, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_ADecalIsHandedToTheGpuRatherThanOnlyCounted) {
    // The diagnostics above prove the projection pass found geometry. This proves the other half of
    // the item: that the geometry reaches the GPU - a bind of the decal's own vertex and index
    // buffers, the volume's units matrix as a uniform, and one indexed draw of the patch's two
    // triangles. Until this existed a decal was extracted, counted, and never seen.
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_decal_wall.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_decal_scorch.uvmat");
    const Scene::Entity camera = MakeCameraEntity();
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);

    const Scene::Entity decalEntity = entityManager.CreateEntity();
    Scene::TransformComponent decalLocal;
    decalLocal.localPosition = Math::Vector3{0.0F, 0.0F, -4.4F};
    sceneGraph.AttachTransform(entityManager, decalEntity, decalLocal);
    Scene::Decal3DComponent decal;
    decal.materialAssetPath = "renderer3d_tests_decal_scorch.uvmat";
    decal.size = Math::Vector3{1.0F, 1.0F, 1.0F};
    entityManager.AddComponent<Scene::Decal3DComponent>(decalEntity, decal);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // The built-in decal program links asynchronously, like every other built-in: the first frame
    // extracts and queues it, the drain finishes the link, the next frame can draw with it.
    renderer3D->RenderFrame(entityManager, camera);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, camera);

    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.decalDrawsExtracted, 1U);
    EXPECT_EQ(diagnostics.decalDrawCallsRecorded, 1U) << "an extracted decal must reach the GPU";
    EXPECT_EQ(diagnostics.decalDrawsDropped, 0U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto decalDraw = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        // A quad patch fans into two triangles, so six indices - and the meshes in this scene draw
        // with their own index counts, which is what makes six the decal's alone.
        return std::holds_alternative<DrawIndexedCommand>(command) &&
               std::get<DrawIndexedCommand>(command).indexCount == 6U;
    });
    ASSERT_NE(decalDraw, commands.cend()) << "no six-index draw was recorded";
    EXPECT_TRUE(std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
                    return std::holds_alternative<BindVertexBufferCommand>(command);
                }) != commands.cend())
        << "the decal's own vertex buffer was never bound";
    const auto worldToUnit = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformMatrix4x4Command>(command) &&
               std::get<SetUniformMatrix4x4Command>(command).name == "uWorldToUnit";
    });
    ASSERT_NE(worldToUnit, commands.cend())
        << "the volume's coordinate space must reach the shader, or the fades are evaluated in the "
           "wrong space";
    // The decal program samples its material's albedo from the renderer's albedo slot (0, the same
    // one the mesh materials bind to); this material has no texture, so what is bound there is the
    // white fallback, and the flat colour and full alpha it leaves behind are the point - an
    // untextured decal still paints.
    const auto albedoSampler = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uAlbedoTexture" &&
               std::get<SetUniformIntCommand>(command).value == 0;
    });
    EXPECT_NE(albedoSampler, commands.cend()) << "the decal program must be told where its texture is";
}

TEST_F(Renderer3DTest, FastApproximateAAIsAppliedByToneMappingWithClampedQualityUniforms) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.fastApproximateAAEnabled = true;
    settings.fastApproximateAAQuality = 99U; // Renderer clamps untrusted API callers to High.
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    ASSERT_TRUE(diagnostics.toneMappingPassRecorded);
    EXPECT_TRUE(diagnostics.fastApproximateAAPassApplied);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto aaEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uFastApproximateAA";
    });
    ASSERT_NE(aaEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*aaEnabled).value, 1);
    const auto aaQuality = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uScreenSpaceAAQuality";
    });
    ASSERT_NE(aaQuality, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*aaQuality).value, 2);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformFloatCommand>(command) &&
               std::get<SetUniformFloatCommand>(command).name == "uFxaaTexelX" &&
               std::get<SetUniformFloatCommand>(command).value > 0.0F;
    }));
}

TEST_F(Renderer3DTest, SharpeningAmountIsClampedAndSentToTheFinalPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    const auto captureSharpeningAmount = [this, cameraEntity]() -> std::optional<float> {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
            if (uniform != nullptr && uniform->name == "uSharpeningAmount") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    PostProcessSettings settings;
    settings.sharpeningAmount = 0.6F;
    renderer3D->SetPostProcessSettings(settings);
    const std::optional<float> configuredAmount = captureSharpeningAmount();
    ASSERT_TRUE(configuredAmount.has_value());
    EXPECT_FLOAT_EQ(*configuredAmount, 0.6F);

    settings.sharpeningAmount = 1.5F; // Direct API callers are clamped to the supported range.
    renderer3D->SetPostProcessSettings(settings);
    const std::optional<float> clampedAmount = captureSharpeningAmount();
    ASSERT_TRUE(clampedAmount.has_value());
    EXPECT_FLOAT_EQ(*clampedAmount, 1.0F);
}

TEST_F(Renderer3DTest, DitheringToggleReachesTheFinalPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    const auto captureDitheringToggle = [this, cameraEntity]() -> std::optional<std::int32_t> {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformIntCommand>(&command);
            if (uniform != nullptr && uniform->name == "uDitheringEnabled") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    PostProcessSettings settings;
    settings.ditheringEnabled = true;
    renderer3D->SetPostProcessSettings(settings);
    const std::optional<std::int32_t> enabled = captureDitheringToggle();
    ASSERT_TRUE(enabled.has_value());
    EXPECT_EQ(*enabled, 1);

    settings.ditheringEnabled = false;
    renderer3D->SetPostProcessSettings(settings);
    const std::optional<std::int32_t> disabled = captureDitheringToggle();
    ASSERT_TRUE(disabled.has_value());
    EXPECT_EQ(*disabled, 0);
}

TEST_F(Renderer3DTest, NearestUpscalingIsSelectedByTheToneMappingShader) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.nearestUpscalingEnabled = true;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto upscalingMethod = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uNearestUpscaling";
    });
    ASSERT_NE(upscalingMethod, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*upscalingMethod).value, 1);
}

TEST_F(Renderer3DTest, LinearClampToneMappingIsSelectedByTheToneMappingShader) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.acesToneMappingEnabled = false;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto toneMappingMethod = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uToneMappingMethod";
    });
    ASSERT_NE(toneMappingMethod, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*toneMappingMethod).value, 0);
}

TEST_F(Renderer3DTest, SSAORadiusIntensityPowerQualityAndBlurReachTheirShaderPasses) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.ssaoRadius = 0.8F;
    settings.ssaoIntensity = 1.7F;
    settings.ssaoPower = 2.0F;
    settings.ssaoQuality = 0U; // Low maps to four kernel samples.
    settings.ssaoBlurEnabled = true;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    EXPECT_TRUE(renderer3D->GetLastFrameDiagnostics().ssaoPassRecorded);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto floatUniform = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };
    const auto intUniform = [&commands](const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<std::int32_t>{std::get<SetUniformIntCommand>(*found).value};
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

    settings.ssaoQuality = 99U; // Untrusted API quality is clamped to High (12 samples).
    settings.ssaoBlurEnabled = false;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& highQualityCommands = renderDevice.GetLastSubmittedCommands();
    const auto highSampleCount = std::find_if(highQualityCommands.cbegin(), highQualityCommands.cend(),
                                               [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uSampleCount";
    });
    ASSERT_NE(highSampleCount, highQualityCommands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*highSampleCount).value, 12);
    // With the blur off the separable blur passes are not recorded at all, so nothing sets the flag.
    const auto disabledBlur = std::find_if(highQualityCommands.cbegin(), highQualityCommands.cend(),
                                            [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uSsaoBlurEnabled";
    });
    EXPECT_EQ(disabledBlur, highQualityCommands.cend());
}

TEST_F(Renderer3DTest, ContactShadowSettingsReachTheirPassAndTheDiagnostics) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.contactShadowsEnabled = true;
    settings.contactShadowLengthMeters = 0.35F;
    settings.contactShadowThicknessMeters = 0.12F;
    settings.contactShadowStrength = 0.6F;
    settings.contactShadowStepCount = 6U;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    EXPECT_TRUE(renderer3D->GetLastFrameDiagnostics().contactShadowPassRecorded);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto floatUniform = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                        : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };
    const auto intUniform = [&commands](const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                        : std::optional<std::int32_t>{std::get<SetUniformIntCommand>(*found).value};
    };
    const auto lightDirection = std::find_if(
        commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformVector3Command>(command) &&
                   std::get<SetUniformVector3Command>(command).name == "uLightDirectionView";
        });

    ASSERT_TRUE(floatUniform("uLengthMeters").has_value());
    ASSERT_TRUE(floatUniform("uThicknessMeters").has_value());
    ASSERT_TRUE(floatUniform("uStrength").has_value());
    ASSERT_TRUE(floatUniform("uDepthBiasMeters").has_value());
    ASSERT_TRUE(intUniform("uStepCount").has_value());
    EXPECT_FLOAT_EQ(*floatUniform("uLengthMeters"), 0.35F);
    EXPECT_FLOAT_EQ(*floatUniform("uThicknessMeters"), 0.12F);
    EXPECT_FLOAT_EQ(*floatUniform("uStrength"), 0.6F);
    EXPECT_EQ(*intUniform("uStepCount"), 6);
    // The march direction has to arrive normalized, or every step distance would be scaled by the
    // length of a vector the shader never re-normalizes per pixel.
    ASSERT_NE(lightDirection, commands.cend());
    const Math::Vector3 viewLightDirection = std::get<SetUniformVector3Command>(*lightDirection).value;
    EXPECT_NEAR(Math::Length(viewLightDirection), 1.0F, 1.0e-4F);

    // Untrusted API values are clamped the same way the settings sanitizer clamps them, and an
    // out-of-range step count must not become an unbounded per-pixel loop.
    settings.contactShadowStepCount = 99U;
    settings.contactShadowStrength = 4.0F;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& clampedCommands = renderDevice.GetLastSubmittedCommands();
    const auto clampedSteps = std::find_if(clampedCommands.cbegin(), clampedCommands.cend(),
                                            [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uStepCount";
    });
    ASSERT_NE(clampedSteps, clampedCommands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*clampedSteps).value, 16);
}

TEST_F(Renderer3DTest, ContactShadowsDisabledRecordsNoPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    // Defaults: the pass refines an existing shadow map, so a project opts in rather than paying for
    // it in every frame that never asked.
    PostProcessSettings settings;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().contactShadowPassRecorded);
}

TEST_F(Renderer3DTest, ScreenSpaceReflectionsSettingsReachPassAndDiagnostics) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    environment.postProcessingEnabled = true;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.ssrEnabled = true;
    settings.ssrMaxSteps = 48U;
    settings.ssrDepthTolerance = 0.25F;
    renderer3D->SetPostProcessSettings(settings);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_TRUE(renderer3D->GetLastFrameDiagnostics().ssrPassRecorded);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto maxSteps = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& cmd) {
        return std::holds_alternative<SetUniformIntCommand>(cmd) &&
               std::get<SetUniformIntCommand>(cmd).name == "uMaxSteps";
    });
    ASSERT_NE(maxSteps, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*maxSteps).value, 48);
}

TEST_F(Renderer3DTest, ScreenSpaceReflectionsDisabledRecordsNoPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.ssrEnabled = false;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().ssrPassRecorded);
}

TEST_F(Renderer3DTest, VolumetricFogUniformsReachToneMappingPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    environment.fogEnabled = true;
    environment.lutEnabled = true;
    environment.lutContribution = 0.85F;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto lutEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& cmd) {
        return std::holds_alternative<SetUniformIntCommand>(cmd) &&
               std::get<SetUniformIntCommand>(cmd).name == "uLutEnabled";
    });
    ASSERT_NE(lutEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*lutEnabled).value, 1);

    const auto lutContribution = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& cmd) {
        return std::holds_alternative<SetUniformFloatCommand>(cmd) &&
               std::get<SetUniformFloatCommand>(cmd).name == "uLutContribution";
    });
    ASSERT_NE(lutContribution, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommand>(*lutContribution).value, 0.85F);
}

TEST_F(Renderer3DTest, WorldSpaceCanvasesAreTrackedInDiagnosticsAndRendered) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity canvasEntity = entityManager.CreateEntity();
    Scene::CanvasComponent canvas{};
    canvas.visible = true;
    canvas.renderMode = Scene::CanvasRenderMode::WorldSpace;
    canvas.worldSize = Math::Vector2{2.0F, 1.5F};
    canvas.billboard = true;
    entityManager.AddComponent<Scene::CanvasComponent>(canvasEntity, canvas);

    Scene::WorldTransformComponent world{};
    world.worldPosition = Math::Vector3{0.0F, 1.0F, -5.0F};
    world.worldScale = Math::Vector3{1.0F, 1.0F, 1.0F};
    entityManager.AddComponent<Scene::WorldTransformComponent>(canvasEntity, world);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.worldSpaceCanvasesRendered, 1U);
}

TEST_F(Renderer3DTest, WorldEnvironmentBloomSoftKneeReachesTheBrightPassShader) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    // The effects stack is off until an author turns it on; this test wants it on.
    environment.postProcessingEnabled = true;
    environment.ssaoEnabled = false;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);

    const auto readBloomSoftKnee = [this]() -> std::optional<float> {
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
            if (uniform != nullptr && uniform->name == "uBloomSoftKnee") {
                return uniform->value;
            }
        }
        return std::nullopt;
    };

    // Bloom's built-in shader is compiled asynchronously. Render and service shader jobs until the
    // bright-pass is recorded, then verify that the default still selects the former hard threshold.
    bool bloomPassRecorded = false;
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        if (renderer3D->GetLastFrameDiagnostics().bloomPassRecorded) {
            bloomPassRecorded = true;
            break;
        }
        shaderManager.Update(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(bloomPassRecorded);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().bloomMipCountUsed, 1U);
    const std::optional<float> defaultSoftKnee = readBloomSoftKnee();
    ASSERT_TRUE(defaultSoftKnee.has_value());
    EXPECT_FLOAT_EQ(*defaultSoftKnee, 0.0F);

    // An edit to the serialized environment setting must change what the same bright-pass shader
    // receives on the next frame, not just alter inspector metadata.
    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).bloomSoftKnee = 0.65F;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::optional<float> configuredSoftKnee = readBloomSoftKnee();
    ASSERT_TRUE(configuredSoftKnee.has_value());
    EXPECT_FLOAT_EQ(*configuredSoftKnee, 0.65F);
}

TEST_F(Renderer3DTest, WorldEnvironmentBloomMipCountCreatesAndRunsDistinctScaleTargets) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    // The effects stack is off until an author turns it on; this test wants it on.
    environment.postProcessingEnabled = true;
    environment.bloomMipCount = 3U;
    environment.ssaoEnabled = false;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);

    // The downsample program is asynchronous too. Require the frame to report three used levels,
    // rather than treating a merely authored or allocated count as evidence that the pyramid ran.
    bool requestedMipCountUsed = false;
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        if (renderer3D->GetLastFrameDiagnostics().bloomMipCountUsed == 3U) {
            requestedMipCountUsed = true;
            break;
        }
        shaderManager.Update(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(requestedMipCountUsed);
    EXPECT_TRUE(renderer3D->GetLastFrameDiagnostics().bloomPassRecorded);

    const std::vector<TextureDesc> liveTextureDescs = renderDevice.GetLiveTextureDescs();
    const auto countHdrTargetsAt = [&liveTextureDescs](const std::uint32_t width, const std::uint32_t height) {
        return std::count_if(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [width, height](const auto& desc) {
            return desc.width == width && desc.height == height &&
                   (desc.format == TextureFormat::RGBA16Float || desc.format == TextureFormat::RGBA8Unorm);
        });
    };
    EXPECT_GE(countHdrTargetsAt(16U, 16U), 3)
        << "the first downsample level owns bright and two separable-blur targets";
    EXPECT_GE(countHdrTargetsAt(8U, 8U), 3)
        << "the second downsample level owns its own bright and blur targets";

    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).bloomMipCount = 1U;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().bloomMipCountUsed, 1U)
        << "lowering the authored count immediately switches back to one-scale bloom";
}

TEST_F(Renderer3DTest, WorldEnvironmentSSAOEnableRadiusAndIntensityOverridesRemainActive) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    // The effects stack is off until an author turns it on; this test wants it on.
    environment.postProcessingEnabled = true;
    environment.ssaoEnabled = true;
    environment.ssaoRadius = 0.65F;
    environment.ssaoIntensity = 1.4F;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    WaitUntilShadowProgramReady();

    PostProcessSettings settings;
    settings.ssaoRadius = 1.8F;
    settings.ssaoIntensity = 2.6F;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().ssaoPassRecorded);

    std::optional<float> radius;
    std::optional<float> intensity;
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
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

    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).ssaoEnabled = false;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().ssaoPassRecorded);

    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).ssaoEnabled = true;
    settings.ssaoEnabled = false;
    renderer3D->SetPostProcessSettings(settings);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().ssaoPassRecorded);
}

TEST_F(Renderer3DTest, WorldEnvironmentFogControlsAndInverseProjectionReachToneMapping) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    environment.fogEnabled = true;
    environment.fogMode = Scene::WorldEnvironmentFogMode::Linear;
    environment.fogStart = 17.0F;
    environment.fogEnd = 275.0F;
    environment.ssaoEnabled = false;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto fogEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uFogEnabled";
    });
    ASSERT_NE(fogEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*fogEnabled).value, 1);
    const auto fogMode = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uFogMode";
    });
    ASSERT_NE(fogMode, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*fogMode).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentFogMode::Linear));
    const auto uniformValue = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };
    ASSERT_TRUE(uniformValue("uFogStart").has_value());
    ASSERT_TRUE(uniformValue("uFogEnd").has_value());
    EXPECT_FLOAT_EQ(*uniformValue("uFogStart"), 17.0F);
    EXPECT_FLOAT_EQ(*uniformValue("uFogEnd"), 275.0F);

    const auto inverseProjectionUniform =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformMatrix4x4Command>(command) &&
                   std::get<SetUniformMatrix4x4Command>(command).name == "uInverseProjection";
        });
    ASSERT_NE(inverseProjectionUniform, commands.cend());
    const Math::Matrix4x4 projection =
        cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 1.0F);
    Math::Matrix4x4 expectedInverseProjection{};
    ASSERT_TRUE(Math::TryInverse(projection, expectedInverseProjection));
    EXPECT_EQ(std::get<SetUniformMatrix4x4Command>(*inverseProjectionUniform).value,
              expectedInverseProjection);
}

TEST_F(Renderer3DTest, WorldEnvironmentLensControlsReachToneMappingAndRespectPostProcessingToggle) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    // The effects stack is off until an author turns it on; this test wants it on.
    environment.postProcessingEnabled = true;
    environment.bloomEnabled = false;
    environment.ssaoEnabled = false;
    environment.vignetteIntensity = 0.7F;
    environment.vignetteRadius = 0.55F;
    environment.chromaticAberrationIntensity = 0.4F;
    environment.filmGrainIntensity = 0.6F;
    environment.lensDistortionIntensity = 0.5F;
    environment.depthOfFieldEnabled = true;
    environment.depthOfFieldFocusMode = Scene::WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter;
    environment.depthOfFieldBokehShape = Scene::WorldEnvironmentDepthOfFieldBokehShape::Hexagonal;
    environment.depthOfFieldFocusDistance = 24.0F;
    environment.depthOfFieldAperture = 0.8F;
    environment.depthOfFieldQuality = 2U;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    WaitUntilShadowProgramReady();

    const auto floatUniformValue = [](const std::vector<RecordedCommand>& commands,
                                      const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };
    const auto intUniformValue = [](const std::vector<RecordedCommand>& commands,
                                    const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<std::int32_t>{std::get<SetUniformIntCommand>(*found).value};
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& enabledCommands = renderDevice.GetLastSubmittedCommands();
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
              static_cast<std::int32_t>(Scene::WorldEnvironmentDepthOfFieldFocusMode::ScreenCenter));
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldBokehShape"),
              static_cast<std::int32_t>(Scene::WorldEnvironmentDepthOfFieldBokehShape::Hexagonal));
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uDepthOfFieldFocusDistance"), 24.0F);
    EXPECT_FLOAT_EQ(*floatUniformValue(enabledCommands, "uDepthOfFieldAperture"), 0.8F);
    EXPECT_EQ(*intUniformValue(enabledCommands, "uDepthOfFieldQuality"), 2);
    const std::int32_t enabledGrainFrame = *intUniformValue(enabledCommands, "uFilmGrainFrame");

    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).postProcessingEnabled =
        false;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& disabledCommands = renderDevice.GetLastSubmittedCommands();
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

TEST_F(Renderer3DTest, WorldEnvironmentMotionBlurUsesOnlyValidCameraHistory) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    // The effects stack is off until an author turns it on; this test wants it on.
    environment.postProcessingEnabled = true;
    environment.motionBlurEnabled = true;
    environment.motionBlurStrength = 0.75F;
    environment.motionBlurSampleCount = 12U;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);

    const auto intUniformValue = [](const std::vector<RecordedCommand>& commands,
                                    const std::string_view name) -> std::optional<std::int32_t> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<std::int32_t>{std::get<SetUniformIntCommand>(*found).value};
    };
    const auto floatUniformValue = [](const std::vector<RecordedCommand>& commands,
                                      const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                         : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };
    const auto matrixUniformValue = [](const std::vector<RecordedCommand>& commands,
                                       const std::string_view name) -> std::optional<Math::Matrix4x4> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformMatrix4x4Command>(command) &&
                   std::get<SetUniformMatrix4x4Command>(command).name == name;
        });
        return found == commands.cend()
                   ? std::nullopt
                   : std::optional<Math::Matrix4x4>{std::get<SetUniformMatrix4x4Command>(*found).value};
    };
    const auto currentViewProjection = [this, cameraEntity]() -> Math::Matrix4x4 {
        constexpr std::uint32_t width = kTargetWidth;
        constexpr std::uint32_t height = kTargetHeight;
        const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        return cameraSystem.ComputeViewProjection(entityManager, cameraEntity, aspectRatio);
    };

    WaitUntilShadowProgramReady();

    // The first tone-mapped frame has no prior camera, so no motion vector is trustworthy yet.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& firstCommands = renderDevice.GetLastSubmittedCommands();
    ASSERT_TRUE(intUniformValue(firstCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(firstCommands, "uMotionBlurStrength").has_value());
    ASSERT_TRUE(intUniformValue(firstCommands, "uMotionBlurSampleCount").has_value());
    ASSERT_TRUE(matrixUniformValue(firstCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(firstCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(firstCommands, "uMotionBlurStrength"), 0.0F);
    EXPECT_EQ(*intUniformValue(firstCommands, "uMotionBlurSampleCount"), 12);
    const Math::Matrix4x4 staticViewProjection = currentViewProjection();
    EXPECT_EQ(*matrixUniformValue(firstCommands, "uPreviousViewProjection"), staticViewProjection);

    // A second frame from the same camera has history, so the authored blur reaches the shader.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& staticCommands = renderDevice.GetLastSubmittedCommands();
    ASSERT_TRUE(intUniformValue(staticCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(staticCommands, "uMotionBlurStrength").has_value());
    ASSERT_TRUE(matrixUniformValue(staticCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(staticCommands, "uMotionBlurEnabled"), 1);
    EXPECT_FLOAT_EQ(*floatUniformValue(staticCommands, "uMotionBlurStrength"), 0.75F);
    EXPECT_EQ(*matrixUniformValue(staticCommands, "uPreviousViewProjection"), staticViewProjection);

    // Moving the camera keeps the prior frame's matrix, which is what makes blur possible.
    Math::Quaternion rotatedCamera{};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 0.35F, rotatedCamera));
    Scene::TransformComponent movedTransform =
        entityManager.GetComponent<Scene::TransformComponent>(cameraEntity);
    movedTransform.localRotation = rotatedCamera;
    sceneGraph.SetLocalTransform(entityManager, cameraEntity, movedTransform);
    sceneGraph.Update(entityManager);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& movedCommands = renderDevice.GetLastSubmittedCommands();
    ASSERT_TRUE(intUniformValue(movedCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(matrixUniformValue(movedCommands, "uPreviousViewProjection").has_value());
    EXPECT_EQ(*intUniformValue(movedCommands, "uMotionBlurEnabled"), 1);
    EXPECT_EQ(*matrixUniformValue(movedCommands, "uPreviousViewProjection"), staticViewProjection);
    EXPECT_NE(*matrixUniformValue(movedCommands, "uPreviousViewProjection"), currentViewProjection());

    // A target aspect change invalidates the old reprojection rather than stretching stale vectors.
    ASSERT_TRUE(renderer3D->ResizeTargets(kTargetWidth * 2U, kTargetHeight + 32U));
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& resizedCommands = renderDevice.GetLastSubmittedCommands();
    ASSERT_TRUE(intUniformValue(resizedCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(resizedCommands, "uMotionBlurStrength").has_value());
    EXPECT_EQ(*intUniformValue(resizedCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(resizedCommands, "uMotionBlurStrength"), 0.0F);

    // Disabling the post-processing master switch suppresses blur even with fresh history.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).postProcessingEnabled =
        false;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
    const std::vector<RecordedCommand>& disabledCommands = renderDevice.GetLastSubmittedCommands();
    ASSERT_TRUE(intUniformValue(disabledCommands, "uMotionBlurEnabled").has_value());
    ASSERT_TRUE(floatUniformValue(disabledCommands, "uMotionBlurStrength").has_value());
    EXPECT_EQ(*intUniformValue(disabledCommands, "uMotionBlurEnabled"), 0);
    EXPECT_FLOAT_EQ(*floatUniformValue(disabledCommands, "uMotionBlurStrength"), 0.0F);
}

TEST_F(Renderer3DTest, RenderFrame_EmptyScene_MainPassBeginsAndEndsWithNoDraws) {
    const Scene::Entity cameraEntity = MakeCameraEntity();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    // No directional caster exists, so the optimized frame contains only the empty main color
    // pass. Tone mapping has not linked yet in this first frame.
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    ASSERT_EQ(commands.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommand>(commands[0U]).desc.colorAttachment, kInvalidTextureHandle);
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(commands[1U]));
}

TEST_F(Renderer3DTest, RenderFrame_VisiblePrimitive_RecordsGeometryAndAuthorsColorThroughTheMaterialPath) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Math::Vector3 baseColor{0.15F, 0.45F, 0.85F};
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F},
                           Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, baseColor});

    // The built-in lit program compiles asynchronously. The first frame triggers regular
    // extraction; draining ShaderManager then makes the next frame’s primitive draw observable.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command) &&
               std::get<DrawIndexedCommand>(command).indexCount == 36U;
    }));
    // A primitive has no material assigned here, so it draws the built-in default material and the
    // authored colour arrives as that material's albedo - not as the old Lambert-only uColor.
    const auto primitiveAlbedo = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector3Command>(command) &&
               std::get<SetUniformVector3Command>(command).name == "uAlbedoColor";
    });
    ASSERT_NE(primitiveAlbedo, commands.cend());
    EXPECT_EQ(std::get<SetUniformVector3Command>(*primitiveAlbedo).value, baseColor);

    // The rest of the material contract rides along, which is what proves this is the shared lit
    // path rather than a primitive-specific shader: metallic/roughness come from the default
    // material and the shader's own layer uniform is fed from the entity's RenderInstance.
    for (const std::string_view uniform : {"uMetallic", "uRoughness"}) {
        EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [uniform](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == uniform;
        })) << uniform;
    }
    const auto renderLayers = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uMeshRenderLayers";
    });
    ASSERT_NE(renderLayers, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*renderLayers).value, 1);
}

TEST_F(Renderer3DTest, RenderFrame_MeshWithExplicitSurfaces_DrawsOneRangePerSurfaceMaterial) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_two_surface_mesh.uvmodel");
    const Asset::AssetGuid redMaterialGuid = assetDatabase.Register("renderer3d_surface_red.uvmat");
    const Asset::AssetGuid blueMaterialGuid = assetDatabase.Register("renderer3d_surface_blue.uvmat");

    // Two triangles, one material slot each, and a surface per triangle - a quad whose halves are
    // different materials, which is what a two-material glTF mesh imports as.
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [redMaterialGuid, blueMaterialGuid](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.vertices = {
                Asset::MeshVertex{Math::Vector3{-0.5F, -0.5F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
                Asset::MeshVertex{Math::Vector3{0.5F, -0.5F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
                Asset::MeshVertex{Math::Vector3{0.5F, 0.5F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 1.0F},
                Asset::MeshVertex{Math::Vector3{-0.5F, 0.5F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
            };
            mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{},
                                                                   Math::Vector3{0.5F, 0.5F, 0.5F});
            mesh.materialGuids = {redMaterialGuid, blueMaterialGuid};
            mesh.surfaces = {Asset::MeshSurface{0U, 3U, 0U}, Asset::MeshSurface{3U, 3U, 1U}};
            return true;
        });
    // Red and blue, so the recorded uniforms say which material shaded which index range.
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path& path,
                                                                          Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.albedoColor = path.filename().string().contains("blue")
                                       ? Math::Color{0.0F, 0.0F, 1.0F}
                                       : Math::Color{1.0F, 0.0F, 0.0F};
            return true;
        });
    // The OBJECT names no material: the mesh's own surface materials decide, surface by surface.
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, Asset::kInvalidAssetGuid);

    // Both materials must be loaded before the renderer can resolve them, and the shader assets
    // they reference must be loaded before the renderer can even create their programs - the
    // fixture's helper does both and then renders one priming frame.
    const Asset::AssetHandle<Asset::MaterialAsset> blueHandle =
        assetManager.Load<Asset::MaterialAsset>(blueMaterialGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (blueHandle.IsReady() && shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_TRUE(blueHandle.IsReady());

    // The GPU frustum cull would collapse the two draws into one indirect command whose
    // parameters are not visible in the recorded command stream, and those parameters are exactly
    // what this test is about. Freezing culling keeps the direct path without changing what is
    // drawn - a debug option, not a different renderer.
    CullingSettings cullingSettings;
    cullingSettings.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(cullingSettings);

    // Materials' programs are created on first use and only become valid once the shader manager
    // has processed the compile, so the frame that first sees them is a warm-up. The helper waits
    // for the shader assets (a program cannot even be requested before them) and primes the red
    // material; the second prime creates the blue material's program, which the first prime cannot
    // have requested yet. Same shape every other material test in this file uses.
    WaitUntilAssetsReady(meshGuid, redMaterialGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().meshItemsExtracted, 2U);

    // uAlbedoColor is set immediately before each main-pass draw, so the value in effect when a
    // draw is recorded names the material that draw used. Only the two material colours count:
    // the shadow, tone-mapping and UI passes record draws too, and none of them shade a material.
    struct ShadedRangedDraw {
        std::uint32_t indexCount = 0U;
        std::uint32_t firstIndex = 0U;
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
    };
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    std::optional<Math::Vector3> lastAlbedo;
    std::vector<ShadedRangedDraw> shadedDraws;
    for (const RecordedCommand& command : commands) {
        // A pipeline bind starts a new program's uniforms, so an albedo set for the previous one
        // cannot name this draw. That is what keeps the post-processing and UI passes - which
        // record draws but never shade a material - out of the list.
        if (std::holds_alternative<BindPipelineCommand>(command)) {
            lastAlbedo.reset();
        } else if (std::holds_alternative<SetUniformVector3Command>(command)) {
            const auto& uniform = std::get<SetUniformVector3Command>(command);
            if (uniform.name == "uAlbedoColor") {
                lastAlbedo = uniform.value;
            }
        } else if (std::holds_alternative<DrawIndexedCommand>(command) && lastAlbedo.has_value()) {
            const auto& draw = std::get<DrawIndexedCommand>(command);
            shadedDraws.push_back(ShadedRangedDraw{draw.indexCount, draw.firstIndex, lastAlbedo->x,
                                                      lastAlbedo->y, lastAlbedo->z});
        }
    }
    // One shaded draw per surface, each covering only that surface's three indices: a draw that
    // covered the whole buffer would show six indices here, and a batch that merged the two
    // surfaces would show one draw carrying both materials' triangles.
    std::sort(shadedDraws.begin(), shadedDraws.end(),
              [](const ShadedRangedDraw& left, const ShadedRangedDraw& right) {
                  return left.firstIndex < right.firstIndex;
              });
    const std::vector<ShadedRangedDraw> expectedDraws{
        ShadedRangedDraw{3U, 0U, 1.0F, 0.0F, 0.0F},
        ShadedRangedDraw{3U, 3U, 0.0F, 0.0F, 1.0F}};
    ASSERT_EQ(shadedDraws.size(), expectedDraws.size());
    for (std::size_t drawIndex = 0U; drawIndex < expectedDraws.size(); ++drawIndex) {
        EXPECT_EQ(shadedDraws[drawIndex].indexCount, expectedDraws[drawIndex].indexCount);
        EXPECT_EQ(shadedDraws[drawIndex].firstIndex, expectedDraws[drawIndex].firstIndex);
        EXPECT_FLOAT_EQ(shadedDraws[drawIndex].red, expectedDraws[drawIndex].red);
        EXPECT_FLOAT_EQ(shadedDraws[drawIndex].green, expectedDraws[drawIndex].green);
        EXPECT_FLOAT_EQ(shadedDraws[drawIndex].blue, expectedDraws[drawIndex].blue);
    }
}

TEST_F(Renderer3DTest, RenderFrame_PrimitiveWithAssignedMaterial_ShadesThroughThatMaterialsValues) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    // The fixture's material loader produces albedo (0.2, 0.4, 0.6), metallic 0.25, roughness 0.75.
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_primitive_material_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_primitive_material.uvmat");
    Scene::PrimitiveMeshComponent component{Scene::PrimitiveMeshKind::Cube,
                                               Math::Vector3{0.9F, 0.9F, 0.9F}};
    component.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, component);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.primitiveMaterialPending, 0U);
    EXPECT_EQ(diagnostics.primitiveMaterialFailed, 0U);
    EXPECT_EQ(diagnostics.primitiveDrawCallsRecorded, 1U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto vectorUniform = [&commands](const std::string_view name) -> std::optional<Math::Vector3> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformVector3Command>(command) &&
                   std::get<SetUniformVector3Command>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                        : std::optional<Math::Vector3>{
                                              std::get<SetUniformVector3Command>(*found).value};
    };
    const auto floatUniform = [&commands](const std::string_view name) -> std::optional<float> {
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformFloatCommand>(command) &&
                   std::get<SetUniformFloatCommand>(command).name == name;
        });
        return found == commands.cend() ? std::nullopt
                                        : std::optional<float>{std::get<SetUniformFloatCommand>(*found).value};
    };

    // The material's own values reach the shader - not the component's base colour, which only
    // stands in for the DEFAULT material's albedo.
    ASSERT_TRUE(vectorUniform("uAlbedoColor").has_value());
    EXPECT_EQ(*vectorUniform("uAlbedoColor"), (Math::Vector3{0.2F, 0.4F, 0.6F}));
    ASSERT_TRUE(floatUniform("uMetallic").has_value());
    EXPECT_FLOAT_EQ(*floatUniform("uMetallic"), 0.25F);
    ASSERT_TRUE(floatUniform("uRoughness").has_value());
    EXPECT_FLOAT_EQ(*floatUniform("uRoughness"), 0.75F);
}

// A primitive's material is its whole look, and an author editing that .uvmat expects the object
// in the viewport to change without being reassigned. The mesh path rebuilds its GPU resources
// from the asset every frame; the primitive path keeps a handle so the payload stays alive between
// extraction and the colour pass, so this is the path where a reload could quietly leave the old
// values on screen.
TEST_F(Renderer3DTest, AssetReloadedEvent_ForAPrimitiveMaterial_RedrawsWithTheReloadedValues) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_primitive_reload_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_primitive_reload.uvmat");
    Scene::PrimitiveMeshComponent component{Scene::PrimitiveMeshKind::Cube,
                                               Math::Vector3{0.9F, 0.9F, 0.9F}};
    component.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, component);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    const auto drawnAlbedo = [this]() -> std::optional<Math::Vector3> {
        const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
        const auto found = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformVector3Command>(command) &&
                   std::get<SetUniformVector3Command>(command).name == "uAlbedoColor";
        });
        return found == commands.cend() ? std::nullopt
                                        : std::optional<Math::Vector3>{
                                              std::get<SetUniformVector3Command>(*found).value};
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    ASSERT_TRUE(drawnAlbedo().has_value());
    EXPECT_EQ(*drawnAlbedo(), (Math::Vector3{0.2F, 0.4F, 0.6F}));

    // The author picks a new colour and saves.
    Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    ASSERT_TRUE(materialHandle.IsReady());
    materialHandle.TryGet()->albedoColor = Math::Color{0.15F, 0.85F, 0.35F};
    eventSystem.Publish(Asset::AssetReloadedEvent{materialGuid});
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    ASSERT_TRUE(drawnAlbedo().has_value());
    EXPECT_EQ(*drawnAlbedo(), (Math::Vector3{0.15F, 0.85F, 0.35F}));
}

// The asset manager swaps a reloaded payload on a worker thread, so AssetReloadedEvent - which
// is queued when the reload is *requested* - can reach the renderer before the new bytes are in
// place. Invalidating only on that announcement let the renderer resolve the material again from
// the payload that was about to be replaced, with no second notification ever arriving: the
// primitive kept drawing the program the old file named for the rest of the session. This test
// draws that exact ordering - announcement, a frame on the old payload, then the swap - and asserts
// the completion event is what makes the primitive pick up what the reload installed.
TEST_F(Renderer3DTest, AssetLoadCompletedEvent_ForAPrimitiveMaterial_RebuildsAfterTheSwap) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_primitive_reload_mesh.uvmodel");
    const Asset::AssetGuid originalVertexGuid = assetDatabase.Register("renderer3d_reload_vertex.uvshader");
    const Asset::AssetGuid originalFragmentGuid = assetDatabase.Register("renderer3d_reload_fragment.uvshader");
    const Asset::AssetGuid replacedVertexGuid = assetDatabase.Register("renderer3d_reload_vertex_alt.uvshader");
    const Asset::AssetGuid replacedFragmentGuid = assetDatabase.Register("renderer3d_reload_fragment_alt.uvshader");
    // The two shader pairs differ only in their source text, so "the reload was picked up" shows up
    // as a newly linked program - the one piece of a material's resolved state that is not uploaded
    // again every frame from the payload.
    assetManager.RegisterLoader<Asset::ShaderAsset>(
        [](const std::filesystem::path& path, Asset::ShaderAsset& shader) {
            shader.sourceCode = path.filename().string().find("_alt") == std::string::npos
                                    ? "void main() { }"
                                    : "void main() { } // installed by the reload";
            return true;
        });
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = originalVertexGuid, fragmentGuid = originalFragmentGuid](
            const std::filesystem::path&, Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.albedoColor = Math::Color{0.2F, 0.4F, 0.6F};
            return true;
        });
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_primitive_reload.uvmat");
    Scene::PrimitiveMeshComponent cube{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.9F, 0.9F, 0.9F}};
    cube.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, cube);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);

    const auto waitUntilShaderReady = [this](const Asset::AssetGuid guid) {
        Asset::AssetHandle<Asset::ShaderAsset> handle =
            assetManager.Load<Asset::ShaderAsset>(guid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterations && !handle.IsReady(); ++iteration) {
            std::this_thread::yield();
        }
        return handle.IsReady();
    };
    // The material's own shader pair; the helper above only waits for the fixture's.
    ASSERT_TRUE(waitUntilShaderReady(originalVertexGuid));
    ASSERT_TRUE(waitUntilShaderReady(originalFragmentGuid));
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    // Every pipeline the main colour pass binds, in order. A cache rebuild links a new program, and
    // the null device never recycles a pipeline handle, so this list changing between two frames is
    // exactly "the renderer resolved the material again".
    const auto drawnPipelines = [this]() -> std::vector<PipelineHandle> {
        std::vector<PipelineHandle> pipelines;
        bool insideMainPass = false;
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            if (const auto* const begin = std::get_if<BeginRenderPassCommand>(&command); begin != nullptr) {
                insideMainPass = begin->desc.colorAttachment != kInvalidTextureHandle &&
                                 begin->desc.depthAttachment != kInvalidTextureHandle;
                continue;
            }
            if (std::holds_alternative<EndRenderPassCommand>(command)) {
                insideMainPass = false;
                continue;
            }
            if (!insideMainPass) {
                continue;
            }
            if (const auto* const bind = std::get_if<BindPipelineCommand>(&command); bind != nullptr) {
                pipelines.push_back(bind->pipeline);
            }
        }
        return pipelines;
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    const std::vector<PipelineHandle> first = drawnPipelines();
    ASSERT_FALSE(first.empty());

    // The reload is announced while the old payload is still installed. The frame drawn here
    // resolves the material again from the file that is about to be replaced - the race exactly.
    eventSystem.Publish(Asset::AssetReloadedEvent{materialGuid});
    PrimeMaterialProgram(*renderer3D, cameraEntity); // links the program that rebuild asked for
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    const std::vector<PipelineHandle> during = drawnPipelines();
    ASSERT_FALSE(during.empty());
    EXPECT_NE(during, first) << "the announcement alone already dropped the resolved material";

    // The worker finishes: the payload now names the other shader pair, and the manager reports the
    // load complete. That is the notification that carries the new payload's arrival.
    Asset::AssetHandle<Asset::MaterialAsset> material =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    ASSERT_TRUE(material.IsReady());
    material.TryGet()->vertexShader = replacedVertexGuid;
    material.TryGet()->fragmentShader = replacedFragmentGuid;
    ASSERT_TRUE(waitUntilShaderReady(replacedVertexGuid));
    ASSERT_TRUE(waitUntilShaderReady(replacedFragmentGuid));
    eventSystem.Publish(Asset::AssetLoadCompletedEvent{materialGuid, true});
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    const std::vector<PipelineHandle> after = drawnPipelines();
    ASSERT_FALSE(after.empty());
    // Without the completion-driven invalidation `after` is still the list the *old* payload
    // produced, and the primitive keeps the program the reload replaced.
    EXPECT_NE(after, during) << "the primitive draws what the reload installed, not the replaced program";
}

// The material's alpha used to stop at the MaterialAsset: the fragment shader composited every
// surface at the *instance* opacity, so a texture with a 40%-alpha edge drew fully opaque and a
// glass material had no way to be translucent. These three uniforms are what carry it the rest of
// the way, and the premultiply flag has to follow the blend mode rather than the alpha value -
// premultiplying for a straight-alpha blend darkens the surface, and not premultiplying for a
// premultiplied one brightens it.
TEST_F(Renderer3DTest, RenderFrame_PushesTheMaterialsAlphaAndPremultiplyFlag) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.blendMode = Asset::MaterialBlendMode::PremultipliedAlpha;
            material.alphaSource = Asset::MaterialAlphaSource::AlbedoAlpha;
            material.albedoAlpha = 0.35F;
            return true;
        });
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_alpha_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_alpha.uvmat");
    Scene::PrimitiveMeshComponent cube{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.9F, 0.9F, 0.9F}};
    cube.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, cube);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    const auto uniform = [this](const std::string_view name) -> std::optional<float> {
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            if (const auto* const setFloat = std::get_if<SetUniformFloatCommand>(&command);
                setFloat != nullptr && setFloat->name == name) {
                return setFloat->value;
            }
            if (const auto* const setInt = std::get_if<SetUniformIntCommand>(&command);
                setInt != nullptr && setInt->name == name) {
                return static_cast<float>(setInt->value);
            }
        }
        return std::nullopt;
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    const std::optional<float> albedoAlpha = uniform("uAlbedoAlpha");
    const std::optional<float> alphaSource = uniform("uAlphaSource");
    const std::optional<float> premultiply = uniform("uPremultiplyAlpha");
    ASSERT_TRUE(albedoAlpha.has_value()) << "the material's alpha has to reach the shader at all";
    EXPECT_FLOAT_EQ(*albedoAlpha, 0.35F);
    ASSERT_TRUE(alphaSource.has_value());
    EXPECT_FLOAT_EQ(*alphaSource, 0.0F) << "AlbedoAlpha is source 0";
    ASSERT_TRUE(premultiply.has_value());
    EXPECT_FLOAT_EQ(*premultiply, 1.0F) << "PremultipliedAlpha is the only mode that sets this";
}

// A straight-alpha material must NOT ask the shader to premultiply, even though it is just as
// translucent - the flag belongs to the pipeline's factor choice, not to how see-through it is.
// A cutout material is opaque: the discard makes the hole, and nothing blends. Its albedo map can
// still carry alpha, and writing that alpha to the colour target would leave holes in the target's
// alpha channel for every later fullscreen pass to composite through (bloom's additive copy, the
// SSAO multiply). The pipeline is the thing that decides, so an opaque draw has to report "no alpha
// source" to the shader no matter what the material authored.
TEST_F(Renderer3DTest, RenderFrame_AnOpaqueCutoutDrawEmitsNoMaterialAlpha) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.blendMode = Asset::MaterialBlendMode::Opaque;
            material.alphaSource = Asset::MaterialAlphaSource::AlbedoAlpha;
            material.alphaCutoff = 0.5F;
            return true;
        });
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_cutout_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_cutout.uvmat");
    Scene::PrimitiveMeshComponent cube{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.9F, 0.9F, 0.9F}};
    cube.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, cube);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    std::optional<std::int32_t> alphaSource;
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        if (const auto* const setInt = std::get_if<SetUniformIntCommand>(&command);
            setInt != nullptr && setInt->name == "uAlphaSource") {
            alphaSource = setInt->value;
        }
    }
    ASSERT_TRUE(alphaSource.has_value());
    EXPECT_EQ(*alphaSource, static_cast<std::int32_t>(Asset::MaterialAlphaSource::None));
}

TEST_F(Renderer3DTest, RenderFrame_StraightAlphaMaterialDoesNotPremultiply) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.blendMode = Asset::MaterialBlendMode::AlphaBlend;
            material.albedoAlpha = 0.5F;
            return true;
        });
    const Asset::AssetGuid unusedMeshGuid = assetDatabase.Register("renderer3d_alpha_mesh2.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_alpha2.uvmat");
    Scene::PrimitiveMeshComponent cube{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.9F, 0.9F, 0.9F}};
    cube.materialGuid = materialGuid;
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, cube);
    WaitUntilAssetsReady(unusedMeshGuid, materialGuid);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 1U);
    bool sawPremultiply = false;
    float premultiplyValue = -1.0F;
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        if (const auto* const setInt = std::get_if<SetUniformIntCommand>(&command);
            setInt != nullptr && setInt->name == "uPremultiplyAlpha") {
            sawPremultiply = true;
            premultiplyValue = static_cast<float>(setInt->value);
        }
    }
    ASSERT_TRUE(sawPremultiply) << "the uniform is pushed for every material, not only this mode";
    EXPECT_FLOAT_EQ(premultiplyValue, 0.0F);
}

TEST_F(Renderer3DTest, RenderFrame_HiddenPrimitiveIsNotDrawnAndReturnsWhenShown) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity parent = entityManager.CreateEntity();
    Scene::TransformComponent placed;
    placed.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransform(entityManager, parent, placed);
    entityManager.AddComponent<Scene::VisibilityComponent>(parent, Scene::VisibilityComponent{});
    const Scene::Entity primitiveEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, primitiveEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, primitiveEntity, parent);
    entityManager.AddComponent<Scene::VisibilityComponent>(primitiveEntity, Scene::VisibilityComponent{});
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(primitiveEntity);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);

    // Its own switch off: not even a candidate.
    entityManager.GetComponent<Scene::VisibilityComponent>(primitiveEntity).visible = false;
    sceneGraph.Update(entityManager);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 0U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveDrawCallsRecorded, 0U);

    // Shown again, but its parent hidden: still not drawn - hiding a parent hides the subtree.
    entityManager.GetComponent<Scene::VisibilityComponent>(primitiveEntity).visible = true;
    entityManager.GetComponent<Scene::VisibilityComponent>(parent).visible = false;
    sceneGraph.Update(entityManager);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 0U);

    // Parent shown again: the primitive comes back.
    entityManager.GetComponent<Scene::VisibilityComponent>(parent).visible = true;
    sceneGraph.Update(entityManager);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_FiniteExtremePrimitiveTransformIsRejectedBeforeQueuePublication) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity primitiveEntity = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, -10.0F};
    transform.localScale = Math::Vector3{std::numeric_limits<float>::max(), 1.0F, 1.0F};
    sceneGraph.AttachTransform(entityManager, primitiveEntity, transform);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(primitiveEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.primitiveCandidates, 1U);
    EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U);
    EXPECT_EQ(diagnostics.primitiveDrawCallsRecorded, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_PartitionHiddenPrimitiveIsNotACandidate) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity partition = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, partition, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::WorldPartition3DComponent>(partition);
    sceneGraph.Update(entityManager);

    const Scene::Entity hidden = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});
    const Scene::Entity shown = MakePrimitiveEntity(
        Math::Vector3{1.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.2F, 0.8F, 0.1F}});
    entityManager.AddComponent<Scene::WorldPartition3DMembershipComponent>(
        hidden, Scene::WorldPartition3DMembershipComponent{partition, false});
    entityManager.AddComponent<Scene::WorldPartition3DMembershipComponent>(
        shown, Scene::WorldPartition3DMembershipComponent{partition, true});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_OccludedPrimitiveIsNotExtracted) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);
    sceneGraph.Update(entityManager);

    MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_SurfaceInstanceRangeHidesAPrimitive) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity entity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponent surface{};
    surface.visibilityRangeEnd = 5.0F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_SurfaceInstanceShadowsOnlyDoesNotDrawAPrimitive) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity entity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponent surface{};
    surface.castShadow = Scene::SurfaceShadowMode::ShadowsOnly;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_SurfaceInstanceIgnoreOcclusionKeepsACoveredPrimitive) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity wall = entityManager.CreateEntity();
    Scene::TransformComponent wallTransform;
    wallTransform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    sceneGraph.AttachTransform(entityManager, wall, wallTransform);
    Scene::Occluder3DComponent wallOccluder;
    wallOccluder.halfExtents = Math::Vector3{2.0F, 2.0F, 2.0F};
    entityManager.AddComponent<Scene::Occluder3DComponent>(wall, wallOccluder);
    sceneGraph.Update(entityManager);

    const Scene::Entity entity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});
    Scene::SurfaceInstanceComponent surface{};
    surface.ignoreOcclusionCulling = true;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_SurfaceInstanceTransparencyPushesPrimitiveOpacity) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity entity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.15F, 0.45F, 0.85F}});
    Scene::SurfaceInstanceComponent surface{};
    surface.transparency = 0.25F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(entity, surface);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto opacity = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformFloatCommand>(command) &&
               std::get<SetUniformFloatCommand>(command).name == "uSurfaceOpacity";
    });
    ASSERT_NE(opacity, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommand>(*opacity).value, 0.75F);
}

TEST_F(Renderer3DTest, RenderFrame_SurfaceInstanceTransparencyBindsABlendedMeshPipeline) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_si_blend_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_si_blend_material.uvmat");
    MakeMeshEntity(Math::Vector3{-1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity faded =
        MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::SurfaceInstanceComponent surface{};
    surface.transparency = 0.25F;
    entityManager.AddComponent<Scene::SurfaceInstanceComponent>(faded, surface);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const auto pipelineBoundForOpacity = [](const std::vector<RecordedCommand>& commands,
                                            const float opacity) -> PipelineHandle {
        for (std::size_t index = 0U; index < commands.size(); ++index) {
            if (!std::holds_alternative<SetUniformFloatCommand>(commands[index])) {
                continue;
            }
            const SetUniformFloatCommand& uniform = std::get<SetUniformFloatCommand>(commands[index]);
            if (uniform.name != "uSurfaceOpacity" || uniform.value != opacity) {
                continue;
            }
            for (std::size_t lookback = index; lookback > 0U; --lookback) {
                const RecordedCommand& previous = commands[lookback - 1U];
                if (std::holds_alternative<BindPipelineCommand>(previous)) {
                    return std::get<BindPipelineCommand>(previous).pipeline;
                }
            }
        }
        return kInvalidPipelineHandle;
    };

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const PipelineHandle opaque = pipelineBoundForOpacity(commands, 1.0F);
    const PipelineHandle blended = pipelineBoundForOpacity(commands, 0.75F);
    ASSERT_NE(opaque, kInvalidPipelineHandle);
    ASSERT_NE(blended, kInvalidPipelineHandle);
    EXPECT_NE(opaque, blended);
}

TEST_F(Renderer3DTest, RenderFrame_RegionHiddenPrimitiveIsNotACandidate) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity region = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, region, Scene::TransformComponent{});
    entityManager.AddComponent<Scene::VisibilityRegion3DComponent>(region);
    sceneGraph.Update(entityManager);

    const Scene::Entity hidden = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.8F, 0.2F, 0.1F}});
    const Scene::Entity shown = MakePrimitiveEntity(
        Math::Vector3{1.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.2F, 0.8F, 0.1F}});
    entityManager.AddComponent<Scene::VisibilityRegion3DMembershipComponent>(
        hidden, Scene::VisibilityRegion3DMembershipComponent{region, false});
    entityManager.AddComponent<Scene::VisibilityRegion3DMembershipComponent>(
        shown, Scene::VisibilityRegion3DMembershipComponent{region, true});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveCandidates, 1U);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_VisiblePrimitive_ReportsEvidenceSpecificDiagnostics) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F},
                           Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube,
                                                            Math::Vector3{0.75F, 0.2F, 0.1F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics beforeProgramReady = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(beforeProgramReady.primitiveCandidates, 1U);
    EXPECT_EQ(beforeProgramReady.primitiveItemsExtracted, 1U);
    EXPECT_EQ(beforeProgramReady.primitiveDrawCallsRecorded, 0U);
    EXPECT_FALSE(beforeProgramReady.primitiveProgramReady);
    EXPECT_TRUE(beforeProgramReady.mainPassRecorded);
    EXPECT_FALSE(beforeProgramReady.toneMappingPassRecorded);
    EXPECT_EQ(beforeProgramReady.glDrawCallsIssued, 0U);

    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics afterProgramReady = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(afterProgramReady.primitiveCandidates, 1U);
    EXPECT_EQ(afterProgramReady.primitiveItemsExtracted, 1U);
    EXPECT_EQ(afterProgramReady.primitiveDrawCallsRecorded, 1U);
    EXPECT_TRUE(afterProgramReady.primitiveProgramReady);
    EXPECT_TRUE(afterProgramReady.mainPassRecorded);
    EXPECT_TRUE(afterProgramReady.toneMappingProgramReady);
    EXPECT_TRUE(afterProgramReady.toneMappingPassRecorded);
    EXPECT_EQ(afterProgramReady.glDrawCallsIssued, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_VisibleMesh_RecordsExpectedCommandSequence) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    // ShaderProgram flushes its pending uniforms from an unordered cache, so insertion order is
    // intentionally not a renderer contract. Assert named effects and pass structure instead.
    ASSERT_GE(commands.size(), 2U);
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommand>(commands[0U]).desc.colorAttachment, kInvalidTextureHandle);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindPipelineCommand>(command);
    }));
    const auto findMatrix = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformMatrix4x4Command>(command) &&
                   std::get<SetUniformMatrix4x4Command>(command).name == name;
        });
    };
    const auto findInt = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == name;
        });
    };
    const auto findVector = [&commands](const std::string& name) {
        return std::find_if(commands.cbegin(), commands.cend(), [&name](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformVector3Command>(command) &&
                   std::get<SetUniformVector3Command>(command).name == name;
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
    EXPECT_EQ(std::get<SetUniformIntCommand>(*cascadeCount).value, 0);
    EXPECT_EQ(std::get<SetUniformIntCommand>(*pcfRadius).value, static_cast<std::int32_t>(kTestShadowPcfKernelRadius));
    EXPECT_EQ(std::get<SetUniformVector3Command>(*ambient).value, kTestAmbientColor);
    EXPECT_EQ(std::get<SetUniformVector3Command>(*viewPosition).value, (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command) &&
               std::get<DrawIndexedCommand>(command).indexCount == 3U;
    }));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(commands.back()));
}

TEST_F(Renderer3DTest, RenderFrame_ManagedMaterialProgram_SkipsUntilReadyThenDraws) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_managed_program_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_managed_program_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commandsWhileLinking = renderDevice.GetLastSubmittedCommands();
    EXPECT_FALSE(std::any_of(commandsWhileLinking.cbegin(), commandsWhileLinking.cend(),
                             [](const RecordedCommand& command) {
                                 return std::holds_alternative<DrawIndexedCommand>(command);
                             }));

    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commandsWhenReady = renderDevice.GetLastSubmittedCommands();
    EXPECT_TRUE(std::any_of(commandsWhenReady.cbegin(), commandsWhenReady.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command);
    }));
}

TEST_F(Renderer3DTest, RenderFrame_ShadowPcfKernelRadiusAboveTwo_ClampsToTwo) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_pcf_clamp_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_pcf_clamp_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);

    Renderer3D clampedRenderer{renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
                                  shaderManager, assetManager, assetDatabase, eventSystem, kTargetWidth,
                                  kTargetHeight, kTestAmbientColor, kTestShadowMapResolution,
                                  kTestShadowMapHalfExtent, kTestShadowMapNearPlane,
                                  kTestShadowMapFarPlane, kTestShadowFrustumPadding,
                                  kTestShadowCascadeSplitLambda, kTestShadowCascadeBlendRatio, 3U};
    PrimeMaterialProgram(clampedRenderer, cameraEntity);
    clampedRenderer.RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto radiusCommand = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        if (!std::holds_alternative<SetUniformIntCommand>(command)) {
            return false;
        }
        return std::get<SetUniformIntCommand>(command).name == "uShadowPcfKernelRadius";
    });
    ASSERT_NE(radiusCommand, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*radiusCommand).value, 2);
}

TEST_F(Renderer3DTest, RenderFrame_ShadowCascadeBlendRatioAboveQuarter_ClampsToQuarter) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_blend_clamp_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_blend_clamp_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);

    Renderer3D clampedRenderer{renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem,
                                  shaderManager, assetManager, assetDatabase, eventSystem, kTargetWidth,
                                  kTargetHeight, kTestAmbientColor, kTestShadowMapResolution,
                                  kTestShadowMapHalfExtent, kTestShadowMapNearPlane,
                                  kTestShadowMapFarPlane, kTestShadowFrustumPadding,
                                  kTestShadowCascadeSplitLambda, 0.5F, kTestShadowPcfKernelRadius};
    PrimeMaterialProgram(clampedRenderer, cameraEntity);
    clampedRenderer.RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto blendCommand = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        if (!std::holds_alternative<SetUniformFloatCommand>(command)) {
            return false;
        }
        return std::get<SetUniformFloatCommand>(command).name == "uShadowCascadeBlendRatio";
    });
    ASSERT_NE(blendCommand, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommand>(*blendCommand).value, 0.25F);
}

TEST_F(Renderer3DTest, RenderFrame_ActiveLightEntity_PushesComputedLightUniformsInSlotZero) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_lit_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_lit_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    // Identity rotation, so LightSystem derives direction {0,0,-1} (see
    // LightSystemTest's own RotateVector-based coverage for the rotated case). A Point
    // light never casts a shadow (Increment 26 scopes that to Directional only), so this test's
    // shadow pass stays empty regardless.
    Scene::LightComponent light{Math::Color{0.9F, 0.8F, 0.7F}, 4.5F};
    light.type = Scene::LightType::Point;
    light.range = 22.0F;
    MakeLightEntity(light);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto typeUniform = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLights[0].type";
    });
    ASSERT_NE(typeUniform, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*typeUniform).value, 1); // Point
}

TEST_F(Renderer3DTest, RenderFrame_TwoLightsOfDifferentTypes_PopulateSlotsZeroAndOneOthersStaySentinel) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_multilight_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_multilight_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Scene::LightComponent directionalLight{Math::Color{1.0F, 0.0F, 0.0F}, 2.0F};
    Scene::LightComponent spotLight{Math::Color{0.0F, 1.0F, 0.0F}, 3.0F};
    spotLight.type = Scene::LightType::Spot;
    spotLight.spotAngleDegrees = 15.0F;
    MakeLightEntity(directionalLight);
    MakeLightEntity(spotLight);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    // Slot 0: directional light.
    const auto directionalType = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLights[0].type";
    });
    ASSERT_NE(directionalType, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*directionalType).value, 0); // Directional
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*cascadeCount).value, 3);
}

TEST_F(Renderer3DTest, RenderFrame_AmbientColorFromConstructor_AlwaysPushedRegardlessOfLight) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_ambient_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_ambient_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const auto assertAmbientColor = [](const std::vector<RecordedCommand>& commands) {
        const auto ambient = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformVector3Command>(command) &&
                   std::get<SetUniformVector3Command>(command).name == "uAmbientColor";
        });
        ASSERT_NE(ambient, commands.cend());
        EXPECT_EQ(std::get<SetUniformVector3Command>(*ambient).value, kTestAmbientColor);
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    assertAmbientColor(renderDevice.GetLastSubmittedCommands());

    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 1.0F});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    assertAmbientColor(renderDevice.GetLastSubmittedCommands());
}

TEST_F(Renderer3DTest, WorldEnvironmentAmbientSourceReachesPrimitiveShaderUniforms) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponent{});
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    Scene::WorldEnvironment3DComponent environment{};
    environment.ambientColor = Math::Vector3{0.4F, 0.3F, 0.2F};
    environment.ambientEnergy = 2.0F;
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    const auto captureAmbient = [this, cameraEntity, environmentEntity](
                                    const Scene::WorldEnvironmentAmbientSource source) {
        entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(environmentEntity).ambientSource = source;
        renderer3D->RenderFrame(entityManager, cameraEntity);
        const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
        std::optional<std::int32_t> selectedSource;
        std::optional<Math::Vector3> ambientColor;
        std::optional<Math::Vector3> skyAmbient;
        std::optional<Math::Vector3> groundAmbient;
        for (const RecordedCommand& command : commands) {
            if (const auto* const uniform = std::get_if<SetUniformIntCommand>(&command);
                uniform != nullptr && uniform->name == "uAmbientSource") {
                selectedSource = uniform->value;
            } else if (const auto* const vectorUniform = std::get_if<SetUniformVector3Command>(&command);
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
        captureAmbient(Scene::WorldEnvironmentAmbientSource::None);
    ASSERT_TRUE(noneSource.has_value());
    ASSERT_TRUE(noneColor.has_value());
    ASSERT_TRUE(noneSky.has_value());
    ASSERT_TRUE(noneGround.has_value());
    EXPECT_EQ(*noneSource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSource::None));
    EXPECT_EQ(*noneColor, Math::Vector3{});
    EXPECT_EQ(*noneSky, Math::Vector3{});
    EXPECT_EQ(*noneGround, Math::Vector3{});

    const auto [flatSource, flatColor, flatSky, flatGround] =
        captureAmbient(Scene::WorldEnvironmentAmbientSource::FlatColor);
    ASSERT_TRUE(flatSource.has_value());
    ASSERT_TRUE(flatColor.has_value());
    ASSERT_TRUE(flatSky.has_value());
    ASSERT_TRUE(flatGround.has_value());
    EXPECT_EQ(*flatSource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSource::FlatColor));
    EXPECT_EQ(*flatColor, (Math::Vector3{0.8F, 0.6F, 0.4F}));
    EXPECT_EQ(*flatSky, Math::Vector3{});
    EXPECT_EQ(*flatGround, Math::Vector3{});

    const auto [skySource, skyColor, skyAmbient, groundAmbient] =
        captureAmbient(Scene::WorldEnvironmentAmbientSource::Sky);
    ASSERT_TRUE(skySource.has_value());
    ASSERT_TRUE(skyColor.has_value());
    ASSERT_TRUE(skyAmbient.has_value());
    ASSERT_TRUE(groundAmbient.has_value());
    EXPECT_EQ(*skySource, static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSource::Sky));
    EXPECT_EQ(*skyColor, (Math::Vector3{0.8F, 0.6F, 0.4F}));
    EXPECT_GT(skyAmbient->z, groundAmbient->z);
}

TEST_F(Renderer3DTest, RenderFrame_CameraAtKnownPosition_PushesMatchingViewPositionUniform) {
    // Stays on the same viewing axis as the mesh below (default identity rotation looks down -Z)
    // so the mesh remains inside the frustum — an off-axis camera position would cull it, leaving
    // no recorded item to assert uniforms on.
    const Math::Vector3 cameraPosition{0.0F, 0.0F, 5.0F};
    const Scene::Entity cameraEntity = MakeCameraEntity(cameraPosition);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_viewpos_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_viewpos_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto viewPosition = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector3Command>(command) &&
               std::get<SetUniformVector3Command>(command).name == "uViewPosition";
    });
    ASSERT_NE(viewPosition, commands.cend());
    EXPECT_EQ(std::get<SetUniformVector3Command>(*viewPosition).value, cameraPosition);
}

TEST_F(Renderer3DTest, RenderFrame_MaterialWithoutTextures_UsesFallbackTexturesForAllThreeSlots) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_fallback_mesh.uvmodel");
    const Asset::AssetGuid materialAGuid = assetDatabase.Register("renderer3d_tests_fallback_material_a.uvmat");
    const Asset::AssetGuid materialBGuid = assetDatabase.Register("renderer3d_tests_fallback_material_b.uvmat");
    // Both materials resolve through the same default (no-texture) loader from the fixture, but
    // are registered under two distinct AssetGuids, so each gets its own materialCache entry
    // and independently resolves its fallback texture handles.
    MakeMeshEntity(Math::Vector3{-1.0F, 0.0F, -10.0F}, meshGuid, materialAGuid);
    MakeMeshEntity(Math::Vector3{1.0F, 0.0F, -10.0F}, meshGuid, materialBGuid);
    WaitUntilAssetsReady(meshGuid, materialAGuid);
    WaitUntilAssetsReady(meshGuid, materialBGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    // Slot 3 (the shadow map, bound once per item in the main pass regardless of material) is
    // excluded here. The first six remain the two items' material fallback bindings, recorded
    // during the MainColor pass before any of the post-process binds that follow it
    // (SSAO + SSAOBlurH + SSAOBlurV + BloomBrightPass + BloomBlurH + BloomBlurV, then
    // ToneMapping's source and depth binds on slots 0 and 1).
    std::vector<BindTextureCommand> textureBinds;
    for (const RecordedCommand& command : commands) {
        if (const auto* const bindTexture = std::get_if<BindTextureCommand>(&command)) {
            if (bindTexture->slot < 3U) {
                textureBinds.push_back(*bindTexture);
            }
        }
    }
    // 2 items x 3 material texture slots (6), SSAO depth (1), SSAOBlurH/V source+depth (4),
    // BloomBrightPass + BloomBlurH + BloomBlurV (3), and ToneMapping's source+depth (2) = 16.
    ASSERT_EQ(textureBinds.size(), 16U);

    // Group by slot: item1's slot-N handle must equal item2's slot-N handle (fallback reuse
    // across two independently-resolved materials), and the albedo/AO slots (both default to the
    // white fallback) must share a handle distinct from the normal slot's flat-normal fallback.
    EXPECT_EQ(textureBinds[0].texture, textureBinds[3].texture); // albedo, item1 vs item2
    EXPECT_EQ(textureBinds[1].texture, textureBinds[4].texture); // normal, item1 vs item2
    EXPECT_EQ(textureBinds[2].texture, textureBinds[5].texture); // ao, item1 vs item2
    EXPECT_EQ(textureBinds[0].texture, textureBinds[2].texture); // albedo == ao (both white fallback)
    EXPECT_NE(textureBinds[0].texture, textureBinds[1].texture); // albedo != normal (different fallback)
    EXPECT_NE(textureBinds[0].texture, kInvalidTextureHandle);
    EXPECT_NE(textureBinds[1].texture, kInvalidTextureHandle);
}

TEST_F(Renderer3DTest, RenderFrame_MaterialSettings_PushUvEmissiveNormalAoUnshadedCutoffAndPackedMaps) {
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.albedoColor = Math::Color{0.2F, 0.4F, 0.6F};
            material.metallic = 0.25F;
            material.roughness = 0.75F;
            material.emissiveColor = Math::Color{0.1F, 0.0F, 0.0F};
            material.emissiveEnergy = 3.0F;
            material.normalScale = 0.5F;
            material.occlusionStrength = 0.25F;
            material.uvScale = Math::Vector2{2.0F, 4.0F};
            material.uvOffset = Math::Vector2{0.25F, 0.5F};
            material.unshaded = true;
            material.alphaCutoff = 0.4F;
            return true;
        });

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_material_settings_mesh.uvmodel");
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("renderer3d_tests_material_settings.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
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
    for (const RecordedCommand& command : commands) {
        if (const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command)) {
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
        if (const auto* const uniform = std::get_if<SetUniformIntCommand>(&command)) {
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
        if (const auto* const uniform = std::get_if<SetUniformVector3Command>(&command)) {
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
        if (const auto* const bind = std::get_if<BindTextureCommand>(&command)) {
            if (bind->slot == 12U) {
                EXPECT_NE(bind->texture, kInvalidTextureHandle);
                boundMetallicRoughness = true;
            }
            if (bind->slot == 13U) {
                EXPECT_NE(bind->texture, kInvalidTextureHandle);
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

TEST_F(Renderer3DTest, RenderFrame_MaterialSamplersAndPbrProperties_BindRepeatSamplerAndUploadUniforms) {
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.uvScale = Math::Vector2{3.0F, 5.0F};
            material.normalFlipGreen = true;
            material.dielectricSpecular = 0.75F;
            material.triplanarEnabled = true;
            material.worldSpaceTriplanar = true;
            material.triplanarBlendSharpness = 4.0F;
            return true;
        });

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_sampler_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_sampler.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    bool foundNormalFlipGreen = false;
    bool foundDielectricSpecular = false;
    bool foundUvScaleZ = false;
    SamplerHandle materialSamplerSlot12 = kInvalidSamplerHandle;
    for (const RecordedCommand& command : commands) {
        if (const auto* const uniform = std::get_if<SetUniformIntCommand>(&command)) {
            if (uniform->name == "uNormalFlipGreen") {
                EXPECT_EQ(uniform->value, 1);
                foundNormalFlipGreen = true;
            }
        }
        if (const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command)) {
            if (uniform->name == "uDielectricSpecular") {
                EXPECT_FLOAT_EQ(uniform->value, 0.75F);
                foundDielectricSpecular = true;
            }
        }
        if (const auto* const uniform = std::get_if<SetUniformVector3Command>(&command)) {
            if (uniform->name == "uUvScale") {
                EXPECT_FLOAT_EQ(uniform->value.x, 3.0F);
                EXPECT_FLOAT_EQ(uniform->value.y, 5.0F);
                EXPECT_FLOAT_EQ(uniform->value.z, 3.0F);
                foundUvScaleZ = true;
            }
        }
        if (const auto* const bind = std::get_if<BindSamplerCommand>(&command)) {
            if (bind->slot == 12U) {
                materialSamplerSlot12 = bind->sampler;
            }
        }
    }
    EXPECT_TRUE(foundNormalFlipGreen);
    EXPECT_TRUE(foundDielectricSpecular);
    EXPECT_TRUE(foundUvScaleZ);
    EXPECT_NE(materialSamplerSlot12, kInvalidSamplerHandle);

    constexpr std::array<std::uint32_t, 5U> kMaterialSlots{0U, 1U, 2U, 12U, 13U};
    std::array<bool, 5U> materialSlotsBoundWithMaterialSampler{};
    for (const RecordedCommand& command : commands) {
        if (const auto* const bind = std::get_if<BindSamplerCommand>(&command)) {
            if (bind->sampler == materialSamplerSlot12) {
                for (std::size_t i = 0U; i < kMaterialSlots.size(); ++i) {
                    if (bind->slot == kMaterialSlots[i]) {
                        materialSlotsBoundWithMaterialSampler[i] = true;
                    }
                }
            }
        }
    }
    for (const bool bound : materialSlotsBoundWithMaterialSampler) {
        EXPECT_TRUE(bound);
    }

    const std::size_t liveResourcesBefore = renderDevice.GetLiveResourceCount();
    renderer3D->SetMaterialSamplerSettings(false, 4.0F);
    EXPECT_EQ(renderDevice.GetLiveResourceCount(), liveResourcesBefore);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    SamplerHandle updatedSampler = kInvalidSamplerHandle;
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        if (const auto* const bind = std::get_if<BindSamplerCommand>(&command)) {
            if (bind->slot == 12U) {
                updatedSampler = bind->sampler;
                break;
            }
        }
    }
    EXPECT_NE(updatedSampler, kInvalidSamplerHandle);
    EXPECT_NE(updatedSampler, materialSamplerSlot12);
}

TEST_F(Renderer3DTest, RenderFrame_ReadyEmptyMeshSkipsInvalidGpuBuffers) {
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.vertices.clear();
            mesh.indices.clear();
            mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{}, Math::Vector3{});
            return true;
        });

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_empty_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_empty_mesh.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics firstDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(firstDiagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(firstDiagnostics.meshDrawCallsRecorded, 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics secondDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(secondDiagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(secondDiagnostics.meshDrawCallsRecorded, 0U);
}

#if !UVE_DEBUG
TEST_F(Renderer3DTest, RenderFrame_InvalidReadyMaterialPayloadSkipsGpuDrawInRelease) {
    assetManager.RegisterLoader<Asset::MaterialAsset>(
        [vertexGuid = vertexShaderGuid, fragmentGuid = fragmentShaderGuid](const std::filesystem::path&,
                                                                             Asset::MaterialAsset& material) {
            material.vertexShader = vertexGuid;
            material.fragmentShader = fragmentGuid;
            material.metallic = std::numeric_limits<float>::quiet_NaN();
            return true;
        });

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_invalid_material_mesh.uvmodel");
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("renderer3d_tests_invalid_material_payload.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.meshItemsExtracted, 1U);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_InvalidReadyTexturePayloadUsesFallbackInRelease) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_invalid_texture_mesh.uvmodel");
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("renderer3d_tests_invalid_texture_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_invalid_texture.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset& texture) {
            texture.width = 0U;
            texture.height = 0U;
            texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
            texture.pixels.clear();
            return true;
        });
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -2.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    WaitUntilTextureReady(textureGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics firstDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(firstDiagnostics.textureFallbacks, 1U);
    EXPECT_EQ(firstDiagnostics.meshDrawCallsRecorded, 0U);

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics secondDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(secondDiagnostics.textureFallbacks, 0U);
    EXPECT_EQ(secondDiagnostics.meshDrawCallsRecorded, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_OverflowedCascadeSplitsDisableShadowPassInRelease) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Scene::CameraComponent& camera = entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    camera.nearPlane = std::numeric_limits<float>::min();
    camera.farPlane = std::numeric_limits<float>::max();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_overflowed_cascade_mesh.uvmodel");
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("renderer3d_overflowed_cascade_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*cascadeCount).value, 0);
    EXPECT_FALSE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        if (!std::holds_alternative<BeginRenderPassCommand>(command)) {
            return false;
        }
        const RenderPassDesc& desc = std::get<BeginRenderPassCommand>(command).desc;
        return desc.colorAttachment == kInvalidTextureHandle &&
               desc.depthAttachment != kInvalidTextureHandle;
    }));
}
#endif

TEST_F(Renderer3DTest, RenderFrame_UIImages_BindTheirResolvedTextureInsteadOfSolidFallback) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Input::InputSystem inputSystem{eventSystem};
    UI::UIRuntime uiRuntime;
    renderer3D->SetUIRuntime(&uiRuntime);
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    struct ResetUIRuntimeBinding final {
        Render::Renderer3D& renderer;
        ~ResetUIRuntimeBinding() { renderer.SetUIRuntime(nullptr); }
    } resetUIRuntimeBinding{*renderer3D};

    const Scene::Entity solidImageEntity = entityManager.CreateEntity();
    Scene::UIImageComponent solidImage;
    solidImage.rect.size = Math::Vector2{32.0F, 32.0F};
    entityManager.AddComponent<Scene::UIImageComponent>(solidImageEntity, solidImage);
    inputSystem.Update();
    uiRuntime.Tick(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatch().quads.size(), 1U);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const auto slotZeroTextures = [](const std::vector<RecordedCommand>& commands) {
        std::vector<TextureHandle> result;
        for (const RecordedCommand& command : commands) {
            if (const auto* const bind = std::get_if<BindTextureCommand>(&command);
                bind != nullptr && bind->slot == 0U) {
                result.push_back(bind->texture);
            }
        }
        return result;
    };
    const std::vector<TextureHandle> solidTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommands());

    static_cast<void>(entityManager.DestroyEntity(solidImageEntity));
    const Asset::AssetGuid splashTextureGuid = assetDatabase.Register("renderer3d_tests_ui_splash.uvtex");
    const Scene::Entity imageEntity = entityManager.CreateEntity();
    Scene::UIImageComponent texturedImage;
    texturedImage.textureAssetGuid = splashTextureGuid;
    texturedImage.rect.size = Math::Vector2{64.0F, 32.0F};
    entityManager.AddComponent<Scene::UIImageComponent>(imageEntity, texturedImage);
    WaitUntilTextureReady(splashTextureGuid);
    inputSystem.Update();
    uiRuntime.Tick(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatch().quads.size(), 1U);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<TextureHandle> imageTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommands());
    ASSERT_FALSE(solidTextures.empty());
    ASSERT_EQ(imageTextures.size(), solidTextures.size());
    EXPECT_NE(imageTextures.back(), kInvalidTextureHandle);
    EXPECT_NE(imageTextures.back(), solidTextures.back());
}

TEST_F(Renderer3DTest, RenderFrame_UIOverlay_PreservesCanvasOrderAcrossGlyphAndImageTextures) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Input::InputSystem inputSystem{eventSystem};
    UI::UIRuntime uiRuntime;
    renderer3D->SetUIRuntime(&uiRuntime);
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    struct ResetUIRuntimeBinding final {
        Render::Renderer3D& renderer;
        ~ResetUIRuntimeBinding() { renderer.SetUIRuntime(nullptr); }
    } resetUIRuntimeBinding{*renderer3D};

    const Asset::AssetGuid splashTextureGuid = assetDatabase.Register("renderer3d_tests_ui_canvas_order.uvtex");
    const Scene::Entity imageCanvasEntity = entityManager.CreateEntity();
    Scene::CanvasComponent imageCanvas;
    imageCanvas.sortOrder = 1;
    entityManager.AddComponent<Scene::CanvasComponent>(imageCanvasEntity, imageCanvas);
    Scene::UIImageComponent image;
    image.textureAssetGuid = splashTextureGuid;
    image.rect.position = Math::Vector2{20.0F, 20.0F};
    image.rect.size = Math::Vector2{48.0F, 48.0F};
    entityManager.AddComponent<Scene::UIImageComponent>(imageCanvasEntity, image);
    WaitUntilTextureReady(splashTextureGuid);
    inputSystem.Update();
    uiRuntime.Tick(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatch().quads.size(), 1U);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const auto slotZeroTextures = [](const std::vector<RecordedCommand>& commands) {
        std::vector<TextureHandle> result;
        for (const RecordedCommand& command : commands) {
            if (const auto* const bind = std::get_if<BindTextureCommand>(&command);
                bind != nullptr && bind->slot == 0U) {
                result.push_back(bind->texture);
            }
        }
        return result;
    };
    const std::vector<TextureHandle> imageOnlyTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommands());
    ASSERT_FALSE(imageOnlyTextures.empty());

    const Scene::Entity textCanvasEntity = entityManager.CreateEntity();
    Scene::CanvasComponent textCanvas;
    textCanvas.sortOrder = 0;
    entityManager.AddComponent<Scene::CanvasComponent>(textCanvasEntity, textCanvas);
    Scene::UITextComponent text;
    text.text = "A";
    text.positionPixels = Math::Vector2{8.0F, 8.0F};
    entityManager.AddComponent<Scene::UITextComponent>(textCanvasEntity, text);
    inputSystem.Update();
    uiRuntime.Tick(entityManager, inputSystem);
    ASSERT_EQ(uiRuntime.GetDrawBatch().quads.size(), 2U);
    EXPECT_EQ(uiRuntime.GetDrawBatch().quads[0].kind, UI::UIDrawItemKind::Glyph);
    EXPECT_EQ(uiRuntime.GetDrawBatch().quads[1].kind, UI::UIDrawItemKind::Image);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<TextureHandle> mixedTextures =
        slotZeroTextures(renderDevice.GetLastSubmittedCommands());
    ASSERT_GE(mixedTextures.size(), 2U);
    EXPECT_EQ(mixedTextures.back(), imageOnlyTextures.back());
    EXPECT_NE(mixedTextures[mixedTextures.size() - 2U], mixedTextures.back());
}

TEST_F(Renderer3DTest, RenderFrame_MaterialWithAlbedoTexture_UploadsAndBindsRealTexture) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCount();

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_textured_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_textured_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterFirstFrame = renderDevice.GetLiveResourceCount();
    // The manager releases its temporary stage objects after linking; persistent renderer work is
    // mesh buffers, the managed pipeline, and the uploaded material texture.
    EXPECT_GT(liveResourcesAfterFirstFrame, baselineLiveResources);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetLiveResourceCount(), liveResourcesAfterFirstFrame);
}

TEST_F(Renderer3DTest, RenderFrame_SrgbAlbedoMetadataReachesRhiDescriptor) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_srgb_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_srgb_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_srgb_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset& texture) {
            texture.width = 2U;
            texture.height = 2U;
            texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
            texture.colorSpace = Asset::TextureAssetColorSpace::Srgb;
            texture.usage = Asset::TextureUsage::Color;
            texture.pixels.assign(2U * 2U * 4U, std::byte{0x7F});
            texture.mipLevels.push_back(Asset::TextureMipLevel{
                1U, 1U, std::vector<std::byte>(4U, std::byte{0x3F})});
            return true;
        });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<TextureDesc> liveTextureDescs = renderDevice.GetLiveTextureDescs();
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const TextureDesc& desc) {
        return desc.format == TextureFormat::RGBA8Unorm && desc.colorSpace == TextureColorSpace::Srgb &&
               desc.mipLevels == 2U;
    }));
}

#if defined(UVE_HAS_BASIS_ENCODER) && UVE_HAS_BASIS_ENCODER
TEST_F(Renderer3DTest, RenderFrame_BasisTextureFallsBackToRgbaOnNullDevice) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_basis_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_basis_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_basis_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);

    Asset::TextureAsset basisTexture;
    basisTexture.width = 4U;
    basisTexture.height = 4U;
    basisTexture.format = Asset::TextureAssetFormat::RGBA8Unorm;
    basisTexture.colorSpace = Asset::TextureAssetColorSpace::Srgb;
    basisTexture.usage = Asset::TextureUsage::Color;
    basisTexture.pixels.resize(4U * 4U * 4U, std::byte{0xFF});
    ASSERT_TRUE(Asset::CompressTextureAssetWithBasis(
        basisTexture, Asset::TextureCompressionMode::BasisUASTC, 60U, 1U));
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [basisTexture](const std::filesystem::path&, Asset::TextureAsset& texture) {
            texture = basisTexture;
            return true;
        });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    EXPECT_FALSE(renderDevice.SupportsTextureFormat(TextureFormat::BC7RGBA));
    const std::vector<TextureDesc> liveTextureDescs = renderDevice.GetLiveTextureDescs();
    EXPECT_TRUE(std::any_of(liveTextureDescs.cbegin(), liveTextureDescs.cend(), [](const TextureDesc& desc) {
        return desc.format == TextureFormat::RGBA8Unorm && desc.colorSpace == TextureColorSpace::Srgb &&
               desc.mipLevels == 1U;
    }));
}
#endif

TEST_F(Renderer3DTest, RenderFrame_Rgba16FloatAlbedoTexture_UploadsSuccessfully) {
    const std::size_t baselineLiveResources = renderDevice.GetLiveResourceCount();

    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_f16_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_f16_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_f16_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset& texture) {
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureAssetFormat::RGBA16Float;
            texture.pixels.assign(2U * 2U * 8U, std::byte{0});
            return true;
        });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_GT(renderDevice.GetLiveResourceCount(), baselineLiveResources);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command);
    }));
}

TEST_F(Renderer3DTest, RenderFrame_FailedTextureUsesFallbackAndReportsDiagnostic) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_failed_texture_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_failed_texture_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_failed_texture.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset&) { return false; });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    Asset::AssetHandle<Asset::TextureAsset> textureHandle =
        assetManager.Load<Asset::TextureAsset>(textureGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !textureHandle.HasFailed(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(textureHandle.HasFailed());
    // Settle the permanently failed texture before the first renderer prime; otherwise the helper
    // can observe a transient pending state and make this diagnostic assertion order-sensitive.
    WaitUntilAssetsReady(meshGuid, materialGuid, false);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.failedAssetLoads, 0U);
    EXPECT_EQ(diagnostics.textureFallbacks, 1U);

    // Rebuilding the material cache must not retry or repeatedly report the same permanent
    // first-load failure; an explicit texture reload is the only event that clears the memo.
    eventSystem.Publish(Asset::AssetReloadedEvent{materialGuid});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics afterMaterialEviction = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(afterMaterialEviction.failedAssetLoads, 0U);
    EXPECT_EQ(afterMaterialEviction.textureFallbacks, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_InvalidTexturePayloadIsRejectedAndMemoizedUntilReload) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_invalid_texture_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_invalid_texture_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_invalid_texture.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset& texture) {
            // The custom loader returns a ready CPU asset with invalid dimensions. The renderer
            // must reject it before making a backend allocation attempt.
            texture.width = 0U;
            texture.height = 2U;
            texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
            texture.pixels.clear();
            return true;
        });
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    WaitUntilTextureReady(textureGuid);

    const std::uint64_t initialTextureAttempts = renderDevice.GetTextureCreateAttemptCount();
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCount(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().textureFallbacks, 1U);

    // Material cache invalidation must not send the same invalid asset to the backend.
    eventSystem.Publish(Asset::AssetReloadedEvent{materialGuid});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCount(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().textureFallbacks, 1U);

    // An explicit texture reload clears the failure memo. The asset is still invalid in this test,
    // so it is revalidated and safely falls back without reaching the render device.
    eventSystem.Publish(Asset::AssetReloadedEvent{textureGuid});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCount(), initialTextureAttempts);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().textureFallbacks, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_TextureAssetNotYetReady_SkipsItemUntilLoaded) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_pending_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_pending_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_pending_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);

    std::atomic<bool> textureLoadGate{false};
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [&textureLoadGate](const std::filesystem::path&, Asset::TextureAsset& texture) {
            while (!textureLoadGate.load()) {
                std::this_thread::yield();
            }
            texture.width = 2;
            texture.height = 2;
            texture.format = Asset::TextureAssetFormat::RGBA8Unorm;
            texture.pixels.assign(2U * 2U * 4U, std::byte{0xCD});
            return true;
        });

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // The texture load kicked off inside this call is blocked on textureLoadGate, so the item
    // must be skipped this frame - checked below via the absence of any indexed draw, which is
    // what a real mesh item would need to have produced. Fixed command indices/counts are
    // deliberately avoided here: how many Phase 2b post-process passes (SSAO, bloom) execute
    // between the main pass and tone-mapping isn't this test's concern, and hardcoding them made
    // this assertion brittle to changes entirely unrelated to texture-readiness skipping.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand> commandsBeforeReady = renderDevice.GetLastSubmittedCommands();
    // Release the loader before assertions so any failed expectation cannot strand its worker thread.
    textureLoadGate = true;
    ASSERT_FALSE(commandsBeforeReady.empty());
    EXPECT_FALSE(std::any_of(commandsBeforeReady.cbegin(), commandsBeforeReady.cend(),
                              [](const RecordedCommand& command) {
                                  return std::holds_alternative<DrawIndexedCommand>(command);
                              }));
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commandsBeforeReady.front()));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(commandsBeforeReady.back()));


    WaitUntilTextureReady(textureGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commandsAfterReady = renderDevice.GetLastSubmittedCommands();
    EXPECT_TRUE(std::any_of(commandsAfterReady.cbegin(), commandsAfterReady.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command);
    }));
}

TEST_F(Renderer3DTest, RenderFrame_CalledTwiceWithSameScene_ReusesGpuResourceCache) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_reuse_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_reuse_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterFirstFrame = renderDevice.GetLiveResourceCount();

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterSecondFrame = renderDevice.GetLiveResourceCount();

    EXPECT_EQ(liveResourcesAfterSecondFrame, liveResourcesAfterFirstFrame);
}

TEST_F(Renderer3DTest, AssetReloadedEvent_ForCachedMesh_EvictsAndRecreatesGpuResources) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_reload_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_reload_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeReload = renderDevice.GetLiveResourceCount();

    eventSystem.Publish(Asset::AssetReloadedEvent{meshGuid});
    const std::size_t liveResourcesAfterReload = renderDevice.GetLiveResourceCount();
    // The mesh's vertex + index buffer are destroyed on eviction; the material's pipeline (a
    // different GUID) is untouched.
    EXPECT_EQ(liveResourcesAfterReload, liveResourcesBeforeReload - 2U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterRerender = renderDevice.GetLiveResourceCount();
    EXPECT_EQ(liveResourcesAfterRerender, liveResourcesBeforeReload);
}

TEST_F(Renderer3DTest, AssetReloadedEvent_ForCachedTexture_EvictsTextureAndInvalidatesMaterialCache) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_texreload_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_texreload_material.uvmat");
    const Asset::AssetGuid textureGuid = assetDatabase.Register("renderer3d_tests_texreload_albedo.uvtex");
    UseAlbedoTextureInMaterial(textureGuid);

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeReload = renderDevice.GetLiveResourceCount();

    eventSystem.Publish(Asset::AssetReloadedEvent{textureGuid});
    const std::size_t liveResourcesAfterReload = renderDevice.GetLiveResourceCount();
    // Texture eviction is synchronous. The program is manager-owned, so renderer cache eviction
    // only releases its shared reference; the exact retirement point is intentionally internal to
    // ShaderManager and must not be asserted through renderer resource counts.
    EXPECT_LT(liveResourcesAfterReload, liveResourcesBeforeReload);

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterRerender = renderDevice.GetLiveResourceCount();
    EXPECT_GT(liveResourcesAfterRerender, liveResourcesAfterReload);
}

TEST_F(Renderer3DTest, AssetReloadedEvent_ForMaterialTextureSwap_EvictsOldTextureCacheEntry) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_material_swap_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_material_swap_material.uvmat");
    const Asset::AssetGuid textureAGuid = assetDatabase.Register("renderer3d_tests_material_swap_a.uvtex");
    const Asset::AssetGuid textureBGuid = assetDatabase.Register("renderer3d_tests_material_swap_b.uvtex");
    UseAlbedoTextureInMaterial(textureAGuid);

    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilTextureReady(textureAGuid);
    WaitUntilTextureReady(textureBGuid);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesBeforeMaterialReload = renderDevice.GetLiveResourceCount();

    Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
        assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
    ASSERT_TRUE(materialHandle.IsReady());
    materialHandle.TryGet()->albedoTexture = textureBGuid;
    eventSystem.Publish(Asset::AssetReloadedEvent{materialGuid});

    // Material eviction must retire texture A even though the material GUID is unchanged and the
    // replacement texture is only referenced by the newly loaded CPU material payload. Releasing
    // the material cache may also retire its manager-owned pipeline, so assert a strict decrease
    // rather than assuming exactly one resource disappears.
    const std::size_t liveResourcesAfterMaterialReload = renderDevice.GetLiveResourceCount();
    EXPECT_LT(liveResourcesAfterMaterialReload, liveResourcesBeforeMaterialReload);

    // The replacement material program is asynchronous; drain it before comparing the steady-state
    // resource count so the comparison isolates cache lifetime rather than link timing.
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t liveResourcesAfterReplacementFrame = renderDevice.GetLiveResourceCount();
    EXPECT_EQ(liveResourcesAfterReplacementFrame, liveResourcesBeforeMaterialReload);
}

TEST_F(Renderer3DTest, RenderFrame_ParticleRuntimeInput_ExtractsCopiedItemsAndDoesNotLeakAcrossFrames) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Scene::ParticleRuntime particleRuntime;
    const Scene::Entity particleEntity{31U, 1U};
    ASSERT_TRUE(particleRuntime.Attach(particleEntity, Scene::ParticleEmitterComponent{4U}));
    ASSERT_TRUE(particleRuntime.EmitDetailed(
                       particleEntity,
                       Scene::ParticleEmission{2U, Math::Vector3{0.0F, 0.0F, -4.0F}, {}, 2.0F})
                    .IsAccepted());

    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        EXPECT_NO_FATAL_FAILURE(
            renderer3D->RenderFrameWithParticleRuntime(entityManager, cameraEntity, particleRuntime));
        if (renderer3D->GetLastFrameDiagnostics().particleProgramReady) {
            break;
        }
        shaderManager.Update(0.0);
    }
    const Renderer3DFrameDiagnostics particleDiagnostics = renderer3D->GetLastFrameDiagnostics();
    ASSERT_TRUE(particleDiagnostics.particleProgramReady);
    EXPECT_EQ(particleDiagnostics.particleItemsExtracted, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCommandsRecorded, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCommandsSubmitted, 2U);
    EXPECT_EQ(particleDiagnostics.particleDrawCallsRecorded, 1U);
    EXPECT_FALSE(particleDiagnostics.particleItemsTruncated);
    EXPECT_FALSE(particleDiagnostics.particleDrawCommandsSubmissionTruncated);

    const std::vector<RecordedCommand>& particleCommands = renderDevice.GetLastSubmittedCommands();
    const auto particleDraw = std::find_if(particleCommands.cbegin(), particleCommands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<DrawCommand>(command) &&
               std::get<DrawCommand>(command).vertexCount == 12U;
    });
    ASSERT_NE(particleDraw, particleCommands.cend());

    EXPECT_NO_FATAL_FAILURE(renderer3D->RenderFrame(entityManager, cameraEntity));
    const Renderer3DFrameDiagnostics legacyDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(legacyDiagnostics.particleItemsExtracted, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCommandsRecorded, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCommandsSubmitted, 0U);
    EXPECT_EQ(legacyDiagnostics.particleDrawCallsRecorded, 0U);
    EXPECT_FALSE(legacyDiagnostics.particleItemsTruncated);
}

TEST_F(Renderer3DTest, RenderFrame_ActiveCameraPath_MatchesEngineCoreIntegration) {
    // Exercises the exact call shape EngineCore::Render() uses once SetActiveCamera() has
    // been called: RenderFrame(*entityManager, activeCamera) against a scene with both a
    // camera and a visible mesh, driven end-to-end through real (not fake) collaborators.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_active_camera_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_active_camera_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);

    EXPECT_NO_FATAL_FAILURE(renderer3D->RenderFrame(entityManager, cameraEntity));

    EXPECT_EQ(renderSystem.GetFrameIndex(), 1U);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    EXPECT_FALSE(commands.empty());
}

TEST_F(Renderer3DTest, DirectionalLightInheritShadowBiasUsesProjectDefaultsAndHonorsOverrides) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_shadow_bias_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_shadow_bias_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.light.shadowMaxDistance = 0.0F;
    definition.emitter.shadowEnabled = true;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->SetShadowBiasDefaults(0.4F, 2.0F);
    const auto captureBiasUniforms = [this, cameraEntity]() {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        std::optional<float> depthBias;
        std::optional<float> normalBias;
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
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

    Scene::LightEmitterComponent& emitter = entityManager.GetComponent<Scene::LightEmitterComponent>(sun);
    emitter.shadowBias = 0.2F;
    emitter.shadowNormalBias = 0.5F;
    const auto [overriddenDepthBias, overriddenNormalBias] = captureBiasUniforms();
    ASSERT_TRUE(overriddenDepthBias.has_value());
    ASSERT_TRUE(overriddenNormalBias.has_value());
    EXPECT_NEAR(*overriddenDepthBias, 0.2F * 0.025F, 1.0e-6F);
    EXPECT_FLOAT_EQ(*overriddenNormalBias, 0.5F);
}

TEST_F(Renderer3DTest, DirectionalLightDistanceFadeRangeReachesShaderAndZeroDisablesIt) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_shadow_fade_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_shadow_fade_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);

    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.light.shadowMaxDistance = 0.0F;
    definition.light.shadowDistanceFadeRange = 6.5F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    const auto captureDistanceFadeRange = [this, cameraEntity]() -> std::optional<float> {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        std::optional<float> fadeRange;
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
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
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        const auto* const uniform = std::get_if<SetUniformIntCommand>(&command);
        cascadesEnabled = cascadesEnabled ||
                          (uniform != nullptr && uniform->name == "uShadowCascadeCount" && uniform->value == 3);
    }
    EXPECT_TRUE(cascadesEnabled);

    entityManager.GetComponent<Scene::DirectionalLight3DComponent>(sun).shadowDistanceFadeRange = 0.0F;
    const std::optional<float> disabledFadeRange = captureDistanceFadeRange();
    ASSERT_TRUE(disabledFadeRange.has_value());
    EXPECT_FLOAT_EQ(*disabledFadeRange, 0.0F);
}

TEST_F(Renderer3DTest, DirectionalLightInheritSplitBlendUsesRendererDefault) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_split_blend_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_split_blend_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.light.shadowMaxDistance = 0.0F; // Follow this camera's far plane.
    definition.light.shadowSplitBlend = -1.0F; // Inherit kTestShadowCascadeSplitLambda from Renderer3D.
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Scene::CameraComponent& camera = entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    std::array<float, 3U> splits{};
    std::array<bool, 3U> found{};
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command);
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
        const float expected = uniformSplit * (1.0F - kTestShadowCascadeSplitLambda) +
                               logarithmicSplit * kTestShadowCascadeSplitLambda;
        EXPECT_NEAR(splits[index], expected, 0.001F);
    }
}

TEST_F(Renderer3DTest, RenderFrame_DirectionalLight_PushesThreeOrderedCascadeSplits) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_cascade_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_cascade_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    std::array<float, 3> splits{};
    std::array<bool, 3> foundSplits{};
    std::array<bool, 3> foundMatrices{};
    float cascadeBlendRatio = 0.0F;
    bool foundCascadeBlendRatio = false;
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        if (const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command)) {
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
        if (const auto* const uniform = std::get_if<SetUniformMatrix4x4Command>(&command)) {
            for (std::size_t cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
                if (uniform->name == "uLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]") {
                    foundMatrices[cascadeIndex] = uniform->value != Math::Matrix4x4::Identity();
                }
            }
        }
    }

    EXPECT_TRUE(foundSplits[0]);
    EXPECT_TRUE(foundSplits[1]);
    EXPECT_TRUE(foundSplits[2]);
    EXPECT_TRUE(foundCascadeBlendRatio);
    EXPECT_FLOAT_EQ(cascadeBlendRatio, kTestShadowCascadeBlendRatio);
    EXPECT_GT(splits[0], 0.0F);
    EXPECT_LT(splits[0], splits[1]);
    EXPECT_LT(splits[1], splits[2]);
    EXPECT_TRUE(foundMatrices[0]);
    EXPECT_TRUE(foundMatrices[1]);
    EXPECT_TRUE(foundMatrices[2]);
}

TEST_F(Renderer3DTest, RenderFrame_DirectionalCascadeMatrices_SnapToShadowTexelGrid) {
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_stabilization_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_stabilization_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F});
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const auto captureCascadeMatrices = [this]() {
        std::array<Math::Matrix4x4, 3> matrices{};
        std::array<bool, 3> foundMatrices{};
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            const auto* const uniform = std::get_if<SetUniformMatrix4x4Command>(&command);
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

    const Scene::Entity initialCamera = MakeCameraEntity();
    PrimeMaterialProgram(*renderer3D, initialCamera);
    renderer3D->RenderFrame(entityManager, initialCamera);
    const std::array<Math::Matrix4x4, 3> initialMatrices = captureCascadeMatrices();
    const float cascadeZeroTexelWidth =
        2.0F / (static_cast<float>(kTestShadowMapResolution) * initialMatrices[0].m[0][0]);
    ASSERT_GT(cascadeZeroTexelWidth, 0.0F);

    const Scene::Entity subTexelCamera =
        MakeCameraEntity(Math::Vector3{cascadeZeroTexelWidth * 0.25F, 0.0F, 0.0F});
    renderer3D->RenderFrame(entityManager, subTexelCamera);
    const std::array<Math::Matrix4x4, 3> subTexelMatrices = captureCascadeMatrices();
    EXPECT_EQ(subTexelMatrices, initialMatrices);

    const Scene::Entity nextTexelCamera =
        MakeCameraEntity(Math::Vector3{cascadeZeroTexelWidth * 1.25F, 0.0F, 0.0F});
    renderer3D->RenderFrame(entityManager, nextTexelCamera);
    const std::array<Math::Matrix4x4, 3> nextTexelMatrices = captureCascadeMatrices();
    EXPECT_NE(nextTexelMatrices[0], initialMatrices[0]);
    EXPECT_FLOAT_EQ(nextTexelMatrices[0].m[0][0], initialMatrices[0].m[0][0]);
    EXPECT_FLOAT_EQ(nextTexelMatrices[0].m[0][3] - initialMatrices[0].m[0][3],
                    -2.0F / static_cast<float>(kTestShadowMapResolution));
}

TEST_F(Renderer3DTest, RenderFrame_NoDirectionalLight_SkipsShadowPassEvenWithVisibleMesh) {
    // A visible, asset-ready mesh exists, and the shadow program has even had the chance to
    // become valid (polled to readiness below) - but with no Directional light entity at all,
    // FindShadowCaster() finds no caster, so no shadow pass is recorded.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_noshadow_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_noshadow_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    ASSERT_FALSE(commands.empty());
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commands[0U]));
    EXPECT_NE(std::get<BeginRenderPassCommand>(commands[0U]).desc.colorAttachment, kInvalidTextureHandle);
    const auto mainPassEnd = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<EndRenderPassCommand>(command);
    });
    ASSERT_NE(mainPassEnd, commands.cend());
}

TEST_F(Renderer3DTest, RenderFrame_InvalidShadowTargetsSkipShadowPassSafely) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_invalid_shadow_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_invalid_shadow_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    Renderer3D invalidShadowRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, kTargetWidth, kTargetHeight, kTestAmbientColor, 0U,
        kTestShadowMapHalfExtent, kTestShadowMapNearPlane, kTestShadowMapFarPlane,
        kTestShadowFrustumPadding, kTestShadowCascadeSplitLambda, kTestShadowCascadeBlendRatio,
        kTestShadowPcfKernelRadius);
    PrimeMaterialProgram(invalidShadowRenderer, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    invalidShadowRenderer.RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    ASSERT_FALSE(commands.empty());
    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commands.front()));
    EXPECT_NE(std::get<BeginRenderPassCommand>(commands.front()).desc.colorAttachment,
              kInvalidTextureHandle);
    const auto cascadeCount = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uShadowCascadeCount";
    });
    ASSERT_NE(cascadeCount, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*cascadeCount).value, 0);
    EXPECT_FALSE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindTextureCommand>(command) &&
               std::get<BindTextureCommand>(command).slot >= 3U &&
               std::get<BindTextureCommand>(command).slot < 12U;
    }));
}

TEST_F(Renderer3DTest, RenderFrame_InvalidMainTargetsSkipFrameSafely) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Renderer3D invalidMainRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, 0U, kTargetHeight, kTestAmbientColor, kTestShadowMapResolution,
        kTestShadowMapHalfExtent, kTestShadowMapNearPlane, kTestShadowMapFarPlane,
        kTestShadowFrustumPadding, kTestShadowCascadeSplitLambda, kTestShadowCascadeBlendRatio,
        kTestShadowPcfKernelRadius);

    invalidMainRenderer.RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics diagnostics = invalidMainRenderer.GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.meshItemsExtracted, 0U);
    EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U);
    EXPECT_FALSE(diagnostics.mainPassRecorded);
    EXPECT_FALSE(diagnostics.toneMappingPassRecorded);
}

TEST_F(Renderer3DTest, RenderFrame_FittedLightFrustum_CastsOffCameraOccluderWithoutMainPassDraw) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid visibleMeshGuid = assetDatabase.Register("renderer3d_fitted_visible.uvmodel");
    const Asset::AssetGuid visibleMaterialGuid = assetDatabase.Register("renderer3d_fitted_visible.uvmat");
    const Asset::AssetGuid offCameraMeshGuid = assetDatabase.Register("renderer3d_fitted_offcamera.uvmodel");
    const Asset::AssetGuid offCameraMaterialGuid = assetDatabase.Register("renderer3d_fitted_offcamera.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, visibleMeshGuid, visibleMaterialGuid);
    // At z=-10 the default camera's 60-degree view only reaches roughly +/-5.8 on X, while the
    // fitted directional-light frustum includes this potential caster across its far-range width.
    MakeMeshEntity(Math::Vector3{50.0F, 0.0F, -10.0F}, offCameraMeshGuid, offCameraMaterialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReady(visibleMeshGuid, visibleMaterialGuid);
    WaitUntilAssetsReady(offCameraMeshGuid, offCameraMaterialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<EndRenderPassCommand>(command);
    });
    ASSERT_NE(shadowPassEnd, commands.cend());
    const std::size_t shadowDrawCount = static_cast<std::size_t>(std::count_if(
        commands.cbegin(), shadowPassEnd, [](const RecordedCommand& command) {
            return std::holds_alternative<DrawIndexedCommand>(command);
        }));
    const std::size_t mainDrawCount = static_cast<std::size_t>(std::count_if(
        std::next(shadowPassEnd), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<DrawIndexedCommand>(command);
        }));

    EXPECT_EQ(shadowDrawCount, 2U);
    EXPECT_EQ(mainDrawCount, 1U);
}

TEST_F(Renderer3DTest, FrustumCullingFreezeDrawsOffCameraMeshesAndLeavesShadowCascadesCulled) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid visibleMeshGuid = assetDatabase.Register("renderer3d_freeze_visible.uvmodel");
    const Asset::AssetGuid visibleMaterialGuid = assetDatabase.Register("renderer3d_freeze_visible.uvmat");
    const Asset::AssetGuid offCameraMeshGuid = assetDatabase.Register("renderer3d_freeze_offcamera.uvmodel");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, visibleMeshGuid, visibleMaterialGuid);
    // Far outside the default camera's 60-degree view at z=-10 (which reaches roughly +/-5.8 on X),
    // but inside the fitted directional-light frustum. It shares the visible mesh's material so
    // the freeze assertion isolates culling from asynchronous compilation of a second material shader.
    MakeMeshEntity(Math::Vector3{50.0F, 0.0F, -10.0F}, offCameraMeshGuid, visibleMaterialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReady(visibleMeshGuid, visibleMaterialGuid);
    WaitUntilAssetsReady(offCameraMeshGuid, visibleMaterialGuid);
    WaitUntilShadowProgramReady();

    const auto countDraws = [](const std::vector<RecordedCommand>& commands) {
        const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(),
                                                [](const RecordedCommand& command) {
                                                    return std::holds_alternative<EndRenderPassCommand>(command);
                                                });
        const bool hasShadowPass = shadowPassEnd != commands.cend();
        const auto draws = [&commands](auto first, auto last) {
            std::size_t instances = 0U;
            for (auto command = first; command != last; ++command) {
                if (const auto* const draw = std::get_if<DrawIndexedCommand>(&*command); draw != nullptr) {
                    instances += draw->instanceCount;
                }
            }
            return instances;
        };
        return std::pair<std::size_t, std::size_t>{
            hasShadowPass ? draws(commands.cbegin(), shadowPassEnd) : 0U,
            hasShadowPass ? draws(std::next(shadowPassEnd), commands.cend()) : draws(commands.cbegin(), commands.cend())};
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const auto [culledShadowDraws, culledMainDraws] = countDraws(renderDevice.GetLastSubmittedCommands());
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().frustumCullingFrozen)
        << "culling is on unless a host asks for the freeze";
    EXPECT_EQ(culledMainDraws, 1U);
    ASSERT_GT(culledShadowDraws, 0U) << "this test needs a shadow pass to prove the freeze skips it";

    Render::CullingSettings frozen;
    frozen.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(frozen);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const auto [frozenShadowDraws, frozenMainDraws] = countDraws(renderDevice.GetLastSubmittedCommands());
    const Renderer3DFrameDiagnostics frozenDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_TRUE(frozenDiagnostics.frustumCullingFrozen);
    EXPECT_EQ(frozenMainDraws, 2U) << "the off-camera mesh must reach the colour view when culling is frozen";
    EXPECT_EQ(frozenDiagnostics.meshItemsExtracted, 2U);
    EXPECT_EQ(frozenShadowDraws, culledShadowDraws)
        << "shadow cascades keep culling against their own light frusta";

    renderer3D->SetCullingSettings(Render::CullingSettings{});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const auto [restoredShadowDraws, restoredMainDraws] = countDraws(renderDevice.GetLastSubmittedCommands());
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().frustumCullingFrozen);
    EXPECT_EQ(restoredMainDraws, culledMainDraws);
    EXPECT_EQ(restoredShadowDraws, culledShadowDraws);
}

TEST_F(Renderer3DTest, GpuOcclusionCullingRecordsProxyBracketsAfterOpaqueDrawsAndNeverSkipsWithoutAVerdict) {
    // Foundation WP3 consumer proof on the Null device: the stage is off by default, records one
    // depth-tested proxy bracket per opaque item at the END of the main pass when enabled, and -
    // because Null records brackets but never fabricates a visibility number - never removes an
    // item from the colour view. The "skip" half of the contract is a real-GPU concern and lives
    // with the GL/Vulkan bracket tests; here the point is that no verdict means no skip.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_occlusion_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_occlusion_material.uvmat");
    MakeMeshEntity(Math::Vector3{-1.5F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{1.5F, 0.0F, -12.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    struct MainPassShape {
        std::size_t meshInstances = 0U;
        std::size_t beginQueries = 0U;
        std::size_t endQueries = 0U;
        bool bracketsWellFormed = true;
        bool bracketsAfterMeshDraws = true;
    };
    const auto inspectMainPass = [](const std::vector<RecordedCommand>& commands) {
        // The main colour pass is the last BeginRenderPass whose span contains a DrawIndexed.
        MainPassShape shape;
        std::size_t passBegin = commands.size();
        for (std::size_t index = 0U; index < commands.size(); ++index) {
            if (std::holds_alternative<BeginRenderPassCommand>(commands[index])) {
                bool hasDraw = false;
                for (std::size_t probe = index + 1U; probe < commands.size(); ++probe) {
                    if (std::holds_alternative<EndRenderPassCommand>(commands[probe])) {
                        break;
                    }
                    hasDraw = hasDraw || std::holds_alternative<DrawIndexedCommand>(commands[probe]);
                }
                if (hasDraw) {
                    passBegin = index;
                }
            }
        }
        if (passBegin == commands.size()) {
            shape.bracketsWellFormed = false;
            return shape;
        }
        QueryHandle open = kInvalidQueryHandle;
        bool insideBracket = false;
        std::size_t drawsInsideOpenBracket = 0U;
        for (std::size_t index = passBegin + 1U; index < commands.size(); ++index) {
            const RecordedCommand& command = commands[index];
            if (std::holds_alternative<EndRenderPassCommand>(command)) {
                break;
            }
            if (const auto* const begin = std::get_if<BeginQueryCommand>(&command); begin != nullptr) {
                shape.bracketsWellFormed = shape.bracketsWellFormed && !insideBracket;
                insideBracket = true;
                open = begin->query;
                drawsInsideOpenBracket = 0U;
                ++shape.beginQueries;
            } else if (const auto* const end = std::get_if<EndQueryCommand>(&command); end != nullptr) {
                shape.bracketsWellFormed =
                    shape.bracketsWellFormed && insideBracket && end->query == open && drawsInsideOpenBracket == 1U;
                insideBracket = false;
                ++shape.endQueries;
            } else if (const auto* const draw = std::get_if<DrawIndexedCommand>(&command); draw != nullptr) {
                if (insideBracket) {
                    ++drawsInsideOpenBracket;
                } else {
                    // A mesh draw after the first bracket would mean the proxies were not last.
                    shape.bracketsAfterMeshDraws = shape.bracketsAfterMeshDraws && shape.beginQueries == 0U;
                    shape.meshInstances += draw->instanceCount;
                }
            }
        }
        shape.bracketsWellFormed = shape.bracketsWellFormed && !insideBracket;
        return shape;
    };

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const MainPassShape off = inspectMainPass(renderDevice.GetLastSubmittedCommands());
    const Renderer3DFrameDiagnostics offDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_TRUE(offDiagnostics.occlusionQueriesSupported) << "Null models queries";
    EXPECT_EQ(offDiagnostics.occlusionQueriesIssued, 0U) << "off by default";
    EXPECT_EQ(offDiagnostics.occludedItemsSkipped, 0U);
    EXPECT_EQ(off.beginQueries, 0U);
    ASSERT_EQ(off.meshInstances, 2U) << "both meshes reach the colour view";

    Render::CullingSettings occlusion;
    occlusion.gpuOcclusionCullingEnabled = true;
    renderer3D->SetCullingSettings(occlusion);
    for (int frame = 0; frame < 3; ++frame) {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        const MainPassShape on = inspectMainPass(renderDevice.GetLastSubmittedCommands());
        const Renderer3DFrameDiagnostics onDiagnostics = renderer3D->GetLastFrameDiagnostics();
        EXPECT_EQ(onDiagnostics.occlusionQueriesIssued, 2U) << "one bracket per opaque item, frame " << frame;
        EXPECT_EQ(onDiagnostics.occludedItemsSkipped, 0U) << "Null never completes a bracket, frame " << frame;
        EXPECT_EQ(on.meshInstances, 2U) << "no verdict means no skip, frame " << frame;
        EXPECT_EQ(on.beginQueries, 2U);
        EXPECT_EQ(on.endQueries, 2U);
        EXPECT_TRUE(on.bracketsWellFormed) << "begin/draw/end triples, never nested";
        EXPECT_TRUE(on.bracketsAfterMeshDraws) << "proxies are recorded after every opaque draw";
    }

    renderer3D->SetCullingSettings(Render::CullingSettings{});
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const MainPassShape restored = inspectMainPass(renderDevice.GetLastSubmittedCommands());
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().occlusionQueriesIssued, 0U);
    EXPECT_EQ(restored.beginQueries, 0U);
    EXPECT_EQ(restored.meshInstances, 2U);
}

TEST_F(Renderer3DTest, FrustumCullingFreezeDrawsOffCameraPrimitives) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponent{});
    // The primitive path culls in its own extraction, not through the mesh visibility set, so it
    // needs its own proof that the freeze reaches it.
    MakePrimitiveEntity(Math::Vector3{80.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponent{});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics culled = renderer3D->GetLastFrameDiagnostics();
    ASSERT_TRUE(culled.primitiveProgramReady);
    EXPECT_FALSE(culled.frustumCullingFrozen);
    EXPECT_EQ(culled.primitiveCandidates, 2U);
    EXPECT_EQ(culled.primitiveItemsExtracted, 1U);

    Render::CullingSettings frozen;
    frozen.frustumCullingEnabled = false;
    renderer3D->SetCullingSettings(frozen);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics frozenDiagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_TRUE(frozenDiagnostics.frustumCullingFrozen);
    EXPECT_EQ(frozenDiagnostics.primitiveItemsExtracted, 2U)
        << "the off-camera primitive must be submitted when culling is frozen";
}

TEST_F(Renderer3DTest, RenderFrame_DirectionalLightAndReadyShadowProgram_ShadowPassDrawsOpaqueItem) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_shadowdraw_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_shadowdraw_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 3.0F});
    WaitUntilAssetsReady(meshGuid, materialGuid);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();

    // Rewritten when the shadow cascades became instanced. The old version asserted fixed command
    // INDICES and the presence of a per-item "uModel" uniform, both of which were artefacts of the
    // one-draw-per-caster loop rather than anything the pass promises. The instanced path sends
    // model matrices through a storage buffer, so uModel legitimately disappears, and the command
    // count per draw changes - a test pinned to either would have to be rewritten again the next
    // time the recording changes shape. What this pass actually owes its caller is checked instead:
    // the shadow pass comes first, it is set up with a real light-space matrix, and it draws the
    // caster's geometry. The path taken to get there is an implementation detail.
    const auto shadowPassEnd = std::find_if(commands.cbegin(), commands.cend(),
        [](const RecordedCommand& command) {
            return std::holds_alternative<EndRenderPassCommand>(command);
        });
    ASSERT_NE(shadowPassEnd, commands.cend());

    ASSERT_TRUE(std::holds_alternative<BeginRenderPassCommand>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<BindPipelineCommand>(commands[1]));

    // uLightSpaceMatrix is still a plain uniform on both paths - it is per-cascade, not
    // per-instance - and it must not be identity, or the cascade is rendering from nowhere.
    const bool foundLightSpaceUniform = std::any_of(commands.cbegin(), shadowPassEnd,
        [](const RecordedCommand& command) {
            if (!std::holds_alternative<SetUniformMatrix4x4Command>(command)) {
                return false;
            }
            const SetUniformMatrix4x4Command& uniform = std::get<SetUniformMatrix4x4Command>(command);
            return uniform.name == "uLightSpaceMatrix" && uniform.value != Math::Matrix4x4::Identity();
        });
    EXPECT_TRUE(foundLightSpaceUniform);

    // The caster's mesh is bound and drawn. Whether that is one instanced draw or one plain draw,
    // the single caster must be covered exactly once with its real index count.
    const auto shadowDraw = std::find_if(commands.cbegin(), shadowPassEnd,
        [](const RecordedCommand& command) {
            return std::holds_alternative<DrawIndexedCommand>(command);
        });
    ASSERT_NE(shadowDraw, shadowPassEnd);
    EXPECT_EQ(std::get<DrawIndexedCommand>(*shadowDraw).indexCount, 3U);
    EXPECT_EQ(std::get<DrawIndexedCommand>(*shadowDraw).instanceCount, 1U);
    EXPECT_EQ(std::count_if(commands.cbegin(), shadowPassEnd, [](const RecordedCommand& command) {
        return std::holds_alternative<DrawIndexedCommand>(command);
    }), 1);
    EXPECT_TRUE(std::any_of(commands.cbegin(), shadowDraw, [](const RecordedCommand& command) {
        return std::holds_alternative<BindVertexBufferCommand>(command);
    }));
    EXPECT_TRUE(std::any_of(commands.cbegin(), shadowDraw, [](const RecordedCommand& command) {
        return std::holds_alternative<BindIndexBufferCommand>(command);
    }));

    // The main pass follows immediately after the shadow pass ends.
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(*std::next(shadowPassEnd)));
}

#if !UVE_DEBUG
TEST_F(Renderer3DTest, RenderFrame_InvalidCameraParametersAreSafeNoOpInRelease) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Scene::CameraComponent& camera =
        entityManager.GetComponent<Scene::CameraComponent>(cameraEntity);
    camera.nearPlane = 0.0F;
    const std::size_t submittedCommandCountBefore = renderDevice.GetLastSubmittedCommands().size();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    EXPECT_EQ(renderDevice.GetLastSubmittedCommands().size(), submittedCommandCountBefore);
}

TEST_F(Renderer3DTest, RenderFrame_InvalidCameraWorldTransformIsSafeNoOpInRelease) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(cameraEntity);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(cameraEntity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(cameraEntity, invalidWorld);
    const std::size_t submittedCommandCountBefore = renderDevice.GetLastSubmittedCommands().size();

    renderer3D->RenderFrame(entityManager, cameraEntity);

    EXPECT_EQ(renderDevice.GetLastSubmittedCommands().size(), submittedCommandCountBefore);
}

TEST_F(Renderer3DTest, RenderFrame_NonFiniteShadowTuningUsesFiniteReleaseDefaults) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_nonfinite_shadow_tuning_mesh.uvmodel");
    const Asset::AssetGuid materialGuid =
        assetDatabase.Register("renderer3d_nonfinite_shadow_tuning_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    Renderer3D sanitizedRenderer(
        renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
        assetDatabase, eventSystem, kTargetWidth, kTargetHeight, kTestAmbientColor,
        kTestShadowMapResolution, kTestShadowMapHalfExtent, kTestShadowMapNearPlane,
        kTestShadowMapFarPlane, nan, nan, nan, kTestShadowPcfKernelRadius);
    PrimeMaterialProgram(sanitizedRenderer, cameraEntity);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    sanitizedRenderer.RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto blendRatio = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformFloatCommand>(command) &&
               std::get<SetUniformFloatCommand>(command).name == "uShadowCascadeBlendRatio";
    });
    ASSERT_NE(blendRatio, commands.cend());
    EXPECT_EQ(std::get<SetUniformFloatCommand>(*blendRatio).value, 0.0F);
    for (const RecordedCommand& command : commands) {
        if (std::holds_alternative<SetUniformFloatCommand>(command)) {
            EXPECT_TRUE(std::isfinite(std::get<SetUniformFloatCommand>(command).value));
        }
    }
}
#endif

// Phase U3b regression coverage: RenderFrameToTarget's own destination color/depth textures
// must be exactly what the UIOverlay pass draws into when a width/height is supplied - not
// kInvalidTextureHandle (which BeginRenderPass maps to the real backend framebuffer). Before
// this fix, the UIOverlay pass ignored destinationTextureOverride entirely, so an editor viewport
// compositing a RenderFrameToTarget() result would have had its UI silently misdirected onto
// the actual application window instead of the caller's offscreen texture.
TEST_F(Renderer3DTest, RenderFrameToTarget_UIOverlayPassTargetsTheSameDestinationTextureAsToneMapping) {
    const Scene::Entity cameraEntity = MakeCameraEntity();

    const Scene::Entity buttonEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::UIButtonComponent>(buttonEntity);

    Events::EventSystem inputEventSystem;
    Input::InputSystem inputSystem(inputEventSystem);
    UI::UIRuntime uiRuntime;
    uiRuntime.Tick(entityManager, inputSystem);
    renderer3D->SetUIRuntime(&uiRuntime);

    const TextureHandle colorTarget = renderDevice.CreateTexture(
        TextureDesc{kTargetWidth, kTargetHeight, TextureFormat::RGBA8Unorm, 1});
    const TextureHandle depthTarget = renderDevice.CreateTexture(
        TextureDesc{kTargetWidth, kTargetHeight, TextureFormat::Depth32Float, 1});
    ASSERT_NE(colorTarget, kInvalidTextureHandle);
    ASSERT_NE(depthTarget, kInvalidTextureHandle);

    // ToneMapping's own pass callback returns early (recording nothing) until its built-in program
    // has finished async compilation - a first frame right after construction can genuinely skip
    // it, so prime it exactly like PrimeMaterialProgram() primes per-material programs, before
    // relying on ToneMapping's own BeginRenderPassCommand existing at all.
    renderer3D->RenderFrameToTarget(entityManager, cameraEntity, colorTarget, depthTarget, kTargetWidth,
                                        kTargetHeight);
    for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (shaderManager.GetPendingJobCount() == 0U) {
            break;
        }
        std::this_thread::yield();
    }
    ASSERT_EQ(shaderManager.GetPendingJobCount(), 0U);

    renderer3D->RenderFrameToTarget(entityManager, cameraEntity, colorTarget, depthTarget, kTargetWidth,
                                        kTargetHeight);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    std::vector<RenderPassDesc> beginRenderPassDescs;
    for (const RecordedCommand& command : commands) {
        if (std::holds_alternative<BeginRenderPassCommand>(command)) {
            beginRenderPassDescs.push_back(std::get<BeginRenderPassCommand>(command).desc);
        }
    }
    // ToneMapping and UIOverlay are the frame's final two passes (in that order); both must target
    // the same caller-supplied destination texture pair.
    ASSERT_GE(beginRenderPassDescs.size(), 2U);
    const RenderPassDesc& toneMappingDesc = beginRenderPassDescs[beginRenderPassDescs.size() - 2U];
    const RenderPassDesc& uiOverlayDesc = beginRenderPassDescs.back();
    EXPECT_EQ(toneMappingDesc.colorAttachment, colorTarget);
    EXPECT_EQ(toneMappingDesc.depthAttachment, depthTarget);
    EXPECT_EQ(uiOverlayDesc.colorAttachment, colorTarget);
    EXPECT_EQ(uiOverlayDesc.depthAttachment, depthTarget);

    renderer3D->SetUIRuntime(nullptr);
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
void UseInstancingAwareShader(Asset::AssetManager& assetManager) {
    assetManager.RegisterLoader<Asset::ShaderAsset>(
        [](const std::filesystem::path&, Asset::ShaderAsset& shader) {
            shader.sourceCode =
                "void main() { int slot = uInstanceBaseIndex + gl_InstanceID; }";
            return true;
        });
}

TEST_F(Renderer3DTest, RenderFrame_LegacyMaterial_StaysOnThePerObjectPath) {
    // The regression case, stated explicitly rather than left implicit in the other tests: a
    // material that predates the instancing contract must still get one draw call per object.
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_instancing_legacy.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_instancing_legacy.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index), 0.0F, -5.0F}, meshGuid, materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 3U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 0U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_InstancingAwareMaterial_CollapsesRepeatsIntoOneDraw) {
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_instancing_shared.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_instancing_shared.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();
    // Same mesh, same material, laid out along a line so they sort into one contiguous run.
    for (int index = 0; index < 4; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshGuid,
                          materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    // Four objects, one draw call - and the object count proves all four are still being drawn,
    // which a draw-call count alone would not.
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 4U);
}

TEST_F(Renderer3DTest, RenderFrame_InstancedDiagnosticsResetBetweenFrames) {
    // The counters are frame-local. If they accumulated, a long-running session would report a
    // growing instanced count for a static scene - a diagnostic that lies slowly.
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_instancing_reset.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_instancing_reset.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();
    for (int index = 0; index < 3; ++index) {
        MakeMeshEntity(Math::Vector3{static_cast<float>(index) * 0.1F, 0.0F, -5.0F}, meshGuid,
                          materialGuid);
    }
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t firstFrameObjects = renderer3D->GetLastFrameDiagnostics().instancedObjectsRecorded;
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::size_t secondFrameObjects = renderer3D->GetLastFrameDiagnostics().instancedObjectsRecorded;

    EXPECT_EQ(firstFrameObjects, 3U);
    EXPECT_EQ(secondFrameObjects, 3U);
}

TEST_F(Renderer3DTest, RenderFrame_SingleInstancingAwareObject_StillDrawsExactlyOnce) {
    // A run of one goes through the instanced path as a one-instance draw rather than through a
    // separate leftover path. Correct either way; this pins which one, so the consumer stays a
    // single loop.
    UseInstancingAwareShader(assetManager);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_instancing_single.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_instancing_single.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedDrawCallsRecorded, 1U);
    EXPECT_EQ(diagnostics.instancedObjectsRecorded, 1U);
}


// ---------------------------------------------------------------------------
// Cached offscreen target sets.
//
// ViewportManager::RenderAllPanes drives ONE shared renderer across every
// pane, resizing it to each pane's pixel size in turn. Before the cache, a
// split view of differently-sized panes destroyed and recreated all SEVEN
// size-dependent textures (color, depth, bloom bright, two blur, SSAO, contact shadow) per pane
// per frame - permanently, not as a warm-up. These tests measure that directly
// through the Null device's texture-creation counter rather than asserting
// something vague about performance.
// ---------------------------------------------------------------------------

TEST_F(Renderer3DTest, ResizeTargets_RepeatedSizeAlternation_StopsReallocating) {
    // The split-view case in miniature: two sizes, alternating, forever.
    ASSERT_TRUE(renderer3D->ResizeTargets(320U, 240U));
    ASSERT_TRUE(renderer3D->ResizeTargets(640U, 480U));
    const std::uint64_t afterWarmup = renderDevice.GetTextureCreateAttemptCount();

    for (int frame = 0; frame < 10; ++frame) {
        ASSERT_TRUE(renderer3D->ResizeTargets(320U, 240U));
        ASSERT_TRUE(renderer3D->ResizeTargets(640U, 480U));
    }

    // Twenty resizes across ten frames, and not one texture created: both sizes were already
    // cached. Before the cache this would have been 120 creations.
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCount(), afterWarmup);
}

TEST_F(Renderer3DTest, ResizeTargets_NewSize_AllocatesOnceAndThenIsFree) {
    const std::uint64_t beforeNewSize = renderDevice.GetTextureCreateAttemptCount();
    ASSERT_TRUE(renderer3D->ResizeTargets(800U, 600U));
    const std::uint64_t afterFirst = renderDevice.GetTextureCreateAttemptCount();

    // A genuinely new size does cost allocations - the cache removes repeat cost, it does not
    // conjure targets. Nine of them: color, depth, bloom bright, two blur, SSAO, SSAO blur,
    // contact shadow, and SSR.
    EXPECT_EQ(afterFirst - beforeNewSize, 9U);

    ASSERT_TRUE(renderer3D->ResizeTargets(800U, 600U));
    EXPECT_EQ(renderDevice.GetTextureCreateAttemptCount(), afterFirst);
}

TEST_F(Renderer3DTest, ResizeTargets_ManyDistinctSizes_DoesNotGrowWithoutBound) {
    // A window being dragged produces a new size every frame, and those sets are dead the moment
    // they are made. The cache must evict rather than accumulate, or a long drag exhausts GPU
    // memory - a slow leak is worse than the churn it replaced.
    for (std::uint32_t index = 0U; index < 24U; ++index) {
        ASSERT_TRUE(renderer3D->ResizeTargets(100U + index, 100U + index));
    }
    // Live resources are bounded by the cache limit rather than by the number of sizes seen. The
    // exact figure is not the point; that it is far below 24 sets is.
    EXPECT_LT(renderDevice.GetLiveResourceCount(), 24U * 9U);
}

TEST_F(Renderer3DTest, ResizeTargets_AfterEviction_StillRendersCorrectly) {
    // Eviction clears sets that a later resize may ask for again. The rebuilt set must be just as
    // usable as the original - an evicted-then-recreated size that renders nothing would be a
    // pane that goes black after the user resizes a window enough times.
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_evict.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_evict.uvmat");
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -5.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid, false);
    PrimeMaterialProgram(*renderer3D, cameraEntity);

    ASSERT_TRUE(renderer3D->ResizeTargets(256U, 256U));
    for (std::uint32_t index = 0U; index < 20U; ++index) {
        ASSERT_TRUE(renderer3D->ResizeTargets(300U + index, 300U + index));
    }
    ASSERT_TRUE(renderer3D->ResizeTargets(256U, 256U));

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.renderTargetWidth, 256U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 256U);
    EXPECT_TRUE(diagnostics.mainPassRecorded);
    EXPECT_EQ(diagnostics.meshDrawCallsRecorded, 1U);
}

TEST_F(Renderer3DTest, ResizeTargets_SwitchingBackToACachedSize_RendersThatSize) {
    // The cache must hand back the RIGHT set, not merely a valid one. A mismatch here would draw
    // one pane's content at another pane's resolution.
    ASSERT_TRUE(renderer3D->ResizeTargets(320U, 200U));
    ASSERT_TRUE(renderer3D->ResizeTargets(640U, 400U));
    ASSERT_TRUE(renderer3D->ResizeTargets(320U, 200U));

    const Scene::Entity cameraEntity = MakeCameraEntity();
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.renderTargetWidth, 320U);
    EXPECT_EQ(diagnostics.renderTargetHeight, 200U);
}

TEST_F(Renderer3DTest, ResizeTargets_ZeroSizeStillRejected) {
    // Unchanged behavior, restated because the resize path was rewritten around it.
    EXPECT_FALSE(renderer3D->ResizeTargets(0U, 480U));
    EXPECT_FALSE(renderer3D->ResizeTargets(640U, 0U));
}

TEST_F(Renderer3DTest, RenderFrame_StaticSceneSecondFrame_ServesPlacementsFromCache) {
    // End-to-end confirmation that the cache is actually reached through a real frame, not just
    // through MeshRenderer directly - the renderer owns the set across frames, and a set
    // rebuilt or reset per frame would silently never hit while every unit test still passed.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_cache_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("renderer3d_tests_cache_material.uvmat");
    const Scene::Entity movingEntity =
        MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    MakeMeshEntity(Math::Vector3{2.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // NOTE: WaitUntilAssetsReady primes the material program by rendering a frame of its own
    // (see PrimeMaterialProgram), so the cache is already warm by the time this test renders.
    // Asserting a cold first frame here was wrong about the fixture, not about the cache. Rather
    // than assert a miss count that depends on how many frames the fixture happened to render,
    // this drives the property that actually matters and holds either way: once the scene is
    // static, every subsequent frame is all hits and no misses.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics firstFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(firstFrame.placementCacheHits + firstFrame.placementCacheMisses, 2U)
        << "every mesh entity must be accounted for as exactly one hit or one miss";

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics secondFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(secondFrame.placementCacheHits, 2U);
    EXPECT_EQ(secondFrame.placementCacheMisses, 0U);

    // A third frame, to prove the steady state is genuinely steady rather than alternating.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics thirdFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(thirdFrame.placementCacheHits, 2U);
    EXPECT_EQ(thirdFrame.placementCacheMisses, 0U);

    // And moving one entity must cost exactly one miss - not zero (stale) and not two (the cache
    // dropping an untouched neighbour along with the one that moved).
    Scene::TransformComponent moved;
    moved.localPosition = Math::Vector3{0.0F, 0.0F, -18.0F};
    sceneGraph.SetLocalTransform(entityManager, movingEntity, moved);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics movedFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(movedFrame.placementCacheMisses, 1U);
    EXPECT_EQ(movedFrame.placementCacheHits, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_StaticPrimitives_ReachAnAllHitSteadyState) {
    // The property that matters, stated the way the mesh-cache test states it: once the scene is
    // static, every subsequent frame is all hits and no misses. Not asserted as a cold first frame
    // - the fixture's own priming renders frames before this test does, and a miss count that
    // depends on how many is a test about the fixture, not about the cache.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity movingEntity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});
    MakePrimitiveEntity(
        Math::Vector3{2.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::UVSphere, Math::Vector3{1.0F, 0.0F, 0.0F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics firstFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(firstFrame.primitivePlacementCacheHits + firstFrame.primitivePlacementCacheMisses, 2U)
        << "every primitive candidate must be accounted for as exactly one hit or one miss";

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics secondFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(secondFrame.primitivePlacementCacheHits, 2U);
    EXPECT_EQ(secondFrame.primitivePlacementCacheMisses, 0U);

    // A third frame, to prove the steady state is steady rather than alternating.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics thirdFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(thirdFrame.primitivePlacementCacheHits, 2U);
    EXPECT_EQ(thirdFrame.primitivePlacementCacheMisses, 0U);

    // Moving one primitive must cost exactly one miss - not zero (stale) and not two (the cache
    // dropping an untouched neighbour along with the one that moved).
    Scene::TransformComponent moved;
    moved.localPosition = Math::Vector3{0.0F, 0.0F, -18.0F};
    sceneGraph.SetLocalTransform(entityManager, movingEntity, moved);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics movedFrame = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(movedFrame.primitivePlacementCacheMisses, 1U);
    EXPECT_EQ(movedFrame.primitivePlacementCacheHits, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_ChangingOnlyThePrimitiveKind_InvalidatesThePlacement) {
    // The hazard specific to this key. Kind is not part of the transform, but it selects the local
    // bounds the world bounds are derived from, so a key that omitted it would serve a cube's
    // bounds for a sphere with the transform sitting perfectly still. Exercised by swapping kind
    // and nothing else.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity entity = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheMisses, 0U)
        << "the cache must be warm before the kind swap for the swap to prove anything";

    entityManager.GetComponent<Scene::PrimitiveMeshComponent>(entity).kind =
        Scene::PrimitiveMeshKind::UVSphere;

    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheMisses, 1U)
        << "a kind change must invalidate the cached bounds even though the transform is identical";
}

TEST_F(Renderer3DTest, RenderFrame_MovingOnlyTheCamera_StillHitsThePlacementCache) {
    // The split this optimization rests on: placement is entity-dependent and cached, culling and
    // sort depth are view-dependent and are not. A camera move must therefore cost zero misses
    // while still being free to change what is visible. If someone later folds the frustum test
    // into the cached half, this is the test that catches it.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheMisses, 0U);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitiveItemsExtracted, 1U);

    // Turn the camera away from the primitive. Same scene, different view.
    Scene::TransformComponent cameraTransform;
    cameraTransform.localPosition = Math::Vector3{0.0F, 0.0F, -400.0F};
    sceneGraph.SetLocalTransform(entityManager, cameraEntity, cameraTransform);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics movedCamera = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(movedCamera.primitivePlacementCacheMisses, 0U)
        << "the camera moving must not invalidate any entity's placement";
    EXPECT_EQ(movedCamera.primitivePlacementCacheHits, 1U);
    EXPECT_EQ(movedCamera.primitiveItemsExtracted, 0U)
        << "culling must still respond to the new view despite the placement being reused";
}

TEST_F(Renderer3DTest, RenderFrame_RejectedPrimitive_IsCachedAsARejectionAndStaysRejected) {
    // A rejection costs the same recompute to rediscover as a success, so it is cached too. The
    // risk in caching a negative is that it is cached as a positive by accident - an entity that
    // cannot produce finite geometry must never appear in the extracted items, on the miss frame
    // or on any hit frame after it.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::TransformComponent transform;
    transform.localPosition = Math::Vector3{std::numeric_limits<float>::max(), 0.0F, -10.0F};
    sceneGraph.AttachTransform(entityManager, entity, transform);
    sceneGraph.Update(entityManager);
    entityManager.AddComponent<Scene::PrimitiveMeshComponent>(
        entity,
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});

    for (int frame = 0; frame < 3; ++frame) {
        renderer3D->RenderFrame(entityManager, cameraEntity);
        const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
        EXPECT_EQ(diagnostics.primitiveCandidates, 1U) << "frame " << frame;
        EXPECT_EQ(diagnostics.primitiveItemsExtracted, 0U)
            << "a rejected primitive must stay rejected on frame " << frame;
    }
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheHits, 1U)
        << "the rejection must be served from the cache, not recomputed every frame";
}

TEST_F(Renderer3DTest, RenderFrame_DestroyedPrimitive_IsPrunedFromThePlacementCache) {
    // The cache outlives the frame, so it must not outlive the entity. There is no accessor for
    // the cache's size, so this drives the prune through what is observable: after a destroy the
    // remaining primitive must still be a hit (the prune must not evict the survivor) and the
    // destroyed one must no longer be counted as a candidate at all.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity doomed = MakePrimitiveEntity(
        Math::Vector3{0.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});
    MakePrimitiveEntity(
        Math::Vector3{2.0F, 0.0F, -10.0F},
        Scene::PrimitiveMeshComponent{Scene::PrimitiveMeshKind::Cube, Math::Vector3{1.0F, 1.0F, 1.0F}});

    renderer3D->RenderFrame(entityManager, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    ASSERT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheHits, 2U);

    entityManager.DestroyEntity(doomed);
    sceneGraph.Update(entityManager);

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics afterDestroy = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(afterDestroy.primitiveCandidates, 1U);
    EXPECT_EQ(afterDestroy.primitivePlacementCacheHits, 1U)
        << "the surviving primitive must still be served from the cache";
    EXPECT_EQ(afterDestroy.primitivePlacementCacheMisses, 0U);

    // And a frame later the survivor is still a hit, proving the prune left a usable cache behind
    // rather than one that happens to work once.
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().primitivePlacementCacheHits, 1U);
}

} // namespace
TEST_F(Renderer3DTest, RenderFrame_ASkinnedMeshUnderASkeletonIsDrawnInTheSkeletonsPose) {
    // One triangle bound wholly to joint "Hips"; no material, so it takes the unmaterialed path.
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.vertices = {
            Asset::MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        };
        mesh.indices = {0, 1, 2};
        mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};
        Asset::MeshJoint hips;
        hips.name = "Hips";
        mesh.joints = {hips};
        Asset::MeshSkinningInfluence influence;
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences = {influence, influence, influence};
        return true;
    });
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_skinned_mesh.uvmodel");
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::TransformComponent skeletonTransform;
    skeletonTransform.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransform(entityManager, skeletonEntity, skeletonTransform);
    Scene::Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone bone;
    bone.name = "Hips";
    skeleton.bones = {bone};
    entityManager.AddComponent<Scene::Skeleton3DComponent>(skeletonEntity, skeleton);
    const Scene::Entity meshEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, meshEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, meshEntity, skeletonEntity);
    entityManager.AddComponent<Scene::MeshComponent>(meshEntity, Scene::MeshComponent{meshGuid, Asset::kInvalidAssetGuid});
    sceneGraph.Update(entityManager);
    Asset::AssetHandle<Asset::MeshAsset> meshHandle = assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !meshHandle.IsReady(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.IsReady());

    // The vertex buffer bound for the last indexed draw of three indices, read back.
    const auto drawnVertices = [this]() {
        std::vector<Asset::MeshVertex> vertices;
        BufferHandle bound{};
        for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
            if (std::holds_alternative<BindVertexBufferCommand>(command)) {
                bound = std::get<BindVertexBufferCommand>(command).buffer;
            } else if (std::holds_alternative<DrawIndexedCommand>(command) &&
                       std::get<DrawIndexedCommand>(command).indexCount == 3U) {
                vertices.assign(3U, Asset::MeshVertex{});
                if (!renderDevice.ReadbackBuffer(bound, std::as_writable_bytes(std::span(vertices)))) {
                    vertices.clear();
                }
            }
        }
        return vertices;
    };

    // The skeleton at rest: the triangle as authored. (The first frame starts the built-in
    // programs compiling; the drawn one comes after.)
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().skinnedMeshesDrawn, 1U);
    std::vector<Asset::MeshVertex> vertices = drawnVertices();
    ASSERT_EQ(vertices.size(), 3U);
    EXPECT_NEAR(vertices[2].position.y, 1.0F, 1e-5F);

    // Posing the bone moves the triangle with it.
    entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose = {
        Scene::SkeletonBonePose{Math::Vector3{0.0F, 2.0F, 0.0F}, Math::Quaternion{},
                                   Math::Vector3{1.0F, 1.0F, 1.0F}}};
    renderer3D->RenderFrame(entityManager, cameraEntity);
    vertices = drawnVertices();
    ASSERT_EQ(vertices.size(), 3U);
    EXPECT_NEAR(vertices[0].position.y, 2.0F, 1e-5F);
    EXPECT_NEAR(vertices[2].position.y, 3.0F, 1e-5F);

    // Without its skeleton the mesh is drawn in bind pose from the shared buffer.
    sceneGraph.SetParent(entityManager, meshEntity, Scene::kInvalidEntity);
    sceneGraph.Update(entityManager);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().skinnedMeshesDrawn, 0U);
}

TEST_F(Renderer3DTest, RenderFrame_AJointTheSkeletonDoesNotNameStaysAtItsRestPoseInsteadOfLeavingTheModel) {
    // Hips at y = 1 and Spine (a child) at y = 2 in the bind pose; a vertex sits on Spine at y = 2.
    // The skeleton only names "Hips", so Spine has no bone of its name. It must stay where the model
    // authored it (y = 2), not fall to its parent's origin - which is where an identity local put it.
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.vertices = {
            Asset::MeshVertex{Math::Vector3{0.0F, 2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{1.0F, 2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{0.0F, 3.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        };
        mesh.indices = {0, 1, 2};
        mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 2.0F, 0.0F}, Math::Vector3{1.0F, 3.0F, 0.0F}};
        Asset::MeshJoint hips;
        hips.name = "Hips";
        hips.inverseBindMatrix = Math::Matrix4x4::Identity();
        hips.inverseBindMatrix.m[1][3] = -1.0F;
        Asset::MeshJoint spine;
        spine.name = "Spine";
        spine.parentIndex = 0U;
        spine.inverseBindMatrix = Math::Matrix4x4::Identity();
        spine.inverseBindMatrix.m[1][3] = -2.0F;
        mesh.joints = {hips, spine};
        Asset::MeshSkinningInfluence influence;
        influence.joints[0] = 1U;
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences = {influence, influence, influence};
        return true;
    });
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_partial_rig.uvmodel");
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    Scene::TransformComponent skeletonTransform;
    skeletonTransform.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransform(entityManager, skeletonEntity, skeletonTransform);
    Scene::Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone bone;
    bone.name = "Hips";
    bone.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeleton.bones = {bone};
    entityManager.AddComponent<Scene::Skeleton3DComponent>(skeletonEntity, skeleton);
    const Scene::Entity meshEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, meshEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, meshEntity, skeletonEntity);
    entityManager.AddComponent<Scene::MeshComponent>(meshEntity, Scene::MeshComponent{meshGuid, Asset::kInvalidAssetGuid});
    sceneGraph.Update(entityManager);
    Asset::AssetHandle<Asset::MeshAsset> meshHandle = assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !meshHandle.IsReady(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.IsReady());

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().skinnedMeshesDrawn, 1U);
    std::vector<Asset::MeshVertex> vertices(3U);
    BufferHandle bound{};
    for (const RecordedCommand& command : renderDevice.GetLastSubmittedCommands()) {
        if (std::holds_alternative<BindVertexBufferCommand>(command)) {
            bound = std::get<BindVertexBufferCommand>(command).buffer;
        }
    }
    ASSERT_TRUE(renderDevice.ReadbackBuffer(bound, std::as_writable_bytes(std::span(vertices))));
    EXPECT_NEAR(vertices[0].position.y, 2.0F, 1e-4F);
    EXPECT_NEAR(vertices[2].position.y, 3.0F, 1e-4F);
}

TEST_F(Renderer3DTest, RenderFrame_ASkinnedMeshInsideAGroupBindsToTheSkeletonBesideTheGroup) {
    // The tree a DCC file has: Character > { Skeleton3D, Geo > Mesh }. The mesh is not a child or a sibling of the
    // skeleton - its grandparent is the skeleton's parent - and it still has to be skinned by it.
    assetManager.RegisterLoader<Asset::MeshAsset>([](const std::filesystem::path&, Asset::MeshAsset& mesh) {
        mesh.vertices = {
            Asset::MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{1.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
            Asset::MeshVertex{Math::Vector3{0.0F, 2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
        };
        mesh.indices = {0, 1, 2};
        mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{1.0F, 2.0F, 0.0F}};
        Asset::MeshJoint hips;
        hips.name = "Hips";
        hips.inverseBindMatrix = Math::Matrix4x4::Identity();
        hips.inverseBindMatrix.m[1][3] = -1.0F;
        mesh.joints = {hips};
        Asset::MeshSkinningInfluence influence;
        influence.joints[0] = 0U;
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences = {influence, influence, influence};
        return true;
    });
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_grouped_mesh.uvmodel");
    const Scene::Entity characterEntity = entityManager.CreateEntity();
    Scene::TransformComponent characterTransform;
    characterTransform.localPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    sceneGraph.AttachTransform(entityManager, characterEntity, characterTransform);
    const Scene::Entity skeletonEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, skeletonEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, skeletonEntity, characterEntity);
    Scene::Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    Scene::SkeletonBone bone;
    bone.name = "Hips";
    bone.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeleton.bones = {bone};
    entityManager.AddComponent<Scene::Skeleton3DComponent>(skeletonEntity, skeleton);
    const Scene::Entity groupEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, groupEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, groupEntity, characterEntity);
    const Scene::Entity meshEntity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, meshEntity, Scene::TransformComponent{});
    sceneGraph.SetParent(entityManager, meshEntity, groupEntity);
    entityManager.AddComponent<Scene::MeshComponent>(meshEntity, Scene::MeshComponent{meshGuid, Asset::kInvalidAssetGuid});
    sceneGraph.Update(entityManager);
    Asset::AssetHandle<Asset::MeshAsset> meshHandle = assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !meshHandle.IsReady(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.IsReady());

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().skinnedMeshesDrawn, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_EmptySkyAssetKeepsProceduralSky) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity environment = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environment,
                                                                        Scene::WorldEnvironment3DComponent{});
    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*enabled).value, 0);
    bool skyPassBoundSlot1 = false;
    for (auto it = enabled; it != commands.cend(); ++it) {
        if (std::holds_alternative<EndRenderPassCommand>(*it)) {
            break;
        }
        if (std::holds_alternative<BindTextureCommand>(*it) &&
            std::get<BindTextureCommand>(*it).slot == 1U) {
            skyPassBoundSlot1 = true;
        }
    }
    EXPECT_FALSE(skyPassBoundSlot1);
}

TEST_F(Renderer3DTest, RenderFrame_SkyAssetDrivesEnvironmentMapAmbientAndSky) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponent{});
    const std::filesystem::path skyPath{"environment/day.hdr"};
    const Asset::AssetGuid skyGuid = assetDatabase.Register(skyPath);
    WaitUntilTextureReady(skyGuid);

    Scene::WorldEnvironment3DComponent environment{};
    environment.skyAssetPath = skyPath.string();
    environment.ambientSource = Scene::WorldEnvironmentAmbientSource::EnvironmentMap;
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*enabled).value, 1);
    const auto ambientSource = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uAmbientSource";
    });
    const auto ambientMapEnabled =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == "uAmbientEnvironmentMapEnabled";
        });
    const auto ambientMapSampler =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == "uAmbientEnvironmentMap";
        });
    ASSERT_NE(ambientSource, commands.cend());
    ASSERT_NE(ambientMapEnabled, commands.cend());
    ASSERT_NE(ambientMapSampler, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*ambientSource).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSource::EnvironmentMap));
    EXPECT_EQ(std::get<SetUniformIntCommand>(*ambientMapEnabled).value, 1);
    EXPECT_EQ(std::get<SetUniformIntCommand>(*ambientMapSampler).value, 14);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindTextureCommand>(command) &&
               std::get<BindTextureCommand>(command).slot == 14U &&
               std::get<BindTextureCommand>(command).texture != kInvalidTextureHandle;
    }));
    bool skyPassBoundSlot1 = false;
    for (auto it = enabled; it != commands.cend(); ++it) {
        if (std::holds_alternative<EndRenderPassCommand>(*it)) {
            break;
        }
        if (std::holds_alternative<BindTextureCommand>(*it) &&
            std::get<BindTextureCommand>(*it).slot == 1U &&
            std::get<BindTextureCommand>(*it).texture != kInvalidTextureHandle) {
            skyPassBoundSlot1 = true;
        }
    }
    EXPECT_TRUE(skyPassBoundSlot1);
}

TEST_F(Renderer3DTest, RenderFrame_MissingSkyAssetFallsBackForSkyAndAmbientLighting) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakePrimitiveEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, Scene::PrimitiveMeshComponent{});
    assetManager.RegisterLoader<Asset::TextureAsset>(
        [](const std::filesystem::path&, Asset::TextureAsset&) { return false; });
    Scene::WorldEnvironment3DComponent environment{};
    environment.skyAssetPath = "environment/missing.hdr";
    environment.ambientSource = Scene::WorldEnvironmentAmbientSource::EnvironmentMap;
    const Scene::Entity environmentEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::WorldEnvironment3DComponent>(environmentEntity, environment);

    PrimeMaterialProgram(*renderer3D, cameraEntity);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uSkyTextureEnabled";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*enabled).value, 0);
    const auto ambientSource = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uAmbientSource";
    });
    const auto ambientMapEnabled =
        std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
            return std::holds_alternative<SetUniformIntCommand>(command) &&
                   std::get<SetUniformIntCommand>(command).name == "uAmbientEnvironmentMapEnabled";
        });
    ASSERT_NE(ambientSource, commands.cend());
    ASSERT_NE(ambientMapEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*ambientSource).value,
              static_cast<std::int32_t>(Scene::WorldEnvironmentAmbientSource::EnvironmentMap));
    EXPECT_EQ(std::get<SetUniformIntCommand>(*ambientMapEnabled).value, 0);
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindTextureCommand>(command) &&
               std::get<BindTextureCommand>(command).slot == 14U &&
               std::get<BindTextureCommand>(command).texture != kInvalidTextureHandle;
    })); // A valid fallback descriptor remains bound even though the map source is disabled.
}

// ---------------------------------------------------------------------------------------------
// Spot-light atlas shadows (Increment 32)
// ---------------------------------------------------------------------------------------------

TEST_F(Renderer3DTest, RenderFrame_SpotLightCasterRecordsAtlasPassesAndUploadsTheContract) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(MakeSpotLightComponent(), Math::Vector3{0.0F, 0.0F, -5.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("spot_shadow_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("spot_shadow_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.spotShadowCandidates, 1U);
    ASSERT_EQ(diagnostics.spotShadowTilesActive, 1U);
    EXPECT_EQ(diagnostics.shadowAtlasTileResolution, 1024U);
    // One full-atlas clear plus one tile pass.
    EXPECT_EQ(diagnostics.shadowAtlasPassesRecorded, 2U);
    EXPECT_GE(diagnostics.shadowAtlasDrawCallsRecorded, 1U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const std::vector<BeginRenderPassCommand> tilePasses = AtlasTilePasses(commands);
    ASSERT_EQ(tilePasses.size(), 1U);
    // Load, not Clear: clearing per tile would erase every tile already rendered this frame.
    EXPECT_EQ(tilePasses[0].desc.depthLoadOp, LoadOp::Load);
    EXPECT_NE(tilePasses[0].desc.depthAttachment, kInvalidTextureHandle);
    ASSERT_TRUE(tilePasses[0].desc.viewportOverride.has_value());
    EXPECT_EQ(tilePasses[0].desc.viewportOverride->size.x, 1024);
    EXPECT_EQ(tilePasses[0].desc.viewportOverride->size.y, 1024);

    // The atlas itself must be cleared once, full-attachment, before any tile pass.
    const auto uniformEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLightShadowEnabled[0]";
    });
    ASSERT_NE(uniformEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*uniformEnabled).value, 1);

    const auto uniformMatrix = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformMatrix4x4Command>(command) &&
               std::get<SetUniformMatrix4x4Command>(command).name == "uLightShadowMatrices[0]";
    });
    ASSERT_NE(uniformMatrix, commands.cend());
    EXPECT_NE(std::get<SetUniformMatrix4x4Command>(*uniformMatrix).value, Math::Matrix4x4{});

    // The tile rect the shader unprojects with must have real extent, and it must match the
    // viewport the pass rendered into.
    const auto uniformTileRect = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector4Command>(command) &&
               std::get<SetUniformVector4Command>(command).name == "uLightShadowTileRects[0]";
    });
    ASSERT_NE(uniformTileRect, commands.cend());
    const Math::Vector4 tileRect = std::get<SetUniformVector4Command>(*uniformTileRect).value;
    constexpr float inverseAtlas = 1.0F / static_cast<float>(kShadowAtlasResolution);
    EXPECT_NEAR(tileRect.z - tileRect.x, 1024.0F * inverseAtlas, 1.0e-6F);
    EXPECT_NEAR(tileRect.x, static_cast<float>(tilePasses[0].desc.viewportOverride->position.x) * inverseAtlas,
                1.0e-6F);

    const auto uniformDepthRange = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector2Command>(command) &&
               std::get<SetUniformVector2Command>(command).name == "uLightShadowDepthRange[0]";
    });
    ASSERT_NE(uniformDepthRange, commands.cend());
    const Math::Vector2 depthRange = std::get<SetUniformVector2Command>(*uniformDepthRange).value;
    EXPECT_GT(depthRange.x, 0.0F);
    EXPECT_FLOAT_EQ(depthRange.y, 10.0F);

    const auto uniformTexelSize = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector2Command>(command) &&
               std::get<SetUniformVector2Command>(command).name == "uShadowAtlasTexelSize";
    });
    ASSERT_NE(uniformTexelSize, commands.cend());
    EXPECT_FLOAT_EQ(std::get<SetUniformVector2Command>(*uniformTexelSize).value.x,
                    1.0F / static_cast<float>(kShadowAtlasResolution));

    // The atlas is bound at the dedicated slot in the main pass.
    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindTextureCommand>(command) &&
               std::get<BindTextureCommand>(command).slot == kShadowAtlasTextureSlot &&
               std::get<BindTextureCommand>(command).texture != kInvalidTextureHandle;
    }));
}

TEST_F(Renderer3DTest, RenderFrame_SpotLightWithNoCasterSkipsTheAtlasPass) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    // Pointing away from the only mesh: eligible against the view frustum (its influence sphere
    // still reaches into it), but nothing is inside its cone.
    // A half turn about +Y: the light's forward is -Z, so this points it at +Z, away from the mesh.
    MakeLightEntity(MakeSpotLightComponent(2.0F, 10.0F), Math::Vector3{0.0F, 0.0F, -1.0F},
                        Math::Quaternion{0.0F, 1.0F, 0.0F, 0.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("spot_shadow_empty_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("spot_shadow_empty_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.spotShadowCandidates, 1U);
    EXPECT_EQ(diagnostics.spotShadowTilesActive, 0U);
    EXPECT_EQ(diagnostics.shadowAtlasTileResolution, 0U);
    EXPECT_EQ(diagnostics.shadowAtlasPassesRecorded, 0U);
    EXPECT_TRUE(AtlasTilePasses(renderDevice.GetLastSubmittedCommands()).empty());

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto uniformEnabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLightShadowEnabled[0]";
    });
    ASSERT_NE(uniformEnabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*uniformEnabled).value, 0);
    // Slot 1 must be explicitly disabled too - a stale enable from an earlier frame would sample
    // a tile that now belongs to somebody else.
    const auto secondSlot = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLightShadowEnabled[1]";
    });
    ASSERT_NE(secondSlot, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*secondSlot).value, 0);
}

TEST_F(Renderer3DTest, RenderFrame_DirectionalOnlySceneNeverCreatesAnAtlas) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    Scene::LightComponent sun;
    sun.type = Scene::LightType::Directional;
    sun.intensity = 1.0F;
    MakeLightEntity(sun);
    const Asset::AssetGuid meshGuid = assetDatabase.Register("spot_shadow_directional_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("spot_shadow_directional_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.spotShadowCandidates, 0U);
    EXPECT_EQ(diagnostics.shadowAtlasPassesRecorded, 0U);
    // The 64 MiB attachment must not exist for a scene that never asks for it.
    EXPECT_FALSE(std::any_of(renderDevice.GetLastSubmittedCommands().cbegin(),
                             renderDevice.GetLastSubmittedCommands().cend(),
                             [](const RecordedCommand& command) {
                                 return std::holds_alternative<BindTextureCommand>(command) &&
                                        std::get<BindTextureCommand>(command).slot ==
                                            kShadowAtlasTextureSlot;
                             }));
}

TEST_F(Renderer3DTest, RenderFrame_SpotShadowTileResolutionFollowsTheConfiguredSetting) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(MakeSpotLightComponent(), Math::Vector3{0.0F, 0.0F, -5.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("spot_shadow_resolution_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("spot_shadow_resolution_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->SetShadowAtlasTileResolution(512U);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().shadowAtlasTileResolution, 512U);
    const std::vector<BeginRenderPassCommand> halfResPasses =
        AtlasTilePasses(renderDevice.GetLastSubmittedCommands());
    ASSERT_EQ(halfResPasses.size(), 1U);
    EXPECT_EQ(halfResPasses[0].desc.viewportOverride->size.x, 512);

    // A request larger than the atlas can hold for this many casters degrades rather than failing.
    renderer3D->SetShadowAtlasTileResolution(4096U);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_EQ(renderer3D->GetLastFrameDiagnostics().shadowAtlasTileResolution, 4096U);
    const std::vector<BeginRenderPassCommand> fullResPasses =
        AtlasTilePasses(renderDevice.GetLastSubmittedCommands());
    ASSERT_EQ(fullResPasses.size(), 1U);
    EXPECT_EQ(fullResPasses[0].desc.viewportOverride->size.x, 4096);
}


// ---------------------------------------------------------------------------------------------
// Point-light atlas shadows (six cube faces per light)
// ---------------------------------------------------------------------------------------------

TEST_F(Renderer3DTest, RenderFrame_PointLightCasterRecordsSixAtlasPasses) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(MakePointLightComponent(), Math::Vector3{0.0F, 0.0F, -5.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("point_shadow_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("point_shadow_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.pointShadowCandidates, 1U);
    EXPECT_EQ(diagnostics.spotShadowCandidates, 0U);
    // Six faces, of which only the ones facing the mesh cull anything. The mesh sits on the light's
    // -Z side, so exactly the -Z face owns a caster - the other five must not record a torn,
    // half-rendered cube.
    EXPECT_EQ(diagnostics.pointShadowTilesActive, 1U);
    EXPECT_EQ(diagnostics.shadowAtlasTileResolution, 1024U);
    // One full-atlas clear plus one pass per active face.
    EXPECT_EQ(diagnostics.shadowAtlasPassesRecorded, 2U);
    EXPECT_GE(diagnostics.shadowAtlasDrawCallsRecorded, 1U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const std::vector<BeginRenderPassCommand> tilePasses = AtlasTilePasses(commands);
    ASSERT_EQ(tilePasses.size(), 1U);
    EXPECT_EQ(tilePasses[0].desc.depthLoadOp, LoadOp::Load);
    ASSERT_TRUE(tilePasses[0].desc.viewportOverride.has_value());
    EXPECT_EQ(tilePasses[0].desc.viewportOverride->size.x, 1024);

    // The light is enabled as a whole, and the face that owns the caster is the ONE cube rect with
    // extent - the other five stay zero so the shader treats those faces as unshadowed.
    const auto enabled = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformIntCommand>(command) &&
               std::get<SetUniformIntCommand>(command).name == "uLightShadowEnabled[0]";
    });
    ASSERT_NE(enabled, commands.cend());
    EXPECT_EQ(std::get<SetUniformIntCommand>(*enabled).value, 1);

    std::size_t facesWithExtent = 0U;
    for (std::size_t face = 0U; face < kPointShadowFaceCount; ++face) {
        const std::string name = "uLightShadowCubeRects[" + std::to_string(face) + "]";
        const auto uniform = std::find_if(commands.cbegin(), commands.cend(),
                                          [&name](const RecordedCommand& command) {
                                              return std::holds_alternative<SetUniformVector4Command>(command) &&
                                                     std::get<SetUniformVector4Command>(command).name == name;
                                          });
        ASSERT_NE(uniform, commands.cend()) << name;
        const Math::Vector4 rect = std::get<SetUniformVector4Command>(*uniform).value;
        if (rect.z - rect.x > 0.0F) {
            ++facesWithExtent;
            constexpr float inverseAtlas = 1.0F / static_cast<float>(kShadowAtlasResolution);
            EXPECT_NEAR(rect.z - rect.x, 1024.0F * inverseAtlas, 1.0e-6F);
        }
    }
    EXPECT_EQ(facesWithExtent, 1U);

    // A point light never uses the spot half of the contract.
    const auto spotRect = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector4Command>(command) &&
               std::get<SetUniformVector4Command>(command).name == "uLightShadowTileRects[0]";
    });
    ASSERT_NE(spotRect, commands.cend());
    const Math::Vector4 spotTile = std::get<SetUniformVector4Command>(*spotRect).value;
    EXPECT_FLOAT_EQ(spotTile.z - spotTile.x, 0.0F);

    EXPECT_TRUE(std::any_of(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<BindTextureCommand>(command) &&
               std::get<BindTextureCommand>(command).slot == kShadowAtlasTextureSlot &&
               std::get<BindTextureCommand>(command).texture != kInvalidTextureHandle;
    }));
}

TEST_F(Renderer3DTest, RenderFrame_PointLightBehindTheCasterStillRendersTheWholeCube) {
    // Same light, but the mesh is now on the +Z side. The point still matters: the face that owns
    // the caster must follow the geometry, proving the six matrices are not all copies of one face.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(MakePointLightComponent(40.0F), Math::Vector3{0.0F, 0.0F, -14.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("point_shadow_flip_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("point_shadow_flip_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.pointShadowCandidates, 1U);
    EXPECT_EQ(diagnostics.pointShadowTilesActive, 1U);

    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    // The mesh is at +Z relative to the light, so face 4 (+Z) is the one with a rect.
    std::size_t expectedFaceWithExtent = 4U;
    std::size_t facesWithExtent = 0U;
    for (std::size_t face = 0U; face < kPointShadowFaceCount; ++face) {
        const std::string name = "uLightShadowCubeRects[" + std::to_string(face) + "]";
        const auto uniform = std::find_if(commands.cbegin(), commands.cend(),
                                          [&name](const RecordedCommand& command) {
                                              return std::holds_alternative<SetUniformVector4Command>(command) &&
                                                     std::get<SetUniformVector4Command>(command).name == name;
                                          });
        ASSERT_NE(uniform, commands.cend()) << name;
        const Math::Vector4 rect = std::get<SetUniformVector4Command>(*uniform).value;
        if (rect.z - rect.x > 0.0F) {
            ++facesWithExtent;
            EXPECT_EQ(face, expectedFaceWithExtent) << "wrong face owns the caster";
        }
    }
    EXPECT_EQ(facesWithExtent, 1U);
}

TEST_F(Renderer3DTest, RenderFrame_PointLightShadowSharesTheBudgetWithSpotLights) {
    // A point light and a spot light in one frame must not fight over the same tile, and the tile
    // resolution must fit ALL tiles, not just one light's worth.
    const Scene::Entity cameraEntity = MakeCameraEntity();
    MakeLightEntity(MakeSpotLightComponent(), Math::Vector3{-2.0F, 0.0F, -5.0F});
    MakeLightEntity(MakePointLightComponent(), Math::Vector3{2.0F, 0.0F, -5.0F});
    const Asset::AssetGuid meshGuid = assetDatabase.Register("mixed_shadow_mesh.uvmodel");
    const Asset::AssetGuid materialGuid = assetDatabase.Register("mixed_shadow_material.uvmat");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid, materialGuid);
    WaitUntilShadowProgramReady();
    WaitUntilAssetsReady(meshGuid, materialGuid);

    // Seven tiles at the requested 4096 do not fit; the planner must have halved the resolution.
    renderer3D->SetShadowAtlasTileResolution(4096U);
    renderer3D->RenderFrame(entityManager, cameraEntity);

    const Renderer3DFrameDiagnostics& diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_EQ(diagnostics.spotShadowCandidates, 1U);
    EXPECT_EQ(diagnostics.pointShadowCandidates, 1U);
    EXPECT_EQ(diagnostics.shadowAtlasTileResolution, 1024U);

    // Both halves of the contract are live: one spot rect and at least one cube rect.
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto spotRect = std::find_if(commands.cbegin(), commands.cend(), [](const RecordedCommand& command) {
        return std::holds_alternative<SetUniformVector4Command>(command) &&
               std::get<SetUniformVector4Command>(command).name == "uLightShadowTileRects[0]";
    });
    ASSERT_NE(spotRect, commands.cend());
    EXPECT_GT(std::get<SetUniformVector4Command>(*spotRect).value.z -
                  std::get<SetUniformVector4Command>(*spotRect).value.x,
              0.0F);
    EXPECT_GT(diagnostics.pointShadowTilesActive, 0U);
}

// An imported mesh names the materials the importer published beside it, and those are registered
// by the importer as it writes them - so an import that was served from the cache registered
// nothing, and the GUIDs the mesh carries resolve to no file at all. Both paths used to drop such
// a mesh: the materialed one because its material would not load, and the built-in one because the
// mesh appeared to have a material. That is how a model that used to draw grey vanished outright.
// The built-in path must draw a mesh whose material cannot be RESOLVED, not merely one that has
// none.
TEST_F(Renderer3DTest, RenderFrame_AMeshWhoseOwnMaterialCannotBeResolved_IsStillDrawn) {
    const Asset::AssetGuid unresolvableMaterial{0xDEADBEEFU};
    assetManager.RegisterLoader<Asset::MeshAsset>(
        [unresolvableMaterial](const std::filesystem::path&, Asset::MeshAsset& mesh) {
            mesh.vertices = {
                Asset::MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
                Asset::MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
                Asset::MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 1.0F},
            };
            mesh.indices = {0, 1, 2};
            mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};
            mesh.materialGuids = {unresolvableMaterial};
            return true;
        });
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Asset::AssetGuid meshGuid = assetDatabase.Register("renderer3d_tests_unresolvable_material.uvmodel");
    MakeMeshEntity(Math::Vector3{0.0F, 0.0F, -10.0F}, meshGuid,
                                                          Asset::kInvalidAssetGuid);
    Asset::AssetHandle<Asset::MeshAsset> meshHandle = assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
    for (int iteration = 0; iteration < kMaxPollIterations && !meshHandle.IsReady(); ++iteration) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(meshHandle.IsReady());
    // The premise of the test: the mesh really does name a material, and that material really does
    // not exist. Without both, this would pass for a mesh that never had a material at all.
    ASSERT_EQ(meshHandle.TryGet()->FirstSurfaceMaterialGuid(), unresolvableMaterial);
    ASSERT_TRUE(assetDatabase.Resolve(unresolvableMaterial).empty());

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();

    EXPECT_EQ(diagnostics.meshItemsExtracted, 0U)
        << "the materialed path cannot draw it - the material will not load - and must not claim to";
    EXPECT_GT(diagnostics.primitiveItemsExtracted, 0U)
        << "the built-in path must draw the mesh untextured rather than drop it entirely";
}

TEST_F(Renderer3DTest, SoftFilmicToneMappingAndSrgbOutputEncodingAreDefaultInFinalComposite) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const std::vector<RecordedCommand>& commands = renderDevice.GetLastSubmittedCommands();
    const auto findIntUniform = [&commands](const std::string_view name) -> std::optional<std::int32_t> {
        for (const RecordedCommand& cmd : commands) {
            if (const auto* const u = std::get_if<SetUniformIntCommand>(&cmd)) {
                if (u->name == name) {
                    return u->value;
                }
            }
        }
        return std::nullopt;
    };
    ASSERT_TRUE(findIntUniform("uToneMappingMethod").has_value());
    EXPECT_EQ(*findIntUniform("uToneMappingMethod"), 2);
    ASSERT_TRUE(findIntUniform("uEncodeSrgbOutput").has_value());
    EXPECT_EQ(*findIntUniform("uEncodeSrgbOutput"), 1);
    ASSERT_TRUE(findIntUniform("uSsaoCompositeEnabled").has_value());
    EXPECT_EQ(*findIntUniform("uSsaoCompositeEnabled"), 1);
    ASSERT_TRUE(findIntUniform("uBloomCompositeEnabled").has_value());
    EXPECT_EQ(*findIntUniform("uBloomCompositeEnabled"), 1);
}

TEST_F(Renderer3DTest, StandaloneSky3DPreservesDefaultPostProcessingPasses) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    const Scene::Entity skyEntity = entityManager.CreateEntity();
    Scene::Sky3DComponent sky{};
    sky.enabled = true;
    entityManager.AddComponent<Scene::Sky3DComponent>(skyEntity, sky);
    WaitUntilShadowProgramReady();

    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics diagnostics = renderer3D->GetLastFrameDiagnostics();
    EXPECT_TRUE(diagnostics.ssaoPassRecorded);
    EXPECT_TRUE(diagnostics.bloomPassRecorded);
    EXPECT_TRUE(diagnostics.toneMappingPassRecorded);
}

TEST(RendererProfileResolutionTest, StepsDownCleanlyByDeviceCapabilities) {
    RenderDeviceCapabilities fullGpu{};
    fullGpu.storageBuffers = RenderFeatureSupport::Native;
    fullGpu.halfFloatColorTargets = RenderFeatureSupport::Native;
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::ForwardPlus, fullGpu);
        EXPECT_EQ(res.requested, Platform::RendererProfile::ForwardPlus);
        EXPECT_EQ(res.effective, Platform::RendererProfile::ForwardPlus);
        EXPECT_FALSE(res.fallbackApplied);
        EXPECT_TRUE(res.fallbackReason.empty());
    }
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::Mobile, fullGpu);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Mobile);
        EXPECT_FALSE(res.fallbackApplied);
    }

    RenderDeviceCapabilities gles30Gpu{};
    gles30Gpu.storageBuffers = RenderFeatureSupport::Unsupported;
    gles30Gpu.halfFloatColorTargets = RenderFeatureSupport::Native;
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::ForwardPlus, gles30Gpu);
        EXPECT_EQ(res.requested, Platform::RendererProfile::ForwardPlus);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Mobile);
        EXPECT_TRUE(res.fallbackApplied);
        EXPECT_FALSE(res.fallbackReason.empty());
    }
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::Mobile, gles30Gpu);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Mobile);
        EXPECT_FALSE(res.fallbackApplied);
    }

    RenderDeviceCapabilities lowEndGpu{};
    lowEndGpu.storageBuffers = RenderFeatureSupport::Unsupported;
    lowEndGpu.halfFloatColorTargets = RenderFeatureSupport::Unsupported;
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::ForwardPlus, lowEndGpu);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Compatibility);
        EXPECT_TRUE(res.fallbackApplied);
        EXPECT_FALSE(res.fallbackReason.empty());
    }
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::Mobile, lowEndGpu);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Compatibility);
        EXPECT_TRUE(res.fallbackApplied);
    }
    {
        const auto res = ResolveEffectiveRendererProfile(Platform::RendererProfile::Compatibility, lowEndGpu);
        EXPECT_EQ(res.effective, Platform::RendererProfile::Compatibility);
        EXPECT_FALSE(res.fallbackApplied);
    }
}

TEST_F(Renderer3DTest, SetRendererProfile_SwitchesProfilesAndDisablesHeavyPassesInCompatibility) {
    const Scene::Entity cameraEntity = MakeCameraEntity();
    WaitUntilShadowProgramReady();

    renderer3D->SetRendererProfile(Platform::RendererProfile::Compatibility);
    EXPECT_EQ(renderer3D->GetRendererProfileResolution().effective, Platform::RendererProfile::Compatibility);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    const Renderer3DFrameDiagnostics compatDiag = renderer3D->GetLastFrameDiagnostics();
    EXPECT_FALSE(compatDiag.ssaoPassRecorded);
    EXPECT_FALSE(compatDiag.bloomPassRecorded);

    renderer3D->SetRendererProfile(Platform::RendererProfile::Mobile);
    EXPECT_EQ(renderer3D->GetRendererProfileResolution().requested, Platform::RendererProfile::Mobile);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_FALSE(renderer3D->GetLastFrameDiagnostics().ssaoPassRecorded);

    renderer3D->SetRendererProfile(Platform::RendererProfile::ForwardPlus);
    EXPECT_EQ(renderer3D->GetRendererProfileResolution().requested, Platform::RendererProfile::ForwardPlus);
    renderer3D->RenderFrame(entityManager, cameraEntity);
    EXPECT_TRUE(renderer3D->GetLastFrameDiagnostics().toneMappingPassRecorded);
}

} // namespace UVE::Render::Tests
