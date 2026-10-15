// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/debug_renderer_uve.h"
#include "uve/rhi/recorded_command_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi_shader/shader_manager_config_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

constexpr Math::Vector3UVE kWhiteUVE{1.0F, 1.0F, 1.0F};

void ExpectVertexUVE(const DebugLineVertexUVE& vertex, float x, float y, float z) {
    EXPECT_FLOAT_EQ(vertex.position.x, x);
    EXPECT_FLOAT_EQ(vertex.position.y, y);
    EXPECT_FLOAT_EQ(vertex.position.z, z);
    EXPECT_FLOAT_EQ(vertex.color.x, kWhiteUVE.x);
    EXPECT_FLOAT_EQ(vertex.color.y, kWhiteUVE.y);
    EXPECT_FLOAT_EQ(vertex.color.z, kWhiteUVE.z);
}

class DebugRendererUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        assetBundle = std::make_unique<Asset::AssetBundleUVE>();
        fileSystem = std::make_unique<Asset::FileSystemUVE>(*assetBundle);
        threadPool = std::make_unique<Threading::ThreadPoolUVE>(1);
        eventSystem = std::make_unique<Events::EventSystemUVE>();
        renderDevice = std::make_unique<NullRenderDeviceUVE>();
        Shader::ShaderManagerConfigUVE config;
        config.cachePath = tempCacheDirectory;
        config.compileSynchronouslyUVE = true;
        shaderManager = std::make_unique<Shader::ShaderManagerUVE>(*threadPool, *eventSystem,
                                                                   *renderDevice, *fileSystem, config);
    }

    void TearDown() override {
        // Renderer before manager before device: the line program's deleter destroys its
        // pipeline on the device.
        renderer.reset();
        shaderManager.reset();
        std::filesystem::remove_all(tempCacheDirectory);
    }

    const std::filesystem::path tempCacheDirectory = "uve_debug_renderer_tests_cache";
    std::unique_ptr<Asset::AssetBundleUVE> assetBundle;
    std::unique_ptr<Asset::FileSystemUVE> fileSystem;
    std::unique_ptr<Threading::ThreadPoolUVE> threadPool;
    std::unique_ptr<Events::EventSystemUVE> eventSystem;
    std::unique_ptr<NullRenderDeviceUVE> renderDevice;
    std::unique_ptr<Shader::ShaderManagerUVE> shaderManager;
    std::unique_ptr<DebugRendererUVE> renderer = std::make_unique<DebugRendererUVE>();
};

} // namespace

TEST(DebugRendererShapeUVETest, DrawBox3D_UnitBox_Produces12EdgesInDocumentedOrder) {
    DebugRendererUVE renderer;
    const Math::AabbUVE box{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    renderer.DrawBox3D(box, kWhiteUVE);

    const std::span<const DebugLineVertexUVE> lines = renderer.LinesUVE();
    ASSERT_EQ(lines.size(), 24U);
    // Bottom ring first edge, top ring first edge, first vertical.
    ExpectVertexUVE(lines[0], 0.0F, 0.0F, 0.0F);
    ExpectVertexUVE(lines[1], 1.0F, 0.0F, 0.0F);
    ExpectVertexUVE(lines[8], 0.0F, 1.0F, 0.0F);
    ExpectVertexUVE(lines[9], 1.0F, 1.0F, 0.0F);
    ExpectVertexUVE(lines[16], 0.0F, 0.0F, 0.0F);
    ExpectVertexUVE(lines[17], 0.0F, 1.0F, 0.0F);
    EXPECT_EQ(renderer.DroppedLineCountUVE(), 0U);
}

TEST(DebugRendererShapeUVETest, DrawRect2D_Produces4EdgesAtZeroZ) {
    DebugRendererUVE renderer;
    renderer.DrawRect2D(Math::Vector2UVE{1.0F, 2.0F}, Math::Vector2UVE{5.0F, 6.0F}, kWhiteUVE);

    const std::span<const DebugLineVertexUVE> lines = renderer.LinesUVE();
    ASSERT_EQ(lines.size(), 8U);
    for (const DebugLineVertexUVE& vertex : lines) {
        EXPECT_FLOAT_EQ(vertex.position.z, 0.0F);
    }
    ExpectVertexUVE(lines[0], 1.0F, 2.0F, 0.0F);
    ExpectVertexUVE(lines[1], 5.0F, 2.0F, 0.0F);
    ExpectVertexUVE(lines[7], 1.0F, 2.0F, 0.0F);
}

TEST(DebugRendererShapeUVETest, DrawSphere3D_ProducesThreeCirclesAtRadius) {
    DebugRendererUVE renderer;
    const Math::Vector3UVE center{1.0F, 2.0F, 3.0F};
    renderer.DrawSphere3D(center, 2.0F, kWhiteUVE);

    const std::span<const DebugLineVertexUVE> lines = renderer.LinesUVE();
    ASSERT_EQ(lines.size(), 144U);
    for (const DebugLineVertexUVE& vertex : lines) {
        const float dx = vertex.position.x - center.x;
        const float dy = vertex.position.y - center.y;
        const float dz = vertex.position.z - center.z;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 2.0F, 1.0e-4F);
    }
    // First vertex of each great circle: XY and XZ start at +X, YZ starts at +Y.
    ExpectVertexUVE(lines[0], 3.0F, 2.0F, 3.0F);
    ExpectVertexUVE(lines[48], 3.0F, 2.0F, 3.0F);
    ExpectVertexUVE(lines[96], 1.0F, 4.0F, 3.0F);
}

TEST(DebugRendererShapeUVETest, OverCap_DropsWholeLinesAndCountsThem) {
    DebugRendererUVE renderer;
    const Math::Vector3UVE from{0.0F, 0.0F, 0.0F};
    const Math::Vector3UVE to{1.0F, 0.0F, 0.0F};
    for (std::size_t i = 0U; i < kMaximumDebugLineVerticesUVE / 2U + 5U; ++i) {
        renderer.DrawLine3D(from, to, kWhiteUVE);
    }
    EXPECT_EQ(renderer.LinesUVE().size(), kMaximumDebugLineVerticesUVE);
    EXPECT_EQ(renderer.DroppedLineCountUVE(), 5U);
    renderer.ClearUVE();
    EXPECT_TRUE(renderer.LinesUVE().empty());
    EXPECT_EQ(renderer.DroppedLineCountUVE(), 0U);
}

TEST(DebugRendererShapeUVETest, NonFiniteInput_IsIgnored) {
    DebugRendererUVE renderer;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    renderer.DrawLine3D(Math::Vector3UVE{nan, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, kWhiteUVE);
    renderer.DrawLine3D(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                        Math::Vector3UVE{inf, 0.0F, 0.0F});
    renderer.DrawBox3D(Math::AabbUVE{Math::Vector3UVE{nan, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}},
                       kWhiteUVE);
    renderer.DrawSphere3D(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 0.0F, kWhiteUVE);
    renderer.DrawSphere3D(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, -2.0F, kWhiteUVE);
    renderer.DrawRect2D(Math::Vector2UVE{0.0F, nan}, Math::Vector2UVE{1.0F, 1.0F}, kWhiteUVE);
    EXPECT_TRUE(renderer.LinesUVE().empty());
    EXPECT_EQ(renderer.DroppedLineCountUVE(), 0U);
}

TEST(DebugRendererShapeUVETest, DrawPoint3D_BatchesSeparatelyWithCapAndClampedSize) {
    DebugRendererUVE renderer;
    EXPECT_FLOAT_EQ(renderer.PointSizeUVE(), kDefaultDebugPointSizeUVE);
    renderer.SetPointSizeUVE(9.0F);
    EXPECT_FLOAT_EQ(renderer.PointSizeUVE(), 9.0F);
    renderer.SetPointSizeUVE(0.0F);
    EXPECT_FLOAT_EQ(renderer.PointSizeUVE(), kMinimumDebugPointSizeUVE) << "clamped, not refused";
    renderer.SetPointSizeUVE(1000.0F);
    EXPECT_FLOAT_EQ(renderer.PointSizeUVE(), kMaximumDebugPointSizeUVE);
    renderer.SetPointSizeUVE(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(renderer.PointSizeUVE(), kMaximumDebugPointSizeUVE) << "non-finite sizes are ignored";

    renderer.DrawPoint3D(Math::Vector3UVE{1.0F, 2.0F, 3.0F}, kWhiteUVE);
    renderer.DrawPoint3D(Math::Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, kWhiteUVE);
    renderer.DrawLine3D(Math::Vector3UVE{}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, kWhiteUVE);
    ASSERT_EQ(renderer.PointsUVE().size(), 1U) << "non-finite points are ignored; lines don't mix in";
    ExpectVertexUVE(renderer.PointsUVE()[0], 1.0F, 2.0F, 3.0F);
    EXPECT_EQ(renderer.LinesUVE().size(), 2U);
    for (std::size_t i = 1U; i < kMaximumDebugPointVerticesUVE + 5U; ++i) {
        renderer.DrawPoint3D(Math::Vector3UVE{static_cast<float>(i), 0.0F, 0.0F}, kWhiteUVE);
    }
    EXPECT_EQ(renderer.PointsUVE().size(), kMaximumDebugPointVerticesUVE);
    EXPECT_EQ(renderer.DroppedPointCountUVE(), 5U);
    EXPECT_EQ(renderer.DroppedLineCountUVE(), 0U);
    renderer.ClearUVE();
    EXPECT_TRUE(renderer.PointsUVE().empty());
    EXPECT_EQ(renderer.DroppedPointCountUVE(), 0U);
}

TEST_F(DebugRendererUVETest, Submit_PointsAndLines_RecordTwoDrawsWithPointSizeAndClearBoth) {
    renderer->DrawLine3D(Math::Vector3UVE{}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, kWhiteUVE);
    renderer->DrawPoint3D(Math::Vector3UVE{0.5F, 0.5F, 0.5F}, kWhiteUVE);
    renderer->DrawPoint3D(Math::Vector3UVE{1.5F, 0.5F, 0.5F}, kWhiteUVE);
    renderer->DrawPoint3D(Math::Vector3UVE{2.5F, 0.5F, 0.5F}, kWhiteUVE);
    renderer->SetPointSizeUVE(6.0F);
    const TextureHandleUVE colorTarget =
        renderDevice->CreateTextureUVE(TextureDescUVE{64U, 64U, TextureFormatUVE::RGBA8Unorm});
    std::unique_ptr<ICommandBufferUVE> commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commands, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = colorTarget;
    commands->BeginRenderPassUVE(passDesc);
    EXPECT_TRUE(renderer->SubmitUVE(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4UVE::IdentityUVE()));
    EXPECT_TRUE(renderer->LinesUVE().empty());
    EXPECT_TRUE(renderer->PointsUVE().empty());
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    // Two programs, two streamed vertex buffers: 2 shaders + 1 pipeline each, 2 buffers.
    std::vector<std::uint32_t> drawCounts;
    std::vector<PipelineHandleUVE> boundPipelines;
    bool sawPointSize = false;
    for (const RecordedCommandUVE& command : renderDevice->GetLastSubmittedCommandsUVE()) {
        if (const auto* const draw = std::get_if<DrawCommandUVE>(&command)) {
            drawCounts.push_back(draw->vertexCount);
        } else if (const auto* const bind = std::get_if<BindPipelineCommandUVE>(&command)) {
            boundPipelines.push_back(bind->pipeline);
        } else if (const auto* const uniform = std::get_if<SetUniformFloatCommandUVE>(&command)) {
            if (uniform->name == "uPointSize") {
                sawPointSize = true;
                EXPECT_FLOAT_EQ(uniform->value, 6.0F);
            }
        }
    }
    ASSERT_EQ(drawCounts.size(), 2U) << "one Lines draw then one Points draw";
    EXPECT_EQ(drawCounts[0], 2U);
    EXPECT_EQ(drawCounts[1], 3U);
    ASSERT_EQ(boundPipelines.size(), 2U);
    EXPECT_NE(boundPipelines[0], boundPipelines[1]) << "points draw through their own pipeline";
    EXPECT_TRUE(sawPointSize);
    // A points-only frame records exactly one draw.
    renderer->DrawPoint3D(Math::Vector3UVE{}, kWhiteUVE);
    std::unique_ptr<ICommandBufferUVE> second = renderDevice->CreateCommandBufferUVE();
    second->BeginRenderPassUVE(passDesc);
    EXPECT_TRUE(renderer->SubmitUVE(*renderDevice, *shaderManager, *second, Math::Matrix4x4UVE::IdentityUVE()));
    second->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(second));
    std::size_t draws = 0U;
    for (const RecordedCommandUVE& command : renderDevice->GetLastSubmittedCommandsUVE()) {
        draws += std::holds_alternative<DrawCommandUVE>(command) ? 1U : 0U;
    }
    EXPECT_EQ(draws, 1U);
    renderer->ShutdownUVE(*renderDevice);
    renderDevice->DestroyTextureUVE(colorTarget);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U) << "both buffers and both programs released";
}

TEST_F(DebugRendererUVETest, Submit_EmptyBatch_TouchesNoGpu) {
    RenderPassDescUVE passDesc;
    std::unique_ptr<ICommandBufferUVE> commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPassUVE(passDesc);
    EXPECT_TRUE(renderer->SubmitUVE(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4UVE::IdentityUVE()));
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    // Only the caller's own pass markers were recorded: no binds, no uniforms, no draw.
    for (const RecordedCommandUVE& command : renderDevice->GetLastSubmittedCommandsUVE()) {
        EXPECT_FALSE(std::holds_alternative<BindPipelineCommandUVE>(command));
        EXPECT_FALSE(std::holds_alternative<BindVertexBufferCommandUVE>(command));
        EXPECT_FALSE(std::holds_alternative<DrawCommandUVE>(command));
    }
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    renderer->ShutdownUVE(*renderDevice);
}

TEST_F(DebugRendererUVETest, Submit_Batch_RecordsOneLineDrawAndClears) {
    const Math::AabbUVE box{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    renderer->DrawBox3D(box, kWhiteUVE);
    renderer->DrawRect2D(Math::Vector2UVE{1.0F, 2.0F}, Math::Vector2UVE{5.0F, 6.0F}, kWhiteUVE);
    ASSERT_EQ(renderer->LinesUVE().size(), 32U);

    const TextureHandleUVE colorTarget =
        renderDevice->CreateTextureUVE(TextureDescUVE{64U, 64U, TextureFormatUVE::RGBA8Unorm});
    std::unique_ptr<ICommandBufferUVE> commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commands, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = colorTarget;
    commands->BeginRenderPassUVE(passDesc);
    EXPECT_TRUE(renderer->SubmitUVE(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4UVE::IdentityUVE()));
    EXPECT_TRUE(renderer->LinesUVE().empty());
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));

    const std::vector<RecordedCommandUVE>& recorded = renderDevice->GetLastSubmittedCommandsUVE();
    bool sawPipeline = false;
    bool sawVertexBuffer = false;
    const DrawCommandUVE* draw = nullptr;
    for (const RecordedCommandUVE& command : recorded) {
        sawPipeline = sawPipeline || std::holds_alternative<BindPipelineCommandUVE>(command);
        sawVertexBuffer = sawVertexBuffer || std::holds_alternative<BindVertexBufferCommandUVE>(command);
        if (std::holds_alternative<DrawCommandUVE>(command)) {
            draw = &std::get<DrawCommandUVE>(command);
        }
    }
    EXPECT_TRUE(sawPipeline);
    EXPECT_TRUE(sawVertexBuffer);
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->vertexCount, 32U);

    renderer->ShutdownUVE(*renderDevice);
    renderer->ShutdownUVE(*renderDevice);
    renderDevice->DestroyTextureUVE(colorTarget);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

} // namespace UVE::Render::Tests
