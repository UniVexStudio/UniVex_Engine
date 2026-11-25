// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "null_render_device.h"

#include <array>
#include <string>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

PipelineDesc MakeGeometryPipeline(NullRenderDevice& device) {
    PipelineDesc desc;
    desc.vertexShader = device.CreateShader({ShaderStage::Vertex, "vertex"});
    desc.fragmentShader = device.CreateShader({ShaderStage::Fragment, "fragment"});
    desc.geometryShader = device.CreateShader({ShaderStage::Geometry, "geometry"});
    return desc;
}

TEST(GeometryPipelineTest, OptionalStageDefaultsToLegacyPairAndReportsOnlyModelSupport) {
    NullRenderDevice device;
    EXPECT_EQ(RenderDeviceCapabilities{}.geometryShaders, RenderFeatureSupport::Unsupported);
    EXPECT_EQ(device.GetCapabilities().geometryShaders, RenderFeatureSupport::Model);
    EXPECT_EQ(PipelineDesc{}.geometryShader, kInvalidShaderHandle);
    PipelineDesc desc;
    desc.vertexShader = device.CreateShader({ShaderStage::Vertex, "vertex"});
    desc.fragmentShader = device.CreateShader({ShaderStage::Fragment, "fragment"});
    EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 3U);
}

TEST(GeometryPipelineTest, OptionalStageCreatesARecordedGraphicsDraw) {
    NullRenderDevice device;
    const PipelineDesc desc = MakeGeometryPipeline(device);
    const auto pipeline = device.CreatePipeline(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 4U);
    auto commands = device.CreateCommandBuffer();
    commands->BeginRenderPass({});
    commands->BindPipeline(pipeline);
    commands->Draw(3U);
    commands->EndRenderPass();
    device.Submit(std::move(commands));
    const auto& recorded = device.GetLastSubmittedCommands();
    ASSERT_EQ(recorded.size(), 4U);
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[1]).pipeline, pipeline);
    EXPECT_EQ(std::get<DrawCommand>(recorded[2]).vertexCount, 3U);
    device.DestroyPipeline(pipeline);
    device.DestroyShader(desc.geometryShader);
    device.DestroyShader(desc.fragmentShader);
    device.DestroyShader(desc.vertexShader);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

TEST(GeometryPipelineTest, UnknownAndDestroyedGeometryAreRejectedBeforePublication) {
    NullRenderDevice device;
    PipelineDesc desc = MakeGeometryPipeline(device);
    const auto geometry = desc.geometryShader;
    const std::size_t live = device.GetLiveResourceCount();
    desc.geometryShader = ShaderHandle{999999U};
    std::string info;
    EXPECT_EQ(device.CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("geometry"), std::string::npos);
    EXPECT_EQ(device.GetLiveResourceCount(), live);
    desc.geometryShader = geometry;
    device.DestroyShader(geometry);
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), live - 1U);
    desc.geometryShader = kInvalidShaderHandle;
    EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
}

TEST(GeometryPipelineTest, WrongStageInGeometrySlotIsRejectedForEveryOtherStage) {
    NullRenderDevice device;
    PipelineDesc desc = MakeGeometryPipeline(device);
    const auto geometry = desc.geometryShader;
    for (const auto stage : std::array{ShaderStage::Vertex, ShaderStage::Fragment, ShaderStage::Compute}) {
        const auto wrong = device.CreateShader({stage, "wrong stage"});
        desc.geometryShader = wrong;
        const auto before = device.GetLiveResourceCount();
        std::string info;
        EXPECT_EQ(device.CreatePipeline(desc, &info), kInvalidPipelineHandle);
        EXPECT_NE(info.find("Geometry-stage"), std::string::npos);
        EXPECT_EQ(device.GetLiveResourceCount(), before);
        device.DestroyShader(wrong);
    }
    desc.geometryShader = geometry;
    EXPECT_NE(device.CreatePipeline(desc), kInvalidPipelineHandle);
}

TEST(GeometryPipelineTest, GeometryCannotReplaceTheRequiredVertexOrFragmentStage) {
    NullRenderDevice device;
    const PipelineDesc valid = MakeGeometryPipeline(device);
    auto desc = valid;
    desc.vertexShader = valid.geometryShader;
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    desc = valid;
    desc.fragmentShader = valid.geometryShader;
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 3U);
    desc = valid;
    std::swap(desc.vertexShader, desc.fragmentShader);
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    EXPECT_EQ(device.GetLiveResourceCount(), 3U);
}

TEST(GeometryPipelineTest, PipelineSurvivesShaderDeletionButNewPipelineRejectsStaleHandle) {
    NullRenderDevice device;
    const PipelineDesc desc = MakeGeometryPipeline(device);
    const auto pipeline = device.CreatePipeline(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    device.DestroyShader(desc.geometryShader);
    device.DestroyShader(desc.fragmentShader);
    device.DestroyShader(desc.vertexShader);
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    EXPECT_EQ(device.CreatePipeline(desc), kInvalidPipelineHandle);
    auto commands = device.CreateCommandBuffer();
    commands->BeginRenderPass({});
    commands->BindPipeline(pipeline);
    commands->Draw(3U);
    commands->EndRenderPass();
    device.Submit(std::move(commands));
    ASSERT_EQ(device.GetLastSubmittedCommands().size(), 4U);
    device.DestroyPipeline(pipeline);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}

} // namespace
} // namespace UVE::Render::Tests
