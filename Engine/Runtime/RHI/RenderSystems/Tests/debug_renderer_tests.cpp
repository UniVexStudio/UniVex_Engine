// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "asset_bundle.h"
#include "file_system.h"
#include "event_system.h"
#include "aabb.h"
#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "debug_renderer.h"
#include "recorded_command.h"
#include "render_resource_descs.h"
#include "shader_manager_config.h"
#include "shader_manager.h"
#include "null_render_device.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

constexpr Math::Vector3 kWhite{1.0F, 1.0F, 1.0F};

void ExpectVertex(const DebugLineVertex& vertex, float x, float y, float z) {
    EXPECT_FLOAT_EQ(vertex.position.x, x);
    EXPECT_FLOAT_EQ(vertex.position.y, y);
    EXPECT_FLOAT_EQ(vertex.position.z, z);
    EXPECT_FLOAT_EQ(vertex.color.x, kWhite.x);
    EXPECT_FLOAT_EQ(vertex.color.y, kWhite.y);
    EXPECT_FLOAT_EQ(vertex.color.z, kWhite.z);
}

class DebugRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        assetBundle = std::make_unique<Asset::AssetBundle>();
        fileSystem = std::make_unique<Asset::FileSystem>(*assetBundle);
        threadPool = std::make_unique<Threading::ThreadPool>(1);
        eventSystem = std::make_unique<Events::EventSystem>();
        renderDevice = std::make_unique<NullRenderDevice>();
        Shader::ShaderManagerConfig config;
        config.cachePath = tempCacheDirectory;
        config.compileSynchronously = true;
        shaderManager = std::make_unique<Shader::ShaderManager>(*threadPool, *eventSystem,
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
    std::unique_ptr<Asset::AssetBundle> assetBundle;
    std::unique_ptr<Asset::FileSystem> fileSystem;
    std::unique_ptr<Threading::ThreadPool> threadPool;
    std::unique_ptr<Events::EventSystem> eventSystem;
    std::unique_ptr<NullRenderDevice> renderDevice;
    std::unique_ptr<Shader::ShaderManager> shaderManager;
    std::unique_ptr<DebugRenderer> renderer = std::make_unique<DebugRenderer>();
};

} // namespace

TEST(DebugRendererShapeTest, DrawBox3D_UnitBox_Produces12EdgesInDocumentedOrder) {
    DebugRenderer renderer;
    const Math::Aabb box{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}};
    renderer.DrawBox3D(box, kWhite);

    const std::span<const DebugLineVertex> lines = renderer.Lines();
    ASSERT_EQ(lines.size(), 24U);
    // Bottom ring first edge, top ring first edge, first vertical.
    ExpectVertex(lines[0], 0.0F, 0.0F, 0.0F);
    ExpectVertex(lines[1], 1.0F, 0.0F, 0.0F);
    ExpectVertex(lines[8], 0.0F, 1.0F, 0.0F);
    ExpectVertex(lines[9], 1.0F, 1.0F, 0.0F);
    ExpectVertex(lines[16], 0.0F, 0.0F, 0.0F);
    ExpectVertex(lines[17], 0.0F, 1.0F, 0.0F);
    EXPECT_EQ(renderer.DroppedLineCount(), 0U);
}

TEST(DebugRendererShapeTest, DrawRect2D_Produces4EdgesAtZeroZ) {
    DebugRenderer renderer;
    renderer.DrawRect2D(Math::Vector2{1.0F, 2.0F}, Math::Vector2{5.0F, 6.0F}, kWhite);

    const std::span<const DebugLineVertex> lines = renderer.Lines();
    ASSERT_EQ(lines.size(), 8U);
    for (const DebugLineVertex& vertex : lines) {
        EXPECT_FLOAT_EQ(vertex.position.z, 0.0F);
    }
    ExpectVertex(lines[0], 1.0F, 2.0F, 0.0F);
    ExpectVertex(lines[1], 5.0F, 2.0F, 0.0F);
    ExpectVertex(lines[7], 1.0F, 2.0F, 0.0F);
}

TEST(DebugRendererShapeTest, DrawSphere3D_ProducesThreeCirclesAtRadius) {
    DebugRenderer renderer;
    const Math::Vector3 center{1.0F, 2.0F, 3.0F};
    renderer.DrawSphere3D(center, 2.0F, kWhite);

    const std::span<const DebugLineVertex> lines = renderer.Lines();
    ASSERT_EQ(lines.size(), 144U);
    for (const DebugLineVertex& vertex : lines) {
        const float dx = vertex.position.x - center.x;
        const float dy = vertex.position.y - center.y;
        const float dz = vertex.position.z - center.z;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 2.0F, 1.0e-4F);
    }
    // First vertex of each great circle: XY and XZ start at +X, YZ starts at +Y.
    ExpectVertex(lines[0], 3.0F, 2.0F, 3.0F);
    ExpectVertex(lines[48], 3.0F, 2.0F, 3.0F);
    ExpectVertex(lines[96], 1.0F, 4.0F, 3.0F);
}

TEST(DebugRendererShapeTest, OverCap_DropsWholeLinesAndCountsThem) {
    DebugRenderer renderer;
    const Math::Vector3 from{0.0F, 0.0F, 0.0F};
    const Math::Vector3 to{1.0F, 0.0F, 0.0F};
    for (std::size_t i = 0U; i < kMaximumDebugLineVertices / 2U + 5U; ++i) {
        renderer.DrawLine3D(from, to, kWhite);
    }
    EXPECT_EQ(renderer.Lines().size(), kMaximumDebugLineVertices);
    EXPECT_EQ(renderer.DroppedLineCount(), 5U);
    renderer.Clear();
    EXPECT_TRUE(renderer.Lines().empty());
    EXPECT_EQ(renderer.DroppedLineCount(), 0U);
}

TEST(DebugRendererShapeTest, NonFiniteInput_IsIgnored) {
    DebugRenderer renderer;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    renderer.DrawLine3D(Math::Vector3{nan, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F}, kWhite);
    renderer.DrawLine3D(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 0.0F, 0.0F},
                        Math::Vector3{inf, 0.0F, 0.0F});
    renderer.DrawBox3D(Math::Aabb{Math::Vector3{nan, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}},
                       kWhite);
    renderer.DrawSphere3D(Math::Vector3{0.0F, 0.0F, 0.0F}, 0.0F, kWhite);
    renderer.DrawSphere3D(Math::Vector3{0.0F, 0.0F, 0.0F}, -2.0F, kWhite);
    renderer.DrawRect2D(Math::Vector2{0.0F, nan}, Math::Vector2{1.0F, 1.0F}, kWhite);
    EXPECT_TRUE(renderer.Lines().empty());
    EXPECT_EQ(renderer.DroppedLineCount(), 0U);
}

TEST(DebugRendererShapeTest, DrawPoint3D_BatchesSeparatelyWithCapAndClampedSize) {
    DebugRenderer renderer;
    EXPECT_FLOAT_EQ(renderer.PointSize(), kDefaultDebugPointSize);
    renderer.SetPointSize(9.0F);
    EXPECT_FLOAT_EQ(renderer.PointSize(), 9.0F);
    renderer.SetPointSize(0.0F);
    EXPECT_FLOAT_EQ(renderer.PointSize(), kMinimumDebugPointSize) << "clamped, not refused";
    renderer.SetPointSize(1000.0F);
    EXPECT_FLOAT_EQ(renderer.PointSize(), kMaximumDebugPointSize);
    renderer.SetPointSize(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(renderer.PointSize(), kMaximumDebugPointSize) << "non-finite sizes are ignored";

    renderer.DrawPoint3D(Math::Vector3{1.0F, 2.0F, 3.0F}, kWhite);
    renderer.DrawPoint3D(Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, kWhite);
    renderer.DrawLine3D(Math::Vector3{}, Math::Vector3{1.0F, 0.0F, 0.0F}, kWhite);
    ASSERT_EQ(renderer.Points().size(), 1U) << "non-finite points are ignored; lines don't mix in";
    ExpectVertex(renderer.Points()[0], 1.0F, 2.0F, 3.0F);
    EXPECT_EQ(renderer.Lines().size(), 2U);
    for (std::size_t i = 1U; i < kMaximumDebugPointVertices + 5U; ++i) {
        renderer.DrawPoint3D(Math::Vector3{static_cast<float>(i), 0.0F, 0.0F}, kWhite);
    }
    EXPECT_EQ(renderer.Points().size(), kMaximumDebugPointVertices);
    EXPECT_EQ(renderer.DroppedPointCount(), 5U);
    EXPECT_EQ(renderer.DroppedLineCount(), 0U);
    renderer.Clear();
    EXPECT_TRUE(renderer.Points().empty());
    EXPECT_EQ(renderer.DroppedPointCount(), 0U);
}

TEST_F(DebugRendererTest, Submit_PointsAndLines_RecordTwoDrawsWithPointSizeAndClearBoth) {
    renderer->DrawLine3D(Math::Vector3{}, Math::Vector3{1.0F, 0.0F, 0.0F}, kWhite);
    renderer->DrawPoint3D(Math::Vector3{0.5F, 0.5F, 0.5F}, kWhite);
    renderer->DrawPoint3D(Math::Vector3{1.5F, 0.5F, 0.5F}, kWhite);
    renderer->DrawPoint3D(Math::Vector3{2.5F, 0.5F, 0.5F}, kWhite);
    renderer->SetPointSize(6.0F);
    const TextureHandle colorTarget =
        renderDevice->CreateTexture(TextureDesc{64U, 64U, TextureFormat::RGBA8Unorm});
    std::unique_ptr<ICommandBuffer> commands = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = colorTarget;
    commands->BeginRenderPass(passDesc);
    EXPECT_TRUE(renderer->Submit(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4::Identity()));
    EXPECT_TRUE(renderer->Lines().empty());
    EXPECT_TRUE(renderer->Points().empty());
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    // Two programs, two streamed vertex buffers: 2 shaders + 1 pipeline each, 2 buffers.
    std::vector<std::uint32_t> drawCounts;
    std::vector<PipelineHandle> boundPipelines;
    bool sawPointSize = false;
    for (const RecordedCommand& command : renderDevice->GetLastSubmittedCommands()) {
        if (const auto* const draw = std::get_if<DrawCommand>(&command)) {
            drawCounts.push_back(draw->vertexCount);
        } else if (const auto* const bind = std::get_if<BindPipelineCommand>(&command)) {
            boundPipelines.push_back(bind->pipeline);
        } else if (const auto* const uniform = std::get_if<SetUniformFloatCommand>(&command)) {
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
    renderer->DrawPoint3D(Math::Vector3{}, kWhite);
    std::unique_ptr<ICommandBuffer> second = renderDevice->CreateCommandBuffer();
    second->BeginRenderPass(passDesc);
    EXPECT_TRUE(renderer->Submit(*renderDevice, *shaderManager, *second, Math::Matrix4x4::Identity()));
    second->EndRenderPass();
    renderDevice->Submit(std::move(second));
    std::size_t draws = 0U;
    for (const RecordedCommand& command : renderDevice->GetLastSubmittedCommands()) {
        draws += std::holds_alternative<DrawCommand>(command) ? 1U : 0U;
    }
    EXPECT_EQ(draws, 1U);
    renderer->Shutdown(*renderDevice);
    renderDevice->DestroyTexture(colorTarget);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U) << "both buffers and both programs released";
}

TEST_F(DebugRendererTest, Submit_EmptyBatch_TouchesNoGpu) {
    RenderPassDesc passDesc;
    std::unique_ptr<ICommandBuffer> commands = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPass(passDesc);
    EXPECT_TRUE(renderer->Submit(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4::Identity()));
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    // Only the caller's own pass markers were recorded: no binds, no uniforms, no draw.
    for (const RecordedCommand& command : renderDevice->GetLastSubmittedCommands()) {
        EXPECT_FALSE(std::holds_alternative<BindPipelineCommand>(command));
        EXPECT_FALSE(std::holds_alternative<BindVertexBufferCommand>(command));
        EXPECT_FALSE(std::holds_alternative<DrawCommand>(command));
    }
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    renderer->Shutdown(*renderDevice);
}

TEST_F(DebugRendererTest, Submit_Batch_RecordsOneLineDrawAndClears) {
    const Math::Aabb box{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}};
    renderer->DrawBox3D(box, kWhite);
    renderer->DrawRect2D(Math::Vector2{1.0F, 2.0F}, Math::Vector2{5.0F, 6.0F}, kWhite);
    ASSERT_EQ(renderer->Lines().size(), 32U);

    const TextureHandle colorTarget =
        renderDevice->CreateTexture(TextureDesc{64U, 64U, TextureFormat::RGBA8Unorm});
    std::unique_ptr<ICommandBuffer> commands = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = colorTarget;
    commands->BeginRenderPass(passDesc);
    EXPECT_TRUE(renderer->Submit(*renderDevice, *shaderManager, *commands,
                                    Math::Matrix4x4::Identity()));
    EXPECT_TRUE(renderer->Lines().empty());
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));

    const std::vector<RecordedCommand>& recorded = renderDevice->GetLastSubmittedCommands();
    bool sawPipeline = false;
    bool sawVertexBuffer = false;
    const DrawCommand* draw = nullptr;
    for (const RecordedCommand& command : recorded) {
        sawPipeline = sawPipeline || std::holds_alternative<BindPipelineCommand>(command);
        sawVertexBuffer = sawVertexBuffer || std::holds_alternative<BindVertexBufferCommand>(command);
        if (std::holds_alternative<DrawCommand>(command)) {
            draw = &std::get<DrawCommand>(command);
        }
    }
    EXPECT_TRUE(sawPipeline);
    EXPECT_TRUE(sawVertexBuffer);
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->vertexCount, 32U);

    renderer->Shutdown(*renderDevice);
    renderer->Shutdown(*renderDevice);
    renderDevice->DestroyTexture(colorTarget);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

} // namespace UVE::Render::Tests
