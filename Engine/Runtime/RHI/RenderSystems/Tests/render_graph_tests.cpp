// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "render_graph.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "i_command_buffer.h"
#include "null_render_device.h"

namespace UVE::Render::Tests {
namespace {

TEST(RenderGraphTest, Execute_ImportedResourcePassesRunInStableInsertionOrder) {
    NullRenderDevice renderDevice;
    const TextureHandle texture = renderDevice.CreateTexture(TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1});
    ASSERT_NE(texture, kInvalidTextureHandle);

    RenderGraph graph;
    const RenderGraphResourceHandle resource = graph.ImportTexture(texture, "TestColor");
    std::vector<std::string> executionOrder;
    graph.AddPass(RenderGraphPassDesc{"Write", {{resource, RenderGraphResourceAccess::Write}},
                                            [&executionOrder](ICommandBuffer&) { executionOrder.push_back("Write"); }});
    graph.AddPass(RenderGraphPassDesc{"Read", {{resource, RenderGraphResourceAccess::Read}},
                                            [&executionOrder](ICommandBuffer&) { executionOrder.push_back("Read"); }});

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice.CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    EXPECT_TRUE(graph.Execute(*commandBuffer));
    EXPECT_EQ(executionOrder, (std::vector<std::string>{"Write", "Read"}));
    EXPECT_EQ(graph.GetPassCount(), 2U);
    renderDevice.DestroyTexture(texture);
}

TEST(RenderGraphTest, Execute_UnknownResourceRejectsWithoutRecordingPasses) {
    NullRenderDevice renderDevice;
    RenderGraph graph;
    bool recorded = false;
    graph.AddPass(RenderGraphPassDesc{"Invalid", {{RenderGraphResourceHandle{0U}, RenderGraphResourceAccess::Read}},
                                            [&recorded](ICommandBuffer&) { recorded = true; }});

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice.CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    EXPECT_FALSE(graph.Execute(*commandBuffer));
    EXPECT_FALSE(recorded);
}

TEST(RenderGraphTest, Clear_RebuildsSmallerGraphWithoutExecutingStaleSlots) {
    NullRenderDevice renderDevice;
    const TextureHandle firstTexture =
        renderDevice.CreateTexture(TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1});
    const TextureHandle secondTexture =
        renderDevice.CreateTexture(TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1});
    ASSERT_NE(firstTexture, kInvalidTextureHandle);
    ASSERT_NE(secondTexture, kInvalidTextureHandle);

    RenderGraph graph;
    graph.Reserve(2U, 2U);
    const RenderGraphResourceHandle firstResource = graph.ImportTexture(firstTexture, "First");
    const RenderGraphResourceHandle secondResource = graph.ImportTexture(secondTexture, "Second");
    std::vector<std::string> executionOrder;
    graph.AddPass(RenderGraphPassDesc{"FirstPass", {{firstResource, RenderGraphResourceAccess::Write}},
                                            [&executionOrder](ICommandBuffer&) {
                                                executionOrder.push_back("FirstPass");
                                            }});
    graph.AddPass(RenderGraphPassDesc{"SecondPass", {{secondResource, RenderGraphResourceAccess::Write}},
                                            [&executionOrder](ICommandBuffer&) {
                                                executionOrder.push_back("SecondPass");
                                            }});

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice.CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    ASSERT_TRUE(graph.Execute(*commandBuffer));
    ASSERT_EQ(graph.GetPassCount(), 2U);

    graph.Clear();
    EXPECT_EQ(graph.GetPassCount(), 0U);
    executionOrder.clear();

    graph.AddPass(RenderGraphPassDesc{"StalePass", {{firstResource, RenderGraphResourceAccess::Read}},
                                            [&executionOrder](ICommandBuffer&) {
                                                executionOrder.push_back("StalePass");
                                            }});
    EXPECT_FALSE(graph.Execute(*commandBuffer));
    EXPECT_TRUE(executionOrder.empty());
    graph.Clear();

    const RenderGraphResourceHandle rebuiltResource = graph.ImportTexture(firstTexture, "Rebuilt");
    graph.AddPass(RenderGraphPassDesc{"RebuiltPass", {{rebuiltResource, RenderGraphResourceAccess::Read}},
                                            [&executionOrder](ICommandBuffer&) {
                                                executionOrder.push_back("RebuiltPass");
                                            }});

    ASSERT_TRUE(graph.Execute(*commandBuffer));
    EXPECT_EQ(executionOrder, (std::vector<std::string>{"RebuiltPass"}));
    EXPECT_EQ(graph.GetPassCount(), 1U);
    renderDevice.DestroyTexture(firstTexture);
    renderDevice.DestroyTexture(secondTexture);
}

} // namespace
} // namespace UVE::Render::Tests
