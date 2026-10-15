// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/rhi_null/null_render_device_uve.h"

#include <array>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

PipelineDescUVE MakeGeometryPipelineUVE(NullRenderDeviceUVE& device) {
    PipelineDescUVE desc;
    desc.vertexShader = device.CreateShaderUVE({ShaderStageUVE::Vertex, "vertex"});
    desc.fragmentShader = device.CreateShaderUVE({ShaderStageUVE::Fragment, "fragment"});
    desc.geometryShader = device.CreateShaderUVE({ShaderStageUVE::Geometry, "geometry"});
    return desc;
}

TEST(GeometryPipelineUVETest, OptionalStageDefaultsToLegacyPairAndReportsOnlyModelSupport) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(RenderDeviceCapabilitiesUVE{}.geometryShaders, RenderFeatureSupportUVE::Unsupported);
    EXPECT_EQ(device.GetCapabilitiesUVE().geometryShaders, RenderFeatureSupportUVE::Model);
    EXPECT_EQ(PipelineDescUVE{}.geometryShader, kInvalidShaderHandleUVE);
    PipelineDescUVE desc;
    desc.vertexShader = device.CreateShaderUVE({ShaderStageUVE::Vertex, "vertex"});
    desc.fragmentShader = device.CreateShaderUVE({ShaderStageUVE::Fragment, "fragment"});
    EXPECT_NE(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 3U);
}

TEST(GeometryPipelineUVETest, OptionalStageCreatesARecordedGraphicsDraw) {
    NullRenderDeviceUVE device;
    const PipelineDescUVE desc = MakeGeometryPipelineUVE(device);
    const auto pipeline = device.CreatePipelineUVE(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 4U);
    auto commands = device.CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    commands->BindPipelineUVE(pipeline);
    commands->DrawUVE(3U);
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));
    const auto& recorded = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(recorded.size(), 4U);
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(recorded[1]).pipeline, pipeline);
    EXPECT_EQ(std::get<DrawCommandUVE>(recorded[2]).vertexCount, 3U);
    device.DestroyPipelineUVE(pipeline);
    device.DestroyShaderUVE(desc.geometryShader);
    device.DestroyShaderUVE(desc.fragmentShader);
    device.DestroyShaderUVE(desc.vertexShader);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

TEST(GeometryPipelineUVETest, UnknownAndDestroyedGeometryAreRejectedBeforePublication) {
    NullRenderDeviceUVE device;
    PipelineDescUVE desc = MakeGeometryPipelineUVE(device);
    const auto geometry = desc.geometryShader;
    const std::size_t live = device.GetLiveResourceCountUVE();
    desc.geometryShader = ShaderHandleUVE{999999U};
    std::string info;
    EXPECT_EQ(device.CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
    EXPECT_NE(info.find("geometry"), std::string::npos);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), live);
    desc.geometryShader = geometry;
    device.DestroyShaderUVE(geometry);
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), live - 1U);
    desc.geometryShader = kInvalidShaderHandleUVE;
    EXPECT_NE(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
}

TEST(GeometryPipelineUVETest, WrongStageInGeometrySlotIsRejectedForEveryOtherStage) {
    NullRenderDeviceUVE device;
    PipelineDescUVE desc = MakeGeometryPipelineUVE(device);
    const auto geometry = desc.geometryShader;
    for (const auto stage : std::array{ShaderStageUVE::Vertex, ShaderStageUVE::Fragment, ShaderStageUVE::Compute}) {
        const auto wrong = device.CreateShaderUVE({stage, "wrong stage"});
        desc.geometryShader = wrong;
        const auto before = device.GetLiveResourceCountUVE();
        std::string info;
        EXPECT_EQ(device.CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
        EXPECT_NE(info.find("Geometry-stage"), std::string::npos);
        EXPECT_EQ(device.GetLiveResourceCountUVE(), before);
        device.DestroyShaderUVE(wrong);
    }
    desc.geometryShader = geometry;
    EXPECT_NE(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
}

TEST(GeometryPipelineUVETest, GeometryCannotReplaceTheRequiredVertexOrFragmentStage) {
    NullRenderDeviceUVE device;
    const PipelineDescUVE valid = MakeGeometryPipelineUVE(device);
    auto desc = valid;
    desc.vertexShader = valid.geometryShader;
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    desc = valid;
    desc.fragmentShader = valid.geometryShader;
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 3U);
    desc = valid;
    std::swap(desc.vertexShader, desc.fragmentShader);
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 3U);
}

TEST(GeometryPipelineUVETest, PipelineSurvivesShaderDeletionButNewPipelineRejectsStaleHandle) {
    NullRenderDeviceUVE device;
    const PipelineDescUVE desc = MakeGeometryPipelineUVE(device);
    const auto pipeline = device.CreatePipelineUVE(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    device.DestroyShaderUVE(desc.geometryShader);
    device.DestroyShaderUVE(desc.fragmentShader);
    device.DestroyShaderUVE(desc.vertexShader);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    EXPECT_EQ(device.CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
    auto commands = device.CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    commands->BindPipelineUVE(pipeline);
    commands->DrawUVE(3U);
    commands->EndRenderPassUVE();
    device.SubmitUVE(std::move(commands));
    ASSERT_EQ(device.GetLastSubmittedCommandsUVE().size(), 4U);
    device.DestroyPipelineUVE(pipeline);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}

} // namespace
} // namespace UVE::Render::Tests
