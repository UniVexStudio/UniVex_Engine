// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "viewport_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_bundle.h"
#include "asset_database.h"
#include "asset_manager.h"
#include "file_system.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "shader_asset.h"
#include "event_system.h"
#include "memory_manager.h"
#include "camera_system.h"
#include "light_system.h"
#include "mesh_renderer.h"
#include "null_render_device.h"
#include "recorded_command.h"
#include "render_system.h"
#include "renderer_3d.h"
#include "shader_manager.h"
#include "camera_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

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
constexpr int kMaxPollIterations = 200000;

// Pure pane-management/hit-testing tests: no renderer/ECS involvement needed.
class ViewportManagerPaneTest : public ::testing::Test {
protected:
    ViewportManager manager;
};

TEST_F(ViewportManagerPaneTest, StartsEmpty) {
    EXPECT_EQ(manager.GetPaneCount(), 0U);
    EXPECT_EQ(manager.GetPane(0), nullptr);
}

TEST_F(ViewportManagerPaneTest, AddPane_AppendsAndIsRetrievable) {
    ViewportPane pane;
    pane.originX01 = 0.25F;
    pane.originY01 = 0.5F;
    pane.sizeX01 = 0.5F;
    pane.sizeY01 = 0.5F;
    manager.AddPane(pane);

    ASSERT_EQ(manager.GetPaneCount(), 1U);
    const ViewportPane* stored = manager.GetPane(0);
    ASSERT_NE(stored, nullptr);
    EXPECT_FLOAT_EQ(stored->originX01, 0.25F);
    EXPECT_FLOAT_EQ(stored->originY01, 0.5F);
    EXPECT_FLOAT_EQ(stored->sizeX01, 0.5F);
    EXPECT_FLOAT_EQ(stored->sizeY01, 0.5F);
}

TEST_F(ViewportManagerPaneTest, GetPane_OutOfRangeReturnsNullptr) {
    manager.AddPane(ViewportPane{});
    EXPECT_EQ(manager.GetPane(1), nullptr);
    EXPECT_EQ(manager.GetPane(1000), nullptr);
}

TEST_F(ViewportManagerPaneTest, RemovePane_RemovesTheCorrectPane) {
    ViewportPane first;
    first.originX01 = 0.0F;
    ViewportPane second;
    second.originX01 = 0.5F;
    manager.AddPane(first);
    manager.AddPane(second);

    manager.RemovePane(0);

    ASSERT_EQ(manager.GetPaneCount(), 1U);
    EXPECT_FLOAT_EQ(manager.GetPane(0)->originX01, 0.5F);
}

TEST_F(ViewportManagerPaneTest, RemovePane_OutOfRangeIsANoOp) {
    manager.AddPane(ViewportPane{});
    manager.RemovePane(5);
    EXPECT_EQ(manager.GetPaneCount(), 1U);
}

TEST_F(ViewportManagerPaneTest, FindPaneAtNormalizedPosition_NoPanesReturnsNullopt) {
    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(0.5F, 0.5F), std::nullopt);
}

TEST_F(ViewportManagerPaneTest, FindPaneAtNormalizedPosition_HitsContainingPane) {
    ViewportPane left;
    left.originX01 = 0.0F;
    left.sizeX01 = 0.5F;
    left.sizeY01 = 1.0F;
    ViewportPane right;
    right.originX01 = 0.5F;
    right.sizeX01 = 0.5F;
    right.sizeY01 = 1.0F;
    manager.AddPane(left);
    manager.AddPane(right);

    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(0.1F, 0.5F), 0U);
    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(0.9F, 0.5F), 1U);
}

TEST_F(ViewportManagerPaneTest, FindPaneAtNormalizedPosition_MissReturnsNullopt) {
    ViewportPane pane;
    pane.originX01 = 0.0F;
    pane.sizeX01 = 0.25F;
    pane.sizeY01 = 0.25F;
    manager.AddPane(pane);

    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(0.9F, 0.9F), std::nullopt);
}

TEST_F(ViewportManagerPaneTest, FindPaneAtNormalizedPosition_OverlapPrefersMostRecentlyAdded) {
    ViewportPane bottom;
    bottom.originX01 = 0.0F;
    bottom.originY01 = 0.0F;
    bottom.sizeX01 = 1.0F;
    bottom.sizeY01 = 1.0F;
    ViewportPane top;
    top.originX01 = 0.25F;
    top.originY01 = 0.25F;
    top.sizeX01 = 0.5F;
    top.sizeY01 = 0.5F;
    manager.AddPane(bottom);
    manager.AddPane(top);

    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(0.5F, 0.5F), 1U);
}

TEST_F(ViewportManagerPaneTest, FindPaneAtNormalizedPosition_NonFiniteInputReturnsNullopt) {
    manager.AddPane(ViewportPane{});
    EXPECT_EQ(manager.FindPaneAtNormalizedPosition(std::numeric_limits<float>::quiet_NaN(), 0.5F), std::nullopt);
}

// Integration tests: RenderAllPanes composing with a real Renderer3D + NullRenderDevice,
// following the exact fixture shape tests/render/renderer_3d_uve_tests.cpp already establishes.
class ViewportManagerRenderTest : public ::testing::Test {
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

    std::unique_ptr<Renderer3D> renderer3D;
    ViewportManager viewportManager;

    ViewportManagerRenderTest() {
        renderer3D = std::make_unique<Renderer3D>(
            renderDevice, renderSystem, meshRenderer, cameraSystem, lightSystem, shaderManager, assetManager,
            assetDatabase, eventSystem, kTargetWidth, kTargetHeight, kTestAmbientColor,
            kTestShadowMapResolution, kTestShadowMapHalfExtent, kTestShadowMapNearPlane,
            kTestShadowMapFarPlane, kTestShadowFrustumPadding, kTestShadowCascadeSplitLambda,
            kTestShadowCascadeBlendRatio, kTestShadowPcfKernelRadius);
    }

    Scene::Entity MakeCameraEntity() {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::CameraComponent>(entity);
        return entity;
    }

    // The built-in ToneMapping program compiles asynchronously (ShaderManager) - the
    // constructor only requests it. Matching renderer_3d_uve_tests.cpp's own established pattern:
    // a first RenderFrame call kicks off the request, then draining ShaderManager's job
    // queue makes it ready for every subsequent frame (including the ones RenderAllPanes
    // drives), so the ToneMapping pass actually gets recorded instead of silently short-circuiting
    // on !toneMappingProgram->IsValid().
    void PrimeToneMappingProgram() {
        renderer3D->RenderFrame(entityManager, MakeCameraEntity());
        for (int iteration = 0; iteration < kMaxPollIterations; ++iteration) {
            shaderManager.Update(0.0);
            if (shaderManager.GetPendingJobCount() == 0U) {
                return;
            }
            std::this_thread::yield();
        }
        ADD_FAILURE() << "Timed out waiting for tone mapping shader compilation";
    }

    // Finds the ToneMapping pass among the last submitted commands: it is the only
    // BeginRenderPassCommand whose desc targets the default framebuffer (both attachments
    // invalid), matching the signature the production code uses at
    // engine/render/src/renderer_3d_uve.cpp's ToneMapping pass lambda.
    [[nodiscard]] static std::optional<RenderPassDesc>
    FindToneMappingPassDesc(const std::vector<RecordedCommand>& commands) {
        for (const RecordedCommand& command : commands) {
            if (const auto* beginPass = std::get_if<BeginRenderPassCommand>(&command)) {
                if (beginPass->desc.colorAttachment == kInvalidTextureHandle &&
                    beginPass->desc.depthAttachment == kInvalidTextureHandle) {
                    return beginPass->desc;
                }
            }
        }
        return std::nullopt;
    }
};

TEST_F(ViewportManagerRenderTest, RenderAllPanes_ZeroWindowSizeIsANoOp) {
    ViewportPane pane;
    pane.cameraEntity = MakeCameraEntity();
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 0U, 128U);
    EXPECT_TRUE(renderDevice.GetLastSubmittedCommands().empty());
}

TEST_F(ViewportManagerRenderTest, RenderAllPanes_SkipsUiOnlyPane) {
    ViewportPane pane;
    pane.cameraEntity = MakeCameraEntity();
    pane.uiOnly = true;
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 128U, 128U);
    EXPECT_TRUE(renderDevice.GetLastSubmittedCommands().empty());
}

TEST_F(ViewportManagerRenderTest, RenderAllPanes_SkipsPaneWithInvalidRect) {
    ViewportPane pane;
    pane.cameraEntity = MakeCameraEntity();
    pane.originX01 = 0.9F;
    pane.sizeX01 = 0.5F; // origin + size > 1.0 -> invalid.
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 128U, 128U);
    EXPECT_TRUE(renderDevice.GetLastSubmittedCommands().empty());
}

TEST_F(ViewportManagerRenderTest, RenderAllPanes_SkipsPaneWithInvalidCameraEntity) {
    ViewportPane pane;
    pane.cameraEntity = Scene::kInvalidEntity;
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 128U, 128U);
    EXPECT_TRUE(renderDevice.GetLastSubmittedCommands().empty());
}

TEST_F(ViewportManagerRenderTest, RenderAllPanes_SkipsPaneWithNonCameraEntity) {
    ViewportPane pane;
    pane.cameraEntity = entityManager.CreateEntity(); // No CameraComponent attached.
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 128U, 128U);
    EXPECT_TRUE(renderDevice.GetLastSubmittedCommands().empty());
}

TEST_F(ViewportManagerRenderTest, RenderAllPanes_ValidPaneRendersWithCorrectPixelRegion) {
    PrimeToneMappingProgram();

    ViewportPane pane;
    pane.cameraEntity = MakeCameraEntity();
    pane.originX01 = 0.5F;
    pane.originY01 = 0.0F;
    pane.sizeX01 = 0.5F;
    pane.sizeY01 = 0.5F;
    viewportManager.AddPane(pane);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 200U, 100U);

    const std::optional<RenderPassDesc> toneMappingDesc =
        FindToneMappingPassDesc(renderDevice.GetLastSubmittedCommands());
    ASSERT_TRUE(toneMappingDesc.has_value());
    ASSERT_TRUE(toneMappingDesc->viewportOverride.has_value());
    // originX01=0.5, sizeX01=0.5 over a 200-wide window -> pixelX=100, width=100.
    EXPECT_EQ(toneMappingDesc->viewportOverride->position.x, 100U);
    EXPECT_EQ(toneMappingDesc->viewportOverride->size.x, 100U);
    // originY01=0.0, sizeY01=0.5 over a 100-tall window, top-left origin, flipped to GL's
    // bottom-left convention: pixelYFromTop=0, height=50 -> pixelYFromBottom=100-0-50=50.
    EXPECT_EQ(toneMappingDesc->viewportOverride->position.y, 50U);
    EXPECT_EQ(toneMappingDesc->viewportOverride->size.y, 50U);
}

// NullRenderDevice::GetLastSubmittedCommands() reflects only the most recently submitted
// frame (each RenderFrameToRegion call is its own Begin/EndFrame), so a multi-pane call
// can't be asserted by counting passes across the whole call. Instead this confirms
// RenderAllPanes actually reaches and renders the second (last) pane with its own distinct
// region, rather than stopping after the first - the single-pane test above already covers the
// per-pane pixel-rect math itself.
TEST_F(ViewportManagerRenderTest, RenderAllPanes_ProcessesEveryValidPaneInOrder) {
    PrimeToneMappingProgram();

    ViewportPane first;
    first.cameraEntity = MakeCameraEntity();
    first.sizeX01 = 0.5F;
    ViewportPane second;
    second.cameraEntity = MakeCameraEntity();
    second.originX01 = 0.5F;
    second.sizeX01 = 0.5F;
    viewportManager.AddPane(first);
    viewportManager.AddPane(second);

    viewportManager.RenderAllPanes(*renderer3D, entityManager, 128U, 128U);

    const std::optional<RenderPassDesc> toneMappingDesc =
        FindToneMappingPassDesc(renderDevice.GetLastSubmittedCommands());
    ASSERT_TRUE(toneMappingDesc.has_value());
    ASSERT_TRUE(toneMappingDesc->viewportOverride.has_value());
    EXPECT_EQ(toneMappingDesc->viewportOverride->position.x, 64U);
    EXPECT_EQ(toneMappingDesc->viewportOverride->size.x, 64U);
}

} // namespace
} // namespace UVE::Render::Tests
