// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/compute_system_uve.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/render_system_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

// The source text never reaches a compiler in the Null-backed tests - Null records the creation
// and hands out a handle - but it is non-empty, because empty source is an engine-level rejection
// the system must catch before the backend ever sees the desc.
[[nodiscard]] ComputeProgramDescUVE MakeTestProgramDescUVE(std::string debugName = "test-kernel") {
    ComputeProgramDescUVE desc;
    desc.sourceCode = "compute kernel source - recorded, never compiled, by the Null backend";
    desc.debugName = std::move(debugName);
    return desc;
}

class ComputeSystemUVETest : public ::testing::Test {
protected:
    NullRenderDeviceUVE device;
    ComputeSystemUVE computeSystem{device};
};

TEST_F(ComputeSystemUVETest, CreateProgramUVE_ValidDesc_ReturnsLiveProgramAndCountsIt) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());

    EXPECT_NE(program, kInvalidPipelineHandleUVE);
    EXPECT_TRUE(computeSystem.IsProgramLiveUVE(program));

    const ComputeSystemDiagnosticsUVE& diagnostics = computeSystem.GetDiagnosticsUVE();
    EXPECT_EQ(diagnostics.programsCreated, 1U);
    EXPECT_EQ(diagnostics.programCreationsFailed, 0U);
    EXPECT_EQ(diagnostics.livePrograms, 1U);
}

TEST_F(ComputeSystemUVETest, CreateProgramUVE_EmptySource_FailsLoudlyWithInfoLog) {
    ComputeProgramDescUVE desc = MakeTestProgramDescUVE("empty-kernel");
    desc.sourceCode.clear();

    std::string infoLog;
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(desc, &infoLog);

    EXPECT_EQ(program, kInvalidPipelineHandleUVE);
    EXPECT_FALSE(infoLog.empty());
    EXPECT_FALSE(computeSystem.IsProgramLiveUVE(program));

    const ComputeSystemDiagnosticsUVE& diagnostics = computeSystem.GetDiagnosticsUVE();
    EXPECT_EQ(diagnostics.programsCreated, 0U);
    EXPECT_EQ(diagnostics.programCreationsFailed, 1U);
    EXPECT_EQ(diagnostics.livePrograms, 0U);
}

TEST_F(ComputeSystemUVETest, DestroyProgramUVE_LiveProgram_SecondDestroyIsLoggedNoOp) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    computeSystem.DestroyProgramUVE(program);
    EXPECT_FALSE(computeSystem.IsProgramLiveUVE(program));
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().livePrograms, 0U);

    computeSystem.DestroyProgramUVE(program); // already destroyed - must not crash or re-destroy
    EXPECT_FALSE(computeSystem.IsProgramLiveUVE(program));
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().livePrograms, 0U);
}

TEST_F(ComputeSystemUVETest, DestroyProgramUVE_InvalidOrForeignHandle_IsLoggedNoOp) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    computeSystem.DestroyProgramUVE(kInvalidPipelineHandleUVE);
    computeSystem.DestroyProgramUVE(PipelineHandleUVE{program.value + 1000U});

    // The owned program must be untouched by the no-op destroys.
    EXPECT_TRUE(computeSystem.IsProgramLiveUVE(program));
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().livePrograms, 1U);
}

TEST_F(ComputeSystemUVETest, IsProgramLiveUVE_ForeignRhiPipelineHandleIsNotOwnedHere) {
    // A pipeline created directly through the RHI is alive at the device level but was never
    // created by this system - liveness here means ownership, not GPU existence.
    const ShaderHandleUVE shader =
        device.CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Compute, "foreign kernel source"});
    ASSERT_NE(shader, kInvalidShaderHandleUVE);
    ComputePipelineDescUVE pipelineDesc;
    pipelineDesc.computeShader = shader;
    const PipelineHandleUVE foreign = device.CreateComputePipelineUVE(pipelineDesc);
    ASSERT_NE(foreign, kInvalidPipelineHandleUVE);

    EXPECT_FALSE(computeSystem.IsProgramLiveUVE(foreign));

    ComputeDispatchDescUVE dispatch;
    dispatch.program = foreign;
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRejected, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);

    device.DestroyPipelineUVE(foreign);
    device.DestroyShaderUVE(shader);
}

TEST_F(ComputeSystemUVETest, EnqueueDispatchUVE_ValidDesc_AcceptsAndCounts) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 4U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;

    EXPECT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 1U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesEnqueued, 1U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRejected, 0U);
}

TEST_F(ComputeSystemUVETest, EnqueueDispatchUVE_ZeroGroupCountOnAnyAxis_Rejects) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;

    dispatch.groupCountX = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));
    dispatch.groupCountX = 1U;
    dispatch.groupCountY = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));
    dispatch.groupCountY = 1U;
    dispatch.groupCountZ = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));

    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRejected, 3U);
}

TEST_F(ComputeSystemUVETest, EnqueueDispatchUVE_InvalidBindingHandleOrEmptyUniformName_Rejects) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;

    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{kInvalidBufferHandleUVE, 0U});
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));
    dispatch.storageBuffers.clear();

    dispatch.textures.push_back(ComputeTextureBindingUVE{kInvalidTextureHandleUVE, 0U});
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));
    dispatch.textures.clear();

    dispatch.uniforms.push_back(MakeComputeUniformFloatUVE("", 1.0F));
    EXPECT_FALSE(computeSystem.EnqueueDispatchUVE(dispatch));

    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRejected, 3U);
}

TEST_F(ComputeSystemUVETest, ExecuteQueuedDispatchesUVE_RecordsBindThenUniformsThenDispatchInOrder) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);
    const BufferHandleUVE buffer =
        device.CreateBufferUVE(BufferDescUVE{64U, BufferUsageUVE::Storage});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);
    TextureDescUVE textureDesc;
    textureDesc.width = 4U;
    textureDesc.height = 4U;
    const TextureHandleUVE texture = device.CreateTextureUVE(textureDesc);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);

    Math::Matrix4x4UVE transform = Math::Matrix4x4UVE::IdentityUVE();
    transform.m[1][2] = 5.0F;
    transform.m[3][0] = -2.0F;

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 4U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{buffer, 3U});
    dispatch.textures.push_back(ComputeTextureBindingUVE{texture, 1U});
    dispatch.uniforms.push_back(MakeComputeUniformFloatUVE("uTime", 2.5F));
    dispatch.uniforms.push_back(MakeComputeUniformIntUVE("uCount", 7));
    dispatch.uniforms.push_back(MakeComputeUniformBoolUVE("uFlag", true));
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3UVE("uDir", Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    dispatch.uniforms.push_back(MakeComputeUniformMatrix4x4UVE("uXform", transform));
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));

    // The frame flow the engine will use: record into RenderSystemUVE's command buffer between
    // BeginFrameUVE() and EndFrameUVE(), outside pass markers, then let EndFrameUVE() submit.
    RenderSystemUVE renderSystem(device);
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 1U);
    renderSystem.EndFrameUVE();

    const std::vector<RecordedCommandUVE>& submitted = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(submitted.size(), 9U);

    ASSERT_TRUE(std::holds_alternative<BindPipelineCommandUVE>(submitted[0]));
    EXPECT_EQ(std::get<BindPipelineCommandUVE>(submitted[0]).pipeline, program);

    ASSERT_TRUE(std::holds_alternative<BindStorageBufferCommandUVE>(submitted[1]));
    EXPECT_EQ(std::get<BindStorageBufferCommandUVE>(submitted[1]).buffer, buffer);
    EXPECT_EQ(std::get<BindStorageBufferCommandUVE>(submitted[1]).slot, 3U);

    ASSERT_TRUE(std::holds_alternative<BindTextureCommandUVE>(submitted[2]));
    EXPECT_EQ(std::get<BindTextureCommandUVE>(submitted[2]).texture, texture);
    EXPECT_EQ(std::get<BindTextureCommandUVE>(submitted[2]).slot, 1U);

    ASSERT_TRUE(std::holds_alternative<SetUniformFloatCommandUVE>(submitted[3]));
    EXPECT_EQ(std::get<SetUniformFloatCommandUVE>(submitted[3]).name, "uTime");
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommandUVE>(submitted[3]).value, 2.5F);

    ASSERT_TRUE(std::holds_alternative<SetUniformIntCommandUVE>(submitted[4]));
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(submitted[4]).name, "uCount");
    EXPECT_EQ(std::get<SetUniformIntCommandUVE>(submitted[4]).value, 7);

    ASSERT_TRUE(std::holds_alternative<SetUniformBoolCommandUVE>(submitted[5]));
    EXPECT_EQ(std::get<SetUniformBoolCommandUVE>(submitted[5]).name, "uFlag");
    EXPECT_TRUE(std::get<SetUniformBoolCommandUVE>(submitted[5]).value);

    ASSERT_TRUE(std::holds_alternative<SetUniformVector3CommandUVE>(submitted[6]));
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(submitted[6]).name, "uDir");
    EXPECT_EQ(std::get<SetUniformVector3CommandUVE>(submitted[6]).value,
              (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));

    ASSERT_TRUE(std::holds_alternative<SetUniformMatrix4x4CommandUVE>(submitted[7]));
    EXPECT_EQ(std::get<SetUniformMatrix4x4CommandUVE>(submitted[7]).name, "uXform");
    EXPECT_EQ(std::get<SetUniformMatrix4x4CommandUVE>(submitted[7]).value, transform);

    ASSERT_TRUE(std::holds_alternative<DispatchCommandUVE>(submitted[8]));
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[8]).groupCountX, 4U);
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[8]).groupCountY, 2U);
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[8]).groupCountZ, 1U);

    // Execute consumed the queue exactly once.
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 1U);

    device.DestroyTextureUVE(texture);
    device.DestroyBufferUVE(buffer);
}

TEST_F(ComputeSystemUVETest, ExecuteQueuedDispatchesUVE_MultipleDispatches_RecordsInEnqueueOrder) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE first;
    first.program = program;
    first.groupCountX = 4U;
    ComputeDispatchDescUVE second;
    second.program = program;
    second.groupCountY = 8U;
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(first));
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(second));
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 2U);

    RenderSystemUVE renderSystem(device);
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 2U);
    renderSystem.EndFrameUVE();

    const std::vector<RecordedCommandUVE>& submitted = device.GetLastSubmittedCommandsUVE();
    ASSERT_EQ(submitted.size(), 4U);
    ASSERT_TRUE(std::holds_alternative<DispatchCommandUVE>(submitted[1]));
    ASSERT_TRUE(std::holds_alternative<DispatchCommandUVE>(submitted[3]));
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[1]).groupCountX, 4U);
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[1]).groupCountY, 1U);
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[3]).groupCountX, 1U);
    EXPECT_EQ(std::get<DispatchCommandUVE>(submitted[3]).groupCountY, 8U);

    // The queue is empty now - a second execute records nothing.
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 0U);
    renderSystem.EndFrameUVE();
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 2U);
}

TEST_F(ComputeSystemUVETest, ExecuteQueuedDispatchesUVE_ProgramDestroyedAfterEnqueue_SkipsLoudly) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));

    computeSystem.DestroyProgramUVE(program);

    RenderSystemUVE renderSystem(device);
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 0U);
    renderSystem.EndFrameUVE();

    // Nothing may reach the device for a dead program - not even a pipeline bind.
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());
    const ComputeSystemDiagnosticsUVE& diagnostics = computeSystem.GetDiagnosticsUVE();
    EXPECT_EQ(diagnostics.dispatchesSkipped, 1U);
    EXPECT_EQ(diagnostics.dispatchesRecorded, 0U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
}

TEST_F(ComputeSystemUVETest, ClearQueueUVE_DropsQueuedDispatchesWithoutRecording) {
    const PipelineHandleUVE program = computeSystem.CreateProgramUVE(MakeTestProgramDescUVE());
    ASSERT_NE(program, kInvalidPipelineHandleUVE);

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));
    ASSERT_TRUE(computeSystem.EnqueueDispatchUVE(dispatch));

    computeSystem.ClearQueueUVE();
    EXPECT_EQ(computeSystem.GetQueuedDispatchCountUVE(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesCleared, 2U);

    RenderSystemUVE renderSystem(device);
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 0U);
    renderSystem.EndFrameUVE();
    EXPECT_TRUE(device.GetLastSubmittedCommandsUVE().empty());
    EXPECT_EQ(computeSystem.GetDiagnosticsUVE().dispatchesRecorded, 0U);
}

// ---------------------------------------------------------------------------
// Real-GL proofs: program compilation and byte-verified GPU execution through
// the engine-level API (skip cleanly without a display, like every GL test).
// ---------------------------------------------------------------------------

// Same rationale as Test/RHI/OpenGL/gl_render_device_uve_tests.cpp: this sandbox's Mesa/llvmpipe
// GLX stack caps at OpenGL 4.5 Core, so the fixture requests 4.5 rather than the production
// default 4.6 and isolates "no display available" as the only GTEST_SKIP() reason. Compute needs
// desktop GL 4.3+, comfortably below the request.
[[nodiscard]] Window::WindowDescUVE MakeComputeTestWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_compute_system_uve_tests";
    desc.width = 64;
    desc.height = 64;
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

// One invocation writes the vec3 uniform it was given into the SSBO at binding 0 as a vec4. The
// GL twin of the M5a RHI-level palette filler, with the value routed through a
// ComputeUniformWriteUVE so the engine-level uniform path is part of what gets proven.
constexpr std::string_view kUniformWriteComputeSource = R"(#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer ResultBlock {
    vec4 resultColor;
};
uniform vec3 uColor;
void main() {
    resultColor = vec4(uColor, 1.0);
}
)";

constexpr std::string_view kBrokenComputeSource = R"(#version 430 core
void main() {
    this is not valid glsl :(
}
)";

class ComputeSystemGlUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeComputeTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
        if (!renderDevice->IsUsableUVE()) {
            GTEST_SKIP() << "GlRenderDeviceUVE came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystemUVE>(*renderDevice);
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
    std::unique_ptr<ComputeSystemUVE> computeSystem;
};

TEST_F(ComputeSystemGlUVETest, CreateProgramUVE_ValidGlslComputeSource_CompilesAndLinks) {
    ComputeProgramDescUVE desc;
    desc.sourceCode = std::string(kUniformWriteComputeSource);
    desc.debugName = "uniform-write-kernel";

    std::string infoLog;
    const PipelineHandleUVE program = computeSystem->CreateProgramUVE(desc, &infoLog);
    if (program == kInvalidPipelineHandleUVE) {
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    EXPECT_TRUE(computeSystem->IsProgramLiveUVE(program));
    EXPECT_EQ(computeSystem->GetDiagnosticsUVE().programsCreated, 1U);
    EXPECT_EQ(computeSystem->GetDiagnosticsUVE().programCreationsFailed, 0U);

    computeSystem->DestroyProgramUVE(program);
    EXPECT_FALSE(computeSystem->IsProgramLiveUVE(program));
}

TEST_F(ComputeSystemGlUVETest, CreateProgramUVE_BrokenSource_FailsWithBackendInfoLog) {
    ComputeProgramDescUVE desc;
    desc.sourceCode = std::string(kBrokenComputeSource);
    desc.debugName = "broken-kernel";

    std::string infoLog;
    const PipelineHandleUVE program = computeSystem->CreateProgramUVE(desc, &infoLog);

    EXPECT_EQ(program, kInvalidPipelineHandleUVE);
    // The GL compiler always has something to say about broken source - the info log must reach
    // the caller, not die inside the backend.
    EXPECT_FALSE(infoLog.empty());
    EXPECT_EQ(computeSystem->GetDiagnosticsUVE().programCreationsFailed, 1U);
    EXPECT_EQ(computeSystem->GetDiagnosticsUVE().livePrograms, 0U);
}

TEST_F(ComputeSystemGlUVETest, ExecuteQueuedDispatchesUVE_ResultReadBackThroughTheRhi_NoRawGlCalls) {
    // CS3's payoff: the same real dispatch, verified end to end through ENGINE APIs only -
    // ComputeSystemUVE queues it, RenderSystemUVE's frame buffer carries it, and
    // IRenderDeviceUVE::ReadbackBufferUVE reads the result. No glGetIntegeri_v, no
    // glGetBufferSubData, no GL type anywhere in the assertions: this is the shape engine and
    // game code will actually use to consume GPU compute output.
    const float zeros[4] = {};
    const BufferHandleUVE resultBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(zeros), BufferUsageUVE::Storage}, std::as_bytes(std::span(zeros)));
    if (resultBuffer == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    ComputeProgramDescUVE programDesc;
    programDesc.sourceCode = std::string(kUniformWriteComputeSource);
    programDesc.debugName = "uniform-write-kernel";
    std::string infoLog;
    const PipelineHandleUVE program = computeSystem->CreateProgramUVE(programDesc, &infoLog);
    if (program == kInvalidPipelineHandleUVE) {
        renderDevice->DestroyBufferUVE(resultBuffer);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    // Control: before the dispatch the RHI readback must show the zero fill.
    std::array<float, 4> control{1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(resultBuffer, std::as_writable_bytes(std::span(control))));
    for (const float value : control) {
        EXPECT_FLOAT_EQ(value, 0.0F) << "control: the result buffer must start zeroed";
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{resultBuffer, 0U});
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3UVE("uColor", Math::Vector3UVE{0.125F, 0.375F, 0.625F}));
    ASSERT_TRUE(computeSystem->EnqueueDispatchUVE(dispatch));

    RenderSystemUVE renderSystem(*renderDevice);
    renderSystem.BeginFrameUVE();
    EXPECT_EQ(computeSystem->ExecuteQueuedDispatchesUVE(renderSystem.GetFrameCommandBufferUVE()), 1U);
    renderSystem.EndFrameUVE();

    std::array<float, 4> readback{};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(resultBuffer, std::as_writable_bytes(std::span(readback))));
    EXPECT_FLOAT_EQ(readback[0], 0.125F);
    EXPECT_FLOAT_EQ(readback[1], 0.375F);
    EXPECT_FLOAT_EQ(readback[2], 0.625F);
    EXPECT_FLOAT_EQ(readback[3], 1.0F);

    computeSystem->DestroyProgramUVE(program);
    renderDevice->DestroyBufferUVE(resultBuffer);
}

TEST_F(ComputeSystemGlUVETest, ExecuteQueuedDispatchesUVE_RealGpuCompute_ByteVerifiedByReadback) {
    // The engine-level GL proof: a ZERO-initialized Storage SSBO, one dispatch enqueued through
    // ComputeSystemUVE with a vec3 uniform write, executed into RenderSystemUVE's frame command
    // buffer (GL runs it at record time, barriers included), then a client-side readback must
    // show exactly the uniform's color. Without a real glDispatchCompute driven through this
    // layer the buffer stays zeroed - the result cannot be faked.
    const float zeros[4] = {};
    const BufferHandleUVE resultBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(zeros), BufferUsageUVE::Storage}, std::as_bytes(std::span(zeros)));
    if (resultBuffer == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    ComputeProgramDescUVE programDesc;
    programDesc.sourceCode = std::string(kUniformWriteComputeSource);
    programDesc.debugName = "uniform-write-kernel";
    std::string infoLog;
    const PipelineHandleUVE program = computeSystem->CreateProgramUVE(programDesc, &infoLog);
    if (program == kInvalidPipelineHandleUVE) {
        renderDevice->DestroyBufferUVE(resultBuffer);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    ComputeDispatchDescUVE dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 1U;
    dispatch.groupCountY = 1U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBindingUVE{resultBuffer, 0U});
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3UVE("uColor", Math::Vector3UVE{0.25F, 0.5F, 0.75F}));
    ASSERT_TRUE(computeSystem->EnqueueDispatchUVE(dispatch));

    RenderSystemUVE renderSystem(*renderDevice);
    renderSystem.BeginFrameUVE();
    ICommandBufferUVE& commandBuffer = renderSystem.GetFrameCommandBufferUVE();
    EXPECT_EQ(computeSystem->ExecuteQueuedDispatchesUVE(commandBuffer), 1U);
    renderSystem.EndFrameUVE();
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    // Readback through the indexed binding the GL backend's glBindBufferBase left behind - the
    // exact pattern the M5a RHI-level proof uses.
    GLint storageName = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &storageName);
    ASSERT_NE(storageName, 0) << "the storage buffer must still be bound at indexed slot 0";
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(storageName));
    float readback[4] = {};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(readback), readback);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    EXPECT_FLOAT_EQ(readback[0], 0.25F);
    EXPECT_FLOAT_EQ(readback[1], 0.5F);
    EXPECT_FLOAT_EQ(readback[2], 0.75F);
    EXPECT_FLOAT_EQ(readback[3], 1.0F);
    EXPECT_EQ(computeSystem->GetDiagnosticsUVE().dispatchesRecorded, 1U);

    computeSystem->DestroyProgramUVE(program);
    renderDevice->DestroyBufferUVE(resultBuffer);
}

} // namespace
} // namespace UVE::Render::Tests
