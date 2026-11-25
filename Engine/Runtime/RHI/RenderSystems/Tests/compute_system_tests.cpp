// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "compute_system.h"

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

#include "event_system.h"
#include "matrix4x4.h"
#include "vector3.h"
#include "render_system.h"
#include "null_render_device.h"

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>

#include "gl_render_device.h"
#include "window_manager.h"

namespace UVE::Render::Tests {
namespace {

// The source text never reaches a compiler in the Null-backed tests - Null records the creation
// and hands out a handle - but it is non-empty, because empty source is an engine-level rejection
// the system must catch before the backend ever sees the desc.
[[nodiscard]] ComputeProgramDesc MakeTestProgramDesc(std::string debugName = "test-kernel") {
    ComputeProgramDesc desc;
    desc.sourceCode = "compute kernel source - recorded, never compiled, by the Null backend";
    desc.debugName = std::move(debugName);
    return desc;
}

class ComputeSystemTest : public ::testing::Test {
protected:
    NullRenderDevice device;
    ComputeSystem computeSystem{device};
};

TEST_F(ComputeSystemTest, CreateProgram_ValidDesc_ReturnsLiveProgramAndCountsIt) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());

    EXPECT_NE(program, kInvalidPipelineHandle);
    EXPECT_TRUE(computeSystem.IsProgramLive(program));

    const ComputeSystemDiagnostics& diagnostics = computeSystem.GetDiagnostics();
    EXPECT_EQ(diagnostics.programsCreated, 1U);
    EXPECT_EQ(diagnostics.programCreationsFailed, 0U);
    EXPECT_EQ(diagnostics.livePrograms, 1U);
}

TEST_F(ComputeSystemTest, CreateProgram_EmptySource_FailsLoudlyWithInfoLog) {
    ComputeProgramDesc desc = MakeTestProgramDesc("empty-kernel");
    desc.sourceCode.clear();

    std::string infoLog;
    const PipelineHandle program = computeSystem.CreateProgram(desc, &infoLog);

    EXPECT_EQ(program, kInvalidPipelineHandle);
    EXPECT_FALSE(infoLog.empty());
    EXPECT_FALSE(computeSystem.IsProgramLive(program));

    const ComputeSystemDiagnostics& diagnostics = computeSystem.GetDiagnostics();
    EXPECT_EQ(diagnostics.programsCreated, 0U);
    EXPECT_EQ(diagnostics.programCreationsFailed, 1U);
    EXPECT_EQ(diagnostics.livePrograms, 0U);
}

TEST_F(ComputeSystemTest, DestroyProgram_LiveProgram_SecondDestroyIsLoggedNoOp) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    computeSystem.DestroyProgram(program);
    EXPECT_FALSE(computeSystem.IsProgramLive(program));
    EXPECT_EQ(computeSystem.GetDiagnostics().livePrograms, 0U);

    computeSystem.DestroyProgram(program); // already destroyed - must not crash or re-destroy
    EXPECT_FALSE(computeSystem.IsProgramLive(program));
    EXPECT_EQ(computeSystem.GetDiagnostics().livePrograms, 0U);
}

TEST_F(ComputeSystemTest, DestroyProgram_InvalidOrForeignHandle_IsLoggedNoOp) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    computeSystem.DestroyProgram(kInvalidPipelineHandle);
    computeSystem.DestroyProgram(PipelineHandle{program.value + 1000U});

    // The owned program must be untouched by the no-op destroys.
    EXPECT_TRUE(computeSystem.IsProgramLive(program));
    EXPECT_EQ(computeSystem.GetDiagnostics().livePrograms, 1U);
}

TEST_F(ComputeSystemTest, IsProgramLive_ForeignRhiPipelineHandleIsNotOwnedHere) {
    // A pipeline created directly through the RHI is alive at the device level but was never
    // created by this system - liveness here means ownership, not GPU existence.
    const ShaderHandle shader =
        device.CreateShader(ShaderDesc{ShaderStage::Compute, "foreign kernel source"});
    ASSERT_NE(shader, kInvalidShaderHandle);
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = shader;
    const PipelineHandle foreign = device.CreateComputePipeline(pipelineDesc);
    ASSERT_NE(foreign, kInvalidPipelineHandle);

    EXPECT_FALSE(computeSystem.IsProgramLive(foreign));

    ComputeDispatchDesc dispatch;
    dispatch.program = foreign;
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRejected, 1U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);

    device.DestroyPipeline(foreign);
    device.DestroyShader(shader);
}

TEST_F(ComputeSystemTest, EnqueueDispatch_ValidDesc_AcceptsAndCounts) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 4U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;

    EXPECT_TRUE(computeSystem.EnqueueDispatch(dispatch));
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 1U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesEnqueued, 1U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRejected, 0U);
}

TEST_F(ComputeSystemTest, EnqueueDispatch_ZeroGroupCountOnAnyAxis_Rejects) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc dispatch;
    dispatch.program = program;

    dispatch.groupCountX = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));
    dispatch.groupCountX = 1U;
    dispatch.groupCountY = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));
    dispatch.groupCountY = 1U;
    dispatch.groupCountZ = 0U;
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));

    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRejected, 3U);
}

TEST_F(ComputeSystemTest, EnqueueDispatch_InvalidBindingHandleOrEmptyUniformName_Rejects) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc dispatch;
    dispatch.program = program;

    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{kInvalidBufferHandle, 0U});
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));
    dispatch.storageBuffers.clear();

    dispatch.textures.push_back(ComputeTextureBinding{kInvalidTextureHandle, 0U});
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));
    dispatch.textures.clear();

    dispatch.uniforms.push_back(MakeComputeUniformFloat("", 1.0F));
    EXPECT_FALSE(computeSystem.EnqueueDispatch(dispatch));

    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRejected, 3U);
}

TEST_F(ComputeSystemTest, ExecuteQueuedDispatches_RecordsBindThenUniformsThenDispatchInOrder) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);
    const BufferHandle buffer =
        device.CreateBuffer(BufferDesc{64U, BufferUsage::Storage});
    ASSERT_NE(buffer, kInvalidBufferHandle);
    TextureDesc textureDesc;
    textureDesc.width = 4U;
    textureDesc.height = 4U;
    const TextureHandle texture = device.CreateTexture(textureDesc);
    ASSERT_NE(texture, kInvalidTextureHandle);

    Math::Matrix4x4 transform = Math::Matrix4x4::Identity();
    transform.m[1][2] = 5.0F;
    transform.m[3][0] = -2.0F;

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 4U;
    dispatch.groupCountY = 2U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{buffer, 3U});
    dispatch.textures.push_back(ComputeTextureBinding{texture, 1U});
    dispatch.uniforms.push_back(MakeComputeUniformFloat("uTime", 2.5F));
    dispatch.uniforms.push_back(MakeComputeUniformInt("uCount", 7));
    dispatch.uniforms.push_back(MakeComputeUniformBool("uFlag", true));
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3("uDir", Math::Vector3{1.0F, 2.0F, 3.0F}));
    dispatch.uniforms.push_back(MakeComputeUniformMatrix4x4("uXform", transform));
    ASSERT_TRUE(computeSystem.EnqueueDispatch(dispatch));

    // The frame flow the engine will use: record into RenderSystem's command buffer between
    // BeginFrame() and EndFrame(), outside pass markers, then let EndFrame() submit.
    RenderSystem renderSystem(device);
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 1U);
    renderSystem.EndFrame();

    const std::vector<RecordedCommand>& submitted = device.GetLastSubmittedCommands();
    ASSERT_EQ(submitted.size(), 9U);

    ASSERT_TRUE(std::holds_alternative<BindPipelineCommand>(submitted[0]));
    EXPECT_EQ(std::get<BindPipelineCommand>(submitted[0]).pipeline, program);

    ASSERT_TRUE(std::holds_alternative<BindStorageBufferCommand>(submitted[1]));
    EXPECT_EQ(std::get<BindStorageBufferCommand>(submitted[1]).buffer, buffer);
    EXPECT_EQ(std::get<BindStorageBufferCommand>(submitted[1]).slot, 3U);

    ASSERT_TRUE(std::holds_alternative<BindTextureCommand>(submitted[2]));
    EXPECT_EQ(std::get<BindTextureCommand>(submitted[2]).texture, texture);
    EXPECT_EQ(std::get<BindTextureCommand>(submitted[2]).slot, 1U);

    ASSERT_TRUE(std::holds_alternative<SetUniformFloatCommand>(submitted[3]));
    EXPECT_EQ(std::get<SetUniformFloatCommand>(submitted[3]).name, "uTime");
    EXPECT_FLOAT_EQ(std::get<SetUniformFloatCommand>(submitted[3]).value, 2.5F);

    ASSERT_TRUE(std::holds_alternative<SetUniformIntCommand>(submitted[4]));
    EXPECT_EQ(std::get<SetUniformIntCommand>(submitted[4]).name, "uCount");
    EXPECT_EQ(std::get<SetUniformIntCommand>(submitted[4]).value, 7);

    ASSERT_TRUE(std::holds_alternative<SetUniformBoolCommand>(submitted[5]));
    EXPECT_EQ(std::get<SetUniformBoolCommand>(submitted[5]).name, "uFlag");
    EXPECT_TRUE(std::get<SetUniformBoolCommand>(submitted[5]).value);

    ASSERT_TRUE(std::holds_alternative<SetUniformVector3Command>(submitted[6]));
    EXPECT_EQ(std::get<SetUniformVector3Command>(submitted[6]).name, "uDir");
    EXPECT_EQ(std::get<SetUniformVector3Command>(submitted[6]).value,
              (Math::Vector3{1.0F, 2.0F, 3.0F}));

    ASSERT_TRUE(std::holds_alternative<SetUniformMatrix4x4Command>(submitted[7]));
    EXPECT_EQ(std::get<SetUniformMatrix4x4Command>(submitted[7]).name, "uXform");
    EXPECT_EQ(std::get<SetUniformMatrix4x4Command>(submitted[7]).value, transform);

    ASSERT_TRUE(std::holds_alternative<DispatchCommand>(submitted[8]));
    EXPECT_EQ(std::get<DispatchCommand>(submitted[8]).groupCountX, 4U);
    EXPECT_EQ(std::get<DispatchCommand>(submitted[8]).groupCountY, 2U);
    EXPECT_EQ(std::get<DispatchCommand>(submitted[8]).groupCountZ, 1U);

    // Execute consumed the queue exactly once.
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 1U);

    device.DestroyTexture(texture);
    device.DestroyBuffer(buffer);
}

TEST_F(ComputeSystemTest, ExecuteQueuedDispatches_MultipleDispatches_RecordsInEnqueueOrder) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc first;
    first.program = program;
    first.groupCountX = 4U;
    ComputeDispatchDesc second;
    second.program = program;
    second.groupCountY = 8U;
    ASSERT_TRUE(computeSystem.EnqueueDispatch(first));
    ASSERT_TRUE(computeSystem.EnqueueDispatch(second));
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 2U);

    RenderSystem renderSystem(device);
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 2U);
    renderSystem.EndFrame();

    const std::vector<RecordedCommand>& submitted = device.GetLastSubmittedCommands();
    ASSERT_EQ(submitted.size(), 4U);
    ASSERT_TRUE(std::holds_alternative<DispatchCommand>(submitted[1]));
    ASSERT_TRUE(std::holds_alternative<DispatchCommand>(submitted[3]));
    EXPECT_EQ(std::get<DispatchCommand>(submitted[1]).groupCountX, 4U);
    EXPECT_EQ(std::get<DispatchCommand>(submitted[1]).groupCountY, 1U);
    EXPECT_EQ(std::get<DispatchCommand>(submitted[3]).groupCountX, 1U);
    EXPECT_EQ(std::get<DispatchCommand>(submitted[3]).groupCountY, 8U);

    // The queue is empty now - a second execute records nothing.
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 0U);
    renderSystem.EndFrame();
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 2U);
}

TEST_F(ComputeSystemTest, ExecuteQueuedDispatches_ProgramDestroyedAfterEnqueue_SkipsLoudly) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    ASSERT_TRUE(computeSystem.EnqueueDispatch(dispatch));

    computeSystem.DestroyProgram(program);

    RenderSystem renderSystem(device);
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 0U);
    renderSystem.EndFrame();

    // Nothing may reach the device for a dead program - not even a pipeline bind.
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());
    const ComputeSystemDiagnostics& diagnostics = computeSystem.GetDiagnostics();
    EXPECT_EQ(diagnostics.dispatchesSkipped, 1U);
    EXPECT_EQ(diagnostics.dispatchesRecorded, 0U);
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
}

TEST_F(ComputeSystemTest, ClearQueue_DropsQueuedDispatchesWithoutRecording) {
    const PipelineHandle program = computeSystem.CreateProgram(MakeTestProgramDesc());
    ASSERT_NE(program, kInvalidPipelineHandle);

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    ASSERT_TRUE(computeSystem.EnqueueDispatch(dispatch));
    ASSERT_TRUE(computeSystem.EnqueueDispatch(dispatch));

    computeSystem.ClearQueue();
    EXPECT_EQ(computeSystem.GetQueuedDispatchCount(), 0U);
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesCleared, 2U);

    RenderSystem renderSystem(device);
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem.ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 0U);
    renderSystem.EndFrame();
    EXPECT_TRUE(device.GetLastSubmittedCommands().empty());
    EXPECT_EQ(computeSystem.GetDiagnostics().dispatchesRecorded, 0U);
}

// ---------------------------------------------------------------------------
// Real-GL proofs: program compilation and byte-verified GPU execution through
// the engine-level API (skip cleanly without a display, like every GL test).
// ---------------------------------------------------------------------------

// Same rationale as Test/RHI/OpenGL/gl_render_device_uve_tests.cpp: this sandbox's Mesa/llvmpipe
// GLX stack caps at OpenGL 4.5 Core, so the fixture requests 4.5 rather than the production
// default 4.6 and isolates "no display available" as the only GTEST_SKIP() reason. Compute needs
// desktop GL 4.3+, comfortably below the request.
[[nodiscard]] Window::WindowDesc MakeComputeTestWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_compute_system_uve_tests";
    desc.width = 64;
    desc.height = 64;
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

// One invocation writes the vec3 uniform it was given into the SSBO at binding 0 as a vec4. The
// GL twin of the M5a RHI-level palette filler, with the value routed through a
// ComputeUniformWrite so the engine-level uniform path is part of what gets proven.
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

class ComputeSystemGlTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager =
            std::make_unique<Window::WindowManager>(eventSystem, MakeComputeTestWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
        if (!renderDevice->IsUsable()) {
            GTEST_SKIP() << "GlRenderDevice came up unusable on this display";
        }
        computeSystem = std::make_unique<ComputeSystem>(*renderDevice);
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
    std::unique_ptr<ComputeSystem> computeSystem;
};

TEST_F(ComputeSystemGlTest, CreateProgram_ValidGlslComputeSource_CompilesAndLinks) {
    ComputeProgramDesc desc;
    desc.sourceCode = std::string(kUniformWriteComputeSource);
    desc.debugName = "uniform-write-kernel";

    std::string infoLog;
    const PipelineHandle program = computeSystem->CreateProgram(desc, &infoLog);
    if (program == kInvalidPipelineHandle) {
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    EXPECT_TRUE(computeSystem->IsProgramLive(program));
    EXPECT_EQ(computeSystem->GetDiagnostics().programsCreated, 1U);
    EXPECT_EQ(computeSystem->GetDiagnostics().programCreationsFailed, 0U);

    computeSystem->DestroyProgram(program);
    EXPECT_FALSE(computeSystem->IsProgramLive(program));
}

TEST_F(ComputeSystemGlTest, CreateProgram_BrokenSource_FailsWithBackendInfoLog) {
    ComputeProgramDesc desc;
    desc.sourceCode = std::string(kBrokenComputeSource);
    desc.debugName = "broken-kernel";

    std::string infoLog;
    const PipelineHandle program = computeSystem->CreateProgram(desc, &infoLog);

    EXPECT_EQ(program, kInvalidPipelineHandle);
    // The GL compiler always has something to say about broken source - the info log must reach
    // the caller, not die inside the backend.
    EXPECT_FALSE(infoLog.empty());
    EXPECT_EQ(computeSystem->GetDiagnostics().programCreationsFailed, 1U);
    EXPECT_EQ(computeSystem->GetDiagnostics().livePrograms, 0U);
}

TEST_F(ComputeSystemGlTest, ExecuteQueuedDispatches_ResultReadBackThroughTheRhi_NoRawGlCalls) {
    // CS3's payoff: the same real dispatch, verified end to end through ENGINE APIs only -
    // ComputeSystem queues it, RenderSystem's frame buffer carries it, and
    // IRenderDevice::ReadbackBuffer reads the result. No glGetIntegeri_v, no
    // glGetBufferSubData, no GL type anywhere in the assertions: this is the shape engine and
    // game code will actually use to consume GPU compute output.
    const float zeros[4] = {};
    const BufferHandle resultBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(zeros), BufferUsage::Storage}, std::as_bytes(std::span(zeros)));
    if (resultBuffer == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    ComputeProgramDesc programDesc;
    programDesc.sourceCode = std::string(kUniformWriteComputeSource);
    programDesc.debugName = "uniform-write-kernel";
    std::string infoLog;
    const PipelineHandle program = computeSystem->CreateProgram(programDesc, &infoLog);
    if (program == kInvalidPipelineHandle) {
        renderDevice->DestroyBuffer(resultBuffer);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    // Control: before the dispatch the RHI readback must show the zero fill.
    std::array<float, 4> control{1.0F, 1.0F, 1.0F, 1.0F};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(resultBuffer, std::as_writable_bytes(std::span(control))));
    for (const float value : control) {
        EXPECT_FLOAT_EQ(value, 0.0F) << "control: the result buffer must start zeroed";
    }

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{resultBuffer, 0U});
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3("uColor", Math::Vector3{0.125F, 0.375F, 0.625F}));
    ASSERT_TRUE(computeSystem->EnqueueDispatch(dispatch));

    RenderSystem renderSystem(*renderDevice);
    renderSystem.BeginFrame();
    EXPECT_EQ(computeSystem->ExecuteQueuedDispatches(renderSystem.GetFrameCommandBuffer()), 1U);
    renderSystem.EndFrame();

    std::array<float, 4> readback{};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(resultBuffer, std::as_writable_bytes(std::span(readback))));
    EXPECT_FLOAT_EQ(readback[0], 0.125F);
    EXPECT_FLOAT_EQ(readback[1], 0.375F);
    EXPECT_FLOAT_EQ(readback[2], 0.625F);
    EXPECT_FLOAT_EQ(readback[3], 1.0F);

    computeSystem->DestroyProgram(program);
    renderDevice->DestroyBuffer(resultBuffer);
}

#if !defined(_WIN32)
TEST_F(ComputeSystemGlTest, ExecuteQueuedDispatches_RealGpuCompute_ByteVerifiedByReadback) {
    // The engine-level GL proof: a ZERO-initialized Storage SSBO, one dispatch enqueued through
    // ComputeSystem with a vec3 uniform write, executed into RenderSystem's frame command
    // buffer (GL runs it at record time, barriers included), then a client-side readback must
    // show exactly the uniform's color. Without a real glDispatchCompute driven through this
    // layer the buffer stays zeroed - the result cannot be faked.
    const float zeros[4] = {};
    const BufferHandle resultBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(zeros), BufferUsage::Storage}, std::as_bytes(std::span(zeros)));
    if (resultBuffer == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    ComputeProgramDesc programDesc;
    programDesc.sourceCode = std::string(kUniformWriteComputeSource);
    programDesc.debugName = "uniform-write-kernel";
    std::string infoLog;
    const PipelineHandle program = computeSystem->CreateProgram(programDesc, &infoLog);
    if (program == kInvalidPipelineHandle) {
        renderDevice->DestroyBuffer(resultBuffer);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+): " << infoLog;
    }

    ComputeDispatchDesc dispatch;
    dispatch.program = program;
    dispatch.groupCountX = 1U;
    dispatch.groupCountY = 1U;
    dispatch.groupCountZ = 1U;
    dispatch.storageBuffers.push_back(ComputeStorageBufferBinding{resultBuffer, 0U});
    dispatch.uniforms.push_back(
        MakeComputeUniformVector3("uColor", Math::Vector3{0.25F, 0.5F, 0.75F}));
    ASSERT_TRUE(computeSystem->EnqueueDispatch(dispatch));

    RenderSystem renderSystem(*renderDevice);
    renderSystem.BeginFrame();
    ICommandBuffer& commandBuffer = renderSystem.GetFrameCommandBuffer();
    EXPECT_EQ(computeSystem->ExecuteQueuedDispatches(commandBuffer), 1U);
    renderSystem.EndFrame();
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
    EXPECT_EQ(computeSystem->GetDiagnostics().dispatchesRecorded, 1U);

    computeSystem->DestroyProgram(program);
    renderDevice->DestroyBuffer(resultBuffer);
}
#endif

} // namespace
} // namespace UVE::Render::Tests
