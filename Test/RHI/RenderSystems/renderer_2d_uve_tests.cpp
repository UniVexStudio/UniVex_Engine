// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/renderer_2d_uve.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/sprite_2d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/render_systems/camera_2d_system_uve.h"
#include "uve/render_systems/render_system_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr int kMaxPollIterationsUVE = 200000;
constexpr std::uint32_t kTargetWidthUVE = 800;
constexpr std::uint32_t kTargetHeightUVE = 600;

/// One recorded fake command: passes carry their descriptor (clear color!), binds carry
/// handle + slot, draws carry their vertex count.
struct FakeCommandUVE final {
    enum class Kind { BeginPass, EndPass, BindPipeline, BindVertexBuffer, BindTexture, Draw };
    Kind kind = Kind::Draw;
    RenderPassDescUVE pass{};
    TextureHandleUVE texture = kInvalidTextureHandleUVE;
    std::uint32_t slot = 0U;
    std::uint32_t vertexCount = 0U;
};

/// A recording ICommandBufferUVE: every call the sprite submit makes lands here in order,
/// so the tests assert the recording itself rather than trusting a call happened.
class FakeCommandBufferUVE final : public ICommandBufferUVE {
public:
    void BeginRenderPassUVE(const RenderPassDescUVE& renderPassDesc) override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::BeginPass;
        command.pass = renderPassDesc;
        commands.push_back(command);
    }
    void EndRenderPassUVE() override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::EndPass;
        commands.push_back(command);
    }
    void BindPipelineUVE(PipelineHandleUVE) override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::BindPipeline;
        commands.push_back(command);
    }
    void BindVertexBufferUVE(BufferHandleUVE, std::uint32_t = 0) override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::BindVertexBuffer;
        commands.push_back(command);
    }
    void BindIndexBufferUVE(BufferHandleUVE) override {}
    void BindTextureUVE(TextureHandleUVE texture, std::uint32_t slot) override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::BindTexture;
        command.texture = texture;
        command.slot = slot;
        commands.push_back(command);
    }
    void BindSamplerUVE(SamplerHandleUVE, std::uint32_t) override {}
    void BindUniformBufferUVE(BufferHandleUVE, std::uint32_t) override {}
    void BindStorageBufferUVE(BufferHandleUVE, std::uint32_t) override {}
    void SetUniformFloatUVE(std::string_view, float) override {}
    void SetUniformIntUVE(std::string_view, std::int32_t) override {}
    void SetUniformBoolUVE(std::string_view, bool) override {}
    void SetUniformVector2UVE(std::string_view, const Math::Vector2UVE&) override {}
    void SetUniformVector3UVE(std::string_view, const Math::Vector3UVE&) override {}
    void SetUniformVector4UVE(std::string_view, const Math::Vector4UVE&) override {}
    void SetUniformMatrix4x4UVE(std::string_view, const Math::Matrix4x4UVE&) override {}
    void DrawIndexedUVE(std::uint32_t, std::uint32_t = 1) override {}
    void DrawUVE(std::uint32_t vertexCount, std::uint32_t = 1) override {
        FakeCommandUVE command{};
        command.kind = FakeCommandUVE::Kind::Draw;
        command.vertexCount = vertexCount;
        commands.push_back(command);
    }
    void DrawIndexedIndirectUVE(BufferHandleUVE, std::uint64_t = 0) override {}
    void DispatchUVE(std::uint32_t, std::uint32_t, std::uint32_t) override {}

    [[nodiscard]] std::size_t CountUVE(FakeCommandUVE::Kind kind) const {
        std::size_t count = 0U;
        for (const FakeCommandUVE& command : commands) {
            count += (command.kind == kind) ? 1U : 0U;
        }
        return count;
    }

    std::vector<FakeCommandUVE> commands;
};

class FakeRenderSystemUVE final : public IRenderSystemUVE {
public:
    void BeginFrameUVE() override { ++begins; }
    [[nodiscard]] ICommandBufferUVE& GetFrameCommandBufferUVE() override { return commands; }
    void EndFrameUVE() override { ++ends; }
    [[nodiscard]] std::uint64_t GetFrameIndexUVE() const noexcept override { return ends; }

    void ResetUVE() {
        commands.commands.clear();
        begins = 0U;
        ends = 0U;
    }

    FakeCommandBufferUVE commands;
    std::uint64_t begins = 0U;
    std::uint64_t ends = 0U;
};

class Renderer2DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Threading::ThreadPoolUVE threadPool{2};
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};
    NullRenderDeviceUVE renderDevice;

    // Real ShaderManagerUVE, matching Renderer3DUVETest's fixture shape: every program in
    // these tests resolves through its embedded fallback source instead of the file system.
    Asset::AssetBundleUVE assetBundle;
    Asset::FileSystemUVE fileSystem{assetBundle};
    Shader::ShaderManagerUVE shaderManager{threadPool, eventSystem, renderDevice, fileSystem,
                                           Shader::ShaderManagerConfigUVE{}};

    Camera2DSystemUVE cameraSystem;
    FakeRenderSystemUVE renderSystem;
    std::unique_ptr<Renderer2DUVE> renderer2D;

    // Texture size per registered path, read by the loader below (registered before any load
    // starts, so the worker thread only ever reads it).
    std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> textureSizes;
    Asset::AssetGuidUVE spriteTextureGuid;

    Renderer2DUVETest() {
        textureSizes["renderer2d_sprite_64x32.uvtexture"] = {64U, 32U};
        assetManager.RegisterLoaderUVE<Asset::TextureAssetUVE>(
            [this](const std::filesystem::path& path, Asset::TextureAssetUVE& texture) {
                const auto found = textureSizes.find(path.generic_string());
                if (found == textureSizes.end()) {
                    return false;
                }
                texture.width = found->second.first;
                texture.height = found->second.second;
                texture.format = Asset::TextureAssetFormatUVE::RGBA8Unorm;
                texture.pixels.assign(static_cast<std::size_t>(texture.width) * texture.height * 4U,
                                      std::byte{0xAB});
                return true;
            });
        spriteTextureGuid = assetDatabase.RegisterUVE("renderer2d_sprite_64x32.uvtexture");

        renderer2D = std::make_unique<Renderer2DUVE>(renderDevice, renderSystem, cameraSystem, shaderManager,
                                                     assetManager, assetDatabase);
    }

    [[nodiscard]] Scene::EntityUVE MakeCameraEntityUVE(Math::Vector3UVE position,
                                                       Scene::Camera2DComponentUVE camera) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::Camera2DComponentUVE>(entity, camera);
        return entity;
    }

    Scene::EntityUVE MakeSpriteEntityUVE(Math::Vector3UVE worldPosition,
                                                       Scene::Sprite2DComponentUVE sprite) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = worldPosition;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::Sprite2DComponentUVE>(entity, sprite);
        return entity;
    }

    void PollUntilTextureReadyUVE(Asset::AssetGuidUVE guid) {
        Asset::AssetHandleUVE<Asset::TextureAssetUVE> handle =
            assetManager.LoadUVE<Asset::TextureAssetUVE>(guid, assetDatabase);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            if (handle.IsReadyUVE()) {
                break;
            }
        }
        ASSERT_TRUE(handle.IsReadyUVE());
    }

    /// Renders one throwaway frame so the sprite program request exists, then drains the
    /// ShaderManagerUVE jobs exactly like Renderer3DUVETest's PrimeMaterialProgramUVE. The
    /// test's subsequent explicit RenderFrameToTargetUVE call observes the ready program, and
    /// the fake is reset so the measured frame's recording stands alone.
    void PrimeSpriteProgramUVE(Renderer2DUVE& renderer, Scene::EntityUVE cameraEntity) {
        const auto [color, depth] = MakeTargetsUVE();
        renderer.RenderFrameToTargetUVE(entityManager, cameraEntity, color, depth, kTargetWidthUVE,
                                        kTargetHeightUVE);
        for (int iteration = 0; iteration < kMaxPollIterationsUVE; ++iteration) {
            shaderManager.UpdateUVE(0.0);
            if (shaderManager.GetPendingJobCountUVE() == 0U) {
                return;
            }
            std::this_thread::yield();
        }
        ADD_FAILURE() << "Timed out waiting for managed sprite program compilation";
    }

    void PrimeSpriteProgramUVE(Scene::EntityUVE cameraEntity) {
        PrimeSpriteProgramUVE(*renderer2D, cameraEntity);
        renderSystem.ResetUVE();
    }

    [[nodiscard]] std::pair<TextureHandleUVE, TextureHandleUVE> MakeTargetsUVE() {
        const TextureHandleUVE color = renderDevice.CreateTextureUVE(
            TextureDescUVE{kTargetWidthUVE, kTargetHeightUVE, TextureFormatUVE::RGBA8Unorm, 1});
        const TextureHandleUVE depth = renderDevice.CreateTextureUVE(
            TextureDescUVE{kTargetWidthUVE, kTargetHeightUVE, TextureFormatUVE::Depth32Float, 1});
        return {color, depth};
    }
};

TEST_F(Renderer2DUVETest, WholeTextureSprite_DrawsSixVertsAtTextureSize) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    const Scene::EntityUVE spriteEntity = MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);
    PrimeSpriteProgramUVE(camera);
    renderer2D->SetSceneClearColorUVE({0.1F, 0.2F, 0.3F, 1.0F});

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.renderTargetWidth, kTargetWidthUVE);
    EXPECT_EQ(diagnostics.renderTargetHeight, kTargetHeightUVE);
    EXPECT_EQ(diagnostics.spritesConsidered, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 1U);
    EXPECT_TRUE(diagnostics.cameraValid);
    EXPECT_TRUE(diagnostics.mainPassRecorded);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 1U);
    const Sprite2DDrawUVE& draw = renderer2D->SpritesUVE()[0];
    EXPECT_EQ(draw.entity, spriteEntity);
    EXPECT_FLOAT_EQ(draw.sortDepth, 0.0F);
    EXPECT_NE(draw.texture, kInvalidTextureHandleUVE);
    // Counter-clockwise (TL,BR,TR) + (TL,BL,BR), y flipped into the y-up world.
    const float expectedX[6] = {-32.0F, 32.0F, 32.0F, -32.0F, -32.0F, 32.0F};
    const float expectedY[6] = {16.0F, -16.0F, 16.0F, 16.0F, -16.0F, -16.0F};
    const float expectedU[6] = {0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F};
    const float expectedV[6] = {0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F};
    for (std::size_t vertex = 0U; vertex < 6U; ++vertex) {
        EXPECT_FLOAT_EQ(draw.vertices[vertex].position.x, expectedX[vertex]) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].position.y, expectedY[vertex]) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].position.z, 0.0F) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].texCoord.x, expectedU[vertex]) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].texCoord.y, expectedV[vertex]) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].color.x, 1.0F) << vertex;
        EXPECT_FLOAT_EQ(draw.vertices[vertex].color.w, 1.0F) << vertex;
    }

    // The recording itself: one pass with the authored clear color, one pipeline + vertex
    // bind, the draw's own texture on slot 0, one 6-vertex draw.
    EXPECT_EQ(renderSystem.begins, 1U);
    EXPECT_EQ(renderSystem.ends, 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BeginPass), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::EndPass), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BindPipeline), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BindVertexBuffer), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BindTexture), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::Draw), 1U);
    const FakeCommandUVE& pass = renderSystem.commands.commands[0];
    EXPECT_FLOAT_EQ(pass.pass.clearColor[0], 0.1F);
    EXPECT_FLOAT_EQ(pass.pass.clearColor[2], 0.3F);
    EXPECT_EQ(pass.pass.colorAttachment, color);
    const FakeCommandUVE* bind = nullptr;
    const FakeCommandUVE* drawn = nullptr;
    for (const FakeCommandUVE& command : renderSystem.commands.commands) {
        if (command.kind == FakeCommandUVE::Kind::BindTexture) {
            bind = &command;
        }
        if (command.kind == FakeCommandUVE::Kind::Draw) {
            drawn = &command;
        }
    }
    ASSERT_NE(bind, nullptr);
    ASSERT_NE(drawn, nullptr);
    EXPECT_EQ(bind->texture, draw.texture);
    EXPECT_EQ(bind->slot, kSprite2DTextureSlotUVE);
    EXPECT_EQ(drawn->vertexCount, 6U);
}

TEST_F(Renderer2DUVETest, Sprites_SubmitInAscendingPainterDepth) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    const Scene::EntityUVE near = MakeSpriteEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 5.0F}, sprite);
    const Scene::EntityUVE far = MakeSpriteEntityUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, sprite);
    PrimeSpriteProgramUVE(camera);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 2U);
    EXPECT_EQ(renderer2D->SpritesUVE()[0].entity, far);
    EXPECT_EQ(renderer2D->SpritesUVE()[1].entity, near);
    EXPECT_EQ(renderer2D->GetLastFrameDiagnosticsUVE().spriteDrawsRecorded, 2U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::Draw), 2U);
}

TEST_F(Renderer2DUVETest, OffscreenSprite_IsCulledAndCounted) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{10000.0F, 0.0F, 0.0F}, sprite);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.spritesConsidered, 1U);
    EXPECT_EQ(diagnostics.spritesCulled, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
    EXPECT_TRUE(diagnostics.mainPassRecorded);
    EXPECT_FALSE(diagnostics.viewCullingFrozen);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::Draw), 0U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BeginPass), 1U);
}

TEST_F(Renderer2DUVETest, CullingFreeze_DrawsOffscreenSprites) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{10000.0F, 0.0F, 0.0F}, sprite);
    renderer2D->SetCullingSettingsUVE(Renderer2DCullingSettingsUVE{false});
    PrimeSpriteProgramUVE(camera);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.spritesCulled, 0U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 1U);
    EXPECT_TRUE(diagnostics.viewCullingFrozen);
}

TEST_F(Renderer2DUVETest, InvalidCamera_ClearsWithoutSprites) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    Scene::Camera2DComponentUVE invalid{};
    invalid.zoom = Math::Vector2UVE{0.0F, 1.0F};
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, invalid);
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_FALSE(diagnostics.cameraValid);
    EXPECT_EQ(diagnostics.spritesConsidered, 0U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
    EXPECT_TRUE(diagnostics.mainPassRecorded);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::BeginPass), 1U);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::Draw), 0U);
}

TEST_F(Renderer2DUVETest, ZeroSizeTarget_RecordsNothing) {
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, 0U, kTargetHeightUVE);

    EXPECT_FALSE(renderer2D->GetLastFrameDiagnosticsUVE().mainPassRecorded);
    EXPECT_EQ(renderSystem.begins, 0U);
    EXPECT_TRUE(renderSystem.commands.commands.empty());
}

TEST_F(Renderer2DUVETest, SpriteWithoutTransform_IsSkippedAndCounted) {
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::Sprite2DComponentUVE>(entity, Scene::Sprite2DComponentUVE{});

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.spritesConsidered, 1U);
    EXPECT_EQ(diagnostics.spritesWithoutTransforms, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
}

TEST_F(Renderer2DUVETest, HiddenSprite_IsSkippedAndCounted) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    const Scene::EntityUVE entity = MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);
    Scene::VisibilityComponentUVE hidden{};
    hidden.visible = false;
    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(entity, hidden);
    sceneGraph.UpdateUVE(entityManager);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.hiddenSprites, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
}

TEST_F(Renderer2DUVETest, UnresolvableTexture_IsSkippedAndCounted) {
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = assetDatabase.RegisterUVE("renderer2d_sprite_missing.uvtexture");
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    // The loader fails this path, so the load never resolves: pending or failed, the sprite
    // waits rather than drawing wrong.
    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.unresolvedTextures, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
}

TEST_F(Renderer2DUVETest, EscapingRegion_IsInvalidAndCounted) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    sprite.sourceRect.position = Math::Vector2UVE{64.0F, 0.0F};
    sprite.sourceRect.size = Math::Vector2UVE{32.0F, 32.0F};
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = renderer2D->GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.invalidSprites, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 0U);
}

TEST_F(Renderer2DUVETest, UntexturedRegionlessSprite_DrawsTheDefaultFlatQuad) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    const Scene::EntityUVE flat = MakeSpriteEntityUVE(Math::Vector3UVE{}, Scene::Sprite2DComponentUVE{});
    Scene::Sprite2DComponentUVE textured{};
    textured.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{200.0F, 0.0F, 0.0F}, textured);
    PrimeSpriteProgramUVE(camera);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 2U);
    const Sprite2DDrawUVE* flatDraw = nullptr;
    const Sprite2DDrawUVE* texturedDraw = nullptr;
    for (const Sprite2DDrawUVE& draw : renderer2D->SpritesUVE()) {
        (draw.entity == flat ? flatDraw : texturedDraw) = &draw;
    }
    ASSERT_NE(flatDraw, nullptr);
    ASSERT_NE(texturedDraw, nullptr);
    EXPECT_FLOAT_EQ(flatDraw->vertices[1].position.x, 32.0F);
    EXPECT_FLOAT_EQ(flatDraw->vertices[1].position.y, -32.0F);
    // The flat quad samples the fallback, not the uploaded texture.
    EXPECT_NE(flatDraw->texture, texturedDraw->texture);
    EXPECT_EQ(renderer2D->GetLastFrameDiagnosticsUVE().spriteDrawsRecorded, 2U);
}

TEST_F(Renderer2DUVETest, UntexturedRegion_SizesFromItsOwnRegion) {
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.sourceRect.position = Math::Vector2UVE{8.0F, 8.0F};
    sprite.sourceRect.size = Math::Vector2UVE{16.0F, 16.0F};
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);
    PrimeSpriteProgramUVE(camera);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 1U);
    EXPECT_FLOAT_EQ(renderer2D->SpritesUVE()[0].vertices[1].position.x, 8.0F);
    EXPECT_FLOAT_EQ(renderer2D->SpritesUVE()[0].vertices[1].position.y, -8.0F);
    EXPECT_EQ(renderer2D->GetLastFrameDiagnosticsUVE().spriteDrawsRecorded, 1U);
}

TEST_F(Renderer2DUVETest, SharedTexture_UploadsOnceAndBindsOneHandle) {
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);
    MakeSpriteEntityUVE(Math::Vector3UVE{200.0F, 0.0F, 0.0F}, sprite);
    PrimeSpriteProgramUVE(camera);

    const auto [color, depth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 2U);
    EXPECT_EQ(renderer2D->SpritesUVE()[0].texture, renderer2D->SpritesUVE()[1].texture);
    EXPECT_EQ(renderSystem.commands.CountUVE(FakeCommandUVE::Kind::Draw), 2U);
    std::size_t binds = 0U;
    for (const FakeCommandUVE& command : renderSystem.commands.commands) {
        if (command.kind == FakeCommandUVE::Kind::BindTexture) {
            EXPECT_EQ(command.texture, renderer2D->SpritesUVE()[0].texture);
            ++binds;
        }
    }
    EXPECT_EQ(binds, 2U);
}

TEST_F(Renderer2DUVETest, TwoContexts_RenderTheirOwnCamerasIntoTheirOwnTargets) {
    // One renderer per viewport backend, sharing device/shaders/assets: the main editor and
    // the entity editor each render their own camera into their own targets through the same
    // entry point, and each sees only its own view.
    PollUntilTextureReadyUVE(spriteTextureGuid);
    Scene::Camera2DComponentUVE mainCamera{};
    const Scene::EntityUVE main = MakeCameraEntityUVE(Math::Vector3UVE{}, mainCamera);
    Scene::Camera2DComponentUVE sideCamera{};
    sideCamera.offset = Math::Vector2UVE{500.0F, 0.0F};
    const Scene::EntityUVE side = MakeCameraEntityUVE(Math::Vector3UVE{}, sideCamera);
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    const Scene::EntityUVE left = MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);
    const Scene::EntityUVE right = MakeSpriteEntityUVE(Math::Vector3UVE{500.0F, 0.0F, 0.0F}, sprite);

    FakeRenderSystemUVE sideSystem;
    Renderer2DUVE sideRenderer(renderDevice, sideSystem, cameraSystem, shaderManager, assetManager, assetDatabase);
    const auto [mainColor, mainDepth] = MakeTargetsUVE();
    const auto [sideColor, sideDepth] = MakeTargetsUVE();
    renderer2D->RenderFrameToTargetUVE(entityManager, main, mainColor, mainDepth, kTargetWidthUVE,
                                       kTargetHeightUVE);
    sideRenderer.RenderFrameToTargetUVE(entityManager, side, sideColor, sideDepth, kTargetWidthUVE,
                                        kTargetHeightUVE);

    ASSERT_EQ(renderer2D->SpritesUVE().size(), 1U);
    EXPECT_EQ(renderer2D->SpritesUVE()[0].entity, left);
    ASSERT_EQ(sideRenderer.SpritesUVE().size(), 1U);
    EXPECT_EQ(sideRenderer.SpritesUVE()[0].entity, right);
    EXPECT_EQ(renderSystem.commands.commands[0].pass.colorAttachment, mainColor);
    EXPECT_EQ(sideSystem.commands.commands[0].pass.colorAttachment, sideColor);
}

TEST_F(Renderer2DUVETest, RealNullFrame_RunsTheFullLifecycleWithoutRecordingFakes) {
    // The same frame through the real RenderSystemUVE + Null device: no assertions on the
    // recording (the Null spy is not test-reachable), but the full lifecycle — program,
    // buffers, textures, begin/end — runs for real.
    RenderSystemUVE realSystem{renderDevice};
    Renderer2DUVE realRenderer(renderDevice, realSystem, cameraSystem, shaderManager, assetManager, assetDatabase);
    PollUntilTextureReadyUVE(spriteTextureGuid);
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Scene::Camera2DComponentUVE{});
    Scene::Sprite2DComponentUVE sprite{};
    sprite.textureAssetGuid = spriteTextureGuid;
    MakeSpriteEntityUVE(Math::Vector3UVE{}, sprite);

    const auto [color, depth] = MakeTargetsUVE();
    PrimeSpriteProgramUVE(realRenderer, camera);
    realRenderer.RenderFrameToTargetUVE(entityManager, camera, color, depth, kTargetWidthUVE, kTargetHeightUVE);

    const Renderer2DFrameDiagnosticsUVE diagnostics = realRenderer.GetLastFrameDiagnosticsUVE();
    EXPECT_EQ(diagnostics.spritesConsidered, 1U);
    EXPECT_EQ(diagnostics.spriteDrawsRecorded, 1U);
    EXPECT_TRUE(diagnostics.mainPassRecorded);
    // Prime frame plus measured frame both ran the real lifecycle.
    EXPECT_EQ(realSystem.GetFrameIndexUVE(), 2U);
}

} // namespace
} // namespace UVE::Render::Tests
