// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "render_system.h"

#include <vector>

#include <gtest/gtest.h>

#include "platform.h"
#include "null_render_device.h"

namespace UVE::Render::Tests {
namespace {

TEST(RenderSystemTest, FrameIndex_StartsAtZero) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    EXPECT_EQ(renderSystem.GetFrameIndex(), 0U);
}

TEST(RenderSystemTest, BeginFrameThenEndFrame_AdvancesFrameIndex) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.BeginFrame();
    renderSystem.EndFrame();
    EXPECT_EQ(renderSystem.GetFrameIndex(), 1U);

    renderSystem.BeginFrame();
    renderSystem.EndFrame();
    EXPECT_EQ(renderSystem.GetFrameIndex(), 2U);
}

TEST(RenderSystemTest, GetFrameCommandBuffer_ReturnsUsableCommandBufferDuringActiveFrame) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.BeginFrame();
    ICommandBuffer& commandBuffer = renderSystem.GetFrameCommandBuffer();
    RenderPassDesc passDesc;
    commandBuffer.BeginRenderPass(passDesc);
    commandBuffer.EndRenderPass();
    renderSystem.EndFrame();

    const std::vector<RecordedCommand>& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    EXPECT_TRUE(std::holds_alternative<EndRenderPassCommand>(recorded[1]));
}

TEST(RenderSystemTest, EndFrame_SubmitsTheSameCommandBufferThatWasRecordedInto) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.BeginFrame();
    ICommandBuffer& commandBuffer = renderSystem.GetFrameCommandBuffer();
    commandBuffer.BeginRenderPass(RenderPassDesc{});
    commandBuffer.EndRenderPass();
    renderSystem.EndFrame();

    EXPECT_FALSE(device.GetLastSubmittedCommands().empty());
}

#if UVE_DEBUG
TEST(RenderSystemDeathTest, GetFrameCommandBuffer_WithoutActiveFrame_Asserts) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    EXPECT_DEATH({ static_cast<void>(renderSystem.GetFrameCommandBuffer()); }, "");
}

TEST(RenderSystemDeathTest, EndFrame_WithoutActiveFrame_Asserts) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    EXPECT_DEATH({ renderSystem.EndFrame(); }, "");
}

TEST(RenderSystemDeathTest, BeginFrame_WhileFrameAlreadyActive_Asserts) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.BeginFrame();
    EXPECT_DEATH({ renderSystem.BeginFrame(); }, "");
}
#endif

#if !UVE_DEBUG
TEST(RenderSystemTest, Release_GetFrameCommandBuffer_WithoutActiveFrameIsSafe) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    ICommandBuffer& fallback = renderSystem.GetFrameCommandBuffer();
    fallback.BeginRenderPass(RenderPassDesc{});
    fallback.EndRenderPass();

    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());
    EXPECT_EQ(renderSystem.GetFrameIndex(), 0U);
}

TEST(RenderSystemTest, Release_EndFrame_WithoutActiveFrameIsSafeNoOp) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.EndFrame();

    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());
    EXPECT_EQ(renderSystem.GetFrameIndex(), 0U);
}

TEST(RenderSystemTest, Release_BeginFrame_WhileActivePreservesCurrentFrame) {
    NullRenderDevice device;
    RenderSystem renderSystem(device);

    renderSystem.BeginFrame();
    ICommandBuffer* const firstBuffer = &renderSystem.GetFrameCommandBuffer();
    renderSystem.BeginFrame();
    ICommandBuffer* const preservedBuffer = &renderSystem.GetFrameCommandBuffer();

    EXPECT_EQ(preservedBuffer, firstBuffer);
    renderSystem.EndFrame();
    EXPECT_EQ(renderSystem.GetFrameIndex(), 1U);
}
#endif

} // namespace
} // namespace UVE::Render::Tests
