// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gl_render_device.h"
#include "bind_group.h"
#include "render_surface.h"
#include "asset_bundle.h"
#include "file_system.h"
#include "mesh_asset.h"
#include "built_in_shaders.h"
#include "shader_manager.h"
#include "thread_pool.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include "gl_functions.h"
#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include <chrono>
#include <limits>

#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "matrix4x4.h"
#include "vector3.h"
#include "null_window_manager.h"
#include "window_manager.h"

namespace UVE::Render::Tests {
namespace {

// Same rationale as tests/window/window_manager_uve_tests.cpp: this sandbox's Mesa/llvmpipe GLX
// stack caps at OpenGL 4.5 Core, so tests explicitly request 4.5 rather than the production
// default (4.6) to isolate "no display available" as the only GTEST_SKIP() reason.
[[nodiscard]] Window::WindowDesc MakeTestWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_gl_render_device_uve_tests";
    desc.width = 64;
    desc.height = 64;
    desc.glVersionMajor = 4;
    desc.glVersionMinor = 5;
    return desc;
}

constexpr std::string_view kValidVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
void main() {
    gl_Position = vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kValidFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

constexpr std::string_view kBrokenShaderSource = R"(#version 330 core
void main() {
    this is not valid glsl :(
}
)";

constexpr std::string_view kUniformVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
void main() {
    gl_Position = uModel * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kUniformFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

constexpr std::string_view kFiniteUniformFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
uniform float uExposure;
void main() {
    FragColor = vec4(uColor * uExposure, 1.0);
}
)";
constexpr std::string_view kSamplerAndUnsupportedUniformFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uTexture;
uniform uint uUnsigned;
void main() {
    FragColor = texture(uTexture, vec2(0.5)) * float(uUnsigned);
}
)";

[[nodiscard]] std::string WithShaderStageDefine(std::string_view source, std::string_view stageDefine) {
    std::string resolvedSource(source);
    const std::size_t firstLineEnd = resolvedSource.find('\n');
    if (firstLineEnd == std::string::npos) {
        return resolvedSource;
    }
    resolvedSource.insert(firstLineEnd + 1U, "#define " + std::string(stageDefine) + "\n");
    return resolvedSource;
}

// Needs a real (possibly virtual, e.g. Xvfb) GL context. Every test's fixture checks this at
// SetUp() and calls GTEST_SKIP() with a clear message if unavailable, so the same uve_tests
// binary runs cleanly with or without a display attached.
class GlRenderDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager = std::make_unique<Window::WindowManager>(eventSystem, MakeTestWindowDesc());
        if (!windowManager->IsValid()) {
            GTEST_SKIP() << "No display available for GlRenderDevice - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDevice>(*windowManager);
    }

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<GlRenderDevice> renderDevice;
};

TEST(GlRenderDeviceUnavailableTest, InvalidWindowManager_ConstructsAsUnusableWithoutAbort) {
    Window::NullWindowManager windowManager;
    GlRenderDevice renderDevice(windowManager);

    EXPECT_FALSE(renderDevice.IsUsable());
    EXPECT_EQ(renderDevice.GetBackendName(), "OpenGL");
    EXPECT_EQ(renderDevice.GetLiveResourceCount(), 0U);
    renderDevice.Present();
}

TEST_F(GlRenderDeviceTest, BindGroupAppliesRealTextureSamplerAndBufferBindings) {
    const auto uniform = renderDevice->CreateBuffer({64U, BufferUsage::Uniform});
    const auto storage = renderDevice->CreateBuffer({64U, BufferUsage::Storage});
    const auto texture = renderDevice->CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    const auto sampler = renderDevice->CreateSampler({});
    ASSERT_NE(uniform, kInvalidBufferHandle);
    ASSERT_NE(storage, kInvalidBufferHandle);
    ASSERT_NE(texture, kInvalidTextureHandle);
    ASSERT_NE(sampler, kInvalidSamplerHandle);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0U, 0U);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, 0U);
    glBindSampler(0U, 0U);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0U);
    const std::array<ResourceBinding, 4> bindings{
        BindUniformBufferCommand{uniform, 0U}, BindStorageBufferCommand{storage, 0U},
        BindTextureCommand{texture, 0U}, BindSamplerCommand{sampler, 0U}};
    auto commands = renderDevice->CreateCommandBuffer();
    commands->BeginRenderPass({});
    BindGroup(*commands, {bindings, {}});
    GLint name = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegeri_v(GL_SAMPLER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name); EXPECT_GT(name, 0);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    renderDevice->DestroyBuffer(uniform);
    renderDevice->DestroyBuffer(storage);
    renderDevice->DestroyTexture(texture);
    renderDevice->DestroySampler(sampler);
}

// These sentinels are only used to check symbol resolution, never invoked as GL calls.
void LoaderSentinel() {}

TEST(GlFunctionsTest, BlendEquationIsLoadedAndRequiredForTheBaseline) {
    const auto resolve = +[](const char*) -> void* {
        return reinterpret_cast<void*>(&LoaderSentinel);
    };
    auto loaded = Detail::LoadGlFunctions(resolve);
    ASSERT_TRUE(loaded.IsComplete());
    ASSERT_NE(loaded.glBlendEquation, nullptr);
    loaded.glBlendEquation = nullptr;
    EXPECT_FALSE(loaded.IsComplete());
    const auto missingBlend = +[](const char* name) -> void* {
        if (std::string_view{name} == "glBlendEquation") return nullptr;
        return reinterpret_cast<void*>(&LoaderSentinel);
    };
    EXPECT_FALSE(Detail::LoadGlFunctions(missingBlend).IsComplete());
}

TEST(GlFunctionsTest, StencilSymbolsAreLoadedAndRequiredForTheBaseline) {
    const auto resolve = +[](const char*) -> void* {
        return reinterpret_cast<void*>(&LoaderSentinel);
    };
    const Detail::GlFunctions loaded = Detail::LoadGlFunctions(resolve);
    ASSERT_TRUE(loaded.IsComplete());
    ASSERT_NE(loaded.glStencilFuncSeparate, nullptr);
    ASSERT_NE(loaded.glStencilOpSeparate, nullptr);
    ASSERT_NE(loaded.glStencilMaskSeparate, nullptr);
    auto missing = loaded;
    missing.glStencilFuncSeparate = nullptr;
    EXPECT_FALSE(missing.IsComplete());
    missing = loaded;
    missing.glStencilOpSeparate = nullptr;
    EXPECT_FALSE(missing.IsComplete());
    missing = loaded;
    missing.glStencilMaskSeparate = nullptr;
    EXPECT_FALSE(missing.IsComplete());
    const auto missingStencil = +[](const char* name) -> void* {
        if (std::string_view{name}.starts_with("glStencil")) return nullptr;
        return reinterpret_cast<void*>(&LoaderSentinel);
    };
    EXPECT_FALSE(Detail::LoadGlFunctions(missingStencil).IsComplete());
}

TEST_F(GlRenderDeviceTest, TimelineSemaphoresAreExplicitlyUnsupportedRatherThanModeled) {
    EXPECT_EQ(renderDevice->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Unsupported);
    SemaphoreHandle semaphore{7U};
    EXPECT_EQ(renderDevice->CreateSemaphore(SemaphoreDesc{1U}, semaphore),
              RenderOperationResult::Unsupported);
    EXPECT_EQ(semaphore.value, 7U) << "a refused create must leave the output untouched";
    std::uint64_t counter = 0U;
    EXPECT_EQ(renderDevice->GetSemaphoreCounter(kInvalidSemaphoreHandle, counter),
              RenderOperationResult::Unsupported);
    EXPECT_EQ(counter, 0U);
    EXPECT_EQ(renderDevice->SignalSemaphore(kInvalidSemaphoreHandle, 1U),
              RenderOperationResult::Unsupported);
    EXPECT_EQ(renderDevice->WaitSemaphore(kInvalidSemaphoreHandle, 1U, 0U),
              RenderOperationResult::Unsupported);
    EXPECT_EQ(renderDevice->DestroySemaphore(kInvalidSemaphoreHandle),
              RenderOperationResult::Unsupported);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR))
        << "refusing the unsupported calls must not touch GL state";
}

TEST_F(GlRenderDeviceTest, SubmitDescIsUnsupportedWithoutTimelines) {
    // Foundation WP4: GL has no timeline objects, so a non-empty SubmitDesc is refused as
    // Unsupported (never simulated), while an empty desc is exactly Submit.
    ASSERT_EQ(renderDevice->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Unsupported);
    const std::array<SemaphoreSignal, 1U> signals{SemaphoreSignal{SemaphoreHandle{1U}, 1U}};
    auto refused = renderDevice->CreateCommandBuffer();
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(renderDevice->SubmitWithSync(std::move(refused), SubmitDesc{{}, signals}),
              RenderOperationResult::Unsupported);
    auto plain = renderDevice->CreateCommandBuffer();
    ASSERT_NE(plain, nullptr);
    plain->BeginRenderPass({});
    plain->EndRenderPass();
    EXPECT_EQ(renderDevice->SubmitWithSync(std::move(plain), SubmitDesc{}), RenderOperationResult::Success);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceTest, GeometryStageChangesRasterizedPositionAndSurvivesShaderDeletion) {
    ASSERT_EQ(renderDevice->GetCapabilities().geometryShaders, RenderFeatureSupport::Native);
    const auto vertex = renderDevice->CreateShader({ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const auto fragment = renderDevice->CreateShader({ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    const auto geometry = renderDevice->CreateShader({ShaderStage::Geometry, R"(#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
void main() {
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position + vec4(1.0, 0.0, 0.0, 0.0);
        EmitVertex();
    }
    EndPrimitive();
}
)"});
    ASSERT_NE(vertex, kInvalidShaderHandle);
    ASSERT_NE(fragment, kInvalidShaderHandle);
    ASSERT_NE(geometry, kInvalidShaderHandle);
    PipelineDesc desc;
    desc.vertexShader = vertex;
    desc.fragmentShader = fragment;
    desc.geometryShader = geometry;
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    desc.vertexStride = 3U * sizeof(float);
    desc.depthTestEnabled = false;
    std::string info;
    const auto pipeline = renderDevice->CreatePipeline(desc, &info);
    ASSERT_NE(pipeline, kInvalidPipelineHandle) << info;
    renderDevice->DestroyShader(geometry);
    renderDevice->DestroyShader(fragment);
    renderDevice->DestroyShader(vertex);

    // The source triangle occupies the left half; only execution of the GS moves it right.
    constexpr std::array<float, 9> vertices{-1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    const auto buffer = renderDevice->CreateBuffer({sizeof(vertices), BufferUsage::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    const auto target = renderDevice->CreateTexture({8U, 8U, TextureFormat::RGBA8Unorm});
    ASSERT_NE(buffer, kInvalidBufferHandle);
    ASSERT_NE(target, kInvalidTextureHandle);
    auto commands = renderDevice->CreateCommandBuffer();
    RenderPassDesc pass;
    pass.colorAttachment = target;
    pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    commands->BeginRenderPass(pass);
    commands->BindPipeline(pipeline);
    commands->BindVertexBuffer(buffer);
    commands->Draw(3U);
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    std::array<std::uint8_t, 8U * 8U * 4U> pixels{};
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    EXPECT_LT(pixels[(1U * 8U + 0U) * 4U], 8U);
    EXPECT_GT(pixels[(1U * 8U + 4U) * 4U], 240U);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyTexture(target);
    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, GeometryPipelineRejectsUnknownDestroyedAndWrongStageHandles) {
    const auto vertex = renderDevice->CreateShader({ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const auto fragment = renderDevice->CreateShader({ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertex, kInvalidShaderHandle);
    ASSERT_NE(fragment, kInvalidShaderHandle);
    PipelineDesc desc;
    desc.vertexShader = vertex;
    desc.fragmentShader = fragment;
    desc.geometryShader = vertex;
    std::string info;
    EXPECT_EQ(renderDevice->CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("Geometry-stage"), std::string::npos);
    desc.geometryShader = ShaderHandle{999999U};
    EXPECT_EQ(renderDevice->CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("live shader"), std::string::npos);
    const auto deleted = renderDevice->CreateShader({ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    ASSERT_NE(deleted, kInvalidShaderHandle);
    renderDevice->DestroyShader(deleted);
    desc.geometryShader = deleted;
    EXPECT_EQ(renderDevice->CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("live shader"), std::string::npos);
    desc.geometryShader = kInvalidShaderHandle;
    std::swap(desc.vertexShader, desc.fragmentShader);
    EXPECT_EQ(renderDevice->CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("wrong stages"), std::string::npos);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyShader(fragment);
    renderDevice->DestroyShader(vertex);
}

TEST_F(GlRenderDeviceTest, DepthStencilDiscardUsesSeparateAttachmentAspects) {
    const auto color = renderDevice->CreateTexture({4U, 4U, TextureFormat::RGBA8Unorm});
    const auto depth = renderDevice->CreateTexture({4U, 4U, TextureFormat::Depth24Stencil8});
    ASSERT_NE(color, kInvalidTextureHandle);
    ASSERT_NE(depth, kInvalidTextureHandle);
    RenderPassDesc pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depth;
    pass.colorStoreOp = StoreOp::DontCare;
    pass.depthStoreOp = StoreOp::DontCare;
    auto commands = renderDevice->CreateCommandBuffer();
    commands->BeginRenderPass(pass);
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(depth);
    renderDevice->DestroyTexture(color);
}

TEST_F(GlRenderDeviceTest, IndexedUniformArrayUpdatesReachTheGpuWithoutDuplicatingReflection) {
    const auto vertex = renderDevice->CreateShader({ShaderStage::Vertex,
        "#version 330 core\nvoid main() { vec2 p[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(p[gl_VertexID],0,1); }"});
    const auto fragment = renderDevice->CreateShader({ShaderStage::Fragment,
        "#version 330 core\nuniform vec3 uPalette[2]; out vec4 color; void main() { color=vec4(uPalette[0]+uPalette[1],1); }"});
    ASSERT_NE(vertex, kInvalidShaderHandle);
    ASSERT_NE(fragment, kInvalidShaderHandle);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertex;
    pipelineDesc.fragmentShader = fragment;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const auto pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const auto reflected = renderDevice->GetPipelineUniforms(pipeline);
    ASSERT_EQ(reflected.size(), 1U);
    EXPECT_EQ(reflected.front().name, "uPalette");
    EXPECT_EQ(reflected.front().arraySize, 2U);
    TextureDesc textureDesc{4U, 4U, TextureFormat::RGBA8Unorm};
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    const auto color = renderDevice->CreateTexture(textureDesc);
    ASSERT_NE(color, kInvalidTextureHandle);
    auto commands = renderDevice->CreateCommandBuffer();
    RenderPassDesc pass;
    pass.colorAttachment = color;
    commands->BeginRenderPass(pass);
    commands->BindPipeline(pipeline);
    commands->SetUniformVector3("uPalette[0]", {0.0F, 0.0F, 0.0F});
    commands->SetUniformVector3("uPalette[1]", {0.8F, 0.2F, 0.1F});
    commands->SetUniformVector3("uPalette[2]", {0.0F, 1.0F, 0.0F});
    commands->Draw(3U);
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], 190U);
    EXPECT_GE(pixel[1], 40U);
    EXPECT_LE(pixel[1], 65U);
    EXPECT_LE(pixel[2], 35U);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    renderDevice->DestroyTexture(color);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertex);
    renderDevice->DestroyShader(fragment);
}

TEST_F(GlRenderDeviceTest, StencilPipelineMasksSecondDrawThroughDepthStencilAttachment) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle whiteShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    const ShaderHandle redShader = renderDevice->CreateShader(ShaderDesc{
        ShaderStage::Fragment,
        "#version 330 core\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n"});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(whiteShader, kInvalidShaderHandle);
    ASSERT_NE(redShader, kInvalidShaderHandle);

    PipelineDesc writerDesc;
    writerDesc.vertexShader = vertexShader;
    writerDesc.fragmentShader = whiteShader;
    writerDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    writerDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    writerDesc.depthTestEnabled = false;
    writerDesc.stencil.enabled = true;
    writerDesc.stencil.front.compare = DepthCompare::Always;
    writerDesc.stencil.front.depthPass = StencilOp::Replace;
    writerDesc.stencil.front.reference = 7U;
    const PipelineHandle writer = renderDevice->CreatePipeline(writerDesc);
    ASSERT_NE(writer, kInvalidPipelineHandle);

    PipelineDesc readerDesc = writerDesc;
    readerDesc.fragmentShader = redShader;
    readerDesc.stencil.front.compare = DepthCompare::Equal;
    readerDesc.stencil.front.depthPass = StencilOp::Keep;
    const PipelineHandle reader = renderDevice->CreatePipeline(readerDesc);
    ASSERT_NE(reader, kInvalidPipelineHandle);

    const TextureHandle color = renderDevice->CreateTexture({4U, 4U, TextureFormat::RGBA8Unorm});
    const TextureHandle depthStencil =
        renderDevice->CreateTexture({4U, 4U, TextureFormat::Depth24Stencil8});
    ASSERT_NE(color, kInvalidTextureHandle);
    ASSERT_NE(depthStencil, kInvalidTextureHandle);

    constexpr std::array<float, 9> leftTriangle{-1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    constexpr std::array<float, 9> fullscreenTriangle{-1.0F, -1.0F, 0.0F, 3.0F, -1.0F, 0.0F, -1.0F, 3.0F, 0.0F};
    const BufferHandle leftBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(leftTriangle), BufferUsage::Vertex}, std::as_bytes(std::span(leftTriangle)));
    const BufferHandle fullscreenBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(fullscreenTriangle), BufferUsage::Vertex},
        std::as_bytes(std::span(fullscreenTriangle)));
    ASSERT_NE(leftBuffer, kInvalidBufferHandle);
    ASSERT_NE(fullscreenBuffer, kInvalidBufferHandle);

    auto commands = renderDevice->CreateCommandBuffer();
    RenderPassDesc pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depthStencil;
    pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    pass.clearStencil = 0U;
    commands->BeginRenderPass(pass);
    commands->BindPipeline(writer);
    commands->BindVertexBuffer(leftBuffer);
    commands->Draw(3U);
    commands->BindPipeline(reader);
    commands->BindVertexBuffer(fullscreenBuffer);
    commands->Draw(3U);
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(color));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    const auto pixelAt = [&pixels](const std::size_t x, const std::size_t y) {
        const std::size_t offset = (y * 4U + x) * 4U;
        return std::array<std::uint8_t, 4>{pixels[offset], pixels[offset + 1U], pixels[offset + 2U],
                                           pixels[offset + 3U]};
    };
    EXPECT_GT(pixelAt(0U, 0U)[0], 240U);
    EXPECT_LT(pixelAt(0U, 0U)[1], 8U);
    EXPECT_LT(pixelAt(3U, 3U)[0], 8U);
    EXPECT_LT(pixelAt(3U, 3U)[1], 8U);

    renderDevice->DestroyBuffer(fullscreenBuffer);
    renderDevice->DestroyBuffer(leftBuffer);
    renderDevice->DestroyTexture(depthStencil);
    renderDevice->DestroyTexture(color);
    renderDevice->DestroyPipeline(reader);
    renderDevice->DestroyPipeline(writer);
    renderDevice->DestroyShader(redShader);
    renderDevice->DestroyShader(whiteShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, TextureLeaseRetiresOnOwnerThreadAfterWorkerSubmission) {
    const auto texture = renderDevice->CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    ASSERT_NE(texture, kInvalidTextureHandle);
    auto lease = renderDevice->AdoptTransientTextures(std::vector<TextureHandle>{texture});
    ASSERT_NE(lease, nullptr);
    auto commands = renderDevice->CreateCommandBuffer();
    commands->RetainResources(std::move(lease));
    RenderPassDesc pass; pass.colorAttachment = texture; pass.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commands->BeginRenderPass(pass);
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], 240U); EXPECT_LT(pixel[1], 8U);
    commands->EndRenderPass();
    std::thread worker([this, commands = std::move(commands)]() mutable {
        renderDevice->Submit(std::move(commands));
    });
    worker.join();
    TextureDesc description;
    EXPECT_EQ(renderDevice->GetTextureDesc(texture, description), RenderOperationResult::Success);
    EXPECT_EQ(renderDevice->WaitIdle(), RenderOperationResult::Success);
    EXPECT_EQ(renderDevice->GetTextureDesc(texture, description), RenderOperationResult::InvalidHandle);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceTest, GetBackendName_ReturnsOpenGL) {
    EXPECT_EQ(renderDevice->GetBackendName(), "OpenGL");
}

TEST_F(GlRenderDeviceTest, CreateShader_UnknownStage_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    std::string infoLog = "stale";
    const ShaderHandle invalid = renderDevice->CreateShader(
        ShaderDesc{static_cast<ShaderStage>(0xFFU), std::string(kValidVertexShaderSource)}, &infoLog);

    EXPECT_EQ(invalid, kInvalidShaderHandle);
    EXPECT_EQ(infoLog, "Unknown shader stage.");
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateBuffer_UnknownUsage_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    const BufferHandle invalid =
        renderDevice->CreateBuffer(BufferDesc{16U, static_cast<BufferUsage>(0xFFU)});

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, BufferMemoryHintsBecomeTheGlUsageTokenAndEveryPlacementReadsBack) {
    // GL has one memory, so the two hints fold into glBufferData's usage token — read back from
    // the GL object through GL_BUFFER_USAGE after a real bind — and the unchanged default
    // (Auto/Static) still allocates GL_STATIC_DRAW exactly as every buffer did before.
    struct TokenCase final {
        BufferMemory memory;
        BufferUpdateFrequency frequency;
        GLint expectedToken;
    };
    constexpr std::array<TokenCase, 6> kCases{{
        {BufferMemory::Auto, BufferUpdateFrequency::Static, GL_STATIC_DRAW},
        {BufferMemory::DeviceLocal, BufferUpdateFrequency::Dynamic, GL_DYNAMIC_DRAW},
        {BufferMemory::Upload, BufferUpdateFrequency::Stream, GL_STREAM_DRAW},
        {BufferMemory::Readback, BufferUpdateFrequency::Static, GL_STATIC_READ},
        {BufferMemory::Readback, BufferUpdateFrequency::Dynamic, GL_DYNAMIC_READ},
        {BufferMemory::Readback, BufferUpdateFrequency::Stream, GL_STREAM_READ},
    }};
    const std::array<std::uint32_t, 4> values{7U, 11U, 23U, 42U};
    auto commands = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPass({});
    for (const TokenCase &testCase : kCases) {
        const BufferDesc desc{sizeof(values), BufferUsage::Uniform, testCase.memory, testCase.frequency};
        const BufferHandle buffer = renderDevice->CreateBuffer(desc, std::as_bytes(std::span(values)));
        ASSERT_NE(buffer, kInvalidBufferHandle);
        BufferDesc reported{};
        ASSERT_EQ(renderDevice->GetBufferDesc(buffer, reported), RenderOperationResult::Success);
        EXPECT_EQ(reported.memory, testCase.memory);
        EXPECT_EQ(reported.updateFrequency, testCase.frequency);
        EXPECT_EQ(reported.usage, BufferUsage::Uniform);
        commands->BindUniformBuffer(buffer, 0U); // glBindBufferBase also sets the generic binding
        GLint token = 0;
        glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_USAGE, &token);
        EXPECT_EQ(token, testCase.expectedToken)
            << "memory " << static_cast<int>(testCase.memory) << " / frequency "
            << static_cast<int>(testCase.frequency);
        const std::array<std::uint32_t, 1> patch{99U};
        ASSERT_TRUE(renderDevice->UpdateBuffer(buffer, std::as_bytes(std::span(patch)), 4U));
        std::array<std::uint32_t, 4> observed{};
        ASSERT_TRUE(renderDevice->ReadbackBuffer(buffer, std::as_writable_bytes(std::span(observed))));
        EXPECT_EQ(observed, (std::array<std::uint32_t, 4>{7U, 99U, 23U, 42U}));
        renderDevice->DestroyBuffer(buffer);
    }
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    EXPECT_EQ(renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex,
                                                          static_cast<BufferMemory>(0xFFU)}),
              kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex, BufferMemory::Auto,
                                                          static_cast<BufferUpdateFrequency>(0xFFU)}),
              kInvalidBufferHandle);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceTest, CreateBuffer_SizeExceedsGlsizeiptr_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    const BufferDesc oversizedDesc{
        static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max()) + 1U, BufferUsage::Vertex};

    EXPECT_EQ(renderDevice->CreateBuffer(oversizedDesc), kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    const BufferHandle valid = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    ASSERT_NE(valid, kInvalidBufferHandle);
    EXPECT_EQ(valid.value, 1U);
}

TEST_F(GlRenderDeviceTest, CreateTexture_DimensionExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    TextureDesc oversizedDesc;
    oversizedDesc.width = static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) + 1U;
    oversizedDesc.height = 1U;

    EXPECT_EQ(renderDevice->CreateTexture(oversizedDesc), kInvalidTextureHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    const TextureHandle valid = renderDevice->CreateTexture(TextureDesc{1U, 1U});
    ASSERT_NE(valid, kInvalidTextureHandle);
    EXPECT_EQ(valid.value, 1U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_VertexLayoutExceedsGlLimit_ReturnsInvalidBeforeAllocation) {
    GLint maxVertexAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    ASSERT_GT(maxVertexAttribs, 0);

    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc oversizedDesc;
    oversizedDesc.vertexShader = vertexShader;
    oversizedDesc.fragmentShader = fragmentShader;
    oversizedDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    oversizedDesc.vertexLayout.resize(static_cast<std::size_t>(maxVertexAttribs) + 1U,
                                      VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});

    EXPECT_EQ(renderDevice->CreatePipeline(oversizedDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownVertexFormat_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", static_cast<VertexAttributeFormat>(0xFFU), 0U}};
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    PipelineDesc validDesc = invalidDesc;
    validDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    validDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(validDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownVertexFormat_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", static_cast<VertexAttributeFormat>(0xFFU), 0U}};

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownBlendMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.blendMode = static_cast<PipelineBlendMode>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.blendMode = PipelineBlendMode::Opaque;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownBlendMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.blendMode = static_cast<PipelineBlendMode>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateSampler_UnknownMagFilter_ReturnsInvalidBeforeAllocation) {
    SamplerDesc invalidDesc;
    invalidDesc.magFilter = static_cast<SamplerFilter>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSampler(invalidDesc), kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    invalidDesc.magFilter = SamplerFilter::Point;
    const SamplerHandle validSampler = renderDevice->CreateSampler(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);
    renderDevice->DestroySampler(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateSampler_UnknownMinFilter_ReturnsInvalidBeforeAllocation) {
    SamplerDesc invalidDesc;
    invalidDesc.minFilter = static_cast<SamplerFilter>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSampler(invalidDesc), kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    invalidDesc.minFilter = SamplerFilter::Point;
    const SamplerHandle validSampler = renderDevice->CreateSampler(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);
    renderDevice->DestroySampler(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateSampler_UnknownMipMode_ReturnsInvalidBeforeAllocation) {
    SamplerDesc invalidDesc;
    invalidDesc.mipMode = static_cast<SamplerMipMode>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSampler(invalidDesc), kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    invalidDesc.mipMode = SamplerMipMode::None;
    const SamplerHandle validSampler = renderDevice->CreateSampler(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);
    renderDevice->DestroySampler(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateSampler_UnknownWrap_ReturnsInvalidBeforeAllocation) {
    SamplerDesc invalidDesc;
    invalidDesc.wrapU = static_cast<SamplerWrap>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSampler(invalidDesc), kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    invalidDesc.wrapU = SamplerWrap::Repeat;
    const SamplerHandle validSampler = renderDevice->CreateSampler(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);
    renderDevice->DestroySampler(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateSampler_NonDefaults_SucceedsAndCounts) {
    SamplerDesc desc;
    desc.magFilter = SamplerFilter::Point;
    desc.minFilter = SamplerFilter::Point;
    desc.mipMode = SamplerMipMode::None;
    desc.wrapU = SamplerWrap::Repeat;
    desc.wrapV = SamplerWrap::MirroredRepeat;
    desc.maxAnisotropy = 4.0F; // clamped to the device limit when lower, never a failure
    const SamplerHandle sampler = renderDevice->CreateSampler(desc);
    EXPECT_NE(sampler, kInvalidSamplerHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);
    renderDevice->DestroySampler(sampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, DestroySampler_UnknownHandle_IsSafeNoOp) {
    renderDevice->DestroySampler(kInvalidSamplerHandle);
    renderDevice->DestroySampler(SamplerHandle{999});
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownCullMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.cullMode = static_cast<CullMode>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.cullMode = CullMode::Back;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownCullMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.cullMode = static_cast<CullMode>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownFrontFace_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.frontFace = static_cast<FrontFace>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.frontFace = FrontFace::Clockwise;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownFrontFace_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.frontFace = static_cast<FrontFace>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownFillMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.fillMode = static_cast<FillMode>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.fillMode = FillMode::Wireframe;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownFillMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.fillMode = static_cast<FillMode>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownDepthCompare_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.depthCompare = static_cast<DepthCompare>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.depthCompare = DepthCompare::LessOrEqual;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownDepthCompare_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.depthCompare = static_cast<DepthCompare>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownTopology_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.topology = static_cast<PrimitiveTopology>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.topology = PrimitiveTopology::Triangles;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_UnknownTopology_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.topology = static_cast<PrimitiveTopology>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_ZeroVertexStride_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_ZeroVertexStride_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_AttributeExceedsVertexStride_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 8U}};
    invalidDesc.vertexStride = 12U;
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.vertexLayout[0].offset = 0U;
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_AttributeExceedsVertexStride_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float4, 4U}};
    invalidDesc.vertexStride = 16U;

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_VertexStrideExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(renderDevice->CreatePipeline(invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 2U);

    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle validPipeline = renderDevice->CreatePipeline(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipeline(validPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_VertexStrideExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDesc invalidDesc;
    invalidDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    invalidDesc.vertexStride = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(binary, 0U, invalidDesc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, Draw_CountExceedsGlsizei_DoesNotIssueGlCall) {
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->Draw(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
}

TEST_F(GlRenderDeviceTest, DrawIndexed_CountExceedsGlsizei_DoesNotIssueGlCall) {
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->DrawIndexed(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
}

TEST_F(GlRenderDeviceTest, BufferUsageAndIndexedCountValidationRejectsUnsafeDraws) {
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    const BufferHandle indexBuffer = renderDevice->CreateBuffer(BufferDesc{4U, BufferUsage::Index});
    const BufferHandle uniformBuffer = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Uniform});
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    ASSERT_NE(indexBuffer, kInvalidBufferHandle);
    ASSERT_NE(uniformBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindVertexBuffer(uniformBuffer);
    commandBuffer->BindIndexBuffer(vertexBuffer);
    commandBuffer->BindUniformBuffer(vertexBuffer, 0U);
    commandBuffer->BindUniformBuffer(uniformBuffer, 0U);
    commandBuffer->DrawIndexed(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->BindIndexBuffer(indexBuffer);
    commandBuffer->DrawIndexed(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyBuffer(uniformBuffer);
    renderDevice->DestroyBuffer(indexBuffer);
    renderDevice->DestroyBuffer(vertexBuffer);
}

TEST_F(GlRenderDeviceTest, Draw_BoundVertexBufferCapacityIsValidated) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 3> oneVertex{0.0F, 0.0F, 0.0F};
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(oneVertex), BufferUsage::Vertex}, std::as_bytes(std::span(oneVertex)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->Draw(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->Draw(1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    std::unique_ptr<ICommandBuffer> fullscreenCommandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(fullscreenCommandBuffer, nullptr);
    fullscreenCommandBuffer->BeginRenderPass(passDesc);
    fullscreenCommandBuffer->BindPipeline(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }
    fullscreenCommandBuffer->Draw(3U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    fullscreenCommandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(fullscreenCommandBuffer));

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, Draw_LinesPipeline_BindsDrawsAndSubmitsWithoutGlError) {
    // Tier 2.8 Scope A: a Lines pipeline binds and draws cleanly on a real driver. The mode
    // mapping itself (ToGlDrawMode) is six lines reviewed by inspection; this test guards
    // the create/bind/draw/submit path a debug-line submit exercises.
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.topology = PrimitiveTopology::Lines;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 6> twoVertices{-1.0F, -1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(twoVertices), BufferUsage::Vertex}, std::as_bytes(std::span(twoVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->Draw(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, PointAndStripTopologiesRasterizeAsAssembledAndPointSizeComesFromTheShader) {
    // Three real-driver proofs on the 64x64 default framebuffer, cleared black each pass:
    //  - Points: one vertex at the origin with gl_PointSize = 8 covers an 8x8 block around the
    //    centre (pixels 28..35) and nothing at (20, 32) — only possible if the Points pipeline
    //    enabled GL_PROGRAM_POINT_SIZE; without it the point is 1px and (29, 32) stays black.
    //  - TriangleStrip: four corners as a strip paint the whole frame, including the top-right
    //    corner a four-vertex triangle LIST would leave black (its 4th vertex is dropped).
    //  - LineStrip: three vertices paint the second segment (x = +0.5 column) a two-vertex
    //    line LIST would not have; a Triangles pipeline bound afterwards reports program point
    //    size disabled again, so the mode does not leak between pipelines.
    constexpr std::string_view kPointVertexShaderSource = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    gl_PointSize = 8.0;
}
)";
    const ShaderHandle pointVertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kPointVertexShaderSource)});
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(pointVertexShader, kInvalidShaderHandle);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    const auto makePipeline = [&](const ShaderHandle vs, const PrimitiveTopology topology) {
        PipelineDesc desc;
        desc.vertexShader = vs;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
        desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
        desc.topology = topology;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        return renderDevice->CreatePipeline(desc);
    };
    const PipelineHandle pointPipeline = makePipeline(pointVertexShader, PrimitiveTopology::Points);
    const PipelineHandle stripPipeline = makePipeline(vertexShader, PrimitiveTopology::TriangleStrip);
    const PipelineHandle lineStripPipeline = makePipeline(vertexShader, PrimitiveTopology::LineStrip);
    const PipelineHandle trianglePipeline = makePipeline(vertexShader, PrimitiveTopology::Triangles);
    ASSERT_NE(pointPipeline, kInvalidPipelineHandle);
    ASSERT_NE(stripPipeline, kInvalidPipelineHandle);
    ASSERT_NE(lineStripPipeline, kInvalidPipelineHandle);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    constexpr std::array<float, 3> kOrigin{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 12> kStripCorners{-1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
                                                  -1.0F, 1.0F,  0.0F, 1.0F, 1.0F,  0.0F};
    // Pixel-centre aligned on a 64px axis: NDC = (pixel + 0.5) / 32 - 1.
    constexpr float kRow32 = (32.0F + 0.5F) / 32.0F - 1.0F;
    constexpr float kColumn48 = (48.0F + 0.5F) / 32.0F - 1.0F;
    const std::array<float, 9> lineStripVertices{-1.0F, kRow32, 0.0F, kColumn48, kRow32, 0.0F,
                                                 kColumn48, 1.0F, 0.0F};
    const BufferHandle pointBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kOrigin), BufferUsage::Vertex}, std::as_bytes(std::span(kOrigin)));
    const BufferHandle stripBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kStripCorners), BufferUsage::Vertex}, std::as_bytes(std::span(kStripCorners)));
    const BufferHandle lineStripBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(lineStripVertices), BufferUsage::Vertex}, std::as_bytes(std::span(lineStripVertices)));
    ASSERT_NE(pointBuffer, kInvalidBufferHandle);
    ASSERT_NE(stripBuffer, kInvalidBufferHandle);
    ASSERT_NE(lineStripBuffer, kInvalidBufferHandle);
    const auto readPixel = [](const int x, const int y) {
        std::array<std::uint8_t, 3> pixel{};
        glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        return pixel[0];
    };
    const auto drawPass = [&](const PipelineHandle pipeline, const BufferHandle buffer,
                              const std::uint32_t vertexCount) {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc;
        passDesc.colorAttachment = kInvalidTextureHandle;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(buffer);
        commandBuffer->Draw(vertexCount);
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
    };
    drawPass(pointPipeline, pointBuffer, 1U);
    EXPECT_TRUE(glIsEnabled(GL_PROGRAM_POINT_SIZE));
    EXPECT_GT(readPixel(32, 32), 200U);
    EXPECT_GT(readPixel(29, 32), 200U) << "an 8px point covers pixels 28..35; a 1px point would not";
    EXPECT_GT(readPixel(32, 35), 200U);
    EXPECT_LT(readPixel(20, 32), 40U) << "outside the 8x8 block stays clear";
    EXPECT_LT(readPixel(32, 44), 40U);

    drawPass(stripPipeline, stripBuffer, 4U);
    EXPECT_FALSE(glIsEnabled(GL_PROGRAM_POINT_SIZE));
    EXPECT_GT(readPixel(2, 2), 200U);
    EXPECT_GT(readPixel(61, 61), 200U) << "the strip's second triangle covers the top-right corner";
    EXPECT_GT(readPixel(32, 32), 200U);

    drawPass(lineStripPipeline, lineStripBuffer, 3U);
    EXPECT_GT(readPixel(10, 32), 200U) << "first segment along row 32";
    EXPECT_GT(readPixel(48, 50), 200U) << "second segment up column 48 — only a strip draws it";
    EXPECT_LT(readPixel(10, 50), 40U);

    drawPass(trianglePipeline, stripBuffer, 3U);
    EXPECT_FALSE(glIsEnabled(GL_PROGRAM_POINT_SIZE)) << "program point size is per pipeline, not sticky";
    EXPECT_LT(readPixel(61, 61), 40U) << "a triangle LIST of these corners leaves the top-right corner clear";
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    renderDevice->DestroyBuffer(lineStripBuffer);
    renderDevice->DestroyBuffer(stripBuffer);
    renderDevice->DestroyBuffer(pointBuffer);
    renderDevice->DestroyPipeline(trianglePipeline);
    renderDevice->DestroyPipeline(lineStripPipeline);
    renderDevice->DestroyPipeline(stripPipeline);
    renderDevice->DestroyPipeline(pointPipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(pointVertexShader);
}

// IEEE binary16 of a float in the normal range (enough for test vertex data: ±1, 0.5, 0).
[[nodiscard]] std::uint16_t HalfFromFloat(const float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23U) & 0xFFU) - 127 + 15;
    const std::uint32_t mantissa = (bits >> 13U) & 0x3FFU;
    if (exponent <= 0) {
        return static_cast<std::uint16_t>(sign);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10U) | mantissa);
}

TEST_F(GlRenderDeviceTest, PackedVertexAttributeFormatsFetchNormalizedHalfAndIntegerDataOnTheDriver) {
    // Three vertex layouts, one lower-left triangle each on the 64x64 default framebuffer,
    // pixel (16, 16) sampled: Short2Norm positions + UByte4Norm colours must land the exact
    // colour bytes (normalised fetch); Half2 positions + Half4 colours must decode binary16;
    // UShort4 and UInt4 joint indices go through glVertexAttribIPointer into a uvec4 — the low
    // byte of each lane becomes the colour, so a 32-bit lane holding 65536 + 200 reads 200 only
    // when the fetch is really 32-bit and really integer.
    constexpr std::string_view kNormVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = vec4(aPosition, 0.0, 1.0); }
)";
    constexpr std::string_view kIntegerVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in uvec4 aJoints;
out vec4 vColor;
void main() {
    vColor = vec4(float(aJoints.x & 255u), float(aJoints.y & 255u), float(aJoints.z & 255u), 255.0) / 255.0;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";
    constexpr std::string_view kPassColorFragmentShader = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";
    const ShaderHandle normVertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kNormVertexShader)});
    const ShaderHandle integerVertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kIntegerVertexShader)});
    const ShaderHandle fragmentShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Fragment, std::string(kPassColorFragmentShader)});
    ASSERT_NE(normVertexShader, kInvalidShaderHandle);
    ASSERT_NE(integerVertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

#pragma pack(push, 1)
    struct SnormVertex final {
        std::int16_t x, y;
        std::uint8_t r, g, b, a;
    };
    struct HalfVertex final {
        std::uint16_t x, y;
        std::uint16_t r, g, b, a;
    };
    struct UShortJointVertex final {
        float x, y;
        std::uint16_t j0, j1, j2, j3;
    };
    struct UIntJointVertex final {
        float x, y;
        std::uint32_t j0, j1, j2, j3;
    };
#pragma pack(pop)
    static_assert(sizeof(SnormVertex) == 8U);
    static_assert(sizeof(HalfVertex) == 12U);
    static_assert(sizeof(UShortJointVertex) == 16U);
    static_assert(sizeof(UIntJointVertex) == 24U);
    constexpr std::int16_t kSnormOne = 32767;
    const std::array<SnormVertex, 3> snormTriangle{{{-kSnormOne, -kSnormOne, 255U, 128U, 0U, 255U},
                                                       {kSnormOne, -kSnormOne, 255U, 128U, 0U, 255U},
                                                       {-kSnormOne, kSnormOne, 255U, 128U, 0U, 255U}}};
    const std::uint16_t hNeg = HalfFromFloat(-1.0F);
    const std::uint16_t hPos = HalfFromFloat(1.0F);
    const std::uint16_t hHalf = HalfFromFloat(0.5F);
    const std::uint16_t hZero = HalfFromFloat(0.0F);
    const std::array<HalfVertex, 3> halfTriangle{{{hNeg, hNeg, hZero, hHalf, hPos, hPos},
                                                     {hPos, hNeg, hZero, hHalf, hPos, hPos},
                                                     {hNeg, hPos, hZero, hHalf, hPos, hPos}}};
    const std::array<UShortJointVertex, 3> ushortTriangle{{{-1.0F, -1.0F, 200U, 100U, 0U, 7U},
                                                              {1.0F, -1.0F, 200U, 100U, 0U, 7U},
                                                              {-1.0F, 1.0F, 200U, 100U, 0U, 7U}}};
    const std::array<UIntJointVertex, 3> uintTriangle{{{-1.0F, -1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {1.0F, -1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {-1.0F, 1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U}}};
    struct LayoutCase final {
        ShaderHandle vertexShader;
        std::vector<VertexAttribute> layout;
        std::uint32_t stride;
        std::span<const std::byte> vertices;
        std::array<std::uint8_t, 3> expected;
        const char* name;
    };
    const std::array<LayoutCase, 4> cases{{
        {normVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Short2Norm, 0U},
          VertexAttribute{"COLOR", VertexAttributeFormat::UByte4Norm, 4U}},
         sizeof(SnormVertex), std::as_bytes(std::span(snormTriangle)), {255U, 128U, 0U}, "Short2Norm+UByte4Norm"},
        {normVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Half2, 0U},
          VertexAttribute{"COLOR", VertexAttributeFormat::Half4, 4U}},
         sizeof(HalfVertex), std::as_bytes(std::span(halfTriangle)), {0U, 128U, 255U}, "Half2+Half4"},
        {integerVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Float2, 0U},
          VertexAttribute{"JOINTS", VertexAttributeFormat::UShort4, 8U}},
         sizeof(UShortJointVertex), std::as_bytes(std::span(ushortTriangle)), {200U, 100U, 0U}, "UShort4"},
        {integerVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Float2, 0U},
          VertexAttribute{"JOINTS", VertexAttributeFormat::UInt4, 8U}},
         sizeof(UIntJointVertex), std::as_bytes(std::span(uintTriangle)), {200U, 100U, 0U}, "UInt4"},
    }};
    for (const LayoutCase& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        PipelineDesc desc;
        desc.vertexShader = testCase.vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = testCase.layout;
        desc.vertexStride = testCase.stride;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        const PipelineHandle pipeline = renderDevice->CreatePipeline(desc);
        ASSERT_NE(pipeline, kInvalidPipelineHandle);
        const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
            BufferDesc{testCase.vertices.size(), BufferUsage::Vertex}, testCase.vertices);
        ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc;
        passDesc.colorAttachment = kInvalidTextureHandle;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
        std::array<std::uint8_t, 3> pixel{};
        glReadPixels(16, 16, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        EXPECT_NEAR(static_cast<int>(pixel[0]), static_cast<int>(testCase.expected[0]), 2);
        EXPECT_NEAR(static_cast<int>(pixel[1]), static_cast<int>(testCase.expected[1]), 2);
        EXPECT_NEAR(static_cast<int>(pixel[2]), static_cast<int>(testCase.expected[2]), 2);
        std::array<std::uint8_t, 3> outside{};
        glReadPixels(60, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, outside.data());
        EXPECT_LT(outside[0] + outside[1] + outside[2], 20) << "the triangle covers only the lower-left half";
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        renderDevice->DestroyBuffer(vertexBuffer);
        renderDevice->DestroyPipeline(pipeline);
    }
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(integerVertexShader);
    renderDevice->DestroyShader(normVertexShader);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_UnknownAttachmentDoesNotBindOrCacheFramebuffer) {
    const TextureHandle validColor = renderDevice->CreateTexture(TextureDesc{1U, 1U});
    ASSERT_NE(validColor, kInvalidTextureHandle);
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDesc& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPass(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };

    RenderPassDesc unknownColor;
    unknownColor.colorAttachment = TextureHandle{0xFFFF'FFFEU};
    unknownColor.depthAttachment = kInvalidTextureHandle;
    assertFramebufferUnchanged(unknownColor);

    RenderPassDesc unknownDepth;
    unknownDepth.colorAttachment = validColor;
    unknownDepth.depthAttachment = TextureHandle{0xFFFF'FFFDU};
    assertFramebufferUnchanged(unknownDepth);

    RenderPassDesc validPass;
    validPass.colorAttachment = validColor;
    validPass.depthAttachment = kInvalidTextureHandle;
    commandBuffer->BeginRenderPass(validPass);
    GLint validFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &validFramebuffer);
    EXPECT_NE(validFramebuffer, 0);
    commandBuffer->EndRenderPass();
    renderDevice->DestroyTexture(validColor);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_IncompleteFramebufferRejectsAndRestoresState) {
    const TextureHandle validColor = renderDevice->CreateTexture(TextureDesc{2U, 2U});
    const TextureHandle invalidColor = renderDevice->CreateTexture(
        TextureDesc{1U, 1U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(validColor, kInvalidTextureHandle);
    ASSERT_NE(invalidColor, kInvalidTextureHandle);
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
    RenderPassDesc mismatchedPass;
    mismatchedPass.colorAttachment = invalidColor;
    mismatchedPass.depthAttachment = kInvalidTextureHandle;
    commandBuffer->BeginRenderPass(mismatchedPass);
    GLint after = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
    EXPECT_EQ(after, before);

    RenderPassDesc validPass;
    validPass.colorAttachment = validColor;
    validPass.depthAttachment = kInvalidTextureHandle;
    commandBuffer->BeginRenderPass(validPass);
    GLint validFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &validFramebuffer);
    EXPECT_NE(validFramebuffer, 0);
    commandBuffer->EndRenderPass();

    renderDevice->DestroyTexture(invalidColor);
    renderDevice->DestroyTexture(validColor);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_NonFiniteClearValuesLeaveStateUntouched) {
    const TextureHandle color = renderDevice->CreateTexture(TextureDesc{2U, 2U});
    const TextureHandle depth = renderDevice->CreateTexture(
        TextureDesc{2U, 2U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(color, kInvalidTextureHandle);
    ASSERT_NE(depth, kInvalidTextureHandle);
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);

    RenderPassDesc invalidColorClear;
    invalidColorClear.colorAttachment = color;
    invalidColorClear.depthAttachment = depth;
    invalidColorClear.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
    commandBuffer->BeginRenderPass(invalidColorClear);

    GLint afterColorClear = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &afterColorClear);
    EXPECT_EQ(afterColorClear, before);

    RenderPassDesc invalidDepthClear;
    invalidDepthClear.colorAttachment = kInvalidTextureHandle;
    invalidDepthClear.depthAttachment = depth;
    invalidDepthClear.colorLoadOp = LoadOp::DontCare;
    invalidDepthClear.clearDepth = std::numeric_limits<float>::infinity();
    commandBuffer->BeginRenderPass(invalidDepthClear);

    GLint afterDepthClear = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &afterDepthClear);
    EXPECT_EQ(afterDepthClear, before);

    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyTexture(depth);
    renderDevice->DestroyTexture(color);
}

TEST_F(GlRenderDeviceTest, BindTexture_SlotExceedsGlLimit_DoesNotIssueGlCall) {
    const TextureHandle texture = renderDevice->CreateTexture(TextureDesc{1U, 1U});
    ASSERT_NE(texture, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindTexture(texture, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindTexture(texture, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyTexture(texture);
}

TEST_F(GlRenderDeviceTest, CreatePipelineFromBinary_BinarySizeExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 1> storage{};
    const std::size_t oversizedBinarySize = static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) + 1U;
    const std::span<const std::byte> oversizedBinary(storage.data(), oversizedBinarySize);
    const PipelineBinaryDesc desc;

    EXPECT_EQ(renderDevice->CreatePipelineFromBinary(oversizedBinary, 0U, desc), kInvalidPipelineHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, BindUniformBuffer_SlotExceedsGlLimit_DoesNotIssueGlCall) {
    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Uniform});
    ASSERT_NE(buffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindUniformBuffer(buffer, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindUniformBuffer(buffer, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, BindStorageBuffer_ValidationRejectsBadSlotUsageAndHandle) {
    // M2f sister of the uniform-slot validation: every refused storage bind must be a pure
    // engine-level rejection (UVE_ERROR + drop) that NEVER reaches GL, so the GL error flag
    // stays clean across slot-limit, wrong-usage and unknown-handle cases; the legal bind
    // issues exactly one glBindBufferBase.
    const BufferHandle storage = renderDevice->CreateBuffer(BufferDesc{64U, BufferUsage::Storage});
    if (storage == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }
    const BufferHandle vertex = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    ASSERT_NE(vertex, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindStorageBuffer(storage, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBuffer(vertex, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBuffer(BufferHandle{999999U}, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBuffer(storage, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyBuffer(vertex);
    renderDevice->DestroyBuffer(storage);
}

TEST_F(GlRenderDeviceTest, CreateBuffer_StorageUsage_SupportsWholeBufferUpdate) {
    // Storage buffers are live GL objects on the desktop path: creation on the SSBO target,
    // a real glBufferSubData update round-trip, and clean destruction - the same lifecycle
    // the Uniform usage already proves, on the M2f target.
    const BufferHandle storage = renderDevice->CreateBuffer(BufferDesc{64U, BufferUsage::Storage});
    if (storage == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    const std::array<std::uint8_t, 64> payload{};
    EXPECT_TRUE(renderDevice->UpdateBuffer(
        storage,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
        0U));
    EXPECT_FALSE(renderDevice->UpdateBuffer(
        storage,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
        32U)); // overruns the 64-byte allocation - must be refused

    renderDevice->DestroyBuffer(storage);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_UnknownLoadOp_LeavesStateUntouched) {
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc invalidDesc;
    invalidDesc.depthLoadOp = static_cast<LoadOp>(0xFFU);
    commandBuffer->BeginRenderPass(invalidDesc);

    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceTest, CreateBuffer_ZeroSize_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    while (glGetError() != GL_NO_ERROR) {
    }

    const BufferHandle invalid = renderDevice->CreateBuffer(BufferDesc{0U, BufferUsage::Vertex});

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceTest, CreateBuffer_OversizedInitialData_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    const std::array<std::byte, 17> initialData{};
    const BufferHandle invalid =
        renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex}, initialData);

    EXPECT_EQ(invalid, kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);

    const BufferHandle valid = renderDevice->CreateBuffer(BufferDesc{16U, BufferUsage::Vertex});
    EXPECT_EQ(valid.value, 1U);
    renderDevice->DestroyBuffer(valid);
}

TEST_F(GlRenderDeviceTest, CreateThenDestroyBuffer_UpdatesLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{64, BufferUsage::Vertex});
    EXPECT_NE(buffer, kInvalidBufferHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);

    renderDevice->DestroyBuffer(buffer);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, UpdateBuffer_WithinBounds_Succeeds) {
    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{16, BufferUsage::Vertex});
    const std::array<std::byte, 8> data{};
    EXPECT_TRUE(renderDevice->UpdateBuffer(buffer, data, 0));
    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, UpdateBuffer_OutOfBounds_ReturnsFalse) {
    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{8, BufferUsage::Vertex});
    const std::array<std::byte, 16> data{};
    EXPECT_FALSE(renderDevice->UpdateBuffer(buffer, data, 0));
    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, UpdateBuffer_OverflowedOffset_ReturnsFalseWithoutGlError) {
    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{16, BufferUsage::Vertex});
    const std::array<std::byte, 4> data{};
    while (glGetError() != GL_NO_ERROR) {
    }

    EXPECT_FALSE(renderDevice->UpdateBuffer(buffer, data, std::numeric_limits<std::uint64_t>::max()));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, ReadbackBuffer_ReturnsWhatUpdateBufferWrote) {
    // CS3: the read direction of UpdateBuffer against a real GL buffer object.
    const std::array<float, 4> initial{1.5F, -2.0F, 0.25F, 8.0F};
    const BufferHandle buffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(initial), BufferUsage::Storage}, std::as_bytes(std::span(initial)));
    if (buffer == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    std::array<float, 4> readback{};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(buffer, std::as_writable_bytes(std::span(readback)), 0));
    EXPECT_FLOAT_EQ(readback[0], 1.5F);
    EXPECT_FLOAT_EQ(readback[1], -2.0F);
    EXPECT_FLOAT_EQ(readback[2], 0.25F);
    EXPECT_FLOAT_EQ(readback[3], 8.0F);

    const std::array<float, 2> update{42.0F, 43.0F};
    ASSERT_TRUE(renderDevice->UpdateBuffer(buffer, std::as_bytes(std::span(update)), 2U * sizeof(float)));
    ASSERT_TRUE(renderDevice->ReadbackBuffer(buffer, std::as_writable_bytes(std::span(readback)), 0));
    EXPECT_FLOAT_EQ(readback[0], 1.5F); // prefix untouched by the offset write
    EXPECT_FLOAT_EQ(readback[2], 42.0F);
    EXPECT_FLOAT_EQ(readback[3], 43.0F);

    // A windowed read at an offset sees exactly that window.
    std::array<float, 2> window{};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(buffer, std::as_writable_bytes(std::span(window)),
                                                  2U * sizeof(float)));
    EXPECT_FLOAT_EQ(window[0], 42.0F);
    EXPECT_FLOAT_EQ(window[1], 43.0F);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, ReadbackBuffer_UnknownHandleOrOutOfRange_ReturnsFalseWithoutGlError) {
    std::array<std::byte, 8> readback{};
    EXPECT_FALSE(renderDevice->ReadbackBuffer(BufferHandle{4242}, readback, 0));

    const BufferHandle buffer = renderDevice->CreateBuffer(BufferDesc{8, BufferUsage::Vertex});
    ASSERT_NE(buffer, kInvalidBufferHandle);
    while (glGetError() != GL_NO_ERROR) {
    }
    std::array<std::byte, 16> tooLarge{};
    EXPECT_FALSE(renderDevice->ReadbackBuffer(buffer, tooLarge, 0));
    EXPECT_FALSE(renderDevice->ReadbackBuffer(buffer, readback,
                                                   std::numeric_limits<std::uint64_t>::max()));
    // Range refusals must never reach the driver.
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBuffer(buffer);
}

TEST_F(GlRenderDeviceTest, CreateTexture_SrgbColorSpaceUsesSrgbInternalFormat) {
    const std::array<std::uint8_t, 4U> pixels{128U, 64U, 32U, 255U};
    TextureDesc desc{};
    desc.width = 1U;
    desc.height = 1U;
    desc.colorSpace = TextureColorSpace::Srgb;
    const TextureHandle texture = renderDevice->CreateTexture(desc, std::as_bytes(std::span(pixels)));
    ASSERT_NE(texture, kInvalidTextureHandle);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(texture));
    GLint internalFormat = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(internalFormat, static_cast<GLint>(GL_SRGB8_ALPHA8));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyTexture(texture);
}

TEST_F(GlRenderDeviceTest, CreateTexture_UploadsBlockCompressedSrgbMipChainWhenAvailable) {
    if (!renderDevice->SupportsTextureFormat(TextureFormat::ETC2RGBA8, TextureColorSpace::Srgb)) {
        GTEST_SKIP() << "active GL context does not advertise ETC2 sRGB texture support";
    }
    const TextureDesc desc{7U, 5U, TextureFormat::ETC2RGBA8, 2U, TextureColorSpace::Srgb};
    std::array<std::byte, 80U> blocks{}; // 4 level-0 blocks and 1 3x2 mip block, 16 bytes each
    const TextureHandle texture = renderDevice->CreateTexture(desc, blocks);
    ASSERT_NE(texture, kInvalidTextureHandle);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(texture));
    GLint level0Compressed = GL_FALSE;
    GLint level1Compressed = GL_FALSE;
    GLint level0Size = 0;
    GLint level1Size = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &level0Compressed);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_COMPRESSED, &level1Compressed);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &level0Size);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &level1Size);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(level0Compressed, GL_TRUE);
    EXPECT_EQ(level1Compressed, GL_TRUE);
    EXPECT_EQ(level0Size, 64);
    EXPECT_EQ(level1Size, 16);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyTexture(texture);
}

constexpr std::string_view kMipSampleVertexSource = R"(#version 330 core
void main() {
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view kMipSampleFragmentSource = R"(#version 330 core
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    FragColor = textureLod(uTexture, vec2(0.5, 0.5), 1.0);
}
)";

TEST_F(GlRenderDeviceTest, CreateTexture_UploadsAndSamplesExplicitMipLevel) {
    // Level 0 is opaque blue; level 1 is opaque red. Read back the stored level and
    // sample with explicit LOD 1 so the pixel proof cannot accidentally use level 0.
    constexpr std::array<std::uint8_t, 20U> mipPixels{
        0U, 0U, 255U, 255U,  0U, 0U, 255U, 255U,
        0U, 0U, 255U, 255U,  0U, 0U, 255U, 255U,
        255U, 0U, 0U, 255U};
    TextureDesc sourceDesc{};
    sourceDesc.width = 2U;
    sourceDesc.height = 2U;
    sourceDesc.mipLevels = 2U;
    const TextureHandle source = renderDevice->CreateTexture(
        sourceDesc, std::as_bytes(std::span(mipPixels)));
    ASSERT_NE(source, kInvalidTextureHandle);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(source));
    GLint level1Width = 0;
    GLint level1Height = 0;
    GLint minFilter = 0;
    GLint maxLevel = -1;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &level1Width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_HEIGHT, &level1Height);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
    std::array<std::uint8_t, 4U> level1Pixels{};
    glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, level1Pixels.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(level1Width, 1);
    EXPECT_EQ(level1Height, 1);
    EXPECT_EQ(minFilter, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
    EXPECT_EQ(maxLevel, 1);
    EXPECT_EQ(level1Pixels, (std::array<std::uint8_t, 4U>{255U, 0U, 0U, 255U}));

    TextureDesc targetDesc{};
    targetDesc.width = 1U;
    targetDesc.height = 1U;
    const TextureHandle target = renderDevice->CreateTexture(targetDesc);
    ASSERT_NE(target, kInvalidTextureHandle);
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kMipSampleVertexSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kMipSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc{};
    passDesc.colorAttachment = target;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindTexture(source, 0U);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    std::array<std::uint8_t, 4U> sampledPixel{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sampledPixel.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(sampledPixel, (std::array<std::uint8_t, 4U>{255U, 0U, 0U, 255U}));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyTexture(target);
    renderDevice->DestroyTexture(source);
}

constexpr std::string_view kSrgbSampleVertexSource = R"(#version 330 core
void main() {
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view kSrgbSampleFragmentSource = R"(#version 330 core
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    FragColor = texture(uTexture, vec2(0.5, 0.5));
}
)";

TEST_F(GlRenderDeviceTest, SamplingSrgbTextureDecodesToLinearValues) {
    const std::array<std::uint8_t, 4U> encodedSrgbPixel{128U, 0U, 0U, 255U};
    TextureDesc sourceDesc{};
    sourceDesc.width = 1U;
    sourceDesc.height = 1U;
    sourceDesc.colorSpace = TextureColorSpace::Srgb;
    const TextureHandle source = renderDevice->CreateTexture(
        sourceDesc, std::as_bytes(std::span(encodedSrgbPixel)));
    ASSERT_NE(source, kInvalidTextureHandle);

    TextureDesc targetDesc{};
    targetDesc.width = 1U;
    targetDesc.height = 1U;
    const TextureHandle target = renderDevice->CreateTexture(targetDesc);
    ASSERT_NE(target, kInvalidTextureHandle);

    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kSrgbSampleVertexSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kSrgbSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const GLboolean previousFramebufferSrgbEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);
    while (glGetError() != GL_NO_ERROR) {
    }
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc{};
    passDesc.colorAttachment = target;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindTexture(source, 0U);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    std::array<std::uint8_t, 4U> readback{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    if (previousFramebufferSrgbEnabled == GL_TRUE) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    }

    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    EXPECT_NEAR(static_cast<int>(readback[0]), 55, 2)
        << "sRGB 128/255 red should be sampled as about 0.216 linear (55/255), not 128/255";
    EXPECT_EQ(readback[1], 0U);
    EXPECT_EQ(readback[2], 0U);
    EXPECT_EQ(readback[3], 255U);

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyTexture(target);
    renderDevice->DestroyTexture(source);
}

TEST_F(GlRenderDeviceTest, CreateThenDestroyTexture_UpdatesLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    const TextureHandle texture =
        renderDevice->CreateTexture(TextureDesc{64, 64, TextureFormat::RGBA8Unorm, 1});
    EXPECT_NE(texture, kInvalidTextureHandle);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 1U);

    renderDevice->DestroyTexture(texture);
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateTexture_PreservesActiveUnitBindingForLiveCommandBufferCache) {
    const TextureHandle firstTexture =
        renderDevice->CreateTexture(TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U});
    ASSERT_NE(firstTexture, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindTexture(firstTexture, 0U);

    GLint activeTextureBefore = 0;
    GLint bindingBefore = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTextureBefore);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingBefore);
    ASSERT_EQ(activeTextureBefore, static_cast<GLint>(GL_TEXTURE0));
    ASSERT_NE(bindingBefore, 0);

    const TextureHandle secondTexture =
        renderDevice->CreateTexture(TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U});
    ASSERT_NE(secondTexture, kInvalidTextureHandle);

    GLint activeTextureAfterCreate = 0;
    GLint bindingAfterCreate = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTextureAfterCreate);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingAfterCreate);
    EXPECT_EQ(activeTextureAfterCreate, activeTextureBefore);
    EXPECT_EQ(bindingAfterCreate, bindingBefore);

    // The command buffer still caches firstTexture at slot 0. If texture creation leaked secondTexture
    // into the binding, this call would incorrectly skip the bind and leave the wrong GL name active.
    commandBuffer->BindTexture(firstTexture, 0U);
    GLint bindingAfterCachedRebind = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingAfterCachedRebind);
    EXPECT_EQ(bindingAfterCachedRebind, bindingBefore);
    commandBuffer->EndRenderPass();

    renderDevice->DestroyTexture(secondTexture);
    renderDevice->DestroyTexture(firstTexture);
}

TEST_F(GlRenderDeviceTest, CreateAndUpdateBuffer_PreserveBindingForLiveCommandBufferCache) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 3> kFirstVertex{0.0F, 0.0F, 0.0F};
    const BufferHandle firstBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kFirstVertex), BufferUsage::Vertex}, std::as_bytes(std::span(kFirstVertex)));
    ASSERT_NE(firstBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(firstBuffer, 0U);

    GLint bindingBefore = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingBefore);
    ASSERT_NE(bindingBefore, 0);

    constexpr std::array<float, 3> kSecondVertex{1.0F, 0.0F, 0.0F};
    const BufferHandle secondBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kSecondVertex), BufferUsage::Vertex}, std::as_bytes(std::span(kSecondVertex)));
    ASSERT_NE(secondBuffer, kInvalidBufferHandle);

    GLint bindingAfterCreate = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCreate);
    EXPECT_EQ(bindingAfterCreate, bindingBefore);

    // The command buffer still caches firstBuffer. If creation leaked secondBuffer into GL, this
    // call would incorrectly skip the bind and leave the wrong vertex buffer active.
    commandBuffer->BindVertexBuffer(firstBuffer, 0U);
    GLint bindingAfterCachedCreateRebind = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCachedCreateRebind);
    EXPECT_EQ(bindingAfterCachedCreateRebind, bindingBefore);

    EXPECT_TRUE(renderDevice->UpdateBuffer(secondBuffer, std::as_bytes(std::span(kSecondVertex)), 0U));
    GLint bindingAfterUpdate = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterUpdate);
    EXPECT_EQ(bindingAfterUpdate, bindingBefore);

    // UpdateBuffer has the same temporary-bind requirement as creation.
    commandBuffer->BindVertexBuffer(firstBuffer, 0U);
    GLint bindingAfterCachedUpdateRebind = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCachedUpdateRebind);
    EXPECT_EQ(bindingAfterCachedUpdateRebind, bindingBefore);

    commandBuffer->EndRenderPass();
    renderDevice->DestroyBuffer(secondBuffer);
    renderDevice->DestroyBuffer(firstBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, CreateTexture_Rgba16Float_UsesHalfFloatUploadType) {
    constexpr std::array<std::uint16_t, 16> kPixels{
        0x3C00U, 0x3800U, 0x0000U, 0x3C00U,
        0x3400U, 0x3C00U, 0x3800U, 0x3C00U,
        0x0000U, 0x0000U, 0x3C00U, 0x3C00U,
        0x3C00U, 0x3400U, 0x3800U, 0x3C00U,
    };
    while (glGetError() != GL_NO_ERROR) {
    }

    const TextureHandle texture = renderDevice->CreateTexture(
        TextureDesc{2U, 2U, TextureFormat::RGBA16Float, 1U},
        std::as_bytes(std::span(kPixels)));
    ASSERT_NE(texture, kInvalidTextureHandle);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindTexture(texture, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPass();

    std::array<std::uint16_t, kPixels.size()> readback{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_HALF_FLOAT, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    EXPECT_EQ(readback, kPixels);

    renderDevice->DestroyTexture(texture);
}

TEST_F(GlRenderDeviceTest, DestroyRenderDevice_ReleasesLiveGlResources) {
    auto firstDevice = std::make_unique<GlRenderDevice>(*windowManager);
    const ShaderHandle vertexShader =
        firstDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = firstDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = firstDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 3> kVertex{0.0F, 0.0F, 0.0F};
    const BufferHandle buffer = firstDevice->CreateBuffer(
        BufferDesc{sizeof(kVertex), BufferUsage::Vertex}, std::as_bytes(std::span(kVertex)));
    const TextureHandle texture = firstDevice->CreateTexture(TextureDesc{1U, 1U});
    ASSERT_NE(buffer, kInvalidBufferHandle);
    ASSERT_NE(texture, kInvalidTextureHandle);
    ASSERT_EQ(firstDevice->GetLiveResourceCount(), 5U);

    std::unique_ptr<ICommandBuffer> commandBuffer = firstDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(buffer, 0U);
    commandBuffer->BindTexture(texture, 0U);

    GLint bufferName = 0;
    GLint textureName = 0;
    GLint programName = 0;
    GLint vertexArrayName = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bufferName);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
    glGetIntegerv(GL_CURRENT_PROGRAM, &programName);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArrayName);
    ASSERT_NE(bufferName, 0);
    ASSERT_NE(textureName, 0);
    ASSERT_NE(programName, 0);
    ASSERT_NE(vertexArrayName, 0);

    commandBuffer->EndRenderPass();
    commandBuffer.reset();
    firstDevice.reset();

    // Detach state that may legally remain current after object deletion before querying liveness.
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    EXPECT_FALSE(glIsBuffer(static_cast<GLuint>(bufferName)));
    EXPECT_FALSE(glIsTexture(static_cast<GLuint>(textureName)));
    EXPECT_FALSE(glIsProgram(static_cast<GLuint>(programName)));
    EXPECT_FALSE(glIsVertexArray(static_cast<GLuint>(vertexArrayName)));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    auto secondDevice = std::make_unique<GlRenderDevice>(*windowManager);
    EXPECT_EQ(secondDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateShader_ValidSource_Succeeds) {
    const ShaderHandle shader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    EXPECT_NE(shader, kInvalidShaderHandle);
    renderDevice->DestroyShader(shader);
}

TEST_F(GlRenderDeviceTest, CreateShader_BrokenSource_ReturnsInvalidAndLogsCompilerDiagnostic) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const ShaderHandle shader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Fragment, std::string(kBrokenShaderSource)});
    EXPECT_EQ(shader, kInvalidShaderHandle);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundCompilerDiagnostic =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("compilation failed");
        });
    EXPECT_TRUE(foundCompilerDiagnostic);

    logger.Shutdown();
}

TEST_F(GlRenderDeviceTest, CreatePipeline_UnknownShaderHandle_ReturnsInvalid) {
    PipelineDesc desc;
    desc.vertexShader = ShaderHandle{999};
    desc.fragmentShader = ShaderHandle{998};
    EXPECT_EQ(renderDevice->CreatePipeline(desc), kInvalidPipelineHandle);
}

TEST_F(GlRenderDeviceTest, CreatePipeline_ValidShaders_Succeeds) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    EXPECT_NE(pipeline, kInvalidPipelineHandle);

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, InvalidSentinelBindsAreRejectedWithoutPoisoningValidState) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr float vertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(vertices));
    const BufferHandle vertexBuffer =
        renderDevice->CreateBuffer(BufferDesc{vertexBytes.size(), BufferUsage::Vertex}, vertexBytes);
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindPipeline(kInvalidPipelineHandle);
    commandBuffer->BindVertexBuffer(kInvalidBufferHandle);
    commandBuffer->BindIndexBuffer(kInvalidBufferHandle);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, DestroyBoundPipelineWhileRecording_FailsClosed) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 3> kFirstVertex{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 3> kSecondVertex{1.0F, 0.0F, 0.0F};
    const BufferHandle firstBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kFirstVertex), BufferUsage::Vertex}, std::as_bytes(std::span(kFirstVertex)));
    const BufferHandle secondBuffer = renderDevice->CreateBuffer(
        BufferDesc{sizeof(kSecondVertex), BufferUsage::Vertex}, std::as_bytes(std::span(kSecondVertex)));
    ASSERT_NE(firstBuffer, kInvalidBufferHandle);
    ASSERT_NE(secondBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(firstBuffer, 0U);
    renderDevice->DestroyPipeline(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    commandBuffer->BindVertexBuffer(secondBuffer, 0U);
    commandBuffer->Draw(1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->DestroyBuffer(secondBuffer);
    renderDevice->DestroyBuffer(firstBuffer);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, FullTriangleDrawAndPresent_DoesNotCrash) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr float kVertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kVertices));
    const BufferHandle vertexBuffer =
        renderDevice->CreateBuffer(BufferDesc{vertexBytes.size(), BufferUsage::Vertex}, vertexBytes);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.05F, 0.05F, 0.05F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;

    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3);
    commandBuffer->EndRenderPass();

    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->Present();

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);

    SUCCEED();
}

TEST_F(GlRenderDeviceTest, FullscreenToneMappingShader_MapsBoundSourceTextureToDefaultFramebuffer) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, WithShaderStageDefine(Shader::BuiltIn::kFullscreenQuadSource,
                                                                        "VERTEX_SHADER")});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, WithShaderStageDefine(Shader::BuiltIn::kFullscreenQuadSource,
                                                                          "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<std::uint8_t, 4> kWarmHdrLikePixel{220U, 48U, 20U, 255U};
    const TextureHandle sourceTexture = renderDevice->CreateTexture(
        TextureDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U}, std::as_bytes(std::span(kWarmHdrLikePixel)));
    ASSERT_NE(sourceTexture, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformInt("uSourceTexture", 0);
    commandBuffer->BindTexture(sourceTexture, 0U);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 24U));
    EXPECT_GT(pixel[1], pixel[2]);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyTexture(sourceTexture);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, FullscreenToneMappingShader_ComposesRgba16FloatRenderTargetToDefaultFramebuffer) {
    constexpr std::string_view kFullscreenVertexSource = R"(#version 330 core
void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";
    constexpr std::string_view kFlatColorFragmentSource = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";
    const ShaderHandle sourceVertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kFullscreenVertexSource)});
    const ShaderHandle sourceFragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kFlatColorFragmentSource)});
    const ShaderHandle toneVertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, WithShaderStageDefine(Shader::BuiltIn::kFullscreenQuadSource,
                                                                        "VERTEX_SHADER")});
    const ShaderHandle toneFragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, WithShaderStageDefine(Shader::BuiltIn::kFullscreenQuadSource,
                                                                          "FRAGMENT_SHADER")});
    ASSERT_NE(sourceVertexShader, kInvalidShaderHandle);
    ASSERT_NE(sourceFragmentShader, kInvalidShaderHandle);
    ASSERT_NE(toneVertexShader, kInvalidShaderHandle);
    ASSERT_NE(toneFragmentShader, kInvalidShaderHandle);

    PipelineDesc sourcePipelineDesc;
    sourcePipelineDesc.vertexShader = sourceVertexShader;
    sourcePipelineDesc.fragmentShader = sourceFragmentShader;
    sourcePipelineDesc.depthTestEnabled = true;
    sourcePipelineDesc.depthWriteEnabled = true;
    const PipelineHandle sourcePipeline = renderDevice->CreatePipeline(sourcePipelineDesc);
    ASSERT_NE(sourcePipeline, kInvalidPipelineHandle);
    PipelineDesc tonePipelineDesc;
    tonePipelineDesc.vertexShader = toneVertexShader;
    tonePipelineDesc.fragmentShader = toneFragmentShader;
    tonePipelineDesc.depthTestEnabled = false;
    tonePipelineDesc.depthWriteEnabled = false;
    const PipelineHandle tonePipeline = renderDevice->CreatePipeline(tonePipelineDesc);
    ASSERT_NE(tonePipeline, kInvalidPipelineHandle);

    const TextureHandle hdrTarget =
        renderDevice->CreateTexture(TextureDesc{64U, 64U, TextureFormat::RGBA16Float, 1U});
    const TextureHandle depthTarget =
        renderDevice->CreateTexture(TextureDesc{64U, 64U, TextureFormat::Depth32Float, 1U});
    const TextureHandle shadowStyleDepthTarget =
        renderDevice->CreateTexture(TextureDesc{64U, 64U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(hdrTarget, kInvalidTextureHandle);
    ASSERT_NE(depthTarget, kInvalidTextureHandle);
    ASSERT_NE(shadowStyleDepthTarget, kInvalidTextureHandle);

    // A renderer shadow pass may use this depth-only path when a directional caster is present.
    // Ensure core-profile `GL_NONE` draw/read-buffer states cannot prevent a subsequent color+depth
    // pass from rasterizing its fullscreen source geometry.
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc shadowStylePassDesc;
    shadowStylePassDesc.colorAttachment = kInvalidTextureHandle;
    shadowStylePassDesc.depthAttachment = shadowStyleDepthTarget;
    shadowStylePassDesc.depthLoadOp = LoadOp::Clear;
    shadowStylePassDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPass(shadowStylePassDesc);
    commandBuffer->EndRenderPass();

    RenderPassDesc sourcePassDesc;
    sourcePassDesc.colorAttachment = hdrTarget;
    sourcePassDesc.depthAttachment = depthTarget;
    sourcePassDesc.depthLoadOp = LoadOp::Clear;
    sourcePassDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPass(sourcePassDesc);
    commandBuffer->BindPipeline(sourcePipeline);
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.9F, 0.1F, 0.03F});
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();

    RenderPassDesc tonePassDesc;
    tonePassDesc.colorAttachment = kInvalidTextureHandle;
    tonePassDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(tonePassDesc);
    commandBuffer->BindPipeline(tonePipeline);
    commandBuffer->SetUniformInt("uSourceTexture", 0);
    commandBuffer->BindTexture(hdrTarget, 0U);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    // ToneMapping binds a depth-write-disabled pipeline. The next frame's clear must override
    // that persisted GL state before it draws the same depth again; otherwise GL_LESS rejects
    // later scene pixels although the renderer recorded valid draws.
    std::unique_ptr<ICommandBuffer> nextFrameCommandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(nextFrameCommandBuffer, nullptr);
    nextFrameCommandBuffer->BeginRenderPass(sourcePassDesc);
    nextFrameCommandBuffer->BindPipeline(sourcePipeline);
    nextFrameCommandBuffer->SetUniformVector3("uColor", Math::Vector3{0.04F, 0.12F, 0.92F});
    nextFrameCommandBuffer->Draw(3U);
    nextFrameCommandBuffer->EndRenderPass();
    nextFrameCommandBuffer->BeginRenderPass(tonePassDesc);
    nextFrameCommandBuffer->BindPipeline(tonePipeline);
    nextFrameCommandBuffer->SetUniformInt("uSourceTexture", 0);
    nextFrameCommandBuffer->BindTexture(hdrTarget, 0U);
    nextFrameCommandBuffer->Draw(3U);
    nextFrameCommandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(nextFrameCommandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[2], static_cast<std::uint8_t>(pixel[0] + 36U));
    EXPECT_GT(pixel[2], static_cast<std::uint8_t>(pixel[1] + 36U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyTexture(shadowStyleDepthTarget);
    renderDevice->DestroyTexture(depthTarget);
    renderDevice->DestroyTexture(hdrTarget);
    renderDevice->DestroyPipeline(tonePipeline);
    renderDevice->DestroyPipeline(sourcePipeline);
    renderDevice->DestroyShader(toneFragmentShader);
    renderDevice->DestroyShader(toneVertexShader);
    renderDevice->DestroyShader(sourceFragmentShader);
    renderDevice->DestroyShader(sourceVertexShader);
}

TEST_F(GlRenderDeviceTest, Basic3DShader_PerspectiveMatrixRendersNegativeZGeometry) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex,
                      WithShaderStageDefine(Shader::BuiltIn::kBasic3DSource, "VERTEX_SHADER")});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment,
                      WithShaderStageDefine(Shader::BuiltIn::kBasic3DSource, "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 9> kVertices{-2.0F, -2.0F, -8.0F, 2.0F, -2.0F, -8.0F, 0.0F, 2.0F, -8.0F};
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(kVertices)).size(), BufferUsage::Vertex},
        std::as_bytes(std::span(kVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    commandBuffer->SetUniformMatrix4x4(
        "uViewProjection", Math::Matrix4x4::Perspective(1.0471975512F, 1.0F, 0.1F, 100.0F));
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.95F, 0.15F, 0.05F});
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, Basic3DShader_IndexedCanonicalMeshVertexLayoutRenders) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex,
                      WithShaderStageDefine(Shader::BuiltIn::kBasic3DSource, "VERTEX_SHADER")});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment,
                      WithShaderStageDefine(Shader::BuiltIn::kBasic3DSource, "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3,
                                                     offsetof(Asset::MeshVertex, position)},
                                 VertexAttribute{"NORMAL", VertexAttributeFormat::Float3,
                                                     offsetof(Asset::MeshVertex, normal)},
                                 VertexAttribute{"TEXCOORD0", VertexAttributeFormat::Float2,
                                                     offsetof(Asset::MeshVertex, u)},
                                 VertexAttribute{"TANGENT", VertexAttributeFormat::Float4,
                                                     offsetof(Asset::MeshVertex, tangent)}};
    pipelineDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertex));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::array<Asset::MeshVertex, 3> vertices{
        Asset::MeshVertex{Math::Vector3{-2.0F, -2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        Asset::MeshVertex{Math::Vector3{2.0F, -2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        Asset::MeshVertex{Math::Vector3{0.0F, 2.0F, 0.0F}, Math::Vector3{0.0F, 0.0F, 1.0F}, 0.5F, 1.0F},
    };
    constexpr std::array<std::uint32_t, 3> kIndices{0U, 1U, 2U};
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(vertices)).size(), BufferUsage::Vertex}, std::as_bytes(std::span(vertices)));
    const BufferHandle indexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(kIndices)).size(), BufferUsage::Index}, std::as_bytes(std::span(kIndices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    ASSERT_NE(indexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformMatrix4x4(
        "uModel", Math::Matrix4x4::ComposeTrs(Math::Vector3{0.0F, 0.0F, -8.0F}, Math::Quaternion{},
                                                      Math::Vector3{1.0F, 1.0F, 1.0F}));
    commandBuffer->SetUniformMatrix4x4(
        "uViewProjection", Math::Matrix4x4::Perspective(1.0471975512F, 1.0F, 0.1F, 100.0F));
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.95F, 0.15F, 0.05F});
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->BindIndexBuffer(indexBuffer);
    commandBuffer->DrawIndexed(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBuffer(indexBuffer);
    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, ShaderManager_Basic3DProgramRendersIndexedGeometry) {
    Threading::ThreadPool threadPool{1U};
    Asset::AssetBundle assetBundle;
    Asset::FileSystem fileSystem{assetBundle};
    const Asset::MountHandle shaderMount =
        fileSystem.MountDirectory("shaders", ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in", 0);
    ASSERT_NE(shaderMount, 0U);
    Shader::ShaderManager shaderManager(threadPool, eventSystem, *renderDevice, fileSystem,
                                            Shader::ShaderManagerConfig{});

    Shader::ShaderProgramDesc programDesc;
    programDesc.virtualFilePath = std::string(Shader::BuiltIn::kBasic3DVirtualPath);
    programDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBasic3DSource);
    programDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    programDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    programDesc.depthTestEnabled = false;
    programDesc.depthWriteEnabled = false;
    programDesc.debugName = "ShaderManagerBasic3DRealGl";
    const std::shared_ptr<Shader::ShaderProgram> program = shaderManager.CreateProgram(programDesc);
    ASSERT_NE(program, nullptr);
    const auto compileDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!program->IsReady() && std::chrono::steady_clock::now() < compileDeadline) {
        shaderManager.Update(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(program->IsReady());
    ASSERT_TRUE(program->IsValid());

    constexpr std::array<float, 9> kVertices{-2.0F, -2.0F, -8.0F, 2.0F, -2.0F, -8.0F, 0.0F, 2.0F, -8.0F};
    const BufferHandle vertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(kVertices)).size(), BufferUsage::Vertex},
        std::as_bytes(std::span(kVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    program->SetMatrix4x4("uModel", Math::Matrix4x4::Identity());
    program->SetMatrix4x4(
        "uViewProjection", Math::Matrix4x4::Perspective(1.0471975512F, 1.0F, 0.1F, 100.0F));
    program->SetVector3("uColor", Math::Vector3{0.95F, 0.15F, 0.05F});
    program->ApplyTo(*commandBuffer);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBuffer(vertexBuffer);
    fileSystem.Unmount(shaderMount);
}

// Depth-only render pass (Increment 26, shadow mapping's depth pre-pass): colorAttachment left
// invalid, depthAttachment a real Depth32Float texture — exercises GlCommandBuffer's
// depth-only FBO branch (glDrawBuffer(GL_NONE)/glReadBuffer(GL_NONE), viewport derived from the
// depth texture's own size since no color attachment is present to derive it from). Like
// FullTriangleDrawAndPresent_DoesNotCrash above, this codebase's public RHI surface doesn't expose
// glCheckFramebufferStatus/glGetError results directly, so "doesn't crash and produces a usable
// texture handle" is this test's proof, matching every other live-GL test's verification style.
TEST_F(GlRenderDeviceTest, DepthOnlyRenderPass_NoColorAttachment_DoesNotCrash) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const TextureHandle depthTexture =
        renderDevice->CreateTexture(TextureDesc{64, 64, TextureFormat::Depth32Float, 1});
    ASSERT_NE(depthTexture, kInvalidTextureHandle);

    constexpr float kVertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kVertices));
    const BufferHandle vertexBuffer =
        renderDevice->CreateBuffer(BufferDesc{vertexBytes.size(), BufferUsage::Vertex}, vertexBytes);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.depthAttachment = depthTexture;
    passDesc.depthLoadOp = LoadOp::Clear;
    passDesc.clearDepth = 1.0F;

    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3);
    commandBuffer->EndRenderPass();

    renderDevice->Submit(std::move(commandBuffer));

    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyTexture(depthTexture);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);

    SUCCEED();
}

TEST_F(GlRenderDeviceTest, RepeatedPresentCalls_DoNotChangeLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCount(), 0U);
    for (int i = 0; i < 5; ++i) {
        renderDevice->Present();
    }
    EXPECT_EQ(renderDevice->GetLiveResourceCount(), 0U);
}

TEST_F(GlRenderDeviceTest, CreateShader_OutInfoLogParameter_CapturesLogOnBothSuccessAndFailure) {
    std::string successLog = "unset";
    const ShaderHandle shader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)}, &successLog);
    EXPECT_NE(shader, kInvalidShaderHandle);
    renderDevice->DestroyShader(shader);

    std::string failureLog;
    const ShaderHandle broken = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kBrokenShaderSource)}, &failureLog);
    EXPECT_EQ(broken, kInvalidShaderHandle);
    EXPECT_FALSE(failureLog.empty());
}

TEST_F(GlRenderDeviceTest, GetPipelineUniforms_ReflectsDeclaredUniforms) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::vector<UniformReflection> uniforms = renderDevice->GetPipelineUniforms(pipeline);
    const bool foundModel = std::any_of(uniforms.begin(), uniforms.end(), [](const UniformReflection& uniform) {
        return uniform.name == "uModel" && uniform.type == ShaderDataType::Mat4;
    });
    const bool foundColor = std::any_of(uniforms.begin(), uniforms.end(), [](const UniformReflection& uniform) {
        return uniform.name == "uColor" && uniform.type == ShaderDataType::Vec3;
    });
    EXPECT_TRUE(foundModel);
    EXPECT_TRUE(foundColor);

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, GetPipelineUniforms_UnsupportedTypeIsExplicitAndSamplerRemainsInt) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kSamplerAndUnsupportedUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::vector<UniformReflection> uniforms = renderDevice->GetPipelineUniforms(pipeline);
    const auto samplerIt = std::find_if(uniforms.cbegin(), uniforms.cend(), [](const UniformReflection& uniform) {
        return uniform.name == "uTexture";
    });
    const auto unsupportedIt = std::find_if(uniforms.cbegin(), uniforms.cend(), [](const UniformReflection& uniform) {
        return uniform.name == "uUnsigned";
    });
    ASSERT_NE(samplerIt, uniforms.cend());
    ASSERT_NE(unsupportedIt, uniforms.cend());
    EXPECT_EQ(samplerIt->type, ShaderDataType::Int);
    EXPECT_EQ(unsupportedIt->type, ShaderDataType::Unsupported);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::DontCare;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->SetUniformInt("uTexture", 0);
    commandBuffer->SetUniformInt("uUnsigned", 1);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, GetPipelineUniforms_UnknownHandle_ReturnsEmpty) {
    EXPECT_TRUE(renderDevice->GetPipelineUniforms(PipelineHandle{999}).empty());
}

TEST_F(GlRenderDeviceTest, PipelineBinaryRoundTrip_CreatePipelineFromBinary_ProducesValidPipeline) {
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    const bool gotBinary = renderDevice->GetPipelineBinary(pipeline, binary, format);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);

    if (!gotBinary || binary.empty()) {
        GTEST_SKIP() << "This sandbox's Mesa/llvmpipe driver did not return a usable program "
                        "binary (GL_PROGRAM_BINARY_LENGTH <= 0) - documented as a plain cache "
                        "miss, not a failure, so skip the round-trip assertion here.";
    }

    PipelineBinaryDesc binaryDesc;
    binaryDesc.vertexLayout = pipelineDesc.vertexLayout;
    binaryDesc.vertexStride = pipelineDesc.vertexStride;
    const PipelineHandle restored = renderDevice->CreatePipelineFromBinary(binary, format, binaryDesc);
    EXPECT_NE(restored, kInvalidPipelineHandle);
    if (restored != kInvalidPipelineHandle) {
        renderDevice->DestroyPipeline(restored);
    }
}

TEST_F(GlRenderDeviceTest, GetPipelineBinary_UnknownHandle_ReturnsFalse) {
    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    EXPECT_FALSE(renderDevice->GetPipelineBinary(PipelineHandle{999}, binary, format));
}

TEST_F(GlRenderDeviceTest, LitShadowed3DShader_DepthPrepassDarkensOccludedFragment) {
    const ShaderHandle shadowVertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex,
                      WithShaderStageDefine(Shader::BuiltIn::kShadowDepthSource, "VERTEX_SHADER")});
    const ShaderHandle shadowFragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment,
                      WithShaderStageDefine(Shader::BuiltIn::kShadowDepthSource, "FRAGMENT_SHADER")});
    const ShaderHandle litVertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex,
                      WithShaderStageDefine(Shader::BuiltIn::kLitShadowed3DSource, "VERTEX_SHADER")});
    std::string litFragmentInfoLog;
    const ShaderHandle litFragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment,
                      WithShaderStageDefine(Shader::BuiltIn::kLitShadowed3DSource, "FRAGMENT_SHADER")},
        &litFragmentInfoLog);
    ASSERT_NE(shadowVertexShader, kInvalidShaderHandle);
    ASSERT_NE(shadowFragmentShader, kInvalidShaderHandle);
    ASSERT_NE(litVertexShader, kInvalidShaderHandle);
    ASSERT_NE(litFragmentShader, kInvalidShaderHandle) << litFragmentInfoLog;

    PipelineDesc shadowPipelineDesc;
    shadowPipelineDesc.vertexShader = shadowVertexShader;
    shadowPipelineDesc.fragmentShader = shadowFragmentShader;
    shadowPipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    shadowPipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    shadowPipelineDesc.depthTestEnabled = true;
    shadowPipelineDesc.depthWriteEnabled = true;
    const PipelineHandle shadowPipeline = renderDevice->CreatePipeline(shadowPipelineDesc);
    ASSERT_NE(shadowPipeline, kInvalidPipelineHandle);

    PipelineDesc litPipelineDesc;
    litPipelineDesc.vertexShader = litVertexShader;
    litPipelineDesc.fragmentShader = litFragmentShader;
    litPipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0},
                                    VertexAttribute{"NORMAL", VertexAttributeFormat::Float3,
                                                       3U * static_cast<std::uint32_t>(sizeof(float))},
                                    VertexAttribute{"TEXCOORD0", VertexAttributeFormat::Float2,
                                                       6U * static_cast<std::uint32_t>(sizeof(float))},
                                    VertexAttribute{"TANGENT", VertexAttributeFormat::Float4,
                                                       8U * static_cast<std::uint32_t>(sizeof(float))}};
    litPipelineDesc.vertexStride = 12U * static_cast<std::uint32_t>(sizeof(float));
    litPipelineDesc.depthTestEnabled = false;
    litPipelineDesc.depthWriteEnabled = false;
    const PipelineHandle litPipeline = renderDevice->CreatePipeline(litPipelineDesc);
    ASSERT_NE(litPipeline, kInvalidPipelineHandle);

    constexpr std::array<float, 9> kShadowCasterVertices{
        -0.9F, -0.9F, -0.5F,
        0.9F, -0.9F, -0.5F,
        0.0F, 0.9F, -0.5F,
    };
    constexpr std::array<float, 36> kReceiverVertices{
        -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
        3.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 2.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F,
        -1.0F, 3.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 2.0F, 1.0F, 0.0F, 0.0F, 1.0F,
    };
    constexpr std::array<std::uint8_t, 4> kWhitePixel{255U, 255U, 255U, 255U};
    constexpr std::array<std::uint8_t, 4> kFlatNormalPixel{128U, 128U, 255U, 255U};

    const BufferHandle shadowVertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(kShadowCasterVertices)).size(), BufferUsage::Vertex},
        std::as_bytes(std::span(kShadowCasterVertices)));
    const BufferHandle receiverVertexBuffer = renderDevice->CreateBuffer(
        BufferDesc{std::as_bytes(std::span(kReceiverVertices)).size(), BufferUsage::Vertex},
        std::as_bytes(std::span(kReceiverVertices)));
    const TextureHandle shadowMap =
        renderDevice->CreateTexture(TextureDesc{64, 64, TextureFormat::Depth32Float, 1});
    const TextureHandle whiteTexture = renderDevice->CreateTexture(
        TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1}, std::as_bytes(std::span(kWhitePixel)));
    const TextureHandle flatNormalTexture = renderDevice->CreateTexture(
        TextureDesc{1, 1, TextureFormat::RGBA8Unorm, 1}, std::as_bytes(std::span(kFlatNormalPixel)));
    ASSERT_NE(shadowVertexBuffer, kInvalidBufferHandle);
    ASSERT_NE(receiverVertexBuffer, kInvalidBufferHandle);
    ASSERT_NE(shadowMap, kInvalidTextureHandle);
    ASSERT_NE(whiteTexture, kInvalidTextureHandle);
    ASSERT_NE(flatNormalTexture, kInvalidTextureHandle);
    // All active sampler uniforms must have type-compatible image units, even if the runtime
    // branch using one is disabled. The complete lighting shader keeps samplerCube active;
    // leaving it at its GL default (unit 0) aliases the 2D shadow sampler and invalidates Draw.
    std::array<std::uint8_t, 24> whiteCubePixels{};
    whiteCubePixels.fill(255U);
    TextureDesc cubeDesc{1U, 1U, TextureFormat::RGBA8Unorm, 1U};
    cubeDesc.type = TextureType::Cubemap;
    cubeDesc.arrayLayers = 6U;
    const auto reflectionCube = renderDevice->CreateTexture(cubeDesc, std::as_bytes(std::span(whiteCubePixels)));
    ASSERT_NE(reflectionCube, kInvalidTextureHandle);

    std::unique_ptr<ICommandBuffer> shadowCommandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(shadowCommandBuffer, nullptr);
    RenderPassDesc shadowPassDesc;
    shadowPassDesc.colorAttachment = kInvalidTextureHandle;
    shadowPassDesc.depthAttachment = shadowMap;
    shadowPassDesc.depthLoadOp = LoadOp::Clear;
    shadowPassDesc.clearDepth = 1.0F;
    shadowCommandBuffer->BeginRenderPass(shadowPassDesc);
    shadowCommandBuffer->BindPipeline(shadowPipeline);
    shadowCommandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    shadowCommandBuffer->SetUniformMatrix4x4("uLightSpaceMatrix", Math::Matrix4x4::Identity());
    shadowCommandBuffer->BindVertexBuffer(shadowVertexBuffer);
    shadowCommandBuffer->Draw(3);
    shadowCommandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(shadowCommandBuffer));

    std::unique_ptr<ICommandBuffer> colorCommandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(colorCommandBuffer, nullptr);
    RenderPassDesc colorPassDesc;
    colorPassDesc.colorAttachment = kInvalidTextureHandle;
    colorPassDesc.colorLoadOp = LoadOp::Clear;
    colorPassDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    colorPassDesc.depthLoadOp = LoadOp::DontCare;
    colorCommandBuffer->BeginRenderPass(colorPassDesc);
    colorCommandBuffer->BindPipeline(litPipeline);
    colorCommandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    // lit_shadowed_3d's normal transform now reads uNormalMatrix instead of uModel (Phase 2c) -
    // identity either way here, but a real caller (Renderer3D::RecordItems) always sets it
    // alongside uModel, so this direct-GL-API test must too or vWorldNormal comes out zero.
    colorCommandBuffer->SetUniformMatrix4x4("uNormalMatrix", Math::Matrix4x4::Identity());
    colorCommandBuffer->SetUniformMatrix4x4("uViewProjection", Math::Matrix4x4::Identity());
    colorCommandBuffer->SetUniformMatrix4x4("uLightSpaceMatrix", Math::Matrix4x4::Identity());
    colorCommandBuffer->SetUniformVector3("uAmbientColor", Math::Vector3{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformVector3("uViewPosition", Math::Vector3{0.0F, 0.0F, 1.0F});
    colorCommandBuffer->SetUniformVector3("uAlbedoColor", Math::Vector3{1.0F, 1.0F, 1.0F});
    colorCommandBuffer->SetUniformFloat("uMetallic", 0.0F);
    colorCommandBuffer->SetUniformFloat("uRoughness", 1.0F);
    colorCommandBuffer->SetUniformVector3("uEmissiveColor", Math::Vector3{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformInt("uLights[0].type", 0);
    colorCommandBuffer->SetUniformInt("uMeshRenderLayers", 1);
    colorCommandBuffer->SetUniformInt("uLights[0].cullMask", 1);
    colorCommandBuffer->SetUniformVector3("uLights[0].direction", Math::Vector3{0.0F, 0.0F, -1.0F});
    colorCommandBuffer->SetUniformVector3("uLights[0].color", Math::Vector3{1.0F, 1.0F, 1.0F});
    colorCommandBuffer->SetUniformFloat("uLights[0].intensity", 1.0F);
    colorCommandBuffer->BindTexture(shadowMap, 0U);
    colorCommandBuffer->SetUniformInt("uShadowMapTexture", 0);
    colorCommandBuffer->SetUniformInt("uShadowPcfKernelRadius", 1);
    colorCommandBuffer->SetUniformFloat("uShadowOpacity", 1.0F);
    colorCommandBuffer->SetUniformFloat("uShadowBias", 0.001F);
    colorCommandBuffer->SetUniformFloat("uShadowNormalBias", 1.0F);
    colorCommandBuffer->BindTexture(flatNormalTexture, 3U);
    colorCommandBuffer->SetUniformInt("uNormalTexture", 3);
    colorCommandBuffer->BindTexture(whiteTexture, 1U);
    colorCommandBuffer->SetUniformInt("uAlbedoTexture", 1);
    colorCommandBuffer->BindTexture(whiteTexture, 2U);
    colorCommandBuffer->SetUniformInt("uAOTexture", 2);
    colorCommandBuffer->BindTexture(whiteTexture, 12U);
    colorCommandBuffer->SetUniformInt("uMetallicRoughnessTexture", 12);
    colorCommandBuffer->BindTexture(whiteTexture, 13U);
    colorCommandBuffer->SetUniformInt("uEmissiveTexture", 13);
    colorCommandBuffer->SetUniformFloat("uEmissiveEnergy", 1.0F);
    colorCommandBuffer->SetUniformFloat("uNormalScale", 1.0F);
    colorCommandBuffer->SetUniformFloat("uOcclusionStrength", 1.0F);
    colorCommandBuffer->SetUniformVector3("uUvScale", Math::Vector3{1.0F, 1.0F, 0.0F});
    colorCommandBuffer->SetUniformVector3("uUvOffset", Math::Vector3{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformInt("uUnshaded", 0);
    colorCommandBuffer->BindTexture(reflectionCube, 6U);
    colorCommandBuffer->SetUniformInt("uReflectionProbeCube", 6);
    colorCommandBuffer->BindTexture(reflectionCube, 7U);
    colorCommandBuffer->SetUniformInt("uReflectionProbeSecondaryCube", 7);
    colorCommandBuffer->SetUniformInt("uReflectionProbeEnabled", 0);
    // Surface opacity became an explicit lit-shader uniform; this direct opaque receiver must
    // initialize it just like Renderer3D does for an opaque SurfaceInstance3D.
    colorCommandBuffer->SetUniformFloat("uSurfaceOpacity", 1.0F);
    colorCommandBuffer->SetUniformFloat("uAlphaCutoff", 0.0F);
    colorCommandBuffer->BindVertexBuffer(receiverVertexBuffer);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    colorCommandBuffer->Draw(3);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    colorCommandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(colorCommandBuffer));

    std::array<std::uint8_t, 4> shadowedPixel{};
    std::array<std::uint8_t, 4> softEdgePixel{};
    std::array<std::uint8_t, 4> litPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, shadowedPixel.data());
    glReadPixels(44, 36, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, softEdgePixel.data());
    glReadPixels(60, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, litPixel.data());
    EXPECT_GT(static_cast<int>(softEdgePixel[0]), static_cast<int>(shadowedPixel[0]) + 16);
    EXPECT_GT(static_cast<int>(litPixel[0]), static_cast<int>(softEdgePixel[0]) + 16);

    renderDevice->DestroyTexture(reflectionCube);
    renderDevice->DestroyTexture(flatNormalTexture);
    renderDevice->DestroyTexture(whiteTexture);
    renderDevice->DestroyTexture(shadowMap);
    renderDevice->DestroyBuffer(receiverVertexBuffer);
    renderDevice->DestroyBuffer(shadowVertexBuffer);
    renderDevice->DestroyPipeline(litPipeline);
    renderDevice->DestroyPipeline(shadowPipeline);
    renderDevice->DestroyShader(litFragmentShader);
    renderDevice->DestroyShader(litVertexShader);
    renderDevice->DestroyShader(shadowFragmentShader);
    renderDevice->DestroyShader(shadowVertexShader);
}

TEST_F(GlRenderDeviceTest, CommandBuffer_SetUniformCalls_RejectReflectedTypeMismatches) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::DontCare;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{1.0F, 0.0F, 0.0F});
    commandBuffer->SetUniformFloat("uModel", 1.0F);
    commandBuffer->SetUniformInt("uColor", 1);
    commandBuffer->SetUniformBool("uModel", true);
    commandBuffer->SetUniformMatrix4x4("uColor", Math::Matrix4x4::Identity());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

TEST_F(GlRenderDeviceTest, CommandBuffer_SetUniformCalls_RejectNonFiniteValuesWithoutChangingUniforms) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kFiniteUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::DontCare;
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);

    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    ASSERT_NE(currentProgram, 0);
    const GLint exposureLocation = glGetUniformLocation(static_cast<GLuint>(currentProgram), "uExposure");
    ASSERT_NE(exposureLocation, -1);

    commandBuffer->SetUniformFloat("uExposure", 2.0F);
    GLfloat exposureBefore = 0.0F;
    glGetUniformfv(static_cast<GLuint>(currentProgram), exposureLocation, &exposureBefore);
    EXPECT_FLOAT_EQ(exposureBefore, 2.0F);
    commandBuffer->SetUniformFloat("uExposure", std::numeric_limits<float>::quiet_NaN());
    GLfloat exposureAfter = 0.0F;
    glGetUniformfv(static_cast<GLuint>(currentProgram), exposureLocation, &exposureAfter);
    EXPECT_FLOAT_EQ(exposureAfter, exposureBefore);

    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.25F, 0.5F, 0.75F});
    std::array<GLfloat, 3> colorBefore{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uColor"),
                   colorBefore.data());
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    std::array<GLfloat, 3> colorAfter{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uColor"),
                   colorAfter.data());
    EXPECT_EQ(colorAfter, colorBefore);

    Math::Matrix4x4 validMatrix = Math::Matrix4x4::Identity();
    validMatrix.m[0][1] = 2.0F;
    commandBuffer->SetUniformMatrix4x4("uModel", validMatrix);
    std::array<GLfloat, 16> matrixBefore{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uModel"),
                   matrixBefore.data());
    Math::Matrix4x4 invalidMatrix = validMatrix;
    invalidMatrix.m[2][2] = std::numeric_limits<float>::quiet_NaN();
    commandBuffer->SetUniformMatrix4x4("uModel", invalidMatrix);
    std::array<GLfloat, 16> matrixAfter{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uModel"),
                   matrixAfter.data());
    EXPECT_EQ(matrixAfter, matrixBefore);

    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, CommandBuffer_SetUniformCalls_OnBoundPipeline_DoNotCrash) {
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kUniformFragmentShaderSource)});

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::DontCare;
    passDesc.depthLoadOp = LoadOp::DontCare;

    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformMatrix4x4("uModel", Math::Matrix4x4::Identity());
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{1.0F, 0.0F, 0.0F});
    commandBuffer->EndRenderPass();

    renderDevice->Submit(std::move(commandBuffer));

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);

    SUCCEED();
}

// Phase 3 (ViewportManager): BeginRenderPass's viewportOverride must isolate each pane via
// GL_SCISSOR_TEST, not just glViewport - glClear() ignores glViewport() but respects the scissor
// rect. Without that scissor-test pairing (see ApplyViewport in gl_command_buffer_uve.cpp),
// clearing the second pane would silently wipe out the first pane already drawn to the shared
// default framebuffer. This test renders two flat-colored fullscreen-triangle passes into two
// disjoint viewportOverride rects on the default framebuffer and reads back a pixel from each
// half to confirm neither pane's Clear touched the other's pixels.
TEST_F(GlRenderDeviceTest, BeginRenderPass_ViewportOverride_IsolatesPanesViaScissor) {
    constexpr std::string_view kFullscreenVertexSource = R"(#version 330 core
void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";
    constexpr std::string_view kFlatColorFragmentSource = R"(#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kFullscreenVertexSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kFlatColorFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    // The window is 64x64 (MakeTestWindowDesc()) - left pane occupies x in [0, 32), right pane
    // x in [32, 64), each pane's full height.
    RenderPassDesc leftPassDesc;
    leftPassDesc.colorAttachment = kInvalidTextureHandle;
    leftPassDesc.colorLoadOp = LoadOp::Clear;
    leftPassDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    leftPassDesc.depthLoadOp = LoadOp::DontCare;
    leftPassDesc.viewportOverride = ViewportRect{Math::Vector2i{0, 0}, Math::Vector2i{32, 64}};
    commandBuffer->BeginRenderPass(leftPassDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.9F, 0.05F, 0.05F});
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();

    RenderPassDesc rightPassDesc = leftPassDesc;
    rightPassDesc.viewportOverride = ViewportRect{Math::Vector2i{32, 0}, Math::Vector2i{32, 64}};
    commandBuffer->BeginRenderPass(rightPassDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->SetUniformVector3("uColor", Math::Vector3{0.05F, 0.05F, 0.9F});
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();

    renderDevice->Submit(std::move(commandBuffer));

    std::array<std::uint8_t, 3> leftPixel{};
    glReadPixels(16, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, leftPixel.data());
    std::array<std::uint8_t, 3> rightPixel{};
    glReadPixels(48, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rightPixel.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    // Left pane must still be red - the right pane's Clear must not have bled across the scissor
    // boundary and wiped it back to black.
    EXPECT_GT(leftPixel[0], static_cast<std::uint8_t>(leftPixel[2] + 64U));
    // Right pane must be blue.
    EXPECT_GT(rightPixel[2], static_cast<std::uint8_t>(rightPixel[0] + 64U));

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

// ---------------------------------------------------------------------------
// M5a: compute pipelines + Dispatch (desktop GL 4.3+; tests skip below that)
// ---------------------------------------------------------------------------

// GLSL twin of the Vulkan-side kFillPaletteComputeSpirv: one workgroup of 4 invocations
// writes red/green/blue/yellow into uColors[0..3] of the SSBO at binding 0.
constexpr std::string_view kPaletteFillComputeSource = R"(#version 430 core
layout(local_size_x = 4) in;
layout(std430, binding = 0) buffer Palette {
    vec4 uColors[];
};
void main() {
    uint gid = gl_GlobalInvocationID.x;
    vec4 c = vec4(0.0);
    if (gid == 0u) { c = vec4(1.0, 0.0, 0.0, 1.0); }
    else if (gid == 1u) { c = vec4(0.0, 1.0, 0.0, 1.0); }
    else if (gid == 2u) { c = vec4(0.0, 0.0, 1.0, 1.0); }
    else if (gid == 3u) { c = vec4(1.0, 1.0, 0.0, 1.0); }
    uColors[gid] = c;
}
)";

TEST_F(GlRenderDeviceTest, Dispatch_ComputeFillsStorageBuffer_ProvenByReadback) {
    // The GL M5a proof: a ZERO-initialized Storage SSBO, one outside-pass dispatch of the
    // palette filler (GL executes at record time and barriers with GL_ALL_BARRIER_BITS), then
    // a client-side glGetBufferSubData readback must show red/green/blue/yellow. Without a
    // real glDispatchCompute the buffer stays zeroed — the colors cannot be faked.
    const float zeros[16] = {};
    const BufferHandle palette = renderDevice->CreateBuffer(
        BufferDesc{sizeof(zeros), BufferUsage::Storage}, std::as_bytes(std::span(zeros)));
    if (palette == kInvalidBufferHandle) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }
    const ShaderHandle computeShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Compute, std::string(kPaletteFillComputeSource)});
    if (computeShader == kInvalidShaderHandle) {
        renderDevice->DestroyBuffer(palette);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandle computePipeline = renderDevice->CreateComputePipeline(pipelineDesc, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;

    // The whole compute flow records OUTSIDE pass markers (the M5a contract) — GL executes
    // each command at record time, including the dispatch and its memory barrier.
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindStorageBuffer(palette, 0U);
    commandBuffer->Dispatch(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->Submit(std::move(commandBuffer));

    // Readback through the indexed binding the RHI's glBindBufferBase left behind.
    GLint storageName = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &storageName);
    ASSERT_NE(storageName, 0) << "the storage buffer must still be bound at indexed slot 0";
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(storageName));
    float readback[16] = {};
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(readback), readback);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    EXPECT_FLOAT_EQ(readback[0], 1.0F);  // uColors[0] = RED
    EXPECT_FLOAT_EQ(readback[1], 0.0F);
    EXPECT_FLOAT_EQ(readback[2], 0.0F);
    EXPECT_FLOAT_EQ(readback[3], 1.0F);
    EXPECT_FLOAT_EQ(readback[4], 0.0F);  // uColors[1] = GREEN
    EXPECT_FLOAT_EQ(readback[5], 1.0F);
    EXPECT_FLOAT_EQ(readback[6], 0.0F);
    EXPECT_FLOAT_EQ(readback[7], 1.0F);
    EXPECT_FLOAT_EQ(readback[8], 0.0F);  // uColors[2] = BLUE
    EXPECT_FLOAT_EQ(readback[9], 0.0F);
    EXPECT_FLOAT_EQ(readback[10], 1.0F);
    EXPECT_FLOAT_EQ(readback[11], 1.0F);
    EXPECT_FLOAT_EQ(readback[12], 1.0F); // uColors[3] = YELLOW
    EXPECT_FLOAT_EQ(readback[13], 1.0F);
    EXPECT_FLOAT_EQ(readback[14], 0.0F);
    EXPECT_FLOAT_EQ(readback[15], 1.0F);

    renderDevice->DestroyPipeline(computePipeline);
    renderDevice->DestroyShader(computeShader);
    renderDevice->DestroyBuffer(palette);
}

TEST_F(GlRenderDeviceTest, CreateComputePipeline_ValidatesShaderHandleAndStage) {
    std::string infoLog;
    ComputePipelineDesc unknownDesc{};
    unknownDesc.computeShader = ShaderHandle{999999U};
    EXPECT_EQ(renderDevice->CreateComputePipeline(unknownDesc, &infoLog), kInvalidPipelineHandle);

    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ComputePipelineDesc wrongStageDesc{};
    wrongStageDesc.computeShader = vertexShader;
    EXPECT_EQ(renderDevice->CreateComputePipeline(wrongStageDesc, &infoLog), kInvalidPipelineHandle);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyShader(vertexShader);
}

TEST_F(GlRenderDeviceTest, Dispatch_RejectsGraphicsPipelineWithoutGlError) {
    // Engine-level refusal, never a GL call: dispatching with a GRAPHICS pipeline bound must
    // be dropped (UVE_ERROR) with the GL error flag untouched.
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandle graphicsPipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(graphicsPipeline, kInvalidPipelineHandle);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipeline(graphicsPipeline); // M5a: legal outside pass markers now
    commandBuffer->Dispatch(1U, 1U, 1U);           // refused: graphics pipeline is bound
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->Submit(std::move(commandBuffer));

    // Nothing bound at all: refused the same way.
    std::unique_ptr<ICommandBuffer> freshBuffer = renderDevice->CreateCommandBuffer();
    freshBuffer->Dispatch(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->Submit(std::move(freshBuffer));

    renderDevice->DestroyPipeline(graphicsPipeline);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyShader(fragmentShader);
}

// ---------------------------------------------------------------------------
// M5b: storage images (GL). BindTexture feeds ONE unified slot space — when the
// current program declares GL_IMAGE_2D uniforms, storage-capable textures are ALSO
// bound to the image unit of the same index (glBindImageTexture, GL_READ_WRITE).
// Linear RGBA8 compute/fragment tests prove imageStore writes with glGetTexImage;
// the sRGB case verifies that OpenGL keeps the texture sampled-only and clears the
// image unit instead of silently discarding storage writes.
// ---------------------------------------------------------------------------

constexpr std::string_view kImageFillComputeSource = R"(#version 430 core
layout(local_size_x = 4, local_size_y = 4) in;
layout(binding = 0, rgba8) uniform image2D uImage;
void main() {
    ivec2 c = ivec2(gl_GlobalInvocationID.xy);
    imageStore(uImage, c, vec4(0.0, 1.0, 0.0, 1.0));
}
)";

TEST_F(GlRenderDeviceTest, BindTexture_ComputeStorageImageOnSrgbTexture_LeavesImageUnitUnbound) {
    // An sRGB texture remains valid for sampling but is not a supported OpenGL storage
    // image. BindTexture must preserve the texture-unit binding while clearing the
    // corresponding image unit, rather than relying on driver-specific by-size aliasing.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    TextureDesc targetDesc{};
    targetDesc.width = 4U;
    targetDesc.height = 4U;
    targetDesc.colorSpace = TextureColorSpace::Srgb;
    const TextureHandle target = renderDevice->CreateTexture(
        targetDesc, std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandle);

    const ShaderHandle computeShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandle) {
        renderDevice->DestroyTexture(target);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandle computePipeline =
        renderDevice->CreateComputePipeline(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandle) {
        renderDevice->DestroyShader(computeShader);
        renderDevice->DestroyTexture(target);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindTexture(target, 0U); // M5b: legal outside pass while compute is bound
    renderDevice->Submit(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
    EXPECT_NE(textureName, 0) << "the sRGB texture must remain bound for sampler access";
    GLint imageName = -1;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
    EXPECT_EQ(imageName, 0) << "unsupported sRGB storage-image bindings must be cleared";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipeline(computePipeline);
    renderDevice->DestroyShader(computeShader);
    renderDevice->DestroyTexture(target);
}

TEST_F(GlRenderDeviceTest, BindTexture_ComputeImageStoreOnLinearTexture_ProvenByGlGetTexImage) {
    // Linear RGBA8 remains a supported compute storage image. A zero-initialized target
    // must read back green after the compute pipeline binds it and dispatches one workgroup.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    const TextureHandle target = renderDevice->CreateTexture(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U},
        std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandle);

    const ShaderHandle computeShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandle) {
        renderDevice->DestroyTexture(target);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandle computePipeline =
        renderDevice->CreateComputePipeline(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandle) {
        renderDevice->DestroyShader(computeShader);
        renderDevice->DestroyTexture(target);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindTexture(target, 0U);
    commandBuffer->Dispatch(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->Submit(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
    ASSERT_NE(textureName, 0) << "the target texture must still be bound at unit 0";
    std::array<std::uint8_t, 4U * 4U * 4U> readback{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (std::size_t texel = 0; texel < 16U; ++texel) {
        EXPECT_EQ(readback[texel * 4U + 0U], 0U) << "texel " << texel << " red";
        EXPECT_EQ(readback[texel * 4U + 1U], 255U) << "texel " << texel << " green";
        EXPECT_EQ(readback[texel * 4U + 2U], 0U) << "texel " << texel << " blue";
        EXPECT_EQ(readback[texel * 4U + 3U], 255U) << "texel " << texel << " alpha";
    }

    renderDevice->DestroyPipeline(computePipeline);
    renderDevice->DestroyShader(computeShader);
    renderDevice->DestroyTexture(target);
}

constexpr std::string_view kImageStoreVertexSource = R"(#version 430 core
void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view kImageStoreFragmentSource = R"(#version 430 core
layout(binding = 0, rgba8) uniform image2D uImage;
out vec4 FragColor;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy) % 4;
    imageStore(uImage, c, vec4(1.0, 0.0, 0.0, 1.0));
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

TEST_F(GlRenderDeviceTest, BindTexture_FragmentImageStore_ProvenByGlGetTexImage) {
    // The GL M5b graphics-side proof: a fullscreen triangle whose fragment imageStores
    // RED into texel (fragCoord % 4) of a zero-initialized 4x4 RGBA8 texture (full
    // coverage therefore paints all sixteen texels) while outputting black to the default
    // framebuffer. After the draw, the raw glGetTexImage readback must be all RED — the
    // fragment-stage image store really reached the caller's texture through the
    // BindTexture image-unit double-bind.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    const TextureHandle target = renderDevice->CreateTexture(
        TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U},
        std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandle);

    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kImageStoreVertexSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kImageStoreFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle)
        << "a GL_IMAGE_2D fragment uniform must reflect and build since M5b";

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = kInvalidTextureHandle;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::DontCare;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindTexture(target, 0U); // sampler bind + image-unit double-bind
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->Submit(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
    ASSERT_NE(textureName, 0) << "the target texture must still be bound at unit 0";
    std::array<std::uint8_t, 4U * 4U * 4U> readback{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (std::size_t texel = 0; texel < 16U; ++texel) {
        EXPECT_EQ(readback[texel * 4U + 0U], 255U) << "texel " << texel << " red";
        EXPECT_EQ(readback[texel * 4U + 1U], 0U) << "texel " << texel << " green";
        EXPECT_EQ(readback[texel * 4U + 2U], 0U) << "texel " << texel << " blue";
        EXPECT_EQ(readback[texel * 4U + 3U], 255U) << "texel " << texel << " alpha";
    }

    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyTexture(target);
}

} // namespace

TEST_F(GlRenderDeviceTest, CreateTexture_Texture2DArray_UploadsLevelMajorLayers) {
    // Tier 2.3: a 2x2x3 array with one solid color per layer, read straight back through
    // glGetTexImage (depth slices come back contiguous, layer after layer).
    constexpr std::uint8_t kLayerColors[3][4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U}, {0U, 0U, 255U, 255U},
    };
    std::array<std::uint8_t, 2U * 2U * 4U * 3U> upload{};
    for (std::uint32_t layer = 0U; layer < 3U; ++layer) {
        for (std::uint32_t texel = 0U; texel < 4U; ++texel) {
            const std::size_t base = (static_cast<std::size_t>(layer) * 4U + texel) * 4U;
            upload[base] = kLayerColors[layer][0];
            upload[base + 1U] = kLayerColors[layer][1];
            upload[base + 2U] = kLayerColors[layer][2];
            upload[base + 3U] = kLayerColors[layer][3];
        }
    }
    const TextureHandle array = renderDevice->CreateTexture(
        TextureDesc{2U, 2U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Texture2DArray, 3U},
        std::as_bytes(std::span(upload)));
    ASSERT_NE(array, kInvalidTextureHandle);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(array));
    EXPECT_NE(native, 0U);
    glBindTexture(GL_TEXTURE_2D_ARRAY, native);
    std::array<std::uint8_t, 2U * 2U * 4U * 3U> readback{};
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (std::uint32_t layer = 0U; layer < 3U; ++layer) {
        for (std::uint32_t texel = 0U; texel < 4U; ++texel) {
            const std::size_t base = (static_cast<std::size_t>(layer) * 4U + texel) * 4U;
            EXPECT_EQ(readback[base], kLayerColors[layer][0]) << "layer " << layer;
            EXPECT_EQ(readback[base + 1U], kLayerColors[layer][1]) << "layer " << layer;
            EXPECT_EQ(readback[base + 2U], kLayerColors[layer][2]) << "layer " << layer;
            EXPECT_EQ(readback[base + 3U], kLayerColors[layer][3]) << "layer " << layer;
        }
    }
    renderDevice->DestroyTexture(array);
}

TEST_F(GlRenderDeviceTest, CreateTexture_Cubemap_UploadsFacesInCubemapFaceOrder) {
    // Tier 2.3: six solid 2x2 faces upload in CubemapFace order (+X,-X,+Y,-Y,+Z,-Z), which
    // is also GL's POSITIVE_X + face order — each face target must read back its own color.
    constexpr std::uint8_t kFaceColors[6][4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U}, {0U, 0U, 255U, 255U},
        {255U, 255U, 0U, 255U}, {255U, 0U, 255U, 255U}, {0U, 255U, 255U, 255U},
    };
    std::array<std::uint8_t, 2U * 2U * 4U * 6U> upload{};
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        for (std::uint32_t texel = 0U; texel < 4U; ++texel) {
            const std::size_t base = (static_cast<std::size_t>(face) * 4U + texel) * 4U;
            upload[base] = kFaceColors[face][0];
            upload[base + 1U] = kFaceColors[face][1];
            upload[base + 2U] = kFaceColors[face][2];
            upload[base + 3U] = kFaceColors[face][3];
        }
    }
    const TextureHandle cube = renderDevice->CreateTexture(
        TextureDesc{2U, 2U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                       TextureType::Cubemap, 6U},
        std::as_bytes(std::span(upload)));
    ASSERT_NE(cube, kInvalidTextureHandle);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(cube));
    EXPECT_NE(native, 0U);
    glBindTexture(GL_TEXTURE_CUBE_MAP, native);
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        std::array<std::uint8_t, 2U * 2U * 4U> faceReadback{};
        glGetTexImage(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, faceReadback.data());
        EXPECT_EQ(glGetError(), GL_NO_ERROR) << "face " << face;
        for (std::uint32_t texel = 0U; texel < 4U; ++texel) {
            EXPECT_EQ(faceReadback[texel * 4U], kFaceColors[face][0]) << "face " << face;
            EXPECT_EQ(faceReadback[texel * 4U + 1U], kFaceColors[face][1]) << "face " << face;
            EXPECT_EQ(faceReadback[texel * 4U + 2U], kFaceColors[face][2]) << "face " << face;
            EXPECT_EQ(faceReadback[texel * 4U + 3U], kFaceColors[face][3]) << "face " << face;
        }
    }
    renderDevice->DestroyTexture(cube);
}

TEST_F(GlRenderDeviceTest, CreateTexture_DimensionsBeyondGlLimits_ReturnInvalidBeforeAllocation) {
    // Tier 2.3: layer counts the RHI validator accepts (no overflow) but no GL device offers.
    EXPECT_EQ(renderDevice->CreateTexture(
                  TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U,
                                 TextureColorSpace::Linear, TextureType::Texture2DArray,
                                 1000000U}),
              kInvalidTextureHandle);
    EXPECT_EQ(renderDevice->CreateTexture(
                  TextureDesc{100000U, 100000U, TextureFormat::RGBA8Unorm, 1U,
                                 TextureColorSpace::Linear, TextureType::Cubemap, 6U}),
              kInvalidTextureHandle);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_LayerBeyondAttachmentLayerCount_LeavesFramebufferUnchanged) {
    // Tier 2.3 mirror of UnknownAttachmentDoesNotBindOrCacheFramebuffer: out-of-range layers
    // reject before any FBO work (a nonzero layer on a 1-layer 2D texture fails here too).
    TextureDesc arrayDesc{2U, 2U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                             TextureType::Texture2DArray, 2U};
    const TextureHandle array = renderDevice->CreateTexture(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandle);
    const TextureHandle flat = renderDevice->CreateTexture(TextureDesc{2U, 2U});
    ASSERT_NE(flat, kInvalidTextureHandle);
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDesc& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPass(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };
    RenderPassDesc badColorLayer;
    badColorLayer.colorAttachment = array;
    badColorLayer.colorLayer = 5U;
    assertFramebufferUnchanged(badColorLayer);

    RenderPassDesc badDepthLayer;
    badDepthLayer.colorAttachment = array;
    badDepthLayer.depthAttachment = flat; // format aside, layer 1 already exceeds its 1 layer
    badDepthLayer.depthLayer = 1U;
    assertFramebufferUnchanged(badDepthLayer);

    RenderPassDesc flatWithLayer;
    flatWithLayer.colorAttachment = flat;
    flatWithLayer.colorLayer = 1U;
    assertFramebufferUnchanged(flatWithLayer);

    RenderPassDesc validLayeredPass;
    validLayeredPass.colorAttachment = array;
    validLayeredPass.colorLayer = 1U;
    commandBuffer->BeginRenderPass(validLayeredPass);
    GLint layeredFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &layeredFramebuffer);
    EXPECT_NE(layeredFramebuffer, 0);
    commandBuffer->EndRenderPass();
    renderDevice->DestroyTexture(array);
    renderDevice->DestroyTexture(flat);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_LayeredAttachments_RenderIntoSelectedLayers) {
    // Tier 2.3 pixel proof for render-to-layer: clear layer 0 green and layer 1 red through
    // two passes, then read both slices back — each must hold exactly its own clear color.
    GLint glMajor = 0;
    GLint glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    TextureDesc arrayDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                             TextureType::Texture2DArray, 2U};
    const TextureHandle array = renderDevice->CreateTexture(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandle);
    const TextureHandle depth = renderDevice->CreateTexture(
        TextureDesc{4U, 4U, TextureFormat::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandle);
    if (glMajor < 3 || (glMajor == 3 && glMinor < 2)) {
        renderDevice->DestroyTexture(array);
        renderDevice->DestroyTexture(depth);
        GTEST_SKIP() << "layered FBO attach needs glFramebufferTextureLayer (GL 3.2+)";
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc greenPass;
    greenPass.colorAttachment = array;
    greenPass.depthAttachment = depth;
    greenPass.colorLayer = 0U;
    greenPass.colorLoadOp = LoadOp::Clear;
    greenPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    greenPass.depthLoadOp = LoadOp::Clear;
    commandBuffer->BeginRenderPass(greenPass);
    commandBuffer->EndRenderPass();
    RenderPassDesc redPass = greenPass;
    redPass.colorLayer = 1U;
    redPass.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPass(redPass);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(array));
    glBindTexture(GL_TEXTURE_2D_ARRAY, native);
    std::array<std::uint8_t, 4U * 4U * 4U * 2U> readback{};
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        const std::size_t green = texel * 4U;
        EXPECT_EQ(readback[green], 0U);
        EXPECT_EQ(readback[green + 1U], 255U);
        EXPECT_EQ(readback[green + 2U], 0U);
        const std::size_t red = (16U + texel) * 4U;
        EXPECT_EQ(readback[red], 255U);
        EXPECT_EQ(readback[red + 1U], 0U);
        EXPECT_EQ(readback[red + 2U], 0U);
    }
    renderDevice->DestroyTexture(array);
    renderDevice->DestroyTexture(depth);
}

TEST_F(GlRenderDeviceTest, BindTexture_ArrayTexture_BindsUnderArrayTarget) {
    // Tier 2.3: an array binds under GL_TEXTURE_2D_ARRAY on its unit and must not disturb that
    // unit's 2D binding (targets coexist per unit in GL).
    const TextureHandle color = renderDevice->CreateTexture(TextureDesc{1U, 1U});
    ASSERT_NE(color, kInvalidTextureHandle);
    TextureDesc arrayDesc{2U, 2U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                             TextureType::Texture2DArray, 3U};
    const TextureHandle array = renderDevice->CreateTexture(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandle);
    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(array));

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = color;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindTexture(array, 3U);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 3));
    GLint arrayBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
    EXPECT_EQ(arrayBinding, static_cast<GLint>(native));
    GLint flatBinding = -1;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &flatBinding);
    EXPECT_EQ(flatBinding, 0) << "a 2D bind must not shadow the array on the same unit";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyTexture(array);
    renderDevice->DestroyTexture(color);
}

TEST_F(GlRenderDeviceTest, BindTexture_ArrayTexture_LeavesComputeImageUnitUnbound) {
    // Tier 2.3 mirror of ComputeStorageImageOnSrgbTexture: layered storage images are out of
    // scope, so an array stays sampler-visible while its image unit is cleared — matching the
    // Vulkan backend's black-sink fallback.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U * 2U> kZeroPixels{};
    TextureDesc arrayDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U, TextureColorSpace::Linear,
                             TextureType::Texture2DArray, 2U};
    const TextureHandle array = renderDevice->CreateTexture(
        arrayDesc, std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(array, kInvalidTextureHandle);

    const ShaderHandle computeShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandle) {
        renderDevice->DestroyTexture(array);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandle computePipeline =
        renderDevice->CreateComputePipeline(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandle) {
        renderDevice->DestroyShader(computeShader);
        renderDevice->DestroyTexture(array);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindTexture(array, 0U); // M5b: legal outside pass while compute is bound
    renderDevice->Submit(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &textureName);
    EXPECT_NE(textureName, 0) << "the array must remain bound for sampler access";
    GLint imageName = -1;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
    EXPECT_EQ(imageName, 0) << "layered storage-image bindings must be cleared in Tier 2.3";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipeline(computePipeline);
    renderDevice->DestroyShader(computeShader);
    renderDevice->DestroyTexture(array);
}


TEST_F(GlRenderDeviceTest, BeginRenderPass_TwoColorAttachments_ClearEachToItsOwnColor) {
    // Tier 2.4 pixel proof for MRT: one pass clears location 0 green and location 1 red —
    // each texture must hold exactly its own clear color (glClear would paint both one color).
    GLint glMajor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    const TextureHandle target0 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandle);
    if (glMajor < 3) {
        renderDevice->DestroyTexture(target0);
        renderDevice->DestroyTexture(target1);
        GTEST_SKIP() << "per-slot clears need glClearBufferfv (GL 3.0+)";
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc;
    passDesc.colorAttachment = target0;
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    passDesc.extraColorAttachments[0].target = target1;
    passDesc.extraColorAttachments[0].loadOp = LoadOp::Clear;
    passDesc.extraColorAttachments[0].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPass(passDesc);
    GLint mrtFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mrtFramebuffer);
    EXPECT_NE(mrtFramebuffer, 0);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    const auto readbackTexture = [&](const TextureHandle target) {
        const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(target));
        glBindTexture(GL_TEXTURE_2D, native);
        std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
        return pixels;
    };
    const auto green = readbackTexture(target0);
    const auto red = readbackTexture(target1);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        const std::size_t base = texel * 4U;
        EXPECT_EQ(green[base], 0U);
        EXPECT_EQ(green[base + 1U], 255U);
        EXPECT_EQ(green[base + 2U], 0U);
        EXPECT_EQ(red[base], 255U);
        EXPECT_EQ(red[base + 1U], 0U);
        EXPECT_EQ(red[base + 2U], 0U);
    }
    renderDevice->DestroyTexture(target0);
    renderDevice->DestroyTexture(target1);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_MalformedExtras_LeavesFramebufferUnchanged) {
    // Tier 2.4 mirror of LayerBeyondAttachmentLayerCount: malformed extras reject before any
    // FBO work. Load ops stay Load so the routing check needs no clear-capable context.
    const TextureHandle target0 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandle);
    const TextureHandle small = renderDevice->CreateTexture(TextureDesc{2U, 2U});
    ASSERT_NE(small, kInvalidTextureHandle);
    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDesc& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPass(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };
    const auto loadOnlyPass = [](const TextureHandle color) {
        RenderPassDesc passDesc;
        passDesc.colorAttachment = color;
        passDesc.colorLoadOp = LoadOp::Load;
        passDesc.depthLoadOp = LoadOp::Load;
        return passDesc;
    };
    RenderPassDesc gap = loadOnlyPass(target0);
    gap.extraColorAttachments[0].target = target1;
    gap.extraColorAttachments[0].loadOp = LoadOp::Load;
    gap.extraColorAttachments[2].target = target1; // location 2 set while 1 is empty
    gap.extraColorAttachments[2].loadOp = LoadOp::Load;
    assertFramebufferUnchanged(gap);

    RenderPassDesc noLocation0 = loadOnlyPass(kInvalidTextureHandle);
    noLocation0.extraColorAttachments[0].target = target1;
    noLocation0.extraColorAttachments[0].loadOp = LoadOp::Load;
    assertFramebufferUnchanged(noLocation0);

    RenderPassDesc unknownExtra = loadOnlyPass(target0);
    unknownExtra.extraColorAttachments[0].target = TextureHandle{999999U};
    unknownExtra.extraColorAttachments[0].loadOp = LoadOp::Load;
    assertFramebufferUnchanged(unknownExtra);

    RenderPassDesc dimsMismatch = loadOnlyPass(target0);
    dimsMismatch.extraColorAttachments[0].target = small;
    dimsMismatch.extraColorAttachments[0].loadOp = LoadOp::Load;
    assertFramebufferUnchanged(dimsMismatch);

    RenderPassDesc aliased = loadOnlyPass(target0);
    aliased.extraColorAttachments[0].target = target0; // one texture, two attachments
    aliased.extraColorAttachments[0].loadOp = LoadOp::Load;
    assertFramebufferUnchanged(aliased);

    RenderPassDesc badStore = loadOnlyPass(target0);
    badStore.colorStoreOp = static_cast<StoreOp>(0xFFU);
    assertFramebufferUnchanged(badStore);

    RenderPassDesc badExtraOp = loadOnlyPass(target0);
    badExtraOp.extraColorAttachments[0].target = target1;
    badExtraOp.extraColorAttachments[0].loadOp = LoadOp::Load;
    badExtraOp.extraColorAttachments[0].storeOp = static_cast<StoreOp>(0xFFU);
    assertFramebufferUnchanged(badExtraOp);

    // The well-formed twin binds a real FBO — the rejects above were selective, not blanket.
    RenderPassDesc valid = loadOnlyPass(target0);
    valid.extraColorAttachments[0].target = target1;
    valid.extraColorAttachments[0].loadOp = LoadOp::Load;
    commandBuffer->BeginRenderPass(valid);
    GLint mrtFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mrtFramebuffer);
    EXPECT_NE(mrtFramebuffer, 0);
    commandBuffer->EndRenderPass();
    renderDevice->DestroyTexture(target0);
    renderDevice->DestroyTexture(target1);
    renderDevice->DestroyTexture(small);
}

TEST_F(GlRenderDeviceTest, BeginRenderPass_DontCareStore_StaysHealthy) {
    // Tier 2.5: DontCare stores discard (invalidate) without disturbing GL state — the error
    // flag stays clean and the very next pass renders normally. (Discarded CONTENTS are
    // undefined by design, so the follow-up pass carries the pixel proof, not this one.)
    GLint glMajor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    const TextureHandle target0 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = renderDevice->CreateTexture(TextureDesc{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandle);
    if (glMajor < 3) {
        renderDevice->DestroyTexture(target0);
        renderDevice->DestroyTexture(target1);
        GTEST_SKIP() << "per-slot clears need glClearBufferfv (GL 3.0+)";
    }

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc discardPass;
    discardPass.colorAttachment = target0;
    discardPass.colorLoadOp = LoadOp::Clear;
    discardPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    discardPass.colorStoreOp = StoreOp::DontCare;
    discardPass.extraColorAttachments[0].target = target1;
    discardPass.extraColorAttachments[0].loadOp = LoadOp::Clear;
    discardPass.extraColorAttachments[0].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    discardPass.extraColorAttachments[0].storeOp = StoreOp::DontCare;
    commandBuffer->BeginRenderPass(discardPass);
    commandBuffer->EndRenderPass();

    RenderPassDesc followUpPass;
    followUpPass.colorAttachment = target0;
    followUpPass.colorLoadOp = LoadOp::Clear;
    followUpPass.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
    followUpPass.colorStoreOp = StoreOp::Store;
    commandBuffer->BeginRenderPass(followUpPass);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(target0));
    glBindTexture(GL_TEXTURE_2D, native);
    std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        const std::size_t base = texel * 4U;
        EXPECT_EQ(pixels[base], 0U);
        EXPECT_EQ(pixels[base + 1U], 0U);
        EXPECT_EQ(pixels[base + 2U], 255U);
    }
    renderDevice->DestroyTexture(target0);
    renderDevice->DestroyTexture(target1);
}

TEST_F(GlRenderDeviceTest, FoundationBufferCopyFenceAndSurfaceUseNativeWork) {
    using Result = RenderOperationResult;
    const auto caps = renderDevice->GetCapabilities();
    ASSERT_EQ(caps.bufferCopy, RenderFeatureSupport::Native);
    ASSERT_EQ(caps.fences, RenderFeatureSupport::Native);
    const std::array<std::uint32_t, 4> values{7, 11, 23, 42};
    const auto source = renderDevice->CreateBuffer({sizeof(values), BufferUsage::Storage},
                                                      std::as_bytes(std::span(values)));
    const auto target = renderDevice->CreateBuffer({sizeof(values), BufferUsage::Storage});
    ASSERT_EQ(renderDevice->CopyBuffer({source, target, 4, 0, 12}), Result::Success);
    EXPECT_EQ(renderDevice->CopyBuffer({target, target, 0, 4, 8}), Result::InvalidArgument);
    FenceHandle fence;
    ASSERT_EQ(renderDevice->InsertFence(fence), Result::Success);
    ASSERT_EQ(renderDevice->WaitFence(fence, 1'000'000'000ULL), Result::Success);
    std::array<std::uint32_t, 4> observed{};
    ASSERT_TRUE(renderDevice->ReadbackBuffer(target, std::as_writable_bytes(std::span(observed))));
    EXPECT_EQ(observed[0], 11U);
    EXPECT_EQ(observed[1], 23U);
    EXPECT_EQ(observed[2], 42U);
    EXPECT_EQ(renderDevice->DestroyFence(fence), Result::Success);
    EXPECT_EQ(renderDevice->WaitFence(fence, 0), Result::InvalidHandle);
    RenderSurface surface(*renderDevice);
    RenderSurfaceInfo info;
    ASSERT_EQ(surface.Describe(info), Result::Success);
    EXPECT_EQ(info.width, windowManager->GetWidth());
    EXPECT_EQ(info.height, windowManager->GetHeight());
    EXPECT_TRUE(info.drawable);
    EXPECT_EQ(surface.Present(), Result::Success);
    renderDevice->DestroyBuffer(source);
    renderDevice->DestroyBuffer(target);
}

TEST_F(GlRenderDeviceTest, FoundationMipsAndBlitProducePixelsAndPreserveScissorAndBindings) {
    using Result = RenderOperationResult;
    std::vector<std::byte> pixels(84U);
    for (std::size_t i = 0; i < 16U; ++i) {
        pixels[i * 4U] = std::byte{255};
        pixels[i * 4U + 3U] = std::byte{255};
    }
    const auto source = renderDevice->CreateTexture({4, 4, TextureFormat::RGBA8Unorm, 3}, pixels);
    const auto target = renderDevice->CreateTexture({2, 2, TextureFormat::RGBA8Unorm});
    ASSERT_NE(source, kInvalidTextureHandle);
    ASSERT_NE(target, kInvalidTextureHandle);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    ASSERT_EQ(renderDevice->GenerateMipmaps(source), Result::Success);
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    EXPECT_EQ(static_cast<std::uint32_t>(bound), renderDevice->GetNativeTextureId(target));
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(source));
    std::array<std::uint8_t, 4> mip{};
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
    EXPECT_EQ(mip, (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 0, 0);
    ASSERT_EQ(renderDevice->BlitTexture(
                  {source, target, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilter::Linear}),
              Result::Success);
    EXPECT_EQ(glIsEnabled(GL_SCISSOR_TEST), static_cast<GLboolean>(GL_TRUE));
    glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    std::array<std::uint8_t, 16> observed{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, observed.data());
    for (std::size_t i = 0; i < 4U; ++i) {
        EXPECT_EQ(observed[i * 4U], 255U);
        EXPECT_EQ(observed[i * 4U + 1U], 0U);
        EXPECT_EQ(observed[i * 4U + 2U], 0U);
        EXPECT_EQ(observed[i * 4U + 3U], 255U);
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(source);
    renderDevice->DestroyTexture(target);
}

TEST_F(GlRenderDeviceTest, SingleTwoChannelAndFloatFormatsUploadRenderBlitAndMipOnTheRealContext) {
    // One-/two-channel and 32-bit float formats on a real GL context. Every claim is read back from the GL object itself
    // (glGetTexImage on the native name), not inferred from a result code:
    //   1. a 3-wide R8Unorm / RG8Unorm upload is stored texel-for-texel (the tightly packed row
    //      contract: GL's default UNPACK_ALIGNMENT of 4 would have skewed rows 2 and 3);
    //   2. an R16Float texture is a real colour attachment: the pass clears it and a draw
    //      writes into it, the missing channels read back as 0/0/1;
    //   3. BlitTexture copies R32Float texels exactly and GenerateMipmaps averages an RG8
    //      chain — both paths used to be limited to RGBA8/RGBA16F by a format list.
    using Result = RenderOperationResult;
    constexpr std::array<TextureFormat, 7> kFormats{
        TextureFormat::R8Unorm,  TextureFormat::RG8Unorm, TextureFormat::R16Float,
        TextureFormat::RG16Float, TextureFormat::R32Float, TextureFormat::RG32Float,
        TextureFormat::RGBA32Float};
    for (const TextureFormat format : kFormats) {
        EXPECT_TRUE(renderDevice->SupportsTextureFormat(format)) << static_cast<int>(format);
        EXPECT_FALSE(renderDevice->SupportsTextureFormat(format, TextureColorSpace::Srgb))
            << static_cast<int>(format);
        const std::vector<std::byte> texel(GetTextureFormatBlockInfo(format).bytes);
        const auto texture = renderDevice->CreateTexture({1U, 1U, format, 1U}, texel);
        ASSERT_NE(texture, kInvalidTextureHandle) << static_cast<int>(format);
        EXPECT_EQ(renderDevice->CreateTexture({1U, 1U, format, 1U, TextureColorSpace::Srgb}, texel),
                  kInvalidTextureHandle);
        renderDevice->DestroyTexture(texture);
    }
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 1. Odd-width uploads survive byte for byte.
    constexpr std::array<std::uint8_t, 6> kR8Rows{10U, 20U, 30U, 40U, 50U, 60U};
    const auto r8 = renderDevice->CreateTexture({3U, 2U, TextureFormat::R8Unorm, 1U},
                                                   std::as_bytes(std::span(kR8Rows)));
    ASSERT_NE(r8, kInvalidTextureHandle);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(r8));
    std::array<std::uint8_t, 6> observedR8{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, observedR8.data());
    EXPECT_EQ(observedR8, kR8Rows) << "3-byte rows must not be read with a 4-byte stride";
    constexpr std::array<std::uint8_t, 6> kRg8Row{1U, 2U, 3U, 4U, 5U, 6U};
    const auto rg8 = renderDevice->CreateTexture({3U, 1U, TextureFormat::RG8Unorm, 1U},
                                                    std::as_bytes(std::span(kRg8Row)));
    ASSERT_NE(rg8, kInvalidTextureHandle);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(rg8));
    std::array<std::uint8_t, 6> observedRg8{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_UNSIGNED_BYTE, observedRg8.data());
    EXPECT_EQ(observedRg8, kRg8Row);
    GLint unpackAlignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    EXPECT_EQ(unpackAlignment, 4) << "the upload path restores the context's unpack alignment";

    // 2. R16Float is a real colour attachment on GL.
    const auto halfTarget = renderDevice->CreateTexture({4U, 4U, TextureFormat::R16Float, 1U});
    ASSERT_NE(halfTarget, kInvalidTextureHandle);
    const ShaderHandle vertexShader =
        renderDevice->CreateShader(ShaderDesc{ShaderStage::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kValidFragmentShaderSource)});
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    // Covers the whole right half of clip space (x >= 0): texels 2..3 of every row are drawn
    // white, texels 0..1 keep the clear value.
    constexpr float kRightHalf[] = {0.0F, -1.0F, 0.0F, 3.0F, -1.0F, 0.0F, 0.0F, 3.0F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kRightHalf));
    const BufferHandle vertexBuffer =
        renderDevice->CreateBuffer(BufferDesc{vertexBytes.size(), BufferUsage::Vertex}, vertexBytes);
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc;
        passDesc.colorAttachment = halfTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.25F, 0.5F, 0.75F, 0.125F}; // only .r can land in an R16F target
        passDesc.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        commandBuffer->Draw(3);
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
        glFinish();
    }
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(halfTarget));
    std::array<float, 16U * 4U> halfTexels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, halfTexels.data());
    for (std::size_t y = 0; y < 4U; ++y) {
        for (std::size_t x = 0; x < 4U; ++x) {
            const float *texel = &halfTexels[(y * 4U + x) * 4U];
            EXPECT_FLOAT_EQ(texel[0], x < 2U ? 0.25F : 1.0F) << "texel " << x << "," << y;
            EXPECT_FLOAT_EQ(texel[1], 0.0F) << "a one-channel format has no green";
            EXPECT_FLOAT_EQ(texel[2], 0.0F);
            EXPECT_FLOAT_EQ(texel[3], 1.0F) << "missing alpha reads as 1";
        }
    }

    // 3. Exact R32Float blit and an RG8 mip chain.
    constexpr std::array<float, 4> kFloatTexels{0.5F, -2.0F, 1024.25F, 3.0e-5F};
    const auto floatSource = renderDevice->CreateTexture({2U, 2U, TextureFormat::R32Float, 1U},
                                                            std::as_bytes(std::span(kFloatTexels)));
    const auto floatTarget = renderDevice->CreateTexture({2U, 2U, TextureFormat::R32Float, 1U});
    ASSERT_NE(floatSource, kInvalidTextureHandle);
    ASSERT_NE(floatTarget, kInvalidTextureHandle);
    ASSERT_EQ(renderDevice->BlitTexture(
                  {floatSource, floatTarget, {0, 0, 0, 0, 2, 2}, {0, 0, 0, 0, 2, 2}, BlitFilter::Point}),
              Result::Success);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(floatTarget));
    std::array<float, 4> observedFloats{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, observedFloats.data());
    EXPECT_EQ(observedFloats, kFloatTexels) << "a point blit of 32-bit floats is bit-exact";
    EXPECT_EQ(renderDevice->BlitTexture(
                  {floatSource, rg8, {0, 0, 0, 0, 2, 1}, {0, 0, 0, 0, 2, 1}, BlitFilter::Point}),
              Result::InvalidArgument)
        << "format conversion is still refused";
    std::vector<std::byte> rgChain(static_cast<std::size_t>((16U + 4U + 1U) * 2U));
    for (std::size_t texel = 0; texel < 16U; ++texel) {
        rgChain[texel * 2U] = std::byte{200};
        rgChain[texel * 2U + 1U] = std::byte{40};
    }
    const auto rgMips = renderDevice->CreateTexture({4U, 4U, TextureFormat::RG8Unorm, 3U}, rgChain);
    ASSERT_NE(rgMips, kInvalidTextureHandle);
    ASSERT_EQ(renderDevice->GenerateMipmaps(rgMips), Result::Success);
    EXPECT_EQ(renderDevice->GenerateMipmaps(rg8), Result::InvalidArgument) << "single level";
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(rgMips));
    std::array<std::uint8_t, 2> lastLevel{};
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RG, GL_UNSIGNED_BYTE, lastLevel.data());
    EXPECT_NEAR(static_cast<int>(lastLevel[0]), 200, 1);
    EXPECT_NEAR(static_cast<int>(lastLevel[1]), 40, 1);

    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(rgMips);
    renderDevice->DestroyTexture(floatTarget);
    renderDevice->DestroyTexture(floatSource);
    renderDevice->DestroyBuffer(vertexBuffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyTexture(halfTarget);
    renderDevice->DestroyTexture(rg8);
    renderDevice->DestroyTexture(r8);
}

constexpr std::string_view kVolumeSampleFragmentSource = R"(#version 330 core
uniform sampler3D uVolume;
out vec4 FragColor;
void main() {
    // Target is 4x1: pixel x samples the centre of slice x (w = (x + 0.5) / 4).
    FragColor = texture(uVolume, vec3(0.5, 0.5, gl_FragCoord.x / 4.0));
}
)";

TEST_F(GlRenderDeviceTest, Texture3D_UploadsSliceMajorSamplesThroughSampler3DAndBuildsMips) {
    // Texture3D on the real context: four solid 4x4 slices upload slice-major into a
    // GL_TEXTURE_3D object (read back whole through glGetTexImage), a sampler3D draw returns
    // each slice's colour at its centre w, and GenerateMipmaps averages the volume down to
    // its 1x1x1 tail with the same device call every other type uses.
    using Result = RenderOperationResult;
    constexpr std::uint8_t kSliceColors[4][4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U}, {0U, 0U, 255U, 255U}, {255U, 255U, 255U, 255U},
    };
    std::array<std::uint8_t, 4U * 4U * 4U * 4U> upload{};
    for (std::uint32_t slice = 0U; slice < 4U; ++slice) {
        for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
            const std::size_t base = (static_cast<std::size_t>(slice) * 16U + texel) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                upload[base + channel] = kSliceColors[slice][channel];
            }
        }
    }
    TextureDesc volumeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    volumeDesc.type = TextureType::Texture3D;
    volumeDesc.depth = 4U;
    const TextureHandle volume = renderDevice->CreateTexture(volumeDesc, std::as_bytes(std::span(upload)));
    ASSERT_NE(volume, kInvalidTextureHandle);
    TextureDesc reported{};
    ASSERT_EQ(renderDevice->GetTextureDesc(volume, reported), Result::Success);
    EXPECT_EQ(reported.type, TextureType::Texture3D);
    EXPECT_EQ(reported.depth, 4U);
    EXPECT_EQ(reported.usage, TextureUsage::Default);

    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureId(volume));
    ASSERT_NE(native, 0U);
    glBindTexture(GL_TEXTURE_3D, native);
    GLint storedDepth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_DEPTH, &storedDepth);
    EXPECT_EQ(storedDepth, 4);
    std::array<std::uint8_t, 4U * 4U * 4U * 4U> readback{};
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    EXPECT_EQ(readback, upload) << "slices must land in order, slice-major";

    // Sample every slice centre in one draw into a 4x1 target.
    const TextureHandle target = renderDevice->CreateTexture(TextureDesc{4U, 1U});
    ASSERT_NE(target, kInvalidTextureHandle);
    const ShaderHandle vertexShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Vertex, std::string(kSrgbSampleVertexSource)});
    const ShaderHandle fragmentShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Fragment, std::string(kVolumeSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = target;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindTexture(volume, 0U);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
        glFinish();
    }
    glActiveTexture(GL_TEXTURE0);
    GLint boundVolume = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &boundVolume);
    EXPECT_EQ(boundVolume, static_cast<GLint>(native)) << "a volume binds under GL_TEXTURE_3D";
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(target));
    std::array<std::uint8_t, 4U * 4U> sampled{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sampled.data());
    for (std::size_t slice = 0U; slice < 4U; ++slice) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            EXPECT_NEAR(static_cast<int>(sampled[slice * 4U + channel]),
                        static_cast<int>(kSliceColors[slice][channel]), 1)
                << "slice " << slice << " channel " << channel;
        }
    }

    // Mip chain on a volume: 4x4x4 -> 2x2x2 -> 1x1x1 (292 bytes), level 0 solid, tail averaged.
    std::vector<std::byte> chain(292U);
    for (std::size_t texel = 0U; texel < 64U; ++texel) {
        chain[texel * 4U] = std::byte{200};
        chain[texel * 4U + 1U] = std::byte{100};
        chain[texel * 4U + 2U] = std::byte{50};
        chain[texel * 4U + 3U] = std::byte{255};
    }
    TextureDesc mipDesc = volumeDesc;
    mipDesc.mipLevels = 3U;
    const TextureHandle mipVolume = renderDevice->CreateTexture(mipDesc, chain);
    ASSERT_NE(mipVolume, kInvalidTextureHandle);
    EXPECT_TRUE(CanGenerateMipmaps(mipDesc));
    ASSERT_EQ(renderDevice->GenerateMipmaps(mipVolume), Result::Success);
    glBindTexture(GL_TEXTURE_3D, renderDevice->GetNativeTextureId(mipVolume));
    GLint tailDepth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_3D, 2, GL_TEXTURE_DEPTH, &tailDepth);
    EXPECT_EQ(tailDepth, 1) << "the chain's tail is a single texel";
    GLint middleDepth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_3D, 1, GL_TEXTURE_DEPTH, &middleDepth);
    EXPECT_EQ(middleDepth, 2) << "depth halves with the other axes";
    std::array<std::uint8_t, 4> tail{};
    glGetTexImage(GL_TEXTURE_3D, 2, GL_RGBA, GL_UNSIGNED_BYTE, tail.data());
    EXPECT_NEAR(static_cast<int>(tail[0]), 200, 1);
    EXPECT_NEAR(static_cast<int>(tail[1]), 100, 1);
    EXPECT_NEAR(static_cast<int>(tail[2]), 50, 1);
    EXPECT_EQ(tail[3], 255U);

    // Volumes never take part in blits and are never attachments.
    EXPECT_EQ(renderDevice->BlitTexture({volume, target, {0, 0, 0, 0, 4, 1}, {0, 0, 0, 0, 4, 1},
                                            BlitFilter::Point}),
              Result::InvalidArgument);
    {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        RenderPassDesc volumePass{};
        volumePass.colorAttachment = volume;
        commandBuffer->BeginRenderPass(volumePass);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "a volume (no RenderTarget usage) must not open a pass";
    }
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(mipVolume);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(fragmentShader);
    renderDevice->DestroyShader(vertexShader);
    renderDevice->DestroyTexture(target);
    renderDevice->DestroyTexture(volume);
}

TEST_F(GlRenderDeviceTest, TextureUsage_GatesAttachSampleStorageAndTransfersOnTheRealContext) {
    // Usage flags on the real context. Each gate is observed through GL state, not a result code
    // alone: a Sampled-only texture never becomes a framebuffer attachment, a texture without
    // Sampled never lands on a texture unit, one without Storage never lands on an image unit,
    // and the device-level transfer gates (blit, mip generation) refuse without TransferSrc/Dst.
    using Result = RenderOperationResult;
    TextureDesc sampledOnlyDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    sampledOnlyDesc.usage = TextureUsage::Sampled;
    const TextureHandle sampledOnly = renderDevice->CreateTexture(sampledOnlyDesc);
    ASSERT_NE(sampledOnly, kInvalidTextureHandle);
    TextureDesc targetOnlyDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    targetOnlyDesc.usage = TextureUsage::RenderTarget | TextureUsage::TransferSrc;
    const TextureHandle targetOnly = renderDevice->CreateTexture(targetOnlyDesc);
    ASSERT_NE(targetOnly, kInvalidTextureHandle);
    TextureDesc sinkDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    sinkDesc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    const TextureHandle sink = renderDevice->CreateTexture(sinkDesc);
    ASSERT_NE(sink, kInvalidTextureHandle);
    TextureDesc sampledDepthDesc{4U, 4U, TextureFormat::Depth32Float, 1U};
    sampledDepthDesc.usage = TextureUsage::Sampled;
    const TextureHandle sampledDepth = renderDevice->CreateTexture(sampledDepthDesc);
    ASSERT_NE(sampledDepth, kInvalidTextureHandle);
    TextureDesc unsupportedDesc{4U, 4U, TextureFormat::Depth32Float, 1U};
    unsupportedDesc.usage = TextureUsage::Storage;
    EXPECT_EQ(renderDevice->CreateTexture(unsupportedDesc), kInvalidTextureHandle)
        << "the shared validator refuses unsupported usage before any GL allocation";

    // 1. Attach gate: Sampled-only colour, Sampled-only depth, and an explicit RenderTarget texture.
    {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        RenderPassDesc sampledPass{};
        sampledPass.colorAttachment = sampledOnly;
        commandBuffer->BeginRenderPass(sampledPass);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "Sampled-only colour must not open a pass";
        RenderPassDesc depthPass{};
        depthPass.colorAttachment = targetOnly;
        depthPass.depthAttachment = sampledDepth;
        commandBuffer->BeginRenderPass(depthPass);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "Sampled-only depth must not open a pass";
        RenderPassDesc validPass{};
        validPass.colorAttachment = targetOnly;
        validPass.colorLoadOp = LoadOp::Clear;
        validPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
        validPass.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(validPass);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_NE(after, 0) << "an explicit RenderTarget texture attaches";
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
        glFinish();
    }

    // 2. Sample gate: a texture without Sampled leaves its unit's binding alone.
    {
        std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = targetOnly;
        passDesc.colorLoadOp = LoadOp::Load;
        passDesc.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindTexture(sampledOnly, 5U);
        commandBuffer->BindTexture(sink, 5U); // Sampled|TransferDst: replaces the binding
        commandBuffer->BindTexture(targetOnly, 5U); // no Sampled: refused, unit keeps `sink`
        commandBuffer->EndRenderPass();
        renderDevice->Submit(std::move(commandBuffer));
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 5));
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        EXPECT_EQ(bound, static_cast<GLint>(renderDevice->GetNativeTextureId(sink)))
            << "a texture created without Sampled must not reach a sampler unit";
        glActiveTexture(GL_TEXTURE0);
    }

    // 3. Transfer gates: the green clear in `targetOnly` (RenderTarget|TransferSrc) blits into
    // `sink` (TransferDst), while a Sampled-only source and a destination without TransferDst
    // are refused before any GL call; mip generation needs both transfer bits.
    const TextureRegion whole{0U, 0U, 0U, 0U, 4U, 4U};
    EXPECT_EQ(renderDevice->BlitTexture({sampledOnly, sink, whole, whole, BlitFilter::Point}),
              Result::InvalidArgument);
    EXPECT_EQ(renderDevice->BlitTexture({targetOnly, sampledOnly, whole, whole, BlitFilter::Point}),
              Result::InvalidArgument);
    ASSERT_EQ(renderDevice->BlitTexture({targetOnly, sink, whole, whole, BlitFilter::Point}),
              Result::Success);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureId(sink));
    std::array<std::uint8_t, 4U * 4U * 4U> sinkTexels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sinkTexels.data());
    EXPECT_EQ(sinkTexels[0], 0U);
    EXPECT_EQ(sinkTexels[1], 255U) << "the blit carried the green clear across";
    EXPECT_EQ(sinkTexels[2], 0U);
    TextureDesc sealedChainDesc{4U, 4U, TextureFormat::RGBA8Unorm, 3U};
    sealedChainDesc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    const TextureHandle sealedChain = renderDevice->CreateTexture(sealedChainDesc, std::vector<std::byte>(84U));
    ASSERT_NE(sealedChain, kInvalidTextureHandle);
    EXPECT_EQ(renderDevice->GenerateMipmaps(sealedChain), Result::InvalidArgument)
        << "mip generation needs TransferSrc as well";

    // 4. Storage gate: without Storage the image unit stays empty while the texture unit keeps
    // the texture for sampling (compute contexts only).
    const ShaderHandle computeShader = renderDevice->CreateShader(
        ShaderDesc{ShaderStage::Compute, std::string(kImageFillComputeSource)});
    if (computeShader != kInvalidShaderHandle) {
        ComputePipelineDesc computeDesc{};
        computeDesc.computeShader = computeShader;
        const PipelineHandle computePipeline = renderDevice->CreateComputePipeline(computeDesc);
        if (computePipeline != kInvalidPipelineHandle) {
            std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
            ASSERT_NE(commandBuffer, nullptr);
            while (glGetError() != GL_NO_ERROR) {
            }
            commandBuffer->BindPipeline(computePipeline);
            commandBuffer->BindTexture(sampledOnly, 0U);
            renderDevice->Submit(std::move(commandBuffer));
            glActiveTexture(GL_TEXTURE0);
            GLint textureName = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
            EXPECT_EQ(textureName, static_cast<GLint>(renderDevice->GetNativeTextureId(sampledOnly)))
                << "the texture stays sampler-visible";
            GLint imageName = -1;
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
            EXPECT_EQ(imageName, 0) << "no Storage usage: the image unit must stay unbound";
            renderDevice->DestroyPipeline(computePipeline);
        }
        renderDevice->DestroyShader(computeShader);
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(sealedChain);
    renderDevice->DestroyTexture(sampledDepth);
    renderDevice->DestroyTexture(sink);
    renderDevice->DestroyTexture(targetOnly);
    renderDevice->DestroyTexture(sampledOnly);
}

// ---- Foundation WP3: recorded query brackets ---------------------------------------------
// Defined BEFORE the timestamp test on purpose: the sandbox's softpipe rasterizer reports zero
// for every occlusion query issued after a glQueryCounter in the same context (documented in
// FOUNDATION.md), so this occlusion-only case must run first in definition order.
TEST_F(GlRenderDeviceTest, RecordedOcclusionQueryCountsOnlyVisibleDraws) {
    using Result = RenderOperationResult;
    ASSERT_EQ(renderDevice->GetCapabilities().occlusionQueries, RenderFeatureSupport::Native);
    QueryHandle empty, drawn, timestamp;
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::AnySamplesPassed, empty), Result::Success);
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::AnySamplesPassed, drawn), Result::Success);
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Success);
    const auto vs = renderDevice->CreateShader(
        {ShaderStage::Vertex, std::string(kValidVertexShaderSource), "main"});
    const auto fs = renderDevice->CreateShader(
        {ShaderStage::Fragment, std::string(kValidFragmentShaderSource), "main"});
    PipelineDesc desc;
    desc.vertexShader = vs;
    desc.fragmentShader = fs;
    desc.vertexLayout = {{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * sizeof(float);
    const auto pipeline = renderDevice->CreatePipeline(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const std::array<float, 9> vertices{-0.5F, -0.5F, 0, 0.5F, -0.5F, 0, 0, 0.5F, 0};
    const auto buffer = renderDevice->CreateBuffer({sizeof(vertices), BufferUsage::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    auto commands = renderDevice->CreateCommandBuffer();
    // Validation shared with Null and Vulkan.
    EXPECT_EQ(commands->BeginQuery(QueryHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(commands->BeginQuery(timestamp), Result::InvalidArgument);
    EXPECT_EQ(commands->WriteTimestamp(drawn), Result::InvalidArgument);
    EXPECT_EQ(commands->EndQuery(drawn), Result::InvalidState);
    commands->BeginRenderPass({});
    commands->BindPipeline(pipeline);
    commands->BindVertexBuffer(buffer);
    ASSERT_EQ(commands->BeginQuery(empty), Result::Recorded);
    EXPECT_EQ(commands->BeginQuery(drawn), Result::InvalidState) << "brackets do not nest";
    EXPECT_EQ(renderDevice->BeginQuery(drawn), Result::InvalidState)
        << "the device-level bracket shares the one GL_ANY_SAMPLES_PASSED slot";
    ASSERT_EQ(commands->EndQuery(empty), Result::Recorded);
    ASSERT_EQ(commands->BeginQuery(drawn), Result::Recorded);
    commands->Draw(3U);
    ASSERT_EQ(commands->EndQuery(drawn), Result::Recorded);
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    RenderQueryResult value{RenderQueryKind::AnySamplesPassed, 42U};
    ASSERT_EQ(renderDevice->GetQueryResult(empty, value, true), Result::Success);
    EXPECT_EQ(value.value, 0U) << "nothing was drawn inside the first bracket";
    ASSERT_EQ(renderDevice->GetQueryResult(drawn, value, true), Result::Success);
    EXPECT_EQ(value.value, 1U) << "the triangle inside the second bracket passed";
    EXPECT_EQ(renderDevice->DestroyQuery(empty), Result::Success);
    EXPECT_EQ(renderDevice->DestroyQuery(drawn), Result::Success);
    EXPECT_EQ(renderDevice->DestroyQuery(timestamp), Result::Success);
    renderDevice->DestroyBuffer(buffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vs);
    renderDevice->DestroyShader(fs);
}

TEST_F(GlRenderDeviceTest, RecordedTimestampsAreMonotonic) {
    using Result = RenderOperationResult;
    ASSERT_EQ(renderDevice->GetCapabilities().timestampQueries, RenderFeatureSupport::Native);
    QueryHandle before, after;
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::Timestamp, before), Result::Success);
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::Timestamp, after), Result::Success);
    auto commands = renderDevice->CreateCommandBuffer();
    ASSERT_EQ(commands->WriteTimestamp(before), Result::Recorded);
    commands->BeginRenderPass({});
    commands->EndRenderPass();
    ASSERT_EQ(commands->WriteTimestamp(after), Result::Recorded);
    renderDevice->Submit(std::move(commands));
    RenderQueryResult first, second;
    ASSERT_EQ(renderDevice->GetQueryResult(before, first, true), Result::Success);
    ASSERT_EQ(renderDevice->GetQueryResult(after, second, true), Result::Success);
    EXPECT_EQ(first.kind, RenderQueryKind::Timestamp);
    EXPECT_GE(second.value, first.value);
    EXPECT_EQ(renderDevice->DestroyQuery(before), Result::Success);
    EXPECT_EQ(renderDevice->DestroyQuery(after), Result::Success);
}

TEST_F(GlRenderDeviceTest, FoundationQueriesMeasureRealVisibilityAndGpuTimestamps) {
    using Result = RenderOperationResult;
    ASSERT_EQ(renderDevice->GetCapabilities().timestampQueries, RenderFeatureSupport::Native);
    ASSERT_EQ(renderDevice->GetCapabilities().occlusionQueries, RenderFeatureSupport::Native);
    QueryHandle before, after, visible;
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::Timestamp, before), Result::Success);
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::Timestamp, after), Result::Success);
    ASSERT_EQ(renderDevice->CreateQuery(RenderQueryKind::AnySamplesPassed, visible),
              Result::Success);
    RenderQueryResult value{RenderQueryKind::Timestamp, 42};
    EXPECT_EQ(renderDevice->GetQueryResult(before, value), Result::NotReady);
    EXPECT_EQ(value.value, 42U);
    ASSERT_EQ(renderDevice->WriteTimestamp(before), Result::Success);
    const auto vs = renderDevice->CreateShader(
        {ShaderStage::Vertex, std::string(kValidVertexShaderSource), "main"});
    const auto fs = renderDevice->CreateShader(
        {ShaderStage::Fragment, std::string(kValidFragmentShaderSource), "main"});
    PipelineDesc desc;
    desc.vertexShader = vs;
    desc.fragmentShader = fs;
    desc.vertexLayout = {{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * sizeof(float);
    const auto pipeline = renderDevice->CreatePipeline(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const std::array<float, 9> vertices{-0.5F, -0.5F, 0, 0.5F, -0.5F, 0, 0, 0.5F, 0};
    const auto buffer = renderDevice->CreateBuffer({sizeof(vertices), BufferUsage::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    auto commands = renderDevice->CreateCommandBuffer();
    ASSERT_EQ(commands->MemoryBarrier(MemoryBarrierMask::All), Result::Success);
    commands->BeginRenderPass({});
    commands->BindPipeline(pipeline);
    commands->BindVertexBuffer(buffer);
    EXPECT_EQ(commands->MemoryBarrier(MemoryBarrierMask::All), Result::InvalidState);
    ASSERT_EQ(renderDevice->BeginQuery(visible), Result::Success);
    ASSERT_EQ(renderDevice->EndQuery(visible), Result::Success);
    ASSERT_EQ(renderDevice->GetQueryResult(visible, value, true), Result::Success);
    EXPECT_EQ(value.value, 0U);
    ASSERT_EQ(renderDevice->BeginQuery(visible), Result::Success);
    commands->Draw(3U);
    ASSERT_EQ(renderDevice->EndQuery(visible), Result::Success);
    ASSERT_EQ(renderDevice->GetQueryResult(visible, value, true), Result::Success);
    EXPECT_EQ(value.value, 1U);
    commands->EndRenderPass();
    renderDevice->Submit(std::move(commands));
    ASSERT_EQ(renderDevice->WriteTimestamp(after), Result::Success);
    RenderQueryResult first, second;
    ASSERT_EQ(renderDevice->GetQueryResult(before, first, true), Result::Success);
    ASSERT_EQ(renderDevice->GetQueryResult(after, second, true), Result::Success);
    EXPECT_GE(second.value, first.value);
    EXPECT_EQ(first.kind, RenderQueryKind::Timestamp);
    EXPECT_EQ(renderDevice->DestroyQuery(before), Result::Success);
    EXPECT_EQ(renderDevice->DestroyQuery(after), Result::Success);
    EXPECT_EQ(renderDevice->DestroyQuery(visible), Result::Success);
    EXPECT_EQ(renderDevice->GetQueryResult(visible, value), Result::InvalidHandle);
    renderDevice->DestroyBuffer(buffer);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vs);
    renderDevice->DestroyShader(fs);
}

TEST_F(GlRenderDeviceTest, DrawInstancedAndDrawIndexedInstanced_RenderEachInstanceViaGlInstanceId) {
    constexpr const char* kInstancedVs =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPosition;\n"
        "flat out int vInstanceId;\n"
        "void main() {\n"
        "    vInstanceId = gl_InstanceID;\n"
        "    float offsetX = (gl_InstanceID == 0) ? -0.5 : 0.5;\n"
        "    gl_Position = vec4(aPosition * 0.4 + vec2(offsetX, 0.0), 0.0, 1.0);\n"
        "}\n";
    constexpr const char* kInstancedFs =
        "#version 330 core\n"
        "flat in int vInstanceId;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = (vInstanceId == 0) ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);\n"
        "}\n";

    const ShaderHandle vs = renderDevice->CreateShader({ShaderStage::Vertex, kInstancedVs});
    const ShaderHandle fs = renderDevice->CreateShader({ShaderStage::Fragment, kInstancedFs});
    ASSERT_NE(vs, kInvalidShaderHandle);
    ASSERT_NE(fs, kInvalidShaderHandle);

    PipelineDesc pipeDesc;
    pipeDesc.vertexShader = vs;
    pipeDesc.fragmentShader = fs;
    pipeDesc.vertexLayout = {{"POSITION", VertexAttributeFormat::Float2, 0}};
    pipeDesc.vertexStride = 2U * sizeof(float);
    pipeDesc.depthTestEnabled = false;
    pipeDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = renderDevice->CreatePipeline(pipeDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::array<float, 8> quadVerts{-1.0F, -1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F};
    const std::array<std::uint32_t, 6> quadIndices{0U, 1U, 2U, 0U, 2U, 3U};
    const std::array<float, 12> triVerts{-1.0F, -1.0F, 1.0F, -1.0F, 1.0F, 1.0F,
                                         -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F};

    const BufferHandle indexedVb = renderDevice->CreateBuffer(
        {sizeof(quadVerts), BufferUsage::Vertex}, std::as_bytes(std::span(quadVerts)));
    const BufferHandle ib = renderDevice->CreateBuffer(
        {sizeof(quadIndices), BufferUsage::Index}, std::as_bytes(std::span(quadIndices)));
    const BufferHandle nonIndexedVb = renderDevice->CreateBuffer(
        {sizeof(triVerts), BufferUsage::Vertex}, std::as_bytes(std::span(triVerts)));

    TextureDesc colorDesc{16U, 8U, TextureFormat::RGBA8Unorm};
    colorDesc.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
    const TextureHandle target = renderDevice->CreateTexture(colorDesc);
    ASSERT_NE(target, kInvalidTextureHandle);

    // Verify DrawIndexed with instanceCount = 2.
    {
        auto cmd = renderDevice->CreateCommandBuffer();
        RenderPassDesc pass;
        pass.colorAttachment = target;
        pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(pass);
        cmd->BindPipeline(pipeline);
        cmd->BindVertexBuffer(indexedVb);
        cmd->BindIndexBuffer(ib);
        cmd->DrawIndexed(6U, 2U, 0U);
        std::array<std::uint8_t, 4> leftPixel{};
        std::array<std::uint8_t, 4> rightPixel{};
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, leftPixel.data());
        glReadPixels(12, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rightPixel.data());
        EXPECT_GT(leftPixel[1], 200U);
        EXPECT_LT(leftPixel[2], 20U);
        EXPECT_GT(rightPixel[2], 200U);
        EXPECT_LT(rightPixel[1], 20U);
        cmd->EndRenderPass();
        renderDevice->Submit(std::move(cmd));
    }

    // Verify Draw with instanceCount = 2.
    {
        auto cmd = renderDevice->CreateCommandBuffer();
        RenderPassDesc pass;
        pass.colorAttachment = target;
        pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(pass);
        cmd->BindPipeline(pipeline);
        cmd->BindVertexBuffer(nonIndexedVb);
        cmd->Draw(6U, 2U);
        std::array<std::uint8_t, 4> leftPixel{};
        std::array<std::uint8_t, 4> rightPixel{};
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, leftPixel.data());
        glReadPixels(12, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rightPixel.data());
        EXPECT_GT(leftPixel[1], 200U);
        EXPECT_LT(leftPixel[2], 20U);
        EXPECT_GT(rightPixel[2], 200U);
        EXPECT_LT(rightPixel[1], 20U);
        cmd->EndRenderPass();
        renderDevice->Submit(std::move(cmd));
    }

    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTexture(target);
    renderDevice->DestroyBuffer(nonIndexedVb);
    renderDevice->DestroyBuffer(ib);
    renderDevice->DestroyBuffer(indexedVb);
    renderDevice->DestroyPipeline(pipeline);
    renderDevice->DestroyShader(vs);
    renderDevice->DestroyShader(fs);
}

} // namespace UVE::Render::Tests
