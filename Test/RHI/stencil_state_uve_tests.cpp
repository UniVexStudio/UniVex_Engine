// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/rhi_null/null_render_device_uve.h"

#include <array>
#include <memory>
#include <variant>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

PipelineDescUVE MakePipelineUVE(NullRenderDeviceUVE &device) {
    PipelineDescUVE desc;
    desc.vertexShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, "vs"});
    desc.fragmentShader = device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, "fs"});
    desc.stencil.enabled = true;
    desc.stencil.front.compare = DepthCompareUVE::Equal;
    desc.stencil.front.depthPass = StencilOpUVE::Replace;
    desc.stencil.front.reference = 0x2AU;
    desc.stencil.back.compare = DepthCompareUVE::Less;
    desc.stencil.back.stencilFail = StencilOpUVE::IncrementWrap;
    desc.stencil.back.writeMask = 0x7FU;
    return desc;
}

TEST(StencilStateUVETest, DefaultsPreserveLegacyDisabledBehavior) {
    const PipelineDescUVE desc;
    EXPECT_FALSE(desc.stencil.enabled);
    EXPECT_TRUE(IsStencilStateValidUVE(desc.stencil));
    EXPECT_EQ(desc.stencil.front.compare, DepthCompareUVE::Always);
    EXPECT_EQ(desc.stencil.front.compareMask, 0xFFU);
    EXPECT_EQ(desc.stencil.back.writeMask, 0xFFU);

    const PipelineBinaryDescUVE binaryDesc;
    EXPECT_FALSE(binaryDesc.stencil.enabled);
    EXPECT_TRUE(IsDepthFormatUVE(TextureFormatUVE::Depth24Stencil8));
    EXPECT_TRUE(IsDepthStencilFormatUVE(TextureFormatUVE::Depth24Stencil8));
    EXPECT_FALSE(IsDepthStencilFormatUVE(TextureFormatUVE::Depth32Float));
}

TEST(StencilStateUVETest, ValidationRejectsUnknownOperationsAndOutOfRangeMasks) {
    StencilStateUVE state;
    state.enabled = true;
    EXPECT_TRUE(IsStencilStateValidUVE(state));

    state.front.stencilFail = static_cast<StencilOpUVE>(0xFFU);
    EXPECT_FALSE(IsStencilStateValidUVE(state));
    state.front.stencilFail = StencilOpUVE::Keep;
    state.back.compare = static_cast<DepthCompareUVE>(0xFFU);
    EXPECT_FALSE(IsStencilStateValidUVE(state));
    state.back.compare = DepthCompareUVE::Always;
    state.front.compareMask = 0x100U;
    EXPECT_FALSE(IsStencilStateValidUVE(state));
    state.front.compareMask = 0xFFU;
    state.back.reference = 0x100U;
    EXPECT_FALSE(IsStencilStateValidUVE(state));
}

TEST(StencilStateUVETest, NullPipelineRejectsInvalidStencilBeforePublication) {
    NullRenderDeviceUVE device;
    PipelineDescUVE desc = MakePipelineUVE(device);
    desc.stencil.back.depthFail = static_cast<StencilOpUVE>(0xFFU);

    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 2U);

    desc.stencil.back.depthFail = StencilOpUVE::DecrementClamp;
    EXPECT_NE(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 3U);
}

TEST(StencilStateUVETest, NullBinaryPipelineRejectsInvalidStencilBeforePublication) {
    NullRenderDeviceUVE device;
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE desc;
    desc.stencil.enabled = true;
    desc.stencil.front.writeMask = 0x100U;

    EXPECT_EQ(device.CreatePipelineFromBinaryUVE(binary, 0U, desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);

    desc.stencil.front.writeMask = 0xFFU;
    EXPECT_NE(device.CreatePipelineFromBinaryUVE(binary, 0U, desc), kInvalidPipelineHandleUVE);
}

TEST(StencilStateUVETest, NullRecordsDepthStencilAttachmentClearAndPipeline) {
    NullRenderDeviceUVE device;
    const PipelineHandleUVE pipeline = device.CreatePipelineUVE(MakePipelineUVE(device));
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE colorDesc;
    colorDesc.width = 8U;
    colorDesc.height = 8U;
    const TextureHandleUVE color = device.CreateTextureUVE(colorDesc);
    TextureDescUVE depthDesc = colorDesc;
    depthDesc.format = TextureFormatUVE::Depth24Stencil8;
    const TextureHandleUVE depth = device.CreateTextureUVE(depthDesc);
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    ASSERT_NE(depth, kInvalidTextureHandleUVE);

    TextureDescUVE observed;
    ASSERT_EQ(device.GetTextureDescUVE(depth, observed), RenderOperationResultUVE::Success);
    EXPECT_EQ(observed.format, TextureFormatUVE::Depth24Stencil8);

    auto commands = device.CreateCommandBufferUVE();
    RenderPassDescUVE pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depth;
    pass.clearStencil = 0x2AU;
    commands->BeginRenderPassUVE(pass);
    commands->BindPipelineUVE(pipeline);
    commands->DrawUVE(3U);
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));

    const auto &recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 4U);
    const auto &begin = std::get<BeginRenderPassCommandUVE>(recorded[0]);
    EXPECT_EQ(begin.desc.depthAttachment, depth);
    EXPECT_EQ(begin.desc.clearStencil, 0x2AU);
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(recorded[1]).pipeline, pipeline);
}

} // namespace
} // namespace UVE::Render::Tests
