// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "null_render_device.h"

#include <array>
#include <memory>
#include <variant>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

PipelineDesc MakePipeline(NullRenderDevice &device) {
    PipelineDesc desc;
    desc.vertexShader = device.CreateShader(ShaderDesc{ShaderStage::Vertex, "vs"});
    desc.fragmentShader = device.CreateShader(ShaderDesc{ShaderStage::Fragment, "fs"});
    desc.stencil.enabled = true;
    desc.stencil.front.compare = DepthCompare::Equal;
    desc.stencil.front.depthPass = StencilOp::Replace;
    desc.stencil.front.reference = 0x2AU;
    desc.stencil.back.compare = DepthCompare::Less;
    desc.stencil.back.stencilFail = StencilOp::IncrementWrap;
    desc.stencil.back.writeMask = 0x7FU;
    return desc;
}

TEST(StencilStateTest, DefaultsPreserveLegacyDisabledBehavior) {
    const PipelineDesc desc;
    EXPECT_FALSE(desc.stencil.enabled);
    EXPECT_TRUE(IsStencilStateValid(desc.stencil));
    EXPECT_EQ(desc.stencil.front.compare, DepthCompare::Always);
    EXPECT_EQ(desc.stencil.front.compareMask, 0xFFU);
    EXPECT_EQ(desc.stencil.back.writeMask, 0xFFU);

    const PipelineBinaryDesc binaryDesc;
    EXPECT_FALSE(binaryDesc.stencil.enabled);
    EXPECT_TRUE(IsDepthFormat(TextureFormat::Depth24Stencil8));
    EXPECT_TRUE(IsDepthStencilFormat(TextureFormat::Depth24Stencil8));
    EXPECT_FALSE(IsDepthStencilFormat(TextureFormat::Depth32Float));
}

TEST(StencilStateTest, ValidationRejectsUnknownOperationsAndOutOfRangeMasks) {
    StencilState state;
    state.enabled = true;
    EXPECT_TRUE(IsStencilStateValid(state));

    state.front.stencilFail = static_cast<StencilOp>(0xFFU);
    EXPECT_FALSE(IsStencilStateValid(state));
    state.front.stencilFail = StencilOp::Keep;
    state.back.compare = static_cast<DepthCompare>(0xFFU);
    EXPECT_FALSE(IsStencilStateValid(state));
    state.back.compare = DepthCompare::Always;
    state.front.compareMask = 0x100U;
    EXPECT_FALSE(IsStencilStateValid(state));
    state.front.compareMask = 0xFFU;
    state.back.reference = 0x100U;
    EXPECT_FALSE(IsStencilStateValid(state));
}

TEST(StencilStateTest, NullPipelineRejectsInvalidStencilBeforePublication) {
    NullRenderDevice device;
    PipelineDesc desc = MakePipeline(device);
    desc.stencil.back.depthFail = static_cast<StencilOp>(0xFFU);

    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 2U);

    desc.stencil.back.depthFail = StencilOp::DecrementClamp;
    EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 3U);
}

TEST(StencilStateTest, NullBinaryPipelineRejectsInvalidStencilBeforePublication) {
    NullRenderDevice device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc desc;
    desc.stencil.enabled = true;
    desc.stencil.front.writeMask = 0x100U;

    EXPECT_EQ(device.CreatePipelineFromBinary(binary, 0U, desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);

    desc.stencil.front.writeMask = 0xFFU;
    EXPECT_NE(device.CreatePipelineFromBinary(binary, 0U, desc), kInvalidPipelineHandle);
}

TEST(StencilStateTest, NullRecordsDepthStencilAttachmentClearAndPipeline) {
    NullRenderDevice device;
    const PipelineHandle pipeline = device.CreatePipeline(MakePipeline(device));
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    TextureDesc colorDesc;
    colorDesc.width = 8U;
    colorDesc.height = 8U;
    const TextureHandle color = device.CreateTexture(colorDesc);
    TextureDesc depthDesc = colorDesc;
    depthDesc.format = TextureFormat::Depth24Stencil8;
    const TextureHandle depth = device.CreateTexture(depthDesc);
    ASSERT_NE(color, kInvalidTextureHandle);
    ASSERT_NE(depth, kInvalidTextureHandle);

    TextureDesc observed;
    ASSERT_EQ(device.GetTextureDesc(depth, observed), RenderOperationResult::Success);
    EXPECT_EQ(observed.format, TextureFormat::Depth24Stencil8);

    auto commands = device.CreateCommandBuffer();
    RenderPassDesc pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depth;
    pass.clearStencil = 0x2AU;
    commands->BeginRenderPass(pass);
    commands->BindPipeline(pipeline);
    commands->Draw(3U);
    commands->EndRenderPass();
    device.Submit(std::move(commands));

    const auto &recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 4U);
    const auto &begin = std::get<BeginRenderPassCommand>(recorded[0]);
    EXPECT_EQ(begin.desc.depthAttachment, depth);
    EXPECT_EQ(begin.desc.clearStencil, 0x2AU);
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[1]).pipeline, pipeline);
}

} // namespace
} // namespace UVE::Render::Tests
