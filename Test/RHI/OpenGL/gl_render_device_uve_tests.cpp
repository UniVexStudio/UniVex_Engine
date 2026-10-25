// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/rhi/bind_group_uve.h"
#include "uve/rhi/render_surface_uve.h"
#include "uve/asset/asset_bundle_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"
#include "uve/rhi_shader/shader_manager_uve.h"
#include "uve/threading/thread_pool_uve.h"

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
#include "uve/rhi_opengl/gl_functions_uve.h"
#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include <chrono>
#include <limits>

#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/window/null_window_manager_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

// Same rationale as tests/window/window_manager_uve_tests.cpp: this sandbox's Mesa/llvmpipe GLX
// stack caps at OpenGL 4.5 Core, so tests explicitly request 4.5 rather than the production
// default (4.6) to isolate "no display available" as the only GTEST_SKIP() reason.
[[nodiscard]] Window::WindowDescUVE MakeTestWindowDescUVE() {
    Window::WindowDescUVE desc;
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

[[nodiscard]] std::string WithShaderStageDefineUVE(std::string_view source, std::string_view stageDefine) {
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
class GlRenderDeviceUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager = std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for GlRenderDeviceUVE - skipping (run under "
                            "xvfb-run to exercise this test)";
        }
        renderDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<GlRenderDeviceUVE> renderDevice;
};

TEST(GlRenderDeviceUnavailableUVETest, InvalidWindowManager_ConstructsAsUnusableWithoutAbort) {
    Window::NullWindowManagerUVE windowManager;
    GlRenderDeviceUVE renderDevice(windowManager);

    EXPECT_FALSE(renderDevice.IsUsableUVE());
    EXPECT_EQ(renderDevice.GetBackendNameUVE(), "OpenGL");
    EXPECT_EQ(renderDevice.GetLiveResourceCountUVE(), 0U);
    renderDevice.PresentUVE();
}

TEST_F(GlRenderDeviceUVETest, BindGroupAppliesRealTextureSamplerAndBufferBindings) {
    const auto uniform = renderDevice->CreateBufferUVE({64U, BufferUsageUVE::Uniform});
    const auto storage = renderDevice->CreateBufferUVE({64U, BufferUsageUVE::Storage});
    const auto texture = renderDevice->CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    const auto sampler = renderDevice->CreateSamplerUVE({});
    ASSERT_NE(uniform, kInvalidBufferHandleUVE);
    ASSERT_NE(storage, kInvalidBufferHandleUVE);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    ASSERT_NE(sampler, kInvalidSamplerHandleUVE);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0U, 0U);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0U, 0U);
    glBindSampler(0U, 0U);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0U);
    const std::array<ResourceBindingUVE, 4> bindings{
        BindUniformBufferCommandUVE{uniform, 0U}, BindStorageBufferCommandUVE{storage, 0U},
        BindTextureCommandUVE{texture, 0U}, BindSamplerCommandUVE{sampler, 0U}};
    auto commands = renderDevice->CreateCommandBufferUVE();
    commands->BeginRenderPassUVE({});
    BindGroupUVE(*commands, {bindings, {}});
    GLint name = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegeri_v(GL_SAMPLER_BINDING, 0U, &name); EXPECT_GT(name, 0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name); EXPECT_GT(name, 0);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    renderDevice->DestroyBufferUVE(uniform);
    renderDevice->DestroyBufferUVE(storage);
    renderDevice->DestroyTextureUVE(texture);
    renderDevice->DestroySamplerUVE(sampler);
}

// These sentinels are only used to check symbol resolution, never invoked as GL calls.
void LoaderSentinelUVE() {}

TEST(GlFunctionsUVETest, BlendEquationIsLoadedAndRequiredForTheBaseline) {
    const auto resolve = +[](const char*) -> void* {
        return reinterpret_cast<void*>(&LoaderSentinelUVE);
    };
    auto loaded = Detail::LoadGlFunctionsUVE(resolve);
    ASSERT_TRUE(loaded.IsCompleteUVE());
    ASSERT_NE(loaded.glBlendEquation, nullptr);
    loaded.glBlendEquation = nullptr;
    EXPECT_FALSE(loaded.IsCompleteUVE());
    const auto missingBlend = +[](const char* name) -> void* {
        if (std::string_view{name} == "glBlendEquation") return nullptr;
        return reinterpret_cast<void*>(&LoaderSentinelUVE);
    };
    EXPECT_FALSE(Detail::LoadGlFunctionsUVE(missingBlend).IsCompleteUVE());
}

TEST(GlFunctionsUVETest, StencilSymbolsAreLoadedAndRequiredForTheBaseline) {
    const auto resolve = +[](const char*) -> void* {
        return reinterpret_cast<void*>(&LoaderSentinelUVE);
    };
    const Detail::GlFunctionsUVE loaded = Detail::LoadGlFunctionsUVE(resolve);
    ASSERT_TRUE(loaded.IsCompleteUVE());
    ASSERT_NE(loaded.glStencilFuncSeparate, nullptr);
    ASSERT_NE(loaded.glStencilOpSeparate, nullptr);
    ASSERT_NE(loaded.glStencilMaskSeparate, nullptr);
    auto missing = loaded;
    missing.glStencilFuncSeparate = nullptr;
    EXPECT_FALSE(missing.IsCompleteUVE());
    missing = loaded;
    missing.glStencilOpSeparate = nullptr;
    EXPECT_FALSE(missing.IsCompleteUVE());
    missing = loaded;
    missing.glStencilMaskSeparate = nullptr;
    EXPECT_FALSE(missing.IsCompleteUVE());
    const auto missingStencil = +[](const char* name) -> void* {
        if (std::string_view{name}.starts_with("glStencil")) return nullptr;
        return reinterpret_cast<void*>(&LoaderSentinelUVE);
    };
    EXPECT_FALSE(Detail::LoadGlFunctionsUVE(missingStencil).IsCompleteUVE());
}

TEST_F(GlRenderDeviceUVETest, TimelineSemaphoresAreExplicitlyUnsupportedRatherThanModeled) {
    EXPECT_EQ(renderDevice->GetCapabilitiesUVE().timelineSemaphores, RenderFeatureSupportUVE::Unsupported);
    SemaphoreHandleUVE semaphore{7U};
    EXPECT_EQ(renderDevice->CreateSemaphoreUVE(SemaphoreDescUVE{1U}, semaphore),
              RenderOperationResultUVE::Unsupported);
    EXPECT_EQ(semaphore.value, 7U) << "a refused create must leave the output untouched";
    std::uint64_t counter = 0U;
    EXPECT_EQ(renderDevice->GetSemaphoreCounterUVE(kInvalidSemaphoreHandleUVE, counter),
              RenderOperationResultUVE::Unsupported);
    EXPECT_EQ(counter, 0U);
    EXPECT_EQ(renderDevice->SignalSemaphoreUVE(kInvalidSemaphoreHandleUVE, 1U),
              RenderOperationResultUVE::Unsupported);
    EXPECT_EQ(renderDevice->WaitSemaphoreUVE(kInvalidSemaphoreHandleUVE, 1U, 0U),
              RenderOperationResultUVE::Unsupported);
    EXPECT_EQ(renderDevice->DestroySemaphoreUVE(kInvalidSemaphoreHandleUVE),
              RenderOperationResultUVE::Unsupported);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR))
        << "refusing the unsupported calls must not touch GL state";
}

TEST_F(GlRenderDeviceUVETest, SubmitDescIsUnsupportedWithoutTimelines) {
    // Foundation WP4: GL has no timeline objects, so a non-empty SubmitDescUVE is refused as
    // Unsupported (never simulated), while an empty desc is exactly SubmitUVE.
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().timelineSemaphores, RenderFeatureSupportUVE::Unsupported);
    const std::array<SemaphoreSignalUVE, 1U> signals{SemaphoreSignalUVE{SemaphoreHandleUVE{1U}, 1U}};
    auto refused = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(refused, nullptr);
    EXPECT_EQ(renderDevice->SubmitWithSyncUVE(std::move(refused), SubmitDescUVE{{}, signals}),
              RenderOperationResultUVE::Unsupported);
    auto plain = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(plain, nullptr);
    plain->BeginRenderPassUVE({});
    plain->EndRenderPassUVE();
    EXPECT_EQ(renderDevice->SubmitWithSyncUVE(std::move(plain), SubmitDescUVE{}), RenderOperationResultUVE::Success);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceUVETest, GeometryStageChangesRasterizedPositionAndSurvivesShaderDeletion) {
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().geometryShaders, RenderFeatureSupportUVE::Native);
    const auto vertex = renderDevice->CreateShaderUVE({ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const auto fragment = renderDevice->CreateShaderUVE({ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    const auto geometry = renderDevice->CreateShaderUVE({ShaderStageUVE::Geometry, R"(#version 330 core
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
    ASSERT_NE(vertex, kInvalidShaderHandleUVE);
    ASSERT_NE(fragment, kInvalidShaderHandleUVE);
    ASSERT_NE(geometry, kInvalidShaderHandleUVE);
    PipelineDescUVE desc;
    desc.vertexShader = vertex;
    desc.fragmentShader = fragment;
    desc.geometryShader = geometry;
    desc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    desc.vertexStride = 3U * sizeof(float);
    desc.depthTestEnabled = false;
    std::string info;
    const auto pipeline = renderDevice->CreatePipelineUVE(desc, &info);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE) << info;
    renderDevice->DestroyShaderUVE(geometry);
    renderDevice->DestroyShaderUVE(fragment);
    renderDevice->DestroyShaderUVE(vertex);

    // The source triangle occupies the left half; only execution of the GS moves it right.
    constexpr std::array<float, 9> vertices{-1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    const auto buffer = renderDevice->CreateBufferUVE({sizeof(vertices), BufferUsageUVE::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    const auto target = renderDevice->CreateTextureUVE({8U, 8U, TextureFormatUVE::RGBA8Unorm});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);
    ASSERT_NE(target, kInvalidTextureHandleUVE);
    auto commands = renderDevice->CreateCommandBufferUVE();
    RenderPassDescUVE pass;
    pass.colorAttachment = target;
    pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    commands->BeginRenderPassUVE(pass);
    commands->BindPipelineUVE(pipeline);
    commands->BindVertexBufferUVE(buffer);
    commands->DrawUVE(3U);
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    std::array<std::uint8_t, 8U * 8U * 4U> pixels{};
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    EXPECT_LT(pixels[(1U * 8U + 0U) * 4U], 8U);
    EXPECT_GT(pixels[(1U * 8U + 4U) * 4U], 240U);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyTextureUVE(target);
    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, GeometryPipelineRejectsUnknownDestroyedAndWrongStageHandles) {
    const auto vertex = renderDevice->CreateShaderUVE({ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const auto fragment = renderDevice->CreateShaderUVE({ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertex, kInvalidShaderHandleUVE);
    ASSERT_NE(fragment, kInvalidShaderHandleUVE);
    PipelineDescUVE desc;
    desc.vertexShader = vertex;
    desc.fragmentShader = fragment;
    desc.geometryShader = vertex;
    std::string info;
    EXPECT_EQ(renderDevice->CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
    EXPECT_NE(info.find("Geometry-stage"), std::string::npos);
    desc.geometryShader = ShaderHandleUVE{999999U};
    EXPECT_EQ(renderDevice->CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
    EXPECT_NE(info.find("live shader"), std::string::npos);
    const auto deleted = renderDevice->CreateShaderUVE({ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    ASSERT_NE(deleted, kInvalidShaderHandleUVE);
    renderDevice->DestroyShaderUVE(deleted);
    desc.geometryShader = deleted;
    EXPECT_EQ(renderDevice->CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
    EXPECT_NE(info.find("live shader"), std::string::npos);
    desc.geometryShader = kInvalidShaderHandleUVE;
    std::swap(desc.vertexShader, desc.fragmentShader);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(desc, &info), kInvalidPipelineHandleUVE);
    EXPECT_NE(info.find("wrong stages"), std::string::npos);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyShaderUVE(fragment);
    renderDevice->DestroyShaderUVE(vertex);
}

TEST_F(GlRenderDeviceUVETest, DepthStencilDiscardUsesSeparateAttachmentAspects) {
    const auto color = renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::RGBA8Unorm});
    const auto depth = renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::Depth24Stencil8});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    ASSERT_NE(depth, kInvalidTextureHandleUVE);
    RenderPassDescUVE pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depth;
    pass.colorStoreOp = StoreOpUVE::DontCare;
    pass.depthStoreOp = StoreOpUVE::DontCare;
    auto commands = renderDevice->CreateCommandBufferUVE();
    commands->BeginRenderPassUVE(pass);
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTextureUVE(depth);
    renderDevice->DestroyTextureUVE(color);
}

TEST_F(GlRenderDeviceUVETest, IndexedUniformArrayUpdatesReachTheGpuWithoutDuplicatingReflection) {
    const auto vertex = renderDevice->CreateShaderUVE({ShaderStageUVE::Vertex,
        "#version 330 core\nvoid main() { vec2 p[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(p[gl_VertexID],0,1); }"});
    const auto fragment = renderDevice->CreateShaderUVE({ShaderStageUVE::Fragment,
        "#version 330 core\nuniform vec3 uPalette[2]; out vec4 color; void main() { color=vec4(uPalette[0]+uPalette[1],1); }"});
    ASSERT_NE(vertex, kInvalidShaderHandleUVE);
    ASSERT_NE(fragment, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertex;
    pipelineDesc.fragmentShader = fragment;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const auto pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    const auto reflected = renderDevice->GetPipelineUniformsUVE(pipeline);
    ASSERT_EQ(reflected.size(), 1U);
    EXPECT_EQ(reflected.front().name, "uPalette");
    EXPECT_EQ(reflected.front().arraySize, 2U);
    TextureDescUVE textureDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm};
    textureDesc.usage = TextureUsageUVE::RenderTarget | TextureUsageUVE::Sampled;
    const auto color = renderDevice->CreateTextureUVE(textureDesc);
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    auto commands = renderDevice->CreateCommandBufferUVE();
    RenderPassDescUVE pass;
    pass.colorAttachment = color;
    commands->BeginRenderPassUVE(pass);
    commands->BindPipelineUVE(pipeline);
    commands->SetUniformVector3UVE("uPalette[0]", {0.0F, 0.0F, 0.0F});
    commands->SetUniformVector3UVE("uPalette[1]", {0.8F, 0.2F, 0.1F});
    commands->SetUniformVector3UVE("uPalette[2]", {0.0F, 1.0F, 0.0F});
    commands->DrawUVE(3U);
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], 190U);
    EXPECT_GE(pixel[1], 40U);
    EXPECT_LE(pixel[1], 65U);
    EXPECT_LE(pixel[2], 35U);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    renderDevice->DestroyTextureUVE(color);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertex);
    renderDevice->DestroyShaderUVE(fragment);
}

TEST_F(GlRenderDeviceUVETest, StencilPipelineMasksSecondDrawThroughDepthStencilAttachment) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE whiteShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    const ShaderHandleUVE redShader = renderDevice->CreateShaderUVE(ShaderDescUVE{
        ShaderStageUVE::Fragment,
        "#version 330 core\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n"});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(whiteShader, kInvalidShaderHandleUVE);
    ASSERT_NE(redShader, kInvalidShaderHandleUVE);

    PipelineDescUVE writerDesc;
    writerDesc.vertexShader = vertexShader;
    writerDesc.fragmentShader = whiteShader;
    writerDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    writerDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    writerDesc.depthTestEnabled = false;
    writerDesc.stencil.enabled = true;
    writerDesc.stencil.front.compare = DepthCompareUVE::Always;
    writerDesc.stencil.front.depthPass = StencilOpUVE::Replace;
    writerDesc.stencil.front.reference = 7U;
    const PipelineHandleUVE writer = renderDevice->CreatePipelineUVE(writerDesc);
    ASSERT_NE(writer, kInvalidPipelineHandleUVE);

    PipelineDescUVE readerDesc = writerDesc;
    readerDesc.fragmentShader = redShader;
    readerDesc.stencil.front.compare = DepthCompareUVE::Equal;
    readerDesc.stencil.front.depthPass = StencilOpUVE::Keep;
    const PipelineHandleUVE reader = renderDevice->CreatePipelineUVE(readerDesc);
    ASSERT_NE(reader, kInvalidPipelineHandleUVE);

    const TextureHandleUVE color = renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::RGBA8Unorm});
    const TextureHandleUVE depthStencil =
        renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::Depth24Stencil8});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    ASSERT_NE(depthStencil, kInvalidTextureHandleUVE);

    constexpr std::array<float, 9> leftTriangle{-1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    constexpr std::array<float, 9> fullscreenTriangle{-1.0F, -1.0F, 0.0F, 3.0F, -1.0F, 0.0F, -1.0F, 3.0F, 0.0F};
    const BufferHandleUVE leftBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(leftTriangle), BufferUsageUVE::Vertex}, std::as_bytes(std::span(leftTriangle)));
    const BufferHandleUVE fullscreenBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(fullscreenTriangle), BufferUsageUVE::Vertex},
        std::as_bytes(std::span(fullscreenTriangle)));
    ASSERT_NE(leftBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(fullscreenBuffer, kInvalidBufferHandleUVE);

    auto commands = renderDevice->CreateCommandBufferUVE();
    RenderPassDescUVE pass;
    pass.colorAttachment = color;
    pass.depthAttachment = depthStencil;
    pass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    pass.clearStencil = 0U;
    commands->BeginRenderPassUVE(pass);
    commands->BindPipelineUVE(writer);
    commands->BindVertexBufferUVE(leftBuffer);
    commands->DrawUVE(3U);
    commands->BindPipelineUVE(reader);
    commands->BindVertexBufferUVE(fullscreenBuffer);
    commands->DrawUVE(3U);
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(color));
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

    renderDevice->DestroyBufferUVE(fullscreenBuffer);
    renderDevice->DestroyBufferUVE(leftBuffer);
    renderDevice->DestroyTextureUVE(depthStencil);
    renderDevice->DestroyTextureUVE(color);
    renderDevice->DestroyPipelineUVE(reader);
    renderDevice->DestroyPipelineUVE(writer);
    renderDevice->DestroyShaderUVE(redShader);
    renderDevice->DestroyShaderUVE(whiteShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, TextureLeaseRetiresOnOwnerThreadAfterWorkerSubmission) {
    const auto texture = renderDevice->CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    auto lease = renderDevice->AdoptTransientTexturesUVE(std::vector<TextureHandleUVE>{texture});
    ASSERT_NE(lease, nullptr);
    auto commands = renderDevice->CreateCommandBufferUVE();
    commands->RetainResourcesUVE(std::move(lease));
    RenderPassDescUVE pass; pass.colorAttachment = texture; pass.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commands->BeginRenderPassUVE(pass);
    std::array<std::uint8_t, 4> pixel{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], 240U); EXPECT_LT(pixel[1], 8U);
    commands->EndRenderPassUVE();
    std::thread worker([this, commands = std::move(commands)]() mutable {
        renderDevice->SubmitUVE(std::move(commands));
    });
    worker.join();
    TextureDescUVE description;
    EXPECT_EQ(renderDevice->GetTextureDescUVE(texture, description), RenderOperationResultUVE::Success);
    EXPECT_EQ(renderDevice->WaitIdleUVE(), RenderOperationResultUVE::Success);
    EXPECT_EQ(renderDevice->GetTextureDescUVE(texture, description), RenderOperationResultUVE::InvalidHandle);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceUVETest, GetBackendNameUVE_ReturnsOpenGL) {
    EXPECT_EQ(renderDevice->GetBackendNameUVE(), "OpenGL");
}

TEST_F(GlRenderDeviceUVETest, CreateShaderUVE_UnknownStage_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    std::string infoLog = "stale";
    const ShaderHandleUVE invalid = renderDevice->CreateShaderUVE(
        ShaderDescUVE{static_cast<ShaderStageUVE>(0xFFU), std::string(kValidVertexShaderSource)}, &infoLog);

    EXPECT_EQ(invalid, kInvalidShaderHandleUVE);
    EXPECT_EQ(infoLog, "Unknown shader stage.");
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateBufferUVE_UnknownUsage_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    const BufferHandleUVE invalid =
        renderDevice->CreateBufferUVE(BufferDescUVE{16U, static_cast<BufferUsageUVE>(0xFFU)});

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, BufferMemoryHintsBecomeTheGlUsageTokenAndEveryPlacementReadsBack) {
    // GL has one memory, so the two hints fold into glBufferData's usage token — read back from
    // the GL object through GL_BUFFER_USAGE after a real bind — and the unchanged default
    // (Auto/Static) still allocates GL_STATIC_DRAW exactly as every buffer did before.
    struct TokenCaseUVE final {
        BufferMemoryUVE memory;
        BufferUpdateFrequencyUVE frequency;
        GLint expectedToken;
    };
    constexpr std::array<TokenCaseUVE, 6> kCases{{
        {BufferMemoryUVE::Auto, BufferUpdateFrequencyUVE::Static, GL_STATIC_DRAW},
        {BufferMemoryUVE::DeviceLocal, BufferUpdateFrequencyUVE::Dynamic, GL_DYNAMIC_DRAW},
        {BufferMemoryUVE::Upload, BufferUpdateFrequencyUVE::Stream, GL_STREAM_DRAW},
        {BufferMemoryUVE::Readback, BufferUpdateFrequencyUVE::Static, GL_STATIC_READ},
        {BufferMemoryUVE::Readback, BufferUpdateFrequencyUVE::Dynamic, GL_DYNAMIC_READ},
        {BufferMemoryUVE::Readback, BufferUpdateFrequencyUVE::Stream, GL_STREAM_READ},
    }};
    const std::array<std::uint32_t, 4> values{7U, 11U, 23U, 42U};
    auto commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPassUVE({});
    for (const TokenCaseUVE &testCase : kCases) {
        const BufferDescUVE desc{sizeof(values), BufferUsageUVE::Uniform, testCase.memory, testCase.frequency};
        const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(desc, std::as_bytes(std::span(values)));
        ASSERT_NE(buffer, kInvalidBufferHandleUVE);
        BufferDescUVE reported{};
        ASSERT_EQ(renderDevice->GetBufferDescUVE(buffer, reported), RenderOperationResultUVE::Success);
        EXPECT_EQ(reported.memory, testCase.memory);
        EXPECT_EQ(reported.updateFrequency, testCase.frequency);
        EXPECT_EQ(reported.usage, BufferUsageUVE::Uniform);
        commands->BindUniformBufferUVE(buffer, 0U); // glBindBufferBase also sets the generic binding
        GLint token = 0;
        glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_USAGE, &token);
        EXPECT_EQ(token, testCase.expectedToken)
            << "memory " << static_cast<int>(testCase.memory) << " / frequency "
            << static_cast<int>(testCase.frequency);
        const std::array<std::uint32_t, 1> patch{99U};
        ASSERT_TRUE(renderDevice->UpdateBufferUVE(buffer, std::as_bytes(std::span(patch)), 4U));
        std::array<std::uint32_t, 4> observed{};
        ASSERT_TRUE(renderDevice->ReadbackBufferUVE(buffer, std::as_writable_bytes(std::span(observed))));
        EXPECT_EQ(observed, (std::array<std::uint32_t, 4>{7U, 99U, 23U, 42U}));
        renderDevice->DestroyBufferUVE(buffer);
    }
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    EXPECT_EQ(renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex,
                                                          static_cast<BufferMemoryUVE>(0xFFU)}),
              kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex, BufferMemoryUVE::Auto,
                                                          static_cast<BufferUpdateFrequencyUVE>(0xFFU)}),
              kInvalidBufferHandleUVE);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlRenderDeviceUVETest, CreateBufferUVE_SizeExceedsGlsizeiptr_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    const BufferDescUVE oversizedDesc{
        static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max()) + 1U, BufferUsageUVE::Vertex};

    EXPECT_EQ(renderDevice->CreateBufferUVE(oversizedDesc), kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    const BufferHandleUVE valid = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    ASSERT_NE(valid, kInvalidBufferHandleUVE);
    EXPECT_EQ(valid.value, 1U);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_DimensionExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    TextureDescUVE oversizedDesc;
    oversizedDesc.width = static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) + 1U;
    oversizedDesc.height = 1U;

    EXPECT_EQ(renderDevice->CreateTextureUVE(oversizedDesc), kInvalidTextureHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    const TextureHandleUVE valid = renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U});
    ASSERT_NE(valid, kInvalidTextureHandleUVE);
    EXPECT_EQ(valid.value, 1U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_VertexLayoutExceedsGlLimit_ReturnsInvalidBeforeAllocation) {
    GLint maxVertexAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    ASSERT_GT(maxVertexAttribs, 0);

    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE oversizedDesc;
    oversizedDesc.vertexShader = vertexShader;
    oversizedDesc.fragmentShader = fragmentShader;
    oversizedDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    oversizedDesc.vertexLayout.resize(static_cast<std::size_t>(maxVertexAttribs) + 1U,
                                      VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});

    EXPECT_EQ(renderDevice->CreatePipelineUVE(oversizedDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownVertexFormat_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", static_cast<VertexAttributeFormatUVE>(0xFFU), 0U}};
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    PipelineDescUVE validDesc = invalidDesc;
    validDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    validDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(validDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownVertexFormat_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", static_cast<VertexAttributeFormatUVE>(0xFFU), 0U}};

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownBlendMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.blendMode = static_cast<PipelineBlendModeUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.blendMode = PipelineBlendModeUVE::Opaque;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownBlendMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.blendMode = static_cast<PipelineBlendModeUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateSamplerUVE_UnknownMagFilter_ReturnsInvalidBeforeAllocation) {
    SamplerDescUVE invalidDesc;
    invalidDesc.magFilter = static_cast<SamplerFilterUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSamplerUVE(invalidDesc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    invalidDesc.magFilter = SamplerFilterUVE::Point;
    const SamplerHandleUVE validSampler = renderDevice->CreateSamplerUVE(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);
    renderDevice->DestroySamplerUVE(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateSamplerUVE_UnknownMinFilter_ReturnsInvalidBeforeAllocation) {
    SamplerDescUVE invalidDesc;
    invalidDesc.minFilter = static_cast<SamplerFilterUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSamplerUVE(invalidDesc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    invalidDesc.minFilter = SamplerFilterUVE::Point;
    const SamplerHandleUVE validSampler = renderDevice->CreateSamplerUVE(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);
    renderDevice->DestroySamplerUVE(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateSamplerUVE_UnknownMipMode_ReturnsInvalidBeforeAllocation) {
    SamplerDescUVE invalidDesc;
    invalidDesc.mipMode = static_cast<SamplerMipModeUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSamplerUVE(invalidDesc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    invalidDesc.mipMode = SamplerMipModeUVE::None;
    const SamplerHandleUVE validSampler = renderDevice->CreateSamplerUVE(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);
    renderDevice->DestroySamplerUVE(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateSamplerUVE_UnknownWrap_ReturnsInvalidBeforeAllocation) {
    SamplerDescUVE invalidDesc;
    invalidDesc.wrapU = static_cast<SamplerWrapUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreateSamplerUVE(invalidDesc), kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    invalidDesc.wrapU = SamplerWrapUVE::Repeat;
    const SamplerHandleUVE validSampler = renderDevice->CreateSamplerUVE(invalidDesc);
    EXPECT_NE(validSampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);
    renderDevice->DestroySamplerUVE(validSampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateSamplerUVE_NonDefaults_SucceedsAndCounts) {
    SamplerDescUVE desc;
    desc.magFilter = SamplerFilterUVE::Point;
    desc.minFilter = SamplerFilterUVE::Point;
    desc.mipMode = SamplerMipModeUVE::None;
    desc.wrapU = SamplerWrapUVE::Repeat;
    desc.wrapV = SamplerWrapUVE::MirroredRepeat;
    desc.maxAnisotropy = 4.0F; // clamped to the device limit when lower, never a failure
    const SamplerHandleUVE sampler = renderDevice->CreateSamplerUVE(desc);
    EXPECT_NE(sampler, kInvalidSamplerHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);
    renderDevice->DestroySamplerUVE(sampler);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, DestroySamplerUVE_UnknownHandle_IsSafeNoOp) {
    renderDevice->DestroySamplerUVE(kInvalidSamplerHandleUVE);
    renderDevice->DestroySamplerUVE(SamplerHandleUVE{999});
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownCullMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.cullMode = static_cast<CullModeUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.cullMode = CullModeUVE::Back;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownCullMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.cullMode = static_cast<CullModeUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownFrontFace_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.frontFace = static_cast<FrontFaceUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.frontFace = FrontFaceUVE::Clockwise;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownFrontFace_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.frontFace = static_cast<FrontFaceUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownFillMode_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.fillMode = static_cast<FillModeUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.fillMode = FillModeUVE::Wireframe;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownFillMode_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.fillMode = static_cast<FillModeUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownDepthCompare_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.depthCompare = static_cast<DepthCompareUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.depthCompare = DepthCompareUVE::LessOrEqual;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownDepthCompare_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.depthCompare = static_cast<DepthCompareUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownTopology_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.topology = static_cast<PrimitiveTopologyUVE>(0xFFU);
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.topology = PrimitiveTopologyUVE::Triangles;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_UnknownTopology_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.topology = static_cast<PrimitiveTopologyUVE>(0xFFU);

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_ZeroVertexStride_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_ZeroVertexStride_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_AttributeExceedsVertexStride_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 8U}};
    invalidDesc.vertexStride = 12U;
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.vertexLayout[0].offset = 0U;
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_AttributeExceedsVertexStride_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float4, 4U}};
    invalidDesc.vertexStride = 16U;

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_VertexStrideExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE invalidDesc;
    invalidDesc.vertexShader = vertexShader;
    invalidDesc.fragmentShader = fragmentShader;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(renderDevice->CreatePipelineUVE(invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 2U);

    invalidDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE validPipeline = renderDevice->CreatePipelineUVE(invalidDesc);
    EXPECT_EQ(validPipeline.value, 1U);

    renderDevice->DestroyPipelineUVE(validPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_VertexStrideExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 4> binary{};
    PipelineBinaryDescUVE invalidDesc;
    invalidDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    invalidDesc.vertexStride = std::numeric_limits<std::uint32_t>::max();

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(binary, 0U, invalidDesc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, DrawUVE_CountExceedsGlsizei_DoesNotIssueGlCall) {
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->DrawUVE(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
}

TEST_F(GlRenderDeviceUVETest, DrawIndexedUVE_CountExceedsGlsizei_DoesNotIssueGlCall) {
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->DrawIndexedUVE(std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
}

TEST_F(GlRenderDeviceUVETest, BufferUsageAndIndexedCountValidationRejectsUnsafeDraws) {
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    const BufferHandleUVE indexBuffer = renderDevice->CreateBufferUVE(BufferDescUVE{4U, BufferUsageUVE::Index});
    const BufferHandleUVE uniformBuffer = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Uniform});
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(indexBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(uniformBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindVertexBufferUVE(uniformBuffer);
    commandBuffer->BindIndexBufferUVE(vertexBuffer);
    commandBuffer->BindUniformBufferUVE(vertexBuffer, 0U);
    commandBuffer->BindUniformBufferUVE(uniformBuffer, 0U);
    commandBuffer->DrawIndexedUVE(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->BindIndexBufferUVE(indexBuffer);
    commandBuffer->DrawIndexedUVE(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyBufferUVE(uniformBuffer);
    renderDevice->DestroyBufferUVE(indexBuffer);
    renderDevice->DestroyBufferUVE(vertexBuffer);
}

TEST_F(GlRenderDeviceUVETest, DrawUVE_BoundVertexBufferCapacityIsValidated) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 3> oneVertex{0.0F, 0.0F, 0.0F};
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(oneVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(oneVertex)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->DrawUVE(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->DrawUVE(1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    std::unique_ptr<ICommandBufferUVE> fullscreenCommandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(fullscreenCommandBuffer, nullptr);
    fullscreenCommandBuffer->BeginRenderPassUVE(passDesc);
    fullscreenCommandBuffer->BindPipelineUVE(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }
    fullscreenCommandBuffer->DrawUVE(3U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    fullscreenCommandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(fullscreenCommandBuffer));

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, DrawUVE_LinesPipeline_BindsDrawsAndSubmitsWithoutGlError) {
    // Tier 2.8 Scope A: a Lines pipeline binds and draws cleanly on a real driver. The mode
    // mapping itself (ToGlDrawModeUVE) is six lines reviewed by inspection; this test guards
    // the create/bind/draw/submit path a debug-line submit exercises.
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.topology = PrimitiveTopologyUVE::Lines;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 6> twoVertices{-1.0F, -1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(twoVertices), BufferUsageUVE::Vertex}, std::as_bytes(std::span(twoVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->DrawUVE(2U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, PointAndStripTopologiesRasterizeAsAssembledAndPointSizeComesFromTheShader) {
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
    const ShaderHandleUVE pointVertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kPointVertexShaderSource)});
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(pointVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    const auto makePipeline = [&](const ShaderHandleUVE vs, const PrimitiveTopologyUVE topology) {
        PipelineDescUVE desc;
        desc.vertexShader = vs;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
        desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
        desc.topology = topology;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        return renderDevice->CreatePipelineUVE(desc);
    };
    const PipelineHandleUVE pointPipeline = makePipeline(pointVertexShader, PrimitiveTopologyUVE::Points);
    const PipelineHandleUVE stripPipeline = makePipeline(vertexShader, PrimitiveTopologyUVE::TriangleStrip);
    const PipelineHandleUVE lineStripPipeline = makePipeline(vertexShader, PrimitiveTopologyUVE::LineStrip);
    const PipelineHandleUVE trianglePipeline = makePipeline(vertexShader, PrimitiveTopologyUVE::Triangles);
    ASSERT_NE(pointPipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(stripPipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(lineStripPipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandleUVE);
    constexpr std::array<float, 3> kOrigin{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 12> kStripCorners{-1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
                                                  -1.0F, 1.0F,  0.0F, 1.0F, 1.0F,  0.0F};
    // Pixel-centre aligned on a 64px axis: NDC = (pixel + 0.5) / 32 - 1.
    constexpr float kRow32 = (32.0F + 0.5F) / 32.0F - 1.0F;
    constexpr float kColumn48 = (48.0F + 0.5F) / 32.0F - 1.0F;
    const std::array<float, 9> lineStripVertices{-1.0F, kRow32, 0.0F, kColumn48, kRow32, 0.0F,
                                                 kColumn48, 1.0F, 0.0F};
    const BufferHandleUVE pointBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kOrigin), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kOrigin)));
    const BufferHandleUVE stripBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kStripCorners), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kStripCorners)));
    const BufferHandleUVE lineStripBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(lineStripVertices), BufferUsageUVE::Vertex}, std::as_bytes(std::span(lineStripVertices)));
    ASSERT_NE(pointBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(stripBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(lineStripBuffer, kInvalidBufferHandleUVE);
    const auto readPixel = [](const int x, const int y) {
        std::array<std::uint8_t, 3> pixel{};
        glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        return pixel[0];
    };
    const auto drawPass = [&](const PipelineHandleUVE pipeline, const BufferHandleUVE buffer,
                              const std::uint32_t vertexCount) {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDescUVE passDesc;
        passDesc.colorAttachment = kInvalidTextureHandleUVE;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(pipeline);
        commandBuffer->BindVertexBufferUVE(buffer);
        commandBuffer->DrawUVE(vertexCount);
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
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

    renderDevice->DestroyBufferUVE(lineStripBuffer);
    renderDevice->DestroyBufferUVE(stripBuffer);
    renderDevice->DestroyBufferUVE(pointBuffer);
    renderDevice->DestroyPipelineUVE(trianglePipeline);
    renderDevice->DestroyPipelineUVE(lineStripPipeline);
    renderDevice->DestroyPipelineUVE(stripPipeline);
    renderDevice->DestroyPipelineUVE(pointPipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(pointVertexShader);
}

// IEEE binary16 of a float in the normal range (enough for test vertex data: ±1, 0.5, 0).
[[nodiscard]] std::uint16_t HalfFromFloatUVE(const float value) {
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

TEST_F(GlRenderDeviceUVETest, PackedVertexAttributeFormatsFetchNormalizedHalfAndIntegerDataOnTheDriver) {
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
    const ShaderHandleUVE normVertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kNormVertexShader)});
    const ShaderHandleUVE integerVertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kIntegerVertexShader)});
    const ShaderHandleUVE fragmentShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kPassColorFragmentShader)});
    ASSERT_NE(normVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(integerVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

#pragma pack(push, 1)
    struct SnormVertexUVE final {
        std::int16_t x, y;
        std::uint8_t r, g, b, a;
    };
    struct HalfVertexUVE final {
        std::uint16_t x, y;
        std::uint16_t r, g, b, a;
    };
    struct UShortJointVertexUVE final {
        float x, y;
        std::uint16_t j0, j1, j2, j3;
    };
    struct UIntJointVertexUVE final {
        float x, y;
        std::uint32_t j0, j1, j2, j3;
    };
#pragma pack(pop)
    static_assert(sizeof(SnormVertexUVE) == 8U);
    static_assert(sizeof(HalfVertexUVE) == 12U);
    static_assert(sizeof(UShortJointVertexUVE) == 16U);
    static_assert(sizeof(UIntJointVertexUVE) == 24U);
    constexpr std::int16_t kSnormOne = 32767;
    const std::array<SnormVertexUVE, 3> snormTriangle{{{-kSnormOne, -kSnormOne, 255U, 128U, 0U, 255U},
                                                       {kSnormOne, -kSnormOne, 255U, 128U, 0U, 255U},
                                                       {-kSnormOne, kSnormOne, 255U, 128U, 0U, 255U}}};
    const std::uint16_t hNeg = HalfFromFloatUVE(-1.0F);
    const std::uint16_t hPos = HalfFromFloatUVE(1.0F);
    const std::uint16_t hHalf = HalfFromFloatUVE(0.5F);
    const std::uint16_t hZero = HalfFromFloatUVE(0.0F);
    const std::array<HalfVertexUVE, 3> halfTriangle{{{hNeg, hNeg, hZero, hHalf, hPos, hPos},
                                                     {hPos, hNeg, hZero, hHalf, hPos, hPos},
                                                     {hNeg, hPos, hZero, hHalf, hPos, hPos}}};
    const std::array<UShortJointVertexUVE, 3> ushortTriangle{{{-1.0F, -1.0F, 200U, 100U, 0U, 7U},
                                                              {1.0F, -1.0F, 200U, 100U, 0U, 7U},
                                                              {-1.0F, 1.0F, 200U, 100U, 0U, 7U}}};
    const std::array<UIntJointVertexUVE, 3> uintTriangle{{{-1.0F, -1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {1.0F, -1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {-1.0F, 1.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U}}};
    struct LayoutCaseUVE final {
        ShaderHandleUVE vertexShader;
        std::vector<VertexAttributeUVE> layout;
        std::uint32_t stride;
        std::span<const std::byte> vertices;
        std::array<std::uint8_t, 3> expected;
        const char* name;
    };
    const std::array<LayoutCaseUVE, 4> cases{{
        {normVertexShader,
         {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Short2Norm, 0U},
          VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::UByte4Norm, 4U}},
         sizeof(SnormVertexUVE), std::as_bytes(std::span(snormTriangle)), {255U, 128U, 0U}, "Short2Norm+UByte4Norm"},
        {normVertexShader,
         {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Half2, 0U},
          VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Half4, 4U}},
         sizeof(HalfVertexUVE), std::as_bytes(std::span(halfTriangle)), {0U, 128U, 255U}, "Half2+Half4"},
        {integerVertexShader,
         {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float2, 0U},
          VertexAttributeUVE{"JOINTS", VertexAttributeFormatUVE::UShort4, 8U}},
         sizeof(UShortJointVertexUVE), std::as_bytes(std::span(ushortTriangle)), {200U, 100U, 0U}, "UShort4"},
        {integerVertexShader,
         {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float2, 0U},
          VertexAttributeUVE{"JOINTS", VertexAttributeFormatUVE::UInt4, 8U}},
         sizeof(UIntJointVertexUVE), std::as_bytes(std::span(uintTriangle)), {200U, 100U, 0U}, "UInt4"},
    }};
    for (const LayoutCaseUVE& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        PipelineDescUVE desc;
        desc.vertexShader = testCase.vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = testCase.layout;
        desc.vertexStride = testCase.stride;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(desc);
        ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
        const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
            BufferDescUVE{testCase.vertices.size(), BufferUsageUVE::Vertex}, testCase.vertices);
        ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDescUVE passDesc;
        passDesc.colorAttachment = kInvalidTextureHandleUVE;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(pipeline);
        commandBuffer->BindVertexBufferUVE(vertexBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
        std::array<std::uint8_t, 3> pixel{};
        glReadPixels(16, 16, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
        EXPECT_NEAR(static_cast<int>(pixel[0]), static_cast<int>(testCase.expected[0]), 2);
        EXPECT_NEAR(static_cast<int>(pixel[1]), static_cast<int>(testCase.expected[1]), 2);
        EXPECT_NEAR(static_cast<int>(pixel[2]), static_cast<int>(testCase.expected[2]), 2);
        std::array<std::uint8_t, 3> outside{};
        glReadPixels(60, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, outside.data());
        EXPECT_LT(outside[0] + outside[1] + outside[2], 20) << "the triangle covers only the lower-left half";
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        renderDevice->DestroyBufferUVE(vertexBuffer);
        renderDevice->DestroyPipelineUVE(pipeline);
    }
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(integerVertexShader);
    renderDevice->DestroyShaderUVE(normVertexShader);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_UnknownAttachmentDoesNotBindOrCacheFramebuffer) {
    const TextureHandleUVE validColor = renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U});
    ASSERT_NE(validColor, kInvalidTextureHandleUVE);
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDescUVE& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPassUVE(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };

    RenderPassDescUVE unknownColor;
    unknownColor.colorAttachment = TextureHandleUVE{0xFFFF'FFFEU};
    unknownColor.depthAttachment = kInvalidTextureHandleUVE;
    assertFramebufferUnchanged(unknownColor);

    RenderPassDescUVE unknownDepth;
    unknownDepth.colorAttachment = validColor;
    unknownDepth.depthAttachment = TextureHandleUVE{0xFFFF'FFFDU};
    assertFramebufferUnchanged(unknownDepth);

    RenderPassDescUVE validPass;
    validPass.colorAttachment = validColor;
    validPass.depthAttachment = kInvalidTextureHandleUVE;
    commandBuffer->BeginRenderPassUVE(validPass);
    GLint validFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &validFramebuffer);
    EXPECT_NE(validFramebuffer, 0);
    commandBuffer->EndRenderPassUVE();
    renderDevice->DestroyTextureUVE(validColor);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_IncompleteFramebufferRejectsAndRestoresState) {
    const TextureHandleUVE validColor = renderDevice->CreateTextureUVE(TextureDescUVE{2U, 2U});
    const TextureHandleUVE invalidColor = renderDevice->CreateTextureUVE(
        TextureDescUVE{1U, 1U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(validColor, kInvalidTextureHandleUVE);
    ASSERT_NE(invalidColor, kInvalidTextureHandleUVE);
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
    RenderPassDescUVE mismatchedPass;
    mismatchedPass.colorAttachment = invalidColor;
    mismatchedPass.depthAttachment = kInvalidTextureHandleUVE;
    commandBuffer->BeginRenderPassUVE(mismatchedPass);
    GLint after = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
    EXPECT_EQ(after, before);

    RenderPassDescUVE validPass;
    validPass.colorAttachment = validColor;
    validPass.depthAttachment = kInvalidTextureHandleUVE;
    commandBuffer->BeginRenderPassUVE(validPass);
    GLint validFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &validFramebuffer);
    EXPECT_NE(validFramebuffer, 0);
    commandBuffer->EndRenderPassUVE();

    renderDevice->DestroyTextureUVE(invalidColor);
    renderDevice->DestroyTextureUVE(validColor);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_NonFiniteClearValuesLeaveStateUntouched) {
    const TextureHandleUVE color = renderDevice->CreateTextureUVE(TextureDescUVE{2U, 2U});
    const TextureHandleUVE depth = renderDevice->CreateTextureUVE(
        TextureDescUVE{2U, 2U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    ASSERT_NE(depth, kInvalidTextureHandleUVE);
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);

    RenderPassDescUVE invalidColorClear;
    invalidColorClear.colorAttachment = color;
    invalidColorClear.depthAttachment = depth;
    invalidColorClear.clearColor[0] = std::numeric_limits<float>::quiet_NaN();
    commandBuffer->BeginRenderPassUVE(invalidColorClear);

    GLint afterColorClear = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &afterColorClear);
    EXPECT_EQ(afterColorClear, before);

    RenderPassDescUVE invalidDepthClear;
    invalidDepthClear.colorAttachment = kInvalidTextureHandleUVE;
    invalidDepthClear.depthAttachment = depth;
    invalidDepthClear.colorLoadOp = LoadOpUVE::DontCare;
    invalidDepthClear.clearDepth = std::numeric_limits<float>::infinity();
    commandBuffer->BeginRenderPassUVE(invalidDepthClear);

    GLint afterDepthClear = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &afterDepthClear);
    EXPECT_EQ(afterDepthClear, before);

    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyTextureUVE(depth);
    renderDevice->DestroyTextureUVE(color);
}

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_SlotExceedsGlLimit_DoesNotIssueGlCall) {
    const TextureHandleUVE texture = renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U});
    ASSERT_NE(texture, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindTextureUVE(texture, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindTextureUVE(texture, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyTextureUVE(texture);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineFromBinaryUVE_BinarySizeExceedsGlsizei_ReturnsInvalidBeforeAllocation) {
    const std::array<std::byte, 1> storage{};
    const std::size_t oversizedBinarySize = static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) + 1U;
    const std::span<const std::byte> oversizedBinary(storage.data(), oversizedBinarySize);
    const PipelineBinaryDescUVE desc;

    EXPECT_EQ(renderDevice->CreatePipelineFromBinaryUVE(oversizedBinary, 0U, desc), kInvalidPipelineHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, BindUniformBufferUVE_SlotExceedsGlLimit_DoesNotIssueGlCall) {
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Uniform});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindUniformBufferUVE(buffer, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindUniformBufferUVE(buffer, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, BindStorageBufferUVE_ValidationRejectsBadSlotUsageAndHandle) {
    // M2f sister of the uniform-slot validation: every refused storage bind must be a pure
    // engine-level rejection (UVE_ERROR + drop) that NEVER reaches GL, so the GL error flag
    // stays clean across slot-limit, wrong-usage and unknown-handle cases; the legal bind
    // issues exactly one glBindBufferBase.
    const BufferHandleUVE storage = renderDevice->CreateBufferUVE(BufferDescUVE{64U, BufferUsageUVE::Storage});
    if (storage == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }
    const BufferHandleUVE vertex = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    ASSERT_NE(vertex, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindStorageBufferUVE(storage, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBufferUVE(vertex, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBufferUVE(BufferHandleUVE{999999U}, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->BindStorageBufferUVE(storage, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyBufferUVE(vertex);
    renderDevice->DestroyBufferUVE(storage);
}

TEST_F(GlRenderDeviceUVETest, CreateBufferUVE_StorageUsage_SupportsWholeBufferUpdate) {
    // Storage buffers are live GL objects on the desktop path: creation on the SSBO target,
    // a real glBufferSubData update round-trip, and clean destruction - the same lifecycle
    // the Uniform usage already proves, on the M2f target.
    const BufferHandleUVE storage = renderDevice->CreateBufferUVE(BufferDescUVE{64U, BufferUsageUVE::Storage});
    if (storage == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    const std::array<std::uint8_t, 64> payload{};
    EXPECT_TRUE(renderDevice->UpdateBufferUVE(
        storage,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
        0U));
    EXPECT_FALSE(renderDevice->UpdateBufferUVE(
        storage,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()), payload.size()),
        32U)); // overruns the 64-byte allocation - must be refused

    renderDevice->DestroyBufferUVE(storage);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_UnknownLoadOp_LeavesStateUntouched) {
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE invalidDesc;
    invalidDesc.depthLoadOp = static_cast<LoadOpUVE>(0xFFU);
    commandBuffer->BeginRenderPassUVE(invalidDesc);

    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceUVETest, CreateBufferUVE_ZeroSize_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    while (glGetError() != GL_NO_ERROR) {
    }

    const BufferHandleUVE invalid = renderDevice->CreateBufferUVE(BufferDescUVE{0U, BufferUsageUVE::Vertex});

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceUVETest, CreateBufferUVE_OversizedInitialData_ReturnsInvalidBeforeAllocation) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    const std::array<std::byte, 17> initialData{};
    const BufferHandleUVE invalid =
        renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex}, initialData);

    EXPECT_EQ(invalid, kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);

    const BufferHandleUVE valid = renderDevice->CreateBufferUVE(BufferDescUVE{16U, BufferUsageUVE::Vertex});
    EXPECT_EQ(valid.value, 1U);
    renderDevice->DestroyBufferUVE(valid);
}

TEST_F(GlRenderDeviceUVETest, CreateThenDestroyBuffer_UpdatesLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{64, BufferUsageUVE::Vertex});
    EXPECT_NE(buffer, kInvalidBufferHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);

    renderDevice->DestroyBufferUVE(buffer);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, UpdateBufferUVE_WithinBounds_Succeeds) {
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Vertex});
    const std::array<std::byte, 8> data{};
    EXPECT_TRUE(renderDevice->UpdateBufferUVE(buffer, data, 0));
    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, UpdateBufferUVE_OutOfBounds_ReturnsFalse) {
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{8, BufferUsageUVE::Vertex});
    const std::array<std::byte, 16> data{};
    EXPECT_FALSE(renderDevice->UpdateBufferUVE(buffer, data, 0));
    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, UpdateBufferUVE_OverflowedOffset_ReturnsFalseWithoutGlError) {
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{16, BufferUsageUVE::Vertex});
    const std::array<std::byte, 4> data{};
    while (glGetError() != GL_NO_ERROR) {
    }

    EXPECT_FALSE(renderDevice->UpdateBufferUVE(buffer, data, std::numeric_limits<std::uint64_t>::max()));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, ReadbackBufferUVE_ReturnsWhatUpdateBufferWrote) {
    // CS3: the read direction of UpdateBufferUVE against a real GL buffer object.
    const std::array<float, 4> initial{1.5F, -2.0F, 0.25F, 8.0F};
    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(initial), BufferUsageUVE::Storage}, std::as_bytes(std::span(initial)));
    if (buffer == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }

    std::array<float, 4> readback{};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(buffer, std::as_writable_bytes(std::span(readback)), 0));
    EXPECT_FLOAT_EQ(readback[0], 1.5F);
    EXPECT_FLOAT_EQ(readback[1], -2.0F);
    EXPECT_FLOAT_EQ(readback[2], 0.25F);
    EXPECT_FLOAT_EQ(readback[3], 8.0F);

    const std::array<float, 2> update{42.0F, 43.0F};
    ASSERT_TRUE(renderDevice->UpdateBufferUVE(buffer, std::as_bytes(std::span(update)), 2U * sizeof(float)));
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(buffer, std::as_writable_bytes(std::span(readback)), 0));
    EXPECT_FLOAT_EQ(readback[0], 1.5F); // prefix untouched by the offset write
    EXPECT_FLOAT_EQ(readback[2], 42.0F);
    EXPECT_FLOAT_EQ(readback[3], 43.0F);

    // A windowed read at an offset sees exactly that window.
    std::array<float, 2> window{};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(buffer, std::as_writable_bytes(std::span(window)),
                                                  2U * sizeof(float)));
    EXPECT_FLOAT_EQ(window[0], 42.0F);
    EXPECT_FLOAT_EQ(window[1], 43.0F);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, ReadbackBufferUVE_UnknownHandleOrOutOfRange_ReturnsFalseWithoutGlError) {
    std::array<std::byte, 8> readback{};
    EXPECT_FALSE(renderDevice->ReadbackBufferUVE(BufferHandleUVE{4242}, readback, 0));

    const BufferHandleUVE buffer = renderDevice->CreateBufferUVE(BufferDescUVE{8, BufferUsageUVE::Vertex});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);
    while (glGetError() != GL_NO_ERROR) {
    }
    std::array<std::byte, 16> tooLarge{};
    EXPECT_FALSE(renderDevice->ReadbackBufferUVE(buffer, tooLarge, 0));
    EXPECT_FALSE(renderDevice->ReadbackBufferUVE(buffer, readback,
                                                   std::numeric_limits<std::uint64_t>::max()));
    // Range refusals must never reach the driver.
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBufferUVE(buffer);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_SrgbColorSpaceUsesSrgbInternalFormat) {
    const std::array<std::uint8_t, 4U> pixels{128U, 64U, 32U, 255U};
    TextureDescUVE desc{};
    desc.width = 1U;
    desc.height = 1U;
    desc.colorSpace = TextureColorSpaceUVE::Srgb;
    const TextureHandleUVE texture = renderDevice->CreateTextureUVE(desc, std::as_bytes(std::span(pixels)));
    ASSERT_NE(texture, kInvalidTextureHandleUVE);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(texture));
    GLint internalFormat = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(internalFormat, static_cast<GLint>(GL_SRGB8_ALPHA8));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyTextureUVE(texture);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_UploadsBlockCompressedSrgbMipChainWhenAvailable) {
    if (!renderDevice->SupportsTextureFormatUVE(TextureFormatUVE::ETC2RGBA8, TextureColorSpaceUVE::Srgb)) {
        GTEST_SKIP() << "active GL context does not advertise ETC2 sRGB texture support";
    }
    const TextureDescUVE desc{7U, 5U, TextureFormatUVE::ETC2RGBA8, 2U, TextureColorSpaceUVE::Srgb};
    std::array<std::byte, 80U> blocks{}; // 4 level-0 blocks and 1 3x2 mip block, 16 bytes each
    const TextureHandleUVE texture = renderDevice->CreateTextureUVE(desc, blocks);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(texture));
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
    renderDevice->DestroyTextureUVE(texture);
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

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_UploadsAndSamplesExplicitMipLevel) {
    // Level 0 is opaque blue; level 1 is opaque red. Read back the stored level and
    // sample with explicit LOD 1 so the pixel proof cannot accidentally use level 0.
    constexpr std::array<std::uint8_t, 20U> mipPixels{
        0U, 0U, 255U, 255U,  0U, 0U, 255U, 255U,
        0U, 0U, 255U, 255U,  0U, 0U, 255U, 255U,
        255U, 0U, 0U, 255U};
    TextureDescUVE sourceDesc{};
    sourceDesc.width = 2U;
    sourceDesc.height = 2U;
    sourceDesc.mipLevels = 2U;
    const TextureHandleUVE source = renderDevice->CreateTextureUVE(
        sourceDesc, std::as_bytes(std::span(mipPixels)));
    ASSERT_NE(source, kInvalidTextureHandleUVE);

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(source));
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

    TextureDescUVE targetDesc{};
    targetDesc.width = 1U;
    targetDesc.height = 1U;
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(targetDesc);
    ASSERT_NE(target, kInvalidTextureHandleUVE);
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kMipSampleVertexSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kMipSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc{};
    passDesc.colorAttachment = target;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindTextureUVE(source, 0U);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
    std::array<std::uint8_t, 4U> sampledPixel{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sampledPixel.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTextureBinding));
    EXPECT_EQ(sampledPixel, (std::array<std::uint8_t, 4U>{255U, 0U, 0U, 255U}));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyTextureUVE(target);
    renderDevice->DestroyTextureUVE(source);
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

TEST_F(GlRenderDeviceUVETest, SamplingSrgbTextureDecodesToLinearValues) {
    const std::array<std::uint8_t, 4U> encodedSrgbPixel{128U, 0U, 0U, 255U};
    TextureDescUVE sourceDesc{};
    sourceDesc.width = 1U;
    sourceDesc.height = 1U;
    sourceDesc.colorSpace = TextureColorSpaceUVE::Srgb;
    const TextureHandleUVE source = renderDevice->CreateTextureUVE(
        sourceDesc, std::as_bytes(std::span(encodedSrgbPixel)));
    ASSERT_NE(source, kInvalidTextureHandleUVE);

    TextureDescUVE targetDesc{};
    targetDesc.width = 1U;
    targetDesc.height = 1U;
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(targetDesc);
    ASSERT_NE(target, kInvalidTextureHandleUVE);

    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kSrgbSampleVertexSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kSrgbSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const GLboolean previousFramebufferSrgbEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_FRAMEBUFFER_SRGB);
    while (glGetError() != GL_NO_ERROR) {
    }
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc{};
    passDesc.colorAttachment = target;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindTextureUVE(source, 0U);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    GLint previousTextureBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTextureBinding);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
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

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyTextureUVE(target);
    renderDevice->DestroyTextureUVE(source);
}

TEST_F(GlRenderDeviceUVETest, CreateThenDestroyTexture_UpdatesLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    const TextureHandleUVE texture =
        renderDevice->CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::RGBA8Unorm, 1});
    EXPECT_NE(texture, kInvalidTextureHandleUVE);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 1U);

    renderDevice->DestroyTextureUVE(texture);
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_PreservesActiveUnitBindingForLiveCommandBufferCache) {
    const TextureHandleUVE firstTexture =
        renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U});
    ASSERT_NE(firstTexture, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindTextureUVE(firstTexture, 0U);

    GLint activeTextureBefore = 0;
    GLint bindingBefore = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTextureBefore);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingBefore);
    ASSERT_EQ(activeTextureBefore, static_cast<GLint>(GL_TEXTURE0));
    ASSERT_NE(bindingBefore, 0);

    const TextureHandleUVE secondTexture =
        renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U});
    ASSERT_NE(secondTexture, kInvalidTextureHandleUVE);

    GLint activeTextureAfterCreate = 0;
    GLint bindingAfterCreate = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTextureAfterCreate);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingAfterCreate);
    EXPECT_EQ(activeTextureAfterCreate, activeTextureBefore);
    EXPECT_EQ(bindingAfterCreate, bindingBefore);

    // The command buffer still caches firstTexture at slot 0. If texture creation leaked secondTexture
    // into the binding, this call would incorrectly skip the bind and leave the wrong GL name active.
    commandBuffer->BindTextureUVE(firstTexture, 0U);
    GLint bindingAfterCachedRebind = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindingAfterCachedRebind);
    EXPECT_EQ(bindingAfterCachedRebind, bindingBefore);
    commandBuffer->EndRenderPassUVE();

    renderDevice->DestroyTextureUVE(secondTexture);
    renderDevice->DestroyTextureUVE(firstTexture);
}

TEST_F(GlRenderDeviceUVETest, CreateAndUpdateBufferUVE_PreserveBindingForLiveCommandBufferCache) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 3> kFirstVertex{0.0F, 0.0F, 0.0F};
    const BufferHandleUVE firstBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kFirstVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kFirstVertex)));
    ASSERT_NE(firstBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(firstBuffer, 0U);

    GLint bindingBefore = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingBefore);
    ASSERT_NE(bindingBefore, 0);

    constexpr std::array<float, 3> kSecondVertex{1.0F, 0.0F, 0.0F};
    const BufferHandleUVE secondBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kSecondVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kSecondVertex)));
    ASSERT_NE(secondBuffer, kInvalidBufferHandleUVE);

    GLint bindingAfterCreate = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCreate);
    EXPECT_EQ(bindingAfterCreate, bindingBefore);

    // The command buffer still caches firstBuffer. If creation leaked secondBuffer into GL, this
    // call would incorrectly skip the bind and leave the wrong vertex buffer active.
    commandBuffer->BindVertexBufferUVE(firstBuffer, 0U);
    GLint bindingAfterCachedCreateRebind = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCachedCreateRebind);
    EXPECT_EQ(bindingAfterCachedCreateRebind, bindingBefore);

    EXPECT_TRUE(renderDevice->UpdateBufferUVE(secondBuffer, std::as_bytes(std::span(kSecondVertex)), 0U));
    GLint bindingAfterUpdate = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterUpdate);
    EXPECT_EQ(bindingAfterUpdate, bindingBefore);

    // UpdateBufferUVE has the same temporary-bind requirement as creation.
    commandBuffer->BindVertexBufferUVE(firstBuffer, 0U);
    GLint bindingAfterCachedUpdateRebind = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &bindingAfterCachedUpdateRebind);
    EXPECT_EQ(bindingAfterCachedUpdateRebind, bindingBefore);

    commandBuffer->EndRenderPassUVE();
    renderDevice->DestroyBufferUVE(secondBuffer);
    renderDevice->DestroyBufferUVE(firstBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_Rgba16Float_UsesHalfFloatUploadType) {
    constexpr std::array<std::uint16_t, 16> kPixels{
        0x3C00U, 0x3800U, 0x0000U, 0x3C00U,
        0x3400U, 0x3C00U, 0x3800U, 0x3C00U,
        0x0000U, 0x0000U, 0x3C00U, 0x3C00U,
        0x3C00U, 0x3400U, 0x3800U, 0x3C00U,
    };
    while (glGetError() != GL_NO_ERROR) {
    }

    const TextureHandleUVE texture = renderDevice->CreateTextureUVE(
        TextureDescUVE{2U, 2U, TextureFormatUVE::RGBA16Float, 1U},
        std::as_bytes(std::span(kPixels)));
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindTextureUVE(texture, 0U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPassUVE();

    std::array<std::uint16_t, kPixels.size()> readback{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_HALF_FLOAT, readback.data());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    EXPECT_EQ(readback, kPixels);

    renderDevice->DestroyTextureUVE(texture);
}

TEST_F(GlRenderDeviceUVETest, DestroyRenderDeviceUVE_ReleasesLiveGlResources) {
    auto firstDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
    const ShaderHandleUVE vertexShader =
        firstDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = firstDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = firstDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 3> kVertex{0.0F, 0.0F, 0.0F};
    const BufferHandleUVE buffer = firstDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kVertex)));
    const TextureHandleUVE texture = firstDevice->CreateTextureUVE(TextureDescUVE{1U, 1U});
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);
    ASSERT_NE(texture, kInvalidTextureHandleUVE);
    ASSERT_EQ(firstDevice->GetLiveResourceCountUVE(), 5U);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = firstDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(buffer, 0U);
    commandBuffer->BindTextureUVE(texture, 0U);

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

    commandBuffer->EndRenderPassUVE();
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

    auto secondDevice = std::make_unique<GlRenderDeviceUVE>(*windowManager);
    EXPECT_EQ(secondDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateShaderUVE_ValidSource_Succeeds) {
    const ShaderHandleUVE shader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    EXPECT_NE(shader, kInvalidShaderHandleUVE);
    renderDevice->DestroyShaderUVE(shader);
}

TEST_F(GlRenderDeviceUVETest, CreateShaderUVE_BrokenSource_ReturnsInvalidAndLogsCompilerDiagnostic) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const ShaderHandleUVE shader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kBrokenShaderSource)});
    EXPECT_EQ(shader, kInvalidShaderHandleUVE);

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundCompilerDiagnostic =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("compilation failed");
        });
    EXPECT_TRUE(foundCompilerDiagnostic);

    logger.Shutdown();
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_UnknownShaderHandle_ReturnsInvalid) {
    PipelineDescUVE desc;
    desc.vertexShader = ShaderHandleUVE{999};
    desc.fragmentShader = ShaderHandleUVE{998};
    EXPECT_EQ(renderDevice->CreatePipelineUVE(desc), kInvalidPipelineHandleUVE);
}

TEST_F(GlRenderDeviceUVETest, CreatePipelineUVE_ValidShaders_Succeeds) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    EXPECT_NE(pipeline, kInvalidPipelineHandleUVE);

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, InvalidSentinelBindsAreRejectedWithoutPoisoningValidState) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr float vertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(vertices));
    const BufferHandleUVE vertexBuffer =
        renderDevice->CreateBufferUVE(BufferDescUVE{vertexBytes.size(), BufferUsageUVE::Vertex}, vertexBytes);
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->BindPipelineUVE(kInvalidPipelineHandleUVE);
    commandBuffer->BindVertexBufferUVE(kInvalidBufferHandleUVE);
    commandBuffer->BindIndexBufferUVE(kInvalidBufferHandleUVE);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, DestroyBoundPipelineWhileRecording_FailsClosed) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 3> kFirstVertex{0.0F, 0.0F, 0.0F};
    constexpr std::array<float, 3> kSecondVertex{1.0F, 0.0F, 0.0F};
    const BufferHandleUVE firstBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kFirstVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kFirstVertex)));
    const BufferHandleUVE secondBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(kSecondVertex), BufferUsageUVE::Vertex}, std::as_bytes(std::span(kSecondVertex)));
    ASSERT_NE(firstBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(secondBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPassUVE(RenderPassDescUVE{});
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(firstBuffer, 0U);
    renderDevice->DestroyPipelineUVE(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    commandBuffer->BindVertexBufferUVE(secondBuffer, 0U);
    commandBuffer->DrawUVE(1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->DestroyBufferUVE(secondBuffer);
    renderDevice->DestroyBufferUVE(firstBuffer);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, FullTriangleDrawAndPresent_DoesNotCrash) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr float kVertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kVertices));
    const BufferHandleUVE vertexBuffer =
        renderDevice->CreateBufferUVE(BufferDescUVE{vertexBytes.size(), BufferUsageUVE::Vertex}, vertexBytes);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.05F, 0.05F, 0.05F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;

    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3);
    commandBuffer->EndRenderPassUVE();

    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->PresentUVE();

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);

    SUCCEED();
}

TEST_F(GlRenderDeviceUVETest, FullscreenToneMappingShader_MapsBoundSourceTextureToDefaultFramebuffer) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, WithShaderStageDefineUVE(Shader::BuiltIn::kFullscreenQuadSource,
                                                                        "VERTEX_SHADER")});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, WithShaderStageDefineUVE(Shader::BuiltIn::kFullscreenQuadSource,
                                                                          "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<std::uint8_t, 4> kWarmHdrLikePixel{220U, 48U, 20U, 255U};
    const TextureHandleUVE sourceTexture = renderDevice->CreateTextureUVE(
        TextureDescUVE{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U}, std::as_bytes(std::span(kWarmHdrLikePixel)));
    ASSERT_NE(sourceTexture, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformIntUVE("uSourceTexture", 0);
    commandBuffer->BindTextureUVE(sourceTexture, 0U);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 24U));
    EXPECT_GT(pixel[1], pixel[2]);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyTextureUVE(sourceTexture);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, FullscreenToneMappingShader_ComposesRgba16FloatRenderTargetToDefaultFramebuffer) {
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
    const ShaderHandleUVE sourceVertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kFullscreenVertexSource)});
    const ShaderHandleUVE sourceFragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kFlatColorFragmentSource)});
    const ShaderHandleUVE toneVertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, WithShaderStageDefineUVE(Shader::BuiltIn::kFullscreenQuadSource,
                                                                        "VERTEX_SHADER")});
    const ShaderHandleUVE toneFragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, WithShaderStageDefineUVE(Shader::BuiltIn::kFullscreenQuadSource,
                                                                          "FRAGMENT_SHADER")});
    ASSERT_NE(sourceVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(sourceFragmentShader, kInvalidShaderHandleUVE);
    ASSERT_NE(toneVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(toneFragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE sourcePipelineDesc;
    sourcePipelineDesc.vertexShader = sourceVertexShader;
    sourcePipelineDesc.fragmentShader = sourceFragmentShader;
    sourcePipelineDesc.depthTestEnabled = true;
    sourcePipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE sourcePipeline = renderDevice->CreatePipelineUVE(sourcePipelineDesc);
    ASSERT_NE(sourcePipeline, kInvalidPipelineHandleUVE);
    PipelineDescUVE tonePipelineDesc;
    tonePipelineDesc.vertexShader = toneVertexShader;
    tonePipelineDesc.fragmentShader = toneFragmentShader;
    tonePipelineDesc.depthTestEnabled = false;
    tonePipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE tonePipeline = renderDevice->CreatePipelineUVE(tonePipelineDesc);
    ASSERT_NE(tonePipeline, kInvalidPipelineHandleUVE);

    const TextureHandleUVE hdrTarget =
        renderDevice->CreateTextureUVE(TextureDescUVE{64U, 64U, TextureFormatUVE::RGBA16Float, 1U});
    const TextureHandleUVE depthTarget =
        renderDevice->CreateTextureUVE(TextureDescUVE{64U, 64U, TextureFormatUVE::Depth32Float, 1U});
    const TextureHandleUVE shadowStyleDepthTarget =
        renderDevice->CreateTextureUVE(TextureDescUVE{64U, 64U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(hdrTarget, kInvalidTextureHandleUVE);
    ASSERT_NE(depthTarget, kInvalidTextureHandleUVE);
    ASSERT_NE(shadowStyleDepthTarget, kInvalidTextureHandleUVE);

    // A renderer shadow pass may use this depth-only path when a directional caster is present.
    // Ensure core-profile `GL_NONE` draw/read-buffer states cannot prevent a subsequent color+depth
    // pass from rasterizing its fullscreen source geometry.
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE shadowStylePassDesc;
    shadowStylePassDesc.colorAttachment = kInvalidTextureHandleUVE;
    shadowStylePassDesc.depthAttachment = shadowStyleDepthTarget;
    shadowStylePassDesc.depthLoadOp = LoadOpUVE::Clear;
    shadowStylePassDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPassUVE(shadowStylePassDesc);
    commandBuffer->EndRenderPassUVE();

    RenderPassDescUVE sourcePassDesc;
    sourcePassDesc.colorAttachment = hdrTarget;
    sourcePassDesc.depthAttachment = depthTarget;
    sourcePassDesc.depthLoadOp = LoadOpUVE::Clear;
    sourcePassDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPassUVE(sourcePassDesc);
    commandBuffer->BindPipelineUVE(sourcePipeline);
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.9F, 0.1F, 0.03F});
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();

    RenderPassDescUVE tonePassDesc;
    tonePassDesc.colorAttachment = kInvalidTextureHandleUVE;
    tonePassDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(tonePassDesc);
    commandBuffer->BindPipelineUVE(tonePipeline);
    commandBuffer->SetUniformIntUVE("uSourceTexture", 0);
    commandBuffer->BindTextureUVE(hdrTarget, 0U);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    // ToneMapping binds a depth-write-disabled pipeline. The next frame's clear must override
    // that persisted GL state before it draws the same depth again; otherwise GL_LESS rejects
    // later scene pixels although the renderer recorded valid draws.
    std::unique_ptr<ICommandBufferUVE> nextFrameCommandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(nextFrameCommandBuffer, nullptr);
    nextFrameCommandBuffer->BeginRenderPassUVE(sourcePassDesc);
    nextFrameCommandBuffer->BindPipelineUVE(sourcePipeline);
    nextFrameCommandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.04F, 0.12F, 0.92F});
    nextFrameCommandBuffer->DrawUVE(3U);
    nextFrameCommandBuffer->EndRenderPassUVE();
    nextFrameCommandBuffer->BeginRenderPassUVE(tonePassDesc);
    nextFrameCommandBuffer->BindPipelineUVE(tonePipeline);
    nextFrameCommandBuffer->SetUniformIntUVE("uSourceTexture", 0);
    nextFrameCommandBuffer->BindTextureUVE(hdrTarget, 0U);
    nextFrameCommandBuffer->DrawUVE(3U);
    nextFrameCommandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(nextFrameCommandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 32, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[2], static_cast<std::uint8_t>(pixel[0] + 36U));
    EXPECT_GT(pixel[2], static_cast<std::uint8_t>(pixel[1] + 36U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyTextureUVE(shadowStyleDepthTarget);
    renderDevice->DestroyTextureUVE(depthTarget);
    renderDevice->DestroyTextureUVE(hdrTarget);
    renderDevice->DestroyPipelineUVE(tonePipeline);
    renderDevice->DestroyPipelineUVE(sourcePipeline);
    renderDevice->DestroyShaderUVE(toneFragmentShader);
    renderDevice->DestroyShaderUVE(toneVertexShader);
    renderDevice->DestroyShaderUVE(sourceFragmentShader);
    renderDevice->DestroyShaderUVE(sourceVertexShader);
}

TEST_F(GlRenderDeviceUVETest, Basic3DShader_PerspectiveMatrixRendersNegativeZGeometry) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kBasic3DSource, "VERTEX_SHADER")});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kBasic3DSource, "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    constexpr std::array<float, 9> kVertices{-2.0F, -2.0F, -8.0F, 2.0F, -2.0F, -8.0F, 0.0F, 2.0F, -8.0F};
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(kVertices)).size(), BufferUsageUVE::Vertex},
        std::as_bytes(std::span(kVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    commandBuffer->SetUniformMatrix4x4UVE(
        "uViewProjection", Math::Matrix4x4UVE::PerspectiveUVE(1.0471975512F, 1.0F, 0.1F, 100.0F));
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.95F, 0.15F, 0.05F});
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, Basic3DShader_IndexedCanonicalMeshVertexLayoutRenders) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kBasic3DSource, "VERTEX_SHADER")});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kBasic3DSource, "FRAGMENT_SHADER")});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3,
                                                     offsetof(Asset::MeshVertexUVE, position)},
                                 VertexAttributeUVE{"NORMAL", VertexAttributeFormatUVE::Float3,
                                                     offsetof(Asset::MeshVertexUVE, normal)},
                                 VertexAttributeUVE{"TEXCOORD0", VertexAttributeFormatUVE::Float2,
                                                     offsetof(Asset::MeshVertexUVE, u)},
                                 VertexAttributeUVE{"TANGENT", VertexAttributeFormatUVE::Float4,
                                                     offsetof(Asset::MeshVertexUVE, tangent)}};
    pipelineDesc.vertexStride = static_cast<std::uint32_t>(sizeof(Asset::MeshVertexUVE));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const std::array<Asset::MeshVertexUVE, 3> vertices{
        Asset::MeshVertexUVE{Math::Vector3UVE{-2.0F, -2.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        Asset::MeshVertexUVE{Math::Vector3UVE{2.0F, -2.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        Asset::MeshVertexUVE{Math::Vector3UVE{0.0F, 2.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 0.5F, 1.0F},
    };
    constexpr std::array<std::uint32_t, 3> kIndices{0U, 1U, 2U};
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(vertices)).size(), BufferUsageUVE::Vertex}, std::as_bytes(std::span(vertices)));
    const BufferHandleUVE indexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(kIndices)).size(), BufferUsageUVE::Index}, std::as_bytes(std::span(kIndices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(indexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformMatrix4x4UVE(
        "uModel", Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{0.0F, 0.0F, -8.0F}, Math::QuaternionUVE{},
                                                      Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    commandBuffer->SetUniformMatrix4x4UVE(
        "uViewProjection", Math::Matrix4x4UVE::PerspectiveUVE(1.0471975512F, 1.0F, 0.1F, 100.0F));
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.95F, 0.15F, 0.05F});
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->BindIndexBufferUVE(indexBuffer);
    commandBuffer->DrawIndexedUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBufferUVE(indexBuffer);
    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, ShaderManager_Basic3DProgramRendersIndexedGeometry) {
    Threading::ThreadPoolUVE threadPool{1U};
    Asset::AssetBundleUVE assetBundle;
    Asset::FileSystemUVE fileSystem{assetBundle};
    const Asset::MountHandleUVE shaderMount =
        fileSystem.MountDirectoryUVE("shaders", ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in", 0);
    ASSERT_NE(shaderMount, 0U);
    Shader::ShaderManagerUVE shaderManager(threadPool, eventSystem, *renderDevice, fileSystem,
                                            Shader::ShaderManagerConfigUVE{});

    Shader::ShaderProgramDescUVE programDesc;
    programDesc.virtualFilePath = std::string(Shader::BuiltIn::kBasic3DVirtualPath);
    programDesc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kBasic3DSource);
    programDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    programDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    programDesc.depthTestEnabled = false;
    programDesc.depthWriteEnabled = false;
    programDesc.debugNameUVE = "ShaderManagerBasic3DRealGl";
    const std::shared_ptr<Shader::ShaderProgramUVE> program = shaderManager.CreateProgramUVE(programDesc);
    ASSERT_NE(program, nullptr);
    const auto compileDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!program->IsReadyUVE() && std::chrono::steady_clock::now() < compileDeadline) {
        shaderManager.UpdateUVE(0.0);
        std::this_thread::yield();
    }
    ASSERT_TRUE(program->IsReadyUVE());
    ASSERT_TRUE(program->IsValidUVE());

    constexpr std::array<float, 9> kVertices{-2.0F, -2.0F, -8.0F, 2.0F, -2.0F, -8.0F, 0.0F, 2.0F, -8.0F};
    const BufferHandleUVE vertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(kVertices)).size(), BufferUsageUVE::Vertex},
        std::as_bytes(std::span(kVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    program->SetMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    program->SetMatrix4x4UVE(
        "uViewProjection", Math::Matrix4x4UVE::PerspectiveUVE(1.0471975512F, 1.0F, 0.1F, 100.0F));
    program->SetVector3UVE("uColor", Math::Vector3UVE{0.95F, 0.15F, 0.05F});
    program->ApplyToUVE(*commandBuffer);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    std::array<std::uint8_t, 3> pixel{};
    glReadPixels(32, 28, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[0], static_cast<std::uint8_t>(pixel[1] + 48U));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyBufferUVE(vertexBuffer);
    fileSystem.UnmountUVE(shaderMount);
}

// Depth-only render pass (Increment 26, shadow mapping's depth pre-pass): colorAttachment left
// invalid, depthAttachment a real Depth32Float texture — exercises GlCommandBufferUVE's
// depth-only FBO branch (glDrawBuffer(GL_NONE)/glReadBuffer(GL_NONE), viewport derived from the
// depth texture's own size since no color attachment is present to derive it from). Like
// FullTriangleDrawAndPresent_DoesNotCrash above, this codebase's public RHI surface doesn't expose
// glCheckFramebufferStatus/glGetError results directly, so "doesn't crash and produces a usable
// texture handle" is this test's proof, matching every other live-GL test's verification style.
TEST_F(GlRenderDeviceUVETest, DepthOnlyRenderPass_NoColorAttachment_DoesNotCrash) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const TextureHandleUVE depthTexture =
        renderDevice->CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::Depth32Float, 1});
    ASSERT_NE(depthTexture, kInvalidTextureHandleUVE);

    constexpr float kVertices[] = {0.0F, 0.5F, 0.0F, -0.5F, -0.5F, 0.0F, 0.5F, -0.5F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kVertices));
    const BufferHandleUVE vertexBuffer =
        renderDevice->CreateBufferUVE(BufferDescUVE{vertexBytes.size(), BufferUsageUVE::Vertex}, vertexBytes);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.depthAttachment = depthTexture;
    passDesc.depthLoadOp = LoadOpUVE::Clear;
    passDesc.clearDepth = 1.0F;

    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3);
    commandBuffer->EndRenderPassUVE();

    renderDevice->SubmitUVE(std::move(commandBuffer));

    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyTextureUVE(depthTexture);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);

    SUCCEED();
}

TEST_F(GlRenderDeviceUVETest, RepeatedPresentCalls_DoNotChangeLiveResourceCount) {
    ASSERT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
    for (int i = 0; i < 5; ++i) {
        renderDevice->PresentUVE();
    }
    EXPECT_EQ(renderDevice->GetLiveResourceCountUVE(), 0U);
}

TEST_F(GlRenderDeviceUVETest, CreateShaderUVE_OutInfoLogParameter_CapturesLogOnBothSuccessAndFailure) {
    std::string successLog = "unset";
    const ShaderHandleUVE shader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)}, &successLog);
    EXPECT_NE(shader, kInvalidShaderHandleUVE);
    renderDevice->DestroyShaderUVE(shader);

    std::string failureLog;
    const ShaderHandleUVE broken = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kBrokenShaderSource)}, &failureLog);
    EXPECT_EQ(broken, kInvalidShaderHandleUVE);
    EXPECT_FALSE(failureLog.empty());
}

TEST_F(GlRenderDeviceUVETest, GetPipelineUniformsUVE_ReflectsDeclaredUniforms) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const std::vector<UniformReflectionUVE> uniforms = renderDevice->GetPipelineUniformsUVE(pipeline);
    const bool foundModel = std::any_of(uniforms.begin(), uniforms.end(), [](const UniformReflectionUVE& uniform) {
        return uniform.name == "uModel" && uniform.type == ShaderDataTypeUVE::Mat4;
    });
    const bool foundColor = std::any_of(uniforms.begin(), uniforms.end(), [](const UniformReflectionUVE& uniform) {
        return uniform.name == "uColor" && uniform.type == ShaderDataTypeUVE::Vec3;
    });
    EXPECT_TRUE(foundModel);
    EXPECT_TRUE(foundColor);

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, GetPipelineUniformsUVE_UnsupportedTypeIsExplicitAndSamplerRemainsInt) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kSamplerAndUnsupportedUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const std::vector<UniformReflectionUVE> uniforms = renderDevice->GetPipelineUniformsUVE(pipeline);
    const auto samplerIt = std::find_if(uniforms.cbegin(), uniforms.cend(), [](const UniformReflectionUVE& uniform) {
        return uniform.name == "uTexture";
    });
    const auto unsupportedIt = std::find_if(uniforms.cbegin(), uniforms.cend(), [](const UniformReflectionUVE& uniform) {
        return uniform.name == "uUnsigned";
    });
    ASSERT_NE(samplerIt, uniforms.cend());
    ASSERT_NE(unsupportedIt, uniforms.cend());
    EXPECT_EQ(samplerIt->type, ShaderDataTypeUVE::Int);
    EXPECT_EQ(unsupportedIt->type, ShaderDataTypeUVE::Unsupported);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::DontCare;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->SetUniformIntUVE("uTexture", 0);
    commandBuffer->SetUniformIntUVE("uUnsigned", 1);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, GetPipelineUniformsUVE_UnknownHandle_ReturnsEmpty) {
    EXPECT_TRUE(renderDevice->GetPipelineUniformsUVE(PipelineHandleUVE{999}).empty());
}

TEST_F(GlRenderDeviceUVETest, PipelineBinaryRoundTrip_CreatePipelineFromBinaryUVE_ProducesValidPipeline) {
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    const bool gotBinary = renderDevice->GetPipelineBinaryUVE(pipeline, binary, format);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);

    if (!gotBinary || binary.empty()) {
        GTEST_SKIP() << "This sandbox's Mesa/llvmpipe driver did not return a usable program "
                        "binary (GL_PROGRAM_BINARY_LENGTH <= 0) - documented as a plain cache "
                        "miss, not a failure, so skip the round-trip assertion here.";
    }

    PipelineBinaryDescUVE binaryDesc;
    binaryDesc.vertexLayout = pipelineDesc.vertexLayout;
    binaryDesc.vertexStride = pipelineDesc.vertexStride;
    const PipelineHandleUVE restored = renderDevice->CreatePipelineFromBinaryUVE(binary, format, binaryDesc);
    EXPECT_NE(restored, kInvalidPipelineHandleUVE);
    if (restored != kInvalidPipelineHandleUVE) {
        renderDevice->DestroyPipelineUVE(restored);
    }
}

TEST_F(GlRenderDeviceUVETest, GetPipelineBinaryUVE_UnknownHandle_ReturnsFalse) {
    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    EXPECT_FALSE(renderDevice->GetPipelineBinaryUVE(PipelineHandleUVE{999}, binary, format));
}

TEST_F(GlRenderDeviceUVETest, LitShadowed3DShader_DepthPrepassDarkensOccludedFragment) {
    const ShaderHandleUVE shadowVertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kShadowDepthSource, "VERTEX_SHADER")});
    const ShaderHandleUVE shadowFragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kShadowDepthSource, "FRAGMENT_SHADER")});
    const ShaderHandleUVE litVertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kLitShadowed3DSource, "VERTEX_SHADER")});
    const ShaderHandleUVE litFragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment,
                      WithShaderStageDefineUVE(Shader::BuiltIn::kLitShadowed3DSource, "FRAGMENT_SHADER")});
    ASSERT_NE(shadowVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(shadowFragmentShader, kInvalidShaderHandleUVE);
    ASSERT_NE(litVertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(litFragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE shadowPipelineDesc;
    shadowPipelineDesc.vertexShader = shadowVertexShader;
    shadowPipelineDesc.fragmentShader = shadowFragmentShader;
    shadowPipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    shadowPipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    shadowPipelineDesc.depthTestEnabled = true;
    shadowPipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE shadowPipeline = renderDevice->CreatePipelineUVE(shadowPipelineDesc);
    ASSERT_NE(shadowPipeline, kInvalidPipelineHandleUVE);

    PipelineDescUVE litPipelineDesc;
    litPipelineDesc.vertexShader = litVertexShader;
    litPipelineDesc.fragmentShader = litFragmentShader;
    litPipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0},
                                    VertexAttributeUVE{"NORMAL", VertexAttributeFormatUVE::Float3,
                                                       3U * static_cast<std::uint32_t>(sizeof(float))},
                                    VertexAttributeUVE{"TEXCOORD0", VertexAttributeFormatUVE::Float2,
                                                       6U * static_cast<std::uint32_t>(sizeof(float))},
                                    VertexAttributeUVE{"TANGENT", VertexAttributeFormatUVE::Float4,
                                                       8U * static_cast<std::uint32_t>(sizeof(float))}};
    litPipelineDesc.vertexStride = 12U * static_cast<std::uint32_t>(sizeof(float));
    litPipelineDesc.depthTestEnabled = false;
    litPipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE litPipeline = renderDevice->CreatePipelineUVE(litPipelineDesc);
    ASSERT_NE(litPipeline, kInvalidPipelineHandleUVE);

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

    const BufferHandleUVE shadowVertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(kShadowCasterVertices)).size(), BufferUsageUVE::Vertex},
        std::as_bytes(std::span(kShadowCasterVertices)));
    const BufferHandleUVE receiverVertexBuffer = renderDevice->CreateBufferUVE(
        BufferDescUVE{std::as_bytes(std::span(kReceiverVertices)).size(), BufferUsageUVE::Vertex},
        std::as_bytes(std::span(kReceiverVertices)));
    const TextureHandleUVE shadowMap =
        renderDevice->CreateTextureUVE(TextureDescUVE{64, 64, TextureFormatUVE::Depth32Float, 1});
    const TextureHandleUVE whiteTexture = renderDevice->CreateTextureUVE(
        TextureDescUVE{1, 1, TextureFormatUVE::RGBA8Unorm, 1}, std::as_bytes(std::span(kWhitePixel)));
    const TextureHandleUVE flatNormalTexture = renderDevice->CreateTextureUVE(
        TextureDescUVE{1, 1, TextureFormatUVE::RGBA8Unorm, 1}, std::as_bytes(std::span(kFlatNormalPixel)));
    ASSERT_NE(shadowVertexBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(receiverVertexBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(shadowMap, kInvalidTextureHandleUVE);
    ASSERT_NE(whiteTexture, kInvalidTextureHandleUVE);
    ASSERT_NE(flatNormalTexture, kInvalidTextureHandleUVE);
    // All active sampler uniforms must have type-compatible image units, even if the runtime
    // branch using one is disabled. The complete lighting shader keeps samplerCube active;
    // leaving it at its GL default (unit 0) aliases the 2D shadow sampler and invalidates Draw.
    std::array<std::uint8_t, 24> whiteCubePixels{};
    whiteCubePixels.fill(255U);
    TextureDescUVE cubeDesc{1U, 1U, TextureFormatUVE::RGBA8Unorm, 1U};
    cubeDesc.type = TextureTypeUVE::Cubemap;
    cubeDesc.arrayLayers = 6U;
    const auto reflectionCube = renderDevice->CreateTextureUVE(cubeDesc, std::as_bytes(std::span(whiteCubePixels)));
    ASSERT_NE(reflectionCube, kInvalidTextureHandleUVE);

    std::unique_ptr<ICommandBufferUVE> shadowCommandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(shadowCommandBuffer, nullptr);
    RenderPassDescUVE shadowPassDesc;
    shadowPassDesc.colorAttachment = kInvalidTextureHandleUVE;
    shadowPassDesc.depthAttachment = shadowMap;
    shadowPassDesc.depthLoadOp = LoadOpUVE::Clear;
    shadowPassDesc.clearDepth = 1.0F;
    shadowCommandBuffer->BeginRenderPassUVE(shadowPassDesc);
    shadowCommandBuffer->BindPipelineUVE(shadowPipeline);
    shadowCommandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    shadowCommandBuffer->SetUniformMatrix4x4UVE("uLightSpaceMatrix", Math::Matrix4x4UVE::IdentityUVE());
    shadowCommandBuffer->BindVertexBufferUVE(shadowVertexBuffer);
    shadowCommandBuffer->DrawUVE(3);
    shadowCommandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(shadowCommandBuffer));

    std::unique_ptr<ICommandBufferUVE> colorCommandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(colorCommandBuffer, nullptr);
    RenderPassDescUVE colorPassDesc;
    colorPassDesc.colorAttachment = kInvalidTextureHandleUVE;
    colorPassDesc.colorLoadOp = LoadOpUVE::Clear;
    colorPassDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    colorPassDesc.depthLoadOp = LoadOpUVE::DontCare;
    colorCommandBuffer->BeginRenderPassUVE(colorPassDesc);
    colorCommandBuffer->BindPipelineUVE(litPipeline);
    colorCommandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    // lit_shadowed_3d's normal transform now reads uNormalMatrix instead of uModel (Phase 2c) -
    // identity either way here, but a real caller (Renderer3DUVE::RecordItemsUVE) always sets it
    // alongside uModel, so this direct-GL-API test must too or vWorldNormal comes out zero.
    colorCommandBuffer->SetUniformMatrix4x4UVE("uNormalMatrix", Math::Matrix4x4UVE::IdentityUVE());
    colorCommandBuffer->SetUniformMatrix4x4UVE("uViewProjection", Math::Matrix4x4UVE::IdentityUVE());
    colorCommandBuffer->SetUniformMatrix4x4UVE("uLightSpaceMatrix", Math::Matrix4x4UVE::IdentityUVE());
    colorCommandBuffer->SetUniformVector3UVE("uAmbientColor", Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformVector3UVE("uViewPosition", Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    colorCommandBuffer->SetUniformVector3UVE("uAlbedoColor", Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    colorCommandBuffer->SetUniformFloatUVE("uMetallic", 0.0F);
    colorCommandBuffer->SetUniformFloatUVE("uRoughness", 1.0F);
    colorCommandBuffer->SetUniformVector3UVE("uEmissiveColor", Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformIntUVE("uLights[0].type", 0);
    colorCommandBuffer->SetUniformIntUVE("uMeshRenderLayers", 1);
    colorCommandBuffer->SetUniformIntUVE("uLights[0].cullMask", 1);
    colorCommandBuffer->SetUniformVector3UVE("uLights[0].direction", Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    colorCommandBuffer->SetUniformVector3UVE("uLights[0].color", Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    colorCommandBuffer->SetUniformFloatUVE("uLights[0].intensity", 1.0F);
    colorCommandBuffer->BindTextureUVE(shadowMap, 0U);
    colorCommandBuffer->SetUniformIntUVE("uShadowMapTexture", 0);
    colorCommandBuffer->SetUniformIntUVE("uShadowPcfKernelRadius", 1);
    colorCommandBuffer->SetUniformFloatUVE("uShadowOpacity", 1.0F);
    colorCommandBuffer->SetUniformFloatUVE("uShadowBias", 0.001F);
    colorCommandBuffer->SetUniformFloatUVE("uShadowNormalBias", 1.0F);
    colorCommandBuffer->BindTextureUVE(flatNormalTexture, 3U);
    colorCommandBuffer->SetUniformIntUVE("uNormalTexture", 3);
    colorCommandBuffer->BindTextureUVE(whiteTexture, 1U);
    colorCommandBuffer->SetUniformIntUVE("uAlbedoTexture", 1);
    colorCommandBuffer->BindTextureUVE(whiteTexture, 2U);
    colorCommandBuffer->SetUniformIntUVE("uAOTexture", 2);
    colorCommandBuffer->BindTextureUVE(whiteTexture, 12U);
    colorCommandBuffer->SetUniformIntUVE("uMetallicRoughnessTexture", 12);
    colorCommandBuffer->BindTextureUVE(whiteTexture, 13U);
    colorCommandBuffer->SetUniformIntUVE("uEmissiveTexture", 13);
    colorCommandBuffer->SetUniformFloatUVE("uEmissiveEnergy", 1.0F);
    colorCommandBuffer->SetUniformFloatUVE("uNormalScale", 1.0F);
    colorCommandBuffer->SetUniformFloatUVE("uOcclusionStrength", 1.0F);
    colorCommandBuffer->SetUniformVector3UVE("uUvScale", Math::Vector3UVE{1.0F, 1.0F, 0.0F});
    colorCommandBuffer->SetUniformVector3UVE("uUvOffset", Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    colorCommandBuffer->SetUniformIntUVE("uUnshaded", 0);
    colorCommandBuffer->BindTextureUVE(reflectionCube, 6U);
    colorCommandBuffer->SetUniformIntUVE("uReflectionProbeCube", 6);
    colorCommandBuffer->SetUniformIntUVE("uReflectionProbeEnabled", 0);
    // Surface opacity became an explicit lit-shader uniform; this direct opaque receiver must
    // initialize it just like Renderer3DUVE does for an opaque SurfaceInstance3D.
    colorCommandBuffer->SetUniformFloatUVE("uSurfaceOpacity", 1.0F);
    colorCommandBuffer->SetUniformFloatUVE("uAlphaCutoff", 0.0F);
    colorCommandBuffer->BindVertexBufferUVE(receiverVertexBuffer);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    colorCommandBuffer->DrawUVE(3);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    colorCommandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(colorCommandBuffer));

    std::array<std::uint8_t, 4> shadowedPixel{};
    std::array<std::uint8_t, 4> softEdgePixel{};
    std::array<std::uint8_t, 4> litPixel{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, shadowedPixel.data());
    glReadPixels(44, 36, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, softEdgePixel.data());
    glReadPixels(60, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, litPixel.data());
    EXPECT_GT(static_cast<int>(softEdgePixel[0]), static_cast<int>(shadowedPixel[0]) + 16);
    EXPECT_GT(static_cast<int>(litPixel[0]), static_cast<int>(softEdgePixel[0]) + 16);

    renderDevice->DestroyTextureUVE(reflectionCube);
    renderDevice->DestroyTextureUVE(flatNormalTexture);
    renderDevice->DestroyTextureUVE(whiteTexture);
    renderDevice->DestroyTextureUVE(shadowMap);
    renderDevice->DestroyBufferUVE(receiverVertexBuffer);
    renderDevice->DestroyBufferUVE(shadowVertexBuffer);
    renderDevice->DestroyPipelineUVE(litPipeline);
    renderDevice->DestroyPipelineUVE(shadowPipeline);
    renderDevice->DestroyShaderUVE(litFragmentShader);
    renderDevice->DestroyShaderUVE(litVertexShader);
    renderDevice->DestroyShaderUVE(shadowFragmentShader);
    renderDevice->DestroyShaderUVE(shadowVertexShader);
}

TEST_F(GlRenderDeviceUVETest, CommandBuffer_SetUniformCalls_RejectReflectedTypeMismatches) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::DontCare;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    while (glGetError() != GL_NO_ERROR) {
    }

    commandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    commandBuffer->SetUniformFloatUVE("uModel", 1.0F);
    commandBuffer->SetUniformIntUVE("uColor", 1);
    commandBuffer->SetUniformBoolUVE("uModel", true);
    commandBuffer->SetUniformMatrix4x4UVE("uColor", Math::Matrix4x4UVE::IdentityUVE());
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

TEST_F(GlRenderDeviceUVETest, CommandBuffer_SetUniformCalls_RejectNonFiniteValuesWithoutChangingUniforms) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kFiniteUniformFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::DontCare;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);

    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    ASSERT_NE(currentProgram, 0);
    const GLint exposureLocation = glGetUniformLocation(static_cast<GLuint>(currentProgram), "uExposure");
    ASSERT_NE(exposureLocation, -1);

    commandBuffer->SetUniformFloatUVE("uExposure", 2.0F);
    GLfloat exposureBefore = 0.0F;
    glGetUniformfv(static_cast<GLuint>(currentProgram), exposureLocation, &exposureBefore);
    EXPECT_FLOAT_EQ(exposureBefore, 2.0F);
    commandBuffer->SetUniformFloatUVE("uExposure", std::numeric_limits<float>::quiet_NaN());
    GLfloat exposureAfter = 0.0F;
    glGetUniformfv(static_cast<GLuint>(currentProgram), exposureLocation, &exposureAfter);
    EXPECT_FLOAT_EQ(exposureAfter, exposureBefore);

    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.25F, 0.5F, 0.75F});
    std::array<GLfloat, 3> colorBefore{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uColor"),
                   colorBefore.data());
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    std::array<GLfloat, 3> colorAfter{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uColor"),
                   colorAfter.data());
    EXPECT_EQ(colorAfter, colorBefore);

    Math::Matrix4x4UVE validMatrix = Math::Matrix4x4UVE::IdentityUVE();
    validMatrix.m[0][1] = 2.0F;
    commandBuffer->SetUniformMatrix4x4UVE("uModel", validMatrix);
    std::array<GLfloat, 16> matrixBefore{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uModel"),
                   matrixBefore.data());
    Math::Matrix4x4UVE invalidMatrix = validMatrix;
    invalidMatrix.m[2][2] = std::numeric_limits<float>::quiet_NaN();
    commandBuffer->SetUniformMatrix4x4UVE("uModel", invalidMatrix);
    std::array<GLfloat, 16> matrixAfter{};
    glGetUniformfv(static_cast<GLuint>(currentProgram), glGetUniformLocation(static_cast<GLuint>(currentProgram), "uModel"),
                   matrixAfter.data());
    EXPECT_EQ(matrixAfter, matrixBefore);

    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, CommandBuffer_SetUniformCalls_OnBoundPipeline_DoNotCrash) {
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kUniformVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kUniformFragmentShaderSource)});

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::DontCare;
    passDesc.depthLoadOp = LoadOpUVE::DontCare;

    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformMatrix4x4UVE("uModel", Math::Matrix4x4UVE::IdentityUVE());
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    commandBuffer->EndRenderPassUVE();

    renderDevice->SubmitUVE(std::move(commandBuffer));

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);

    SUCCEED();
}

// Phase 3 (ViewportManagerUVE): BeginRenderPassUVE's viewportOverride must isolate each pane via
// GL_SCISSOR_TEST, not just glViewport - glClear() ignores glViewport() but respects the scissor
// rect. Without that scissor-test pairing (see ApplyViewportUVE in gl_command_buffer_uve.cpp),
// clearing the second pane would silently wipe out the first pane already drawn to the shared
// default framebuffer. This test renders two flat-colored fullscreen-triangle passes into two
// disjoint viewportOverride rects on the default framebuffer and reads back a pixel from each
// half to confirm neither pane's Clear touched the other's pixels.
TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_ViewportOverride_IsolatesPanesViaScissor) {
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
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kFullscreenVertexSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kFlatColorFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    // The window is 64x64 (MakeTestWindowDescUVE()) - left pane occupies x in [0, 32), right pane
    // x in [32, 64), each pane's full height.
    RenderPassDescUVE leftPassDesc;
    leftPassDesc.colorAttachment = kInvalidTextureHandleUVE;
    leftPassDesc.colorLoadOp = LoadOpUVE::Clear;
    leftPassDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    leftPassDesc.depthLoadOp = LoadOpUVE::DontCare;
    leftPassDesc.viewportOverride = ViewportRectUVE{Math::Vector2iUVE{0, 0}, Math::Vector2iUVE{32, 64}};
    commandBuffer->BeginRenderPassUVE(leftPassDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.9F, 0.05F, 0.05F});
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();

    RenderPassDescUVE rightPassDesc = leftPassDesc;
    rightPassDesc.viewportOverride = ViewportRectUVE{Math::Vector2iUVE{32, 0}, Math::Vector2iUVE{32, 64}};
    commandBuffer->BeginRenderPassUVE(rightPassDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->SetUniformVector3UVE("uColor", Math::Vector3UVE{0.05F, 0.05F, 0.9F});
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();

    renderDevice->SubmitUVE(std::move(commandBuffer));

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

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

// ---------------------------------------------------------------------------
// M5a: compute pipelines + DispatchUVE (desktop GL 4.3+; tests skip below that)
// ---------------------------------------------------------------------------

// GLSL twin of the Vulkan-side kFillPaletteComputeSpirvUVE: one workgroup of 4 invocations
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

TEST_F(GlRenderDeviceUVETest, DispatchUVE_ComputeFillsStorageBuffer_ProvenByReadback) {
    // The GL M5a proof: a ZERO-initialized Storage SSBO, one outside-pass dispatch of the
    // palette filler (GL executes at record time and barriers with GL_ALL_BARRIER_BITS), then
    // a client-side glGetBufferSubData readback must show red/green/blue/yellow. Without a
    // real glDispatchCompute the buffer stays zeroed — the colors cannot be faked.
    const float zeros[16] = {};
    const BufferHandleUVE palette = renderDevice->CreateBufferUVE(
        BufferDescUVE{sizeof(zeros), BufferUsageUVE::Storage}, std::as_bytes(std::span(zeros)));
    if (palette == kInvalidBufferHandleUVE) {
        GTEST_SKIP() << "context lacks desktop GL 4.3 shader storage buffers";
    }
    const ShaderHandleUVE computeShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Compute, std::string(kPaletteFillComputeSource)});
    if (computeShader == kInvalidShaderHandleUVE) {
        renderDevice->DestroyBufferUVE(palette);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDescUVE pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandleUVE computePipeline = renderDevice->CreateComputePipelineUVE(pipelineDesc, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandleUVE) << infoLog;

    // The whole compute flow records OUTSIDE pass markers (the M5a contract) — GL executes
    // each command at record time, including the dispatch and its memory barrier.
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindStorageBufferUVE(palette, 0U);
    commandBuffer->DispatchUVE(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->SubmitUVE(std::move(commandBuffer));

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

    renderDevice->DestroyPipelineUVE(computePipeline);
    renderDevice->DestroyShaderUVE(computeShader);
    renderDevice->DestroyBufferUVE(palette);
}

TEST_F(GlRenderDeviceUVETest, CreateComputePipelineUVE_ValidatesShaderHandleAndStage) {
    std::string infoLog;
    ComputePipelineDescUVE unknownDesc{};
    unknownDesc.computeShader = ShaderHandleUVE{999999U};
    EXPECT_EQ(renderDevice->CreateComputePipelineUVE(unknownDesc, &infoLog), kInvalidPipelineHandleUVE);

    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ComputePipelineDescUVE wrongStageDesc{};
    wrongStageDesc.computeShader = vertexShader;
    EXPECT_EQ(renderDevice->CreateComputePipelineUVE(wrongStageDesc, &infoLog), kInvalidPipelineHandleUVE);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyShaderUVE(vertexShader);
}

TEST_F(GlRenderDeviceUVETest, DispatchUVE_RejectsGraphicsPipelineWithoutGlError) {
    // Engine-level refusal, never a GL call: dispatching with a GRAPHICS pipeline bound must
    // be dropped (UVE_ERROR) with the GL error flag untouched.
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const PipelineHandleUVE graphicsPipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(graphicsPipeline, kInvalidPipelineHandleUVE);

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipelineUVE(graphicsPipeline); // M5a: legal outside pass markers now
    commandBuffer->DispatchUVE(1U, 1U, 1U);           // refused: graphics pipeline is bound
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->SubmitUVE(std::move(commandBuffer));

    // Nothing bound at all: refused the same way.
    std::unique_ptr<ICommandBufferUVE> freshBuffer = renderDevice->CreateCommandBufferUVE();
    freshBuffer->DispatchUVE(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->SubmitUVE(std::move(freshBuffer));

    renderDevice->DestroyPipelineUVE(graphicsPipeline);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyShaderUVE(fragmentShader);
}

// ---------------------------------------------------------------------------
// M5b: storage images (GL). BindTextureUVE feeds ONE unified slot space — when the
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

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_ComputeStorageImageOnSrgbTexture_LeavesImageUnitUnbound) {
    // An sRGB texture remains valid for sampling but is not a supported OpenGL storage
    // image. BindTextureUVE must preserve the texture-unit binding while clearing the
    // corresponding image unit, rather than relying on driver-specific by-size aliasing.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    TextureDescUVE targetDesc{};
    targetDesc.width = 4U;
    targetDesc.height = 4U;
    targetDesc.colorSpace = TextureColorSpaceUVE::Srgb;
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(
        targetDesc, std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandleUVE);

    const ShaderHandleUVE computeShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandleUVE) {
        renderDevice->DestroyTextureUVE(target);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDescUVE pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandleUVE computePipeline =
        renderDevice->CreateComputePipelineUVE(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandleUVE) {
        renderDevice->DestroyShaderUVE(computeShader);
        renderDevice->DestroyTextureUVE(target);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindTextureUVE(target, 0U); // M5b: legal outside pass while compute is bound
    renderDevice->SubmitUVE(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
    EXPECT_NE(textureName, 0) << "the sRGB texture must remain bound for sampler access";
    GLint imageName = -1;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
    EXPECT_EQ(imageName, 0) << "unsupported sRGB storage-image bindings must be cleared";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipelineUVE(computePipeline);
    renderDevice->DestroyShaderUVE(computeShader);
    renderDevice->DestroyTextureUVE(target);
}

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_ComputeImageStoreOnLinearTexture_ProvenByGlGetTexImage) {
    // Linear RGBA8 remains a supported compute storage image. A zero-initialized target
    // must read back green after the compute pipeline binds it and dispatches one workgroup.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U},
        std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandleUVE);

    const ShaderHandleUVE computeShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandleUVE) {
        renderDevice->DestroyTextureUVE(target);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDescUVE pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandleUVE computePipeline =
        renderDevice->CreateComputePipelineUVE(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandleUVE) {
        renderDevice->DestroyShaderUVE(computeShader);
        renderDevice->DestroyTextureUVE(target);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindTextureUVE(target, 0U);
    commandBuffer->DispatchUVE(1U, 1U, 1U);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->SubmitUVE(std::move(commandBuffer));

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

    renderDevice->DestroyPipelineUVE(computePipeline);
    renderDevice->DestroyShaderUVE(computeShader);
    renderDevice->DestroyTextureUVE(target);
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

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_FragmentImageStore_ProvenByGlGetTexImage) {
    // The GL M5b graphics-side proof: a fullscreen triangle whose fragment imageStores
    // RED into texel (fragCoord % 4) of a zero-initialized 4x4 RGBA8 texture (full
    // coverage therefore paints all sixteen texels) while outputting black to the default
    // framebuffer. After the draw, the raw glGetTexImage readback must be all RED — the
    // fragment-stage image store really reached the caller's texture through the
    // BindTextureUVE image-unit double-bind.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U> kZeroPixels{};
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U},
        std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(target, kInvalidTextureHandleUVE);

    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kImageStoreVertexSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kImageStoreFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE)
        << "a GL_IMAGE_2D fragment uniform must reflect and build since M5b";

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = kInvalidTextureHandleUVE;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindTextureUVE(target, 0U); // sampler bind + image-unit double-bind
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->SubmitUVE(std::move(commandBuffer));

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

    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyTextureUVE(target);
}

} // namespace

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_Texture2DArray_UploadsLevelMajorLayers) {
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
    const TextureHandleUVE array = renderDevice->CreateTextureUVE(
        TextureDescUVE{2U, 2U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Texture2DArray, 3U},
        std::as_bytes(std::span(upload)));
    ASSERT_NE(array, kInvalidTextureHandleUVE);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(array));
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
    renderDevice->DestroyTextureUVE(array);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_Cubemap_UploadsFacesInCubemapFaceOrder) {
    // Tier 2.3: six solid 2x2 faces upload in CubemapFaceUVE order (+X,-X,+Y,-Y,+Z,-Z), which
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
    const TextureHandleUVE cube = renderDevice->CreateTextureUVE(
        TextureDescUVE{2U, 2U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                       TextureTypeUVE::Cubemap, 6U},
        std::as_bytes(std::span(upload)));
    ASSERT_NE(cube, kInvalidTextureHandleUVE);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(cube));
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
    renderDevice->DestroyTextureUVE(cube);
}

TEST_F(GlRenderDeviceUVETest, CreateTextureUVE_DimensionsBeyondGlLimits_ReturnInvalidBeforeAllocation) {
    // Tier 2.3: layer counts the RHI validator accepts (no overflow) but no GL device offers.
    EXPECT_EQ(renderDevice->CreateTextureUVE(
                  TextureDescUVE{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U,
                                 TextureColorSpaceUVE::Linear, TextureTypeUVE::Texture2DArray,
                                 1000000U}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(renderDevice->CreateTextureUVE(
                  TextureDescUVE{100000U, 100000U, TextureFormatUVE::RGBA8Unorm, 1U,
                                 TextureColorSpaceUVE::Linear, TextureTypeUVE::Cubemap, 6U}),
              kInvalidTextureHandleUVE);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_LayerBeyondAttachmentLayerCount_LeavesFramebufferUnchanged) {
    // Tier 2.3 mirror of UnknownAttachmentDoesNotBindOrCacheFramebuffer: out-of-range layers
    // reject before any FBO work (a nonzero layer on a 1-layer 2D texture fails here too).
    TextureDescUVE arrayDesc{2U, 2U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                             TextureTypeUVE::Texture2DArray, 2U};
    const TextureHandleUVE array = renderDevice->CreateTextureUVE(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandleUVE);
    const TextureHandleUVE flat = renderDevice->CreateTextureUVE(TextureDescUVE{2U, 2U});
    ASSERT_NE(flat, kInvalidTextureHandleUVE);
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDescUVE& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPassUVE(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };
    RenderPassDescUVE badColorLayer;
    badColorLayer.colorAttachment = array;
    badColorLayer.colorLayer = 5U;
    assertFramebufferUnchanged(badColorLayer);

    RenderPassDescUVE badDepthLayer;
    badDepthLayer.colorAttachment = array;
    badDepthLayer.depthAttachment = flat; // format aside, layer 1 already exceeds its 1 layer
    badDepthLayer.depthLayer = 1U;
    assertFramebufferUnchanged(badDepthLayer);

    RenderPassDescUVE flatWithLayer;
    flatWithLayer.colorAttachment = flat;
    flatWithLayer.colorLayer = 1U;
    assertFramebufferUnchanged(flatWithLayer);

    RenderPassDescUVE validLayeredPass;
    validLayeredPass.colorAttachment = array;
    validLayeredPass.colorLayer = 1U;
    commandBuffer->BeginRenderPassUVE(validLayeredPass);
    GLint layeredFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &layeredFramebuffer);
    EXPECT_NE(layeredFramebuffer, 0);
    commandBuffer->EndRenderPassUVE();
    renderDevice->DestroyTextureUVE(array);
    renderDevice->DestroyTextureUVE(flat);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_LayeredAttachments_RenderIntoSelectedLayers) {
    // Tier 2.3 pixel proof for render-to-layer: clear layer 0 green and layer 1 red through
    // two passes, then read both slices back — each must hold exactly its own clear color.
    GLint glMajor = 0;
    GLint glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    TextureDescUVE arrayDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                             TextureTypeUVE::Texture2DArray, 2U};
    const TextureHandleUVE array = renderDevice->CreateTextureUVE(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandleUVE);
    const TextureHandleUVE depth = renderDevice->CreateTextureUVE(
        TextureDescUVE{4U, 4U, TextureFormatUVE::Depth32Float, 1U});
    ASSERT_NE(depth, kInvalidTextureHandleUVE);
    if (glMajor < 3 || (glMajor == 3 && glMinor < 2)) {
        renderDevice->DestroyTextureUVE(array);
        renderDevice->DestroyTextureUVE(depth);
        GTEST_SKIP() << "layered FBO attach needs glFramebufferTextureLayer (GL 3.2+)";
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE greenPass;
    greenPass.colorAttachment = array;
    greenPass.depthAttachment = depth;
    greenPass.colorLayer = 0U;
    greenPass.colorLoadOp = LoadOpUVE::Clear;
    greenPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    greenPass.depthLoadOp = LoadOpUVE::Clear;
    commandBuffer->BeginRenderPassUVE(greenPass);
    commandBuffer->EndRenderPassUVE();
    RenderPassDescUVE redPass = greenPass;
    redPass.colorLayer = 1U;
    redPass.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPassUVE(redPass);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(array));
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
    renderDevice->DestroyTextureUVE(array);
    renderDevice->DestroyTextureUVE(depth);
}

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_ArrayTexture_BindsUnderArrayTarget) {
    // Tier 2.3: an array binds under GL_TEXTURE_2D_ARRAY on its unit and must not disturb that
    // unit's 2D binding (targets coexist per unit in GL).
    const TextureHandleUVE color = renderDevice->CreateTextureUVE(TextureDescUVE{1U, 1U});
    ASSERT_NE(color, kInvalidTextureHandleUVE);
    TextureDescUVE arrayDesc{2U, 2U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                             TextureTypeUVE::Texture2DArray, 3U};
    const TextureHandleUVE array = renderDevice->CreateTextureUVE(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandleUVE);
    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(array));

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = color;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindTextureUVE(array, 3U);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 3));
    GLint arrayBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
    EXPECT_EQ(arrayBinding, static_cast<GLint>(native));
    GLint flatBinding = -1;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &flatBinding);
    EXPECT_EQ(flatBinding, 0) << "a 2D bind must not shadow the array on the same unit";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    renderDevice->DestroyTextureUVE(array);
    renderDevice->DestroyTextureUVE(color);
}

TEST_F(GlRenderDeviceUVETest, BindTextureUVE_ArrayTexture_LeavesComputeImageUnitUnbound) {
    // Tier 2.3 mirror of ComputeStorageImageOnSrgbTexture: layered storage images are out of
    // scope, so an array stays sampler-visible while its image unit is cleared — matching the
    // Vulkan backend's black-sink fallback.
    constexpr std::array<std::uint8_t, 4U * 4U * 4U * 2U> kZeroPixels{};
    TextureDescUVE arrayDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U, TextureColorSpaceUVE::Linear,
                             TextureTypeUVE::Texture2DArray, 2U};
    const TextureHandleUVE array = renderDevice->CreateTextureUVE(
        arrayDesc, std::as_bytes(std::span(kZeroPixels)));
    ASSERT_NE(array, kInvalidTextureHandleUVE);

    const ShaderHandleUVE computeShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Compute, std::string(kImageFillComputeSource)});
    if (computeShader == kInvalidShaderHandleUVE) {
        renderDevice->DestroyTextureUVE(array);
        GTEST_SKIP() << "context lacks compute shaders (GL 4.3+)";
    }
    ComputePipelineDescUVE pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    std::string infoLog;
    const PipelineHandleUVE computePipeline =
        renderDevice->CreateComputePipelineUVE(pipelineDesc, &infoLog);
    if (computePipeline == kInvalidPipelineHandleUVE) {
        renderDevice->DestroyShaderUVE(computeShader);
        renderDevice->DestroyTextureUVE(array);
        GTEST_SKIP() << "compute pipeline refused: " << infoLog;
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    while (glGetError() != GL_NO_ERROR) {
    }
    commandBuffer->BindPipelineUVE(computePipeline);
    commandBuffer->BindTextureUVE(array, 0U); // M5b: legal outside pass while compute is bound
    renderDevice->SubmitUVE(std::move(commandBuffer));

    GLint textureName = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &textureName);
    EXPECT_NE(textureName, 0) << "the array must remain bound for sampler access";
    GLint imageName = -1;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
    EXPECT_EQ(imageName, 0) << "layered storage-image bindings must be cleared in Tier 2.3";
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    renderDevice->DestroyPipelineUVE(computePipeline);
    renderDevice->DestroyShaderUVE(computeShader);
    renderDevice->DestroyTextureUVE(array);
}


TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_TwoColorAttachments_ClearEachToItsOwnColor) {
    // Tier 2.4 pixel proof for MRT: one pass clears location 0 green and location 1 red —
    // each texture must hold exactly its own clear color (glClear would paint both one color).
    GLint glMajor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    const TextureHandleUVE target0 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandleUVE);
    const TextureHandleUVE target1 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandleUVE);
    if (glMajor < 3) {
        renderDevice->DestroyTextureUVE(target0);
        renderDevice->DestroyTextureUVE(target1);
        GTEST_SKIP() << "per-slot clears need glClearBufferfv (GL 3.0+)";
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc;
    passDesc.colorAttachment = target0;
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    passDesc.extraColorAttachments[0].target = target1;
    passDesc.extraColorAttachments[0].loadOp = LoadOpUVE::Clear;
    passDesc.extraColorAttachments[0].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    commandBuffer->BeginRenderPassUVE(passDesc);
    GLint mrtFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mrtFramebuffer);
    EXPECT_NE(mrtFramebuffer, 0);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));

    const auto readbackTexture = [&](const TextureHandleUVE target) {
        const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(target));
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
    renderDevice->DestroyTextureUVE(target0);
    renderDevice->DestroyTextureUVE(target1);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_MalformedExtras_LeavesFramebufferUnchanged) {
    // Tier 2.4 mirror of LayerBeyondAttachmentLayerCount: malformed extras reject before any
    // FBO work. Load ops stay Load so the routing check needs no clear-capable context.
    const TextureHandleUVE target0 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandleUVE);
    const TextureHandleUVE target1 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandleUVE);
    const TextureHandleUVE small = renderDevice->CreateTextureUVE(TextureDescUVE{2U, 2U});
    ASSERT_NE(small, kInvalidTextureHandleUVE);
    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);

    const auto assertFramebufferUnchanged = [&commandBuffer](const RenderPassDescUVE& passDesc) {
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        commandBuffer->BeginRenderPassUVE(passDesc);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before);
    };
    const auto loadOnlyPass = [](const TextureHandleUVE color) {
        RenderPassDescUVE passDesc;
        passDesc.colorAttachment = color;
        passDesc.colorLoadOp = LoadOpUVE::Load;
        passDesc.depthLoadOp = LoadOpUVE::Load;
        return passDesc;
    };
    RenderPassDescUVE gap = loadOnlyPass(target0);
    gap.extraColorAttachments[0].target = target1;
    gap.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    gap.extraColorAttachments[2].target = target1; // location 2 set while 1 is empty
    gap.extraColorAttachments[2].loadOp = LoadOpUVE::Load;
    assertFramebufferUnchanged(gap);

    RenderPassDescUVE noLocation0 = loadOnlyPass(kInvalidTextureHandleUVE);
    noLocation0.extraColorAttachments[0].target = target1;
    noLocation0.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    assertFramebufferUnchanged(noLocation0);

    RenderPassDescUVE unknownExtra = loadOnlyPass(target0);
    unknownExtra.extraColorAttachments[0].target = TextureHandleUVE{999999U};
    unknownExtra.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    assertFramebufferUnchanged(unknownExtra);

    RenderPassDescUVE dimsMismatch = loadOnlyPass(target0);
    dimsMismatch.extraColorAttachments[0].target = small;
    dimsMismatch.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    assertFramebufferUnchanged(dimsMismatch);

    RenderPassDescUVE aliased = loadOnlyPass(target0);
    aliased.extraColorAttachments[0].target = target0; // one texture, two attachments
    aliased.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    assertFramebufferUnchanged(aliased);

    RenderPassDescUVE badStore = loadOnlyPass(target0);
    badStore.colorStoreOp = static_cast<StoreOpUVE>(0xFFU);
    assertFramebufferUnchanged(badStore);

    RenderPassDescUVE badExtraOp = loadOnlyPass(target0);
    badExtraOp.extraColorAttachments[0].target = target1;
    badExtraOp.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    badExtraOp.extraColorAttachments[0].storeOp = static_cast<StoreOpUVE>(0xFFU);
    assertFramebufferUnchanged(badExtraOp);

    // The well-formed twin binds a real FBO — the rejects above were selective, not blanket.
    RenderPassDescUVE valid = loadOnlyPass(target0);
    valid.extraColorAttachments[0].target = target1;
    valid.extraColorAttachments[0].loadOp = LoadOpUVE::Load;
    commandBuffer->BeginRenderPassUVE(valid);
    GLint mrtFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mrtFramebuffer);
    EXPECT_NE(mrtFramebuffer, 0);
    commandBuffer->EndRenderPassUVE();
    renderDevice->DestroyTextureUVE(target0);
    renderDevice->DestroyTextureUVE(target1);
    renderDevice->DestroyTextureUVE(small);
}

TEST_F(GlRenderDeviceUVETest, BeginRenderPassUVE_DontCareStore_StaysHealthy) {
    // Tier 2.5: DontCare stores discard (invalidate) without disturbing GL state — the error
    // flag stays clean and the very next pass renders normally. (Discarded CONTENTS are
    // undefined by design, so the follow-up pass carries the pixel proof, not this one.)
    GLint glMajor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    const TextureHandleUVE target0 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target0, kInvalidTextureHandleUVE);
    const TextureHandleUVE target1 = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 4U});
    ASSERT_NE(target1, kInvalidTextureHandleUVE);
    if (glMajor < 3) {
        renderDevice->DestroyTextureUVE(target0);
        renderDevice->DestroyTextureUVE(target1);
        GTEST_SKIP() << "per-slot clears need glClearBufferfv (GL 3.0+)";
    }

    std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE discardPass;
    discardPass.colorAttachment = target0;
    discardPass.colorLoadOp = LoadOpUVE::Clear;
    discardPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
    discardPass.colorStoreOp = StoreOpUVE::DontCare;
    discardPass.extraColorAttachments[0].target = target1;
    discardPass.extraColorAttachments[0].loadOp = LoadOpUVE::Clear;
    discardPass.extraColorAttachments[0].clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
    discardPass.extraColorAttachments[0].storeOp = StoreOpUVE::DontCare;
    commandBuffer->BeginRenderPassUVE(discardPass);
    commandBuffer->EndRenderPassUVE();

    RenderPassDescUVE followUpPass;
    followUpPass.colorAttachment = target0;
    followUpPass.colorLoadOp = LoadOpUVE::Clear;
    followUpPass.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
    followUpPass.colorStoreOp = StoreOpUVE::Store;
    commandBuffer->BeginRenderPassUVE(followUpPass);
    commandBuffer->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commandBuffer));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);

    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(target0));
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
    renderDevice->DestroyTextureUVE(target0);
    renderDevice->DestroyTextureUVE(target1);
}

TEST_F(GlRenderDeviceUVETest, FoundationBufferCopyFenceAndSurfaceUseNativeWork) {
    using ResultUVE = RenderOperationResultUVE;
    const auto caps = renderDevice->GetCapabilitiesUVE();
    ASSERT_EQ(caps.bufferCopy, RenderFeatureSupportUVE::Native);
    ASSERT_EQ(caps.fences, RenderFeatureSupportUVE::Native);
    const std::array<std::uint32_t, 4> values{7, 11, 23, 42};
    const auto source = renderDevice->CreateBufferUVE({sizeof(values), BufferUsageUVE::Storage},
                                                      std::as_bytes(std::span(values)));
    const auto target = renderDevice->CreateBufferUVE({sizeof(values), BufferUsageUVE::Storage});
    ASSERT_EQ(renderDevice->CopyBufferUVE({source, target, 4, 0, 12}), ResultUVE::Success);
    EXPECT_EQ(renderDevice->CopyBufferUVE({target, target, 0, 4, 8}), ResultUVE::InvalidArgument);
    FenceHandleUVE fence;
    ASSERT_EQ(renderDevice->InsertFenceUVE(fence), ResultUVE::Success);
    ASSERT_EQ(renderDevice->WaitFenceUVE(fence, 1'000'000'000ULL), ResultUVE::Success);
    std::array<std::uint32_t, 4> observed{};
    ASSERT_TRUE(renderDevice->ReadbackBufferUVE(target, std::as_writable_bytes(std::span(observed))));
    EXPECT_EQ(observed[0], 11U);
    EXPECT_EQ(observed[1], 23U);
    EXPECT_EQ(observed[2], 42U);
    EXPECT_EQ(renderDevice->DestroyFenceUVE(fence), ResultUVE::Success);
    EXPECT_EQ(renderDevice->WaitFenceUVE(fence, 0), ResultUVE::InvalidHandle);
    RenderSurfaceUVE surface(*renderDevice);
    RenderSurfaceInfoUVE info;
    ASSERT_EQ(surface.DescribeUVE(info), ResultUVE::Success);
    EXPECT_EQ(info.width, windowManager->GetWidthUVE());
    EXPECT_EQ(info.height, windowManager->GetHeightUVE());
    EXPECT_TRUE(info.drawable);
    EXPECT_EQ(surface.PresentUVE(), ResultUVE::Success);
    renderDevice->DestroyBufferUVE(source);
    renderDevice->DestroyBufferUVE(target);
}

TEST_F(GlRenderDeviceUVETest, FoundationMipsAndBlitProducePixelsAndPreserveScissorAndBindings) {
    using ResultUVE = RenderOperationResultUVE;
    std::vector<std::byte> pixels(84U);
    for (std::size_t i = 0; i < 16U; ++i) {
        pixels[i * 4U] = std::byte{255};
        pixels[i * 4U + 3U] = std::byte{255};
    }
    const auto source = renderDevice->CreateTextureUVE({4, 4, TextureFormatUVE::RGBA8Unorm, 3}, pixels);
    const auto target = renderDevice->CreateTextureUVE({2, 2, TextureFormatUVE::RGBA8Unorm});
    ASSERT_NE(source, kInvalidTextureHandleUVE);
    ASSERT_NE(target, kInvalidTextureHandleUVE);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
    ASSERT_EQ(renderDevice->GenerateMipmapsUVE(source), ResultUVE::Success);
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    EXPECT_EQ(static_cast<std::uint32_t>(bound), renderDevice->GetNativeTextureIdUVE(target));
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(source));
    std::array<std::uint8_t, 4> mip{};
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
    EXPECT_EQ(mip, (std::array<std::uint8_t, 4>{255, 0, 0, 255}));
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 0, 0);
    ASSERT_EQ(renderDevice->BlitTextureUVE(
                  {source, target, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilterUVE::Linear}),
              ResultUVE::Success);
    EXPECT_EQ(glIsEnabled(GL_SCISSOR_TEST), static_cast<GLboolean>(GL_TRUE));
    glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
    std::array<std::uint8_t, 16> observed{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, observed.data());
    for (std::size_t i = 0; i < 4U; ++i) {
        EXPECT_EQ(observed[i * 4U], 255U);
        EXPECT_EQ(observed[i * 4U + 1U], 0U);
        EXPECT_EQ(observed[i * 4U + 2U], 0U);
        EXPECT_EQ(observed[i * 4U + 3U], 255U);
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTextureUVE(source);
    renderDevice->DestroyTextureUVE(target);
}

TEST_F(GlRenderDeviceUVETest, SingleTwoChannelAndFloatFormatsUploadRenderBlitAndMipOnTheRealContext) {
    // One-/two-channel and 32-bit float formats on a real GL context. Every claim is read back from the GL object itself
    // (glGetTexImage on the native name), not inferred from a result code:
    //   1. a 3-wide R8Unorm / RG8Unorm upload is stored texel-for-texel (the tightly packed row
    //      contract: GL's default UNPACK_ALIGNMENT of 4 would have skewed rows 2 and 3);
    //   2. an R16Float texture is a real colour attachment: the pass clears it and a draw
    //      writes into it, the missing channels read back as 0/0/1;
    //   3. BlitTextureUVE copies R32Float texels exactly and GenerateMipmapsUVE averages an RG8
    //      chain — both paths used to be limited to RGBA8/RGBA16F by a format list.
    using ResultUVE = RenderOperationResultUVE;
    constexpr std::array<TextureFormatUVE, 7> kFormats{
        TextureFormatUVE::R8Unorm,  TextureFormatUVE::RG8Unorm, TextureFormatUVE::R16Float,
        TextureFormatUVE::RG16Float, TextureFormatUVE::R32Float, TextureFormatUVE::RG32Float,
        TextureFormatUVE::RGBA32Float};
    for (const TextureFormatUVE format : kFormats) {
        EXPECT_TRUE(renderDevice->SupportsTextureFormatUVE(format)) << static_cast<int>(format);
        EXPECT_FALSE(renderDevice->SupportsTextureFormatUVE(format, TextureColorSpaceUVE::Srgb))
            << static_cast<int>(format);
        const std::vector<std::byte> texel(GetTextureFormatBlockInfoUVE(format).bytes);
        const auto texture = renderDevice->CreateTextureUVE({1U, 1U, format, 1U}, texel);
        ASSERT_NE(texture, kInvalidTextureHandleUVE) << static_cast<int>(format);
        EXPECT_EQ(renderDevice->CreateTextureUVE({1U, 1U, format, 1U, TextureColorSpaceUVE::Srgb}, texel),
                  kInvalidTextureHandleUVE);
        renderDevice->DestroyTextureUVE(texture);
    }
    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 1. Odd-width uploads survive byte for byte.
    constexpr std::array<std::uint8_t, 6> kR8Rows{10U, 20U, 30U, 40U, 50U, 60U};
    const auto r8 = renderDevice->CreateTextureUVE({3U, 2U, TextureFormatUVE::R8Unorm, 1U},
                                                   std::as_bytes(std::span(kR8Rows)));
    ASSERT_NE(r8, kInvalidTextureHandleUVE);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(r8));
    std::array<std::uint8_t, 6> observedR8{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, observedR8.data());
    EXPECT_EQ(observedR8, kR8Rows) << "3-byte rows must not be read with a 4-byte stride";
    constexpr std::array<std::uint8_t, 6> kRg8Row{1U, 2U, 3U, 4U, 5U, 6U};
    const auto rg8 = renderDevice->CreateTextureUVE({3U, 1U, TextureFormatUVE::RG8Unorm, 1U},
                                                    std::as_bytes(std::span(kRg8Row)));
    ASSERT_NE(rg8, kInvalidTextureHandleUVE);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(rg8));
    std::array<std::uint8_t, 6> observedRg8{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RG, GL_UNSIGNED_BYTE, observedRg8.data());
    EXPECT_EQ(observedRg8, kRg8Row);
    GLint unpackAlignment = 0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    EXPECT_EQ(unpackAlignment, 4) << "the upload path restores the context's unpack alignment";

    // 2. R16Float is a real colour attachment on GL.
    const auto halfTarget = renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::R16Float, 1U});
    ASSERT_NE(halfTarget, kInvalidTextureHandleUVE);
    const ShaderHandleUVE vertexShader =
        renderDevice->CreateShaderUVE(ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource)});
    PipelineDescUVE pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexLayout = {VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    pipelineDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    // Covers the whole right half of clip space (x >= 0): texels 2..3 of every row are drawn
    // white, texels 0..1 keep the clear value.
    constexpr float kRightHalf[] = {0.0F, -1.0F, 0.0F, 3.0F, -1.0F, 0.0F, 0.0F, 3.0F, 0.0F};
    const std::span<const std::byte> vertexBytes = std::as_bytes(std::span<const float>(kRightHalf));
    const BufferHandleUVE vertexBuffer =
        renderDevice->CreateBufferUVE(BufferDescUVE{vertexBytes.size(), BufferUsageUVE::Vertex}, vertexBytes);
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);
    {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDescUVE passDesc;
        passDesc.colorAttachment = halfTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.25F, 0.5F, 0.75F, 0.125F}; // only .r can land in an R16F target
        passDesc.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(pipeline);
        commandBuffer->BindVertexBufferUVE(vertexBuffer);
        commandBuffer->DrawUVE(3);
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
        glFinish();
    }
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(halfTarget));
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
    const auto floatSource = renderDevice->CreateTextureUVE({2U, 2U, TextureFormatUVE::R32Float, 1U},
                                                            std::as_bytes(std::span(kFloatTexels)));
    const auto floatTarget = renderDevice->CreateTextureUVE({2U, 2U, TextureFormatUVE::R32Float, 1U});
    ASSERT_NE(floatSource, kInvalidTextureHandleUVE);
    ASSERT_NE(floatTarget, kInvalidTextureHandleUVE);
    ASSERT_EQ(renderDevice->BlitTextureUVE(
                  {floatSource, floatTarget, {0, 0, 0, 0, 2, 2}, {0, 0, 0, 0, 2, 2}, BlitFilterUVE::Point}),
              ResultUVE::Success);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(floatTarget));
    std::array<float, 4> observedFloats{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, observedFloats.data());
    EXPECT_EQ(observedFloats, kFloatTexels) << "a point blit of 32-bit floats is bit-exact";
    EXPECT_EQ(renderDevice->BlitTextureUVE(
                  {floatSource, rg8, {0, 0, 0, 0, 2, 1}, {0, 0, 0, 0, 2, 1}, BlitFilterUVE::Point}),
              ResultUVE::InvalidArgument)
        << "format conversion is still refused";
    std::vector<std::byte> rgChain(static_cast<std::size_t>((16U + 4U + 1U) * 2U));
    for (std::size_t texel = 0; texel < 16U; ++texel) {
        rgChain[texel * 2U] = std::byte{200};
        rgChain[texel * 2U + 1U] = std::byte{40};
    }
    const auto rgMips = renderDevice->CreateTextureUVE({4U, 4U, TextureFormatUVE::RG8Unorm, 3U}, rgChain);
    ASSERT_NE(rgMips, kInvalidTextureHandleUVE);
    ASSERT_EQ(renderDevice->GenerateMipmapsUVE(rgMips), ResultUVE::Success);
    EXPECT_EQ(renderDevice->GenerateMipmapsUVE(rg8), ResultUVE::InvalidArgument) << "single level";
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(rgMips));
    std::array<std::uint8_t, 2> lastLevel{};
    glGetTexImage(GL_TEXTURE_2D, 2, GL_RG, GL_UNSIGNED_BYTE, lastLevel.data());
    EXPECT_NEAR(static_cast<int>(lastLevel[0]), 200, 1);
    EXPECT_NEAR(static_cast<int>(lastLevel[1]), 40, 1);

    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTextureUVE(rgMips);
    renderDevice->DestroyTextureUVE(floatTarget);
    renderDevice->DestroyTextureUVE(floatSource);
    renderDevice->DestroyBufferUVE(vertexBuffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyTextureUVE(halfTarget);
    renderDevice->DestroyTextureUVE(rg8);
    renderDevice->DestroyTextureUVE(r8);
}

constexpr std::string_view kVolumeSampleFragmentSource = R"(#version 330 core
uniform sampler3D uVolume;
out vec4 FragColor;
void main() {
    // Target is 4x1: pixel x samples the centre of slice x (w = (x + 0.5) / 4).
    FragColor = texture(uVolume, vec3(0.5, 0.5, gl_FragCoord.x / 4.0));
}
)";

TEST_F(GlRenderDeviceUVETest, Texture3D_UploadsSliceMajorSamplesThroughSampler3DAndBuildsMips) {
    // Texture3D on the real context: four solid 4x4 slices upload slice-major into a
    // GL_TEXTURE_3D object (read back whole through glGetTexImage), a sampler3D draw returns
    // each slice's colour at its centre w, and GenerateMipmapsUVE averages the volume down to
    // its 1x1x1 tail with the same device call every other type uses.
    using ResultUVE = RenderOperationResultUVE;
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
    TextureDescUVE volumeDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U};
    volumeDesc.type = TextureTypeUVE::Texture3D;
    volumeDesc.depth = 4U;
    const TextureHandleUVE volume = renderDevice->CreateTextureUVE(volumeDesc, std::as_bytes(std::span(upload)));
    ASSERT_NE(volume, kInvalidTextureHandleUVE);
    TextureDescUVE reported{};
    ASSERT_EQ(renderDevice->GetTextureDescUVE(volume, reported), ResultUVE::Success);
    EXPECT_EQ(reported.type, TextureTypeUVE::Texture3D);
    EXPECT_EQ(reported.depth, 4U);
    EXPECT_EQ(reported.usage, TextureUsageUVE::Default);

    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const GLuint native = static_cast<GLuint>(renderDevice->GetNativeTextureIdUVE(volume));
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
    const TextureHandleUVE target = renderDevice->CreateTextureUVE(TextureDescUVE{4U, 1U});
    ASSERT_NE(target, kInvalidTextureHandleUVE);
    const ShaderHandleUVE vertexShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Vertex, std::string(kSrgbSampleVertexSource)});
    const ShaderHandleUVE fragmentShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Fragment, std::string(kVolumeSampleFragmentSource)});
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);
    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = renderDevice->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = target;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(pipeline);
        commandBuffer->BindTextureUVE(volume, 0U);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
        glFinish();
    }
    glActiveTexture(GL_TEXTURE0);
    GLint boundVolume = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &boundVolume);
    EXPECT_EQ(boundVolume, static_cast<GLint>(native)) << "a volume binds under GL_TEXTURE_3D";
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(target));
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
    TextureDescUVE mipDesc = volumeDesc;
    mipDesc.mipLevels = 3U;
    const TextureHandleUVE mipVolume = renderDevice->CreateTextureUVE(mipDesc, chain);
    ASSERT_NE(mipVolume, kInvalidTextureHandleUVE);
    EXPECT_TRUE(CanGenerateMipmapsUVE(mipDesc));
    ASSERT_EQ(renderDevice->GenerateMipmapsUVE(mipVolume), ResultUVE::Success);
    glBindTexture(GL_TEXTURE_3D, renderDevice->GetNativeTextureIdUVE(mipVolume));
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
    EXPECT_EQ(renderDevice->BlitTextureUVE({volume, target, {0, 0, 0, 0, 4, 1}, {0, 0, 0, 0, 4, 1},
                                            BlitFilterUVE::Point}),
              ResultUVE::InvalidArgument);
    {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        RenderPassDescUVE volumePass{};
        volumePass.colorAttachment = volume;
        commandBuffer->BeginRenderPassUVE(volumePass);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "a volume (no RenderTarget usage) must not open a pass";
    }
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTextureUVE(mipVolume);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(fragmentShader);
    renderDevice->DestroyShaderUVE(vertexShader);
    renderDevice->DestroyTextureUVE(target);
    renderDevice->DestroyTextureUVE(volume);
}

TEST_F(GlRenderDeviceUVETest, TextureUsageUVE_GatesAttachSampleStorageAndTransfersOnTheRealContext) {
    // Usage flags on the real context. Each gate is observed through GL state, not a result code
    // alone: a Sampled-only texture never becomes a framebuffer attachment, a texture without
    // Sampled never lands on a texture unit, one without Storage never lands on an image unit,
    // and the device-level transfer gates (blit, mip generation) refuse without TransferSrc/Dst.
    using ResultUVE = RenderOperationResultUVE;
    TextureDescUVE sampledOnlyDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U};
    sampledOnlyDesc.usage = TextureUsageUVE::Sampled;
    const TextureHandleUVE sampledOnly = renderDevice->CreateTextureUVE(sampledOnlyDesc);
    ASSERT_NE(sampledOnly, kInvalidTextureHandleUVE);
    TextureDescUVE targetOnlyDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U};
    targetOnlyDesc.usage = TextureUsageUVE::RenderTarget | TextureUsageUVE::TransferSrc;
    const TextureHandleUVE targetOnly = renderDevice->CreateTextureUVE(targetOnlyDesc);
    ASSERT_NE(targetOnly, kInvalidTextureHandleUVE);
    TextureDescUVE sinkDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 1U};
    sinkDesc.usage = TextureUsageUVE::Sampled | TextureUsageUVE::TransferDst;
    const TextureHandleUVE sink = renderDevice->CreateTextureUVE(sinkDesc);
    ASSERT_NE(sink, kInvalidTextureHandleUVE);
    TextureDescUVE sampledDepthDesc{4U, 4U, TextureFormatUVE::Depth32Float, 1U};
    sampledDepthDesc.usage = TextureUsageUVE::Sampled;
    const TextureHandleUVE sampledDepth = renderDevice->CreateTextureUVE(sampledDepthDesc);
    ASSERT_NE(sampledDepth, kInvalidTextureHandleUVE);
    TextureDescUVE unsupportedDesc{4U, 4U, TextureFormatUVE::Depth32Float, 1U};
    unsupportedDesc.usage = TextureUsageUVE::Storage;
    EXPECT_EQ(renderDevice->CreateTextureUVE(unsupportedDesc), kInvalidTextureHandleUVE)
        << "the shared validator refuses unsupported usage before any GL allocation";

    // 1. Attach gate: Sampled-only colour, Sampled-only depth, and an explicit RenderTarget texture.
    {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        GLint before = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
        RenderPassDescUVE sampledPass{};
        sampledPass.colorAttachment = sampledOnly;
        commandBuffer->BeginRenderPassUVE(sampledPass);
        GLint after = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "Sampled-only colour must not open a pass";
        RenderPassDescUVE depthPass{};
        depthPass.colorAttachment = targetOnly;
        depthPass.depthAttachment = sampledDepth;
        commandBuffer->BeginRenderPassUVE(depthPass);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_EQ(after, before) << "Sampled-only depth must not open a pass";
        RenderPassDescUVE validPass{};
        validPass.colorAttachment = targetOnly;
        validPass.colorLoadOp = LoadOpUVE::Clear;
        validPass.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
        validPass.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(validPass);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &after);
        EXPECT_NE(after, 0) << "an explicit RenderTarget texture attaches";
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
        glFinish();
    }

    // 2. Sample gate: a texture without Sampled leaves its unit's binding alone.
    {
        std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = targetOnly;
        passDesc.colorLoadOp = LoadOpUVE::Load;
        passDesc.depthLoadOp = LoadOpUVE::DontCare;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindTextureUVE(sampledOnly, 5U);
        commandBuffer->BindTextureUVE(sink, 5U); // Sampled|TransferDst: replaces the binding
        commandBuffer->BindTextureUVE(targetOnly, 5U); // no Sampled: refused, unit keeps `sink`
        commandBuffer->EndRenderPassUVE();
        renderDevice->SubmitUVE(std::move(commandBuffer));
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + 5));
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        EXPECT_EQ(bound, static_cast<GLint>(renderDevice->GetNativeTextureIdUVE(sink)))
            << "a texture created without Sampled must not reach a sampler unit";
        glActiveTexture(GL_TEXTURE0);
    }

    // 3. Transfer gates: the green clear in `targetOnly` (RenderTarget|TransferSrc) blits into
    // `sink` (TransferDst), while a Sampled-only source and a destination without TransferDst
    // are refused before any GL call; mip generation needs both transfer bits.
    const TextureRegionUVE whole{0U, 0U, 0U, 0U, 4U, 4U};
    EXPECT_EQ(renderDevice->BlitTextureUVE({sampledOnly, sink, whole, whole, BlitFilterUVE::Point}),
              ResultUVE::InvalidArgument);
    EXPECT_EQ(renderDevice->BlitTextureUVE({targetOnly, sampledOnly, whole, whole, BlitFilterUVE::Point}),
              ResultUVE::InvalidArgument);
    ASSERT_EQ(renderDevice->BlitTextureUVE({targetOnly, sink, whole, whole, BlitFilterUVE::Point}),
              ResultUVE::Success);
    glBindTexture(GL_TEXTURE_2D, renderDevice->GetNativeTextureIdUVE(sink));
    std::array<std::uint8_t, 4U * 4U * 4U> sinkTexels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sinkTexels.data());
    EXPECT_EQ(sinkTexels[0], 0U);
    EXPECT_EQ(sinkTexels[1], 255U) << "the blit carried the green clear across";
    EXPECT_EQ(sinkTexels[2], 0U);
    TextureDescUVE sealedChainDesc{4U, 4U, TextureFormatUVE::RGBA8Unorm, 3U};
    sealedChainDesc.usage = TextureUsageUVE::Sampled | TextureUsageUVE::TransferDst;
    const TextureHandleUVE sealedChain = renderDevice->CreateTextureUVE(sealedChainDesc, std::vector<std::byte>(84U));
    ASSERT_NE(sealedChain, kInvalidTextureHandleUVE);
    EXPECT_EQ(renderDevice->GenerateMipmapsUVE(sealedChain), ResultUVE::InvalidArgument)
        << "mip generation needs TransferSrc as well";

    // 4. Storage gate: without Storage the image unit stays empty while the texture unit keeps
    // the texture for sampling (compute contexts only).
    const ShaderHandleUVE computeShader = renderDevice->CreateShaderUVE(
        ShaderDescUVE{ShaderStageUVE::Compute, std::string(kImageFillComputeSource)});
    if (computeShader != kInvalidShaderHandleUVE) {
        ComputePipelineDescUVE computeDesc{};
        computeDesc.computeShader = computeShader;
        const PipelineHandleUVE computePipeline = renderDevice->CreateComputePipelineUVE(computeDesc);
        if (computePipeline != kInvalidPipelineHandleUVE) {
            std::unique_ptr<ICommandBufferUVE> commandBuffer = renderDevice->CreateCommandBufferUVE();
            ASSERT_NE(commandBuffer, nullptr);
            while (glGetError() != GL_NO_ERROR) {
            }
            commandBuffer->BindPipelineUVE(computePipeline);
            commandBuffer->BindTextureUVE(sampledOnly, 0U);
            renderDevice->SubmitUVE(std::move(commandBuffer));
            glActiveTexture(GL_TEXTURE0);
            GLint textureName = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureName);
            EXPECT_EQ(textureName, static_cast<GLint>(renderDevice->GetNativeTextureIdUVE(sampledOnly)))
                << "the texture stays sampler-visible";
            GLint imageName = -1;
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0U, &imageName);
            EXPECT_EQ(imageName, 0) << "no Storage usage: the image unit must stay unbound";
            renderDevice->DestroyPipelineUVE(computePipeline);
        }
        renderDevice->DestroyShaderUVE(computeShader);
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    renderDevice->DestroyTextureUVE(sealedChain);
    renderDevice->DestroyTextureUVE(sampledDepth);
    renderDevice->DestroyTextureUVE(sink);
    renderDevice->DestroyTextureUVE(targetOnly);
    renderDevice->DestroyTextureUVE(sampledOnly);
}

// ---- Foundation WP3: recorded query brackets ---------------------------------------------
// Defined BEFORE the timestamp test on purpose: the sandbox's softpipe rasterizer reports zero
// for every occlusion query issued after a glQueryCounter in the same context (documented in
// FOUNDATION.md), so this occlusion-only case must run first in definition order.
TEST_F(GlRenderDeviceUVETest, RecordedOcclusionQueryCountsOnlyVisibleDraws) {
    using ResultUVE = RenderOperationResultUVE;
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().occlusionQueries, RenderFeatureSupportUVE::Native);
    QueryHandleUVE empty, drawn, timestamp;
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::AnySamplesPassed, empty), ResultUVE::Success);
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::AnySamplesPassed, drawn), ResultUVE::Success);
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::Timestamp, timestamp), ResultUVE::Success);
    const auto vs = renderDevice->CreateShaderUVE(
        {ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource), "main"});
    const auto fs = renderDevice->CreateShaderUVE(
        {ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource), "main"});
    PipelineDescUVE desc;
    desc.vertexShader = vs;
    desc.fragmentShader = fs;
    desc.vertexLayout = {{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    desc.vertexStride = 3U * sizeof(float);
    const auto pipeline = renderDevice->CreatePipelineUVE(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    const std::array<float, 9> vertices{-0.5F, -0.5F, 0, 0.5F, -0.5F, 0, 0, 0.5F, 0};
    const auto buffer = renderDevice->CreateBufferUVE({sizeof(vertices), BufferUsageUVE::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    auto commands = renderDevice->CreateCommandBufferUVE();
    // Validation shared with Null and Vulkan.
    EXPECT_EQ(commands->BeginQueryUVE(QueryHandleUVE{999999U}), ResultUVE::InvalidHandle);
    EXPECT_EQ(commands->BeginQueryUVE(timestamp), ResultUVE::InvalidArgument);
    EXPECT_EQ(commands->WriteTimestampUVE(drawn), ResultUVE::InvalidArgument);
    EXPECT_EQ(commands->EndQueryUVE(drawn), ResultUVE::InvalidState);
    commands->BeginRenderPassUVE({});
    commands->BindPipelineUVE(pipeline);
    commands->BindVertexBufferUVE(buffer);
    ASSERT_EQ(commands->BeginQueryUVE(empty), ResultUVE::Recorded);
    EXPECT_EQ(commands->BeginQueryUVE(drawn), ResultUVE::InvalidState) << "brackets do not nest";
    EXPECT_EQ(renderDevice->BeginQueryUVE(drawn), ResultUVE::InvalidState)
        << "the device-level bracket shares the one GL_ANY_SAMPLES_PASSED slot";
    ASSERT_EQ(commands->EndQueryUVE(empty), ResultUVE::Recorded);
    ASSERT_EQ(commands->BeginQueryUVE(drawn), ResultUVE::Recorded);
    commands->DrawUVE(3U);
    ASSERT_EQ(commands->EndQueryUVE(drawn), ResultUVE::Recorded);
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    RenderQueryResultUVE value{RenderQueryKindUVE::AnySamplesPassed, 42U};
    ASSERT_EQ(renderDevice->GetQueryResultUVE(empty, value, true), ResultUVE::Success);
    EXPECT_EQ(value.value, 0U) << "nothing was drawn inside the first bracket";
    ASSERT_EQ(renderDevice->GetQueryResultUVE(drawn, value, true), ResultUVE::Success);
    EXPECT_EQ(value.value, 1U) << "the triangle inside the second bracket passed";
    EXPECT_EQ(renderDevice->DestroyQueryUVE(empty), ResultUVE::Success);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(drawn), ResultUVE::Success);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(timestamp), ResultUVE::Success);
    renderDevice->DestroyBufferUVE(buffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vs);
    renderDevice->DestroyShaderUVE(fs);
}

TEST_F(GlRenderDeviceUVETest, RecordedTimestampsAreMonotonic) {
    using ResultUVE = RenderOperationResultUVE;
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().timestampQueries, RenderFeatureSupportUVE::Native);
    QueryHandleUVE before, after;
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::Timestamp, before), ResultUVE::Success);
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::Timestamp, after), ResultUVE::Success);
    auto commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_EQ(commands->WriteTimestampUVE(before), ResultUVE::Recorded);
    commands->BeginRenderPassUVE({});
    commands->EndRenderPassUVE();
    ASSERT_EQ(commands->WriteTimestampUVE(after), ResultUVE::Recorded);
    renderDevice->SubmitUVE(std::move(commands));
    RenderQueryResultUVE first, second;
    ASSERT_EQ(renderDevice->GetQueryResultUVE(before, first, true), ResultUVE::Success);
    ASSERT_EQ(renderDevice->GetQueryResultUVE(after, second, true), ResultUVE::Success);
    EXPECT_EQ(first.kind, RenderQueryKindUVE::Timestamp);
    EXPECT_GE(second.value, first.value);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(before), ResultUVE::Success);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(after), ResultUVE::Success);
}

TEST_F(GlRenderDeviceUVETest, FoundationQueriesMeasureRealVisibilityAndGpuTimestamps) {
    using ResultUVE = RenderOperationResultUVE;
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().timestampQueries, RenderFeatureSupportUVE::Native);
    ASSERT_EQ(renderDevice->GetCapabilitiesUVE().occlusionQueries, RenderFeatureSupportUVE::Native);
    QueryHandleUVE before, after, visible;
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::Timestamp, before), ResultUVE::Success);
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::Timestamp, after), ResultUVE::Success);
    ASSERT_EQ(renderDevice->CreateQueryUVE(RenderQueryKindUVE::AnySamplesPassed, visible),
              ResultUVE::Success);
    RenderQueryResultUVE value{RenderQueryKindUVE::Timestamp, 42};
    EXPECT_EQ(renderDevice->GetQueryResultUVE(before, value), ResultUVE::NotReady);
    EXPECT_EQ(value.value, 42U);
    ASSERT_EQ(renderDevice->WriteTimestampUVE(before), ResultUVE::Success);
    const auto vs = renderDevice->CreateShaderUVE(
        {ShaderStageUVE::Vertex, std::string(kValidVertexShaderSource), "main"});
    const auto fs = renderDevice->CreateShaderUVE(
        {ShaderStageUVE::Fragment, std::string(kValidFragmentShaderSource), "main"});
    PipelineDescUVE desc;
    desc.vertexShader = vs;
    desc.fragmentShader = fs;
    desc.vertexLayout = {{"POSITION", VertexAttributeFormatUVE::Float3, 0}};
    desc.vertexStride = 3U * sizeof(float);
    const auto pipeline = renderDevice->CreatePipelineUVE(desc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);
    const std::array<float, 9> vertices{-0.5F, -0.5F, 0, 0.5F, -0.5F, 0, 0, 0.5F, 0};
    const auto buffer = renderDevice->CreateBufferUVE({sizeof(vertices), BufferUsageUVE::Vertex},
                                                      std::as_bytes(std::span(vertices)));
    auto commands = renderDevice->CreateCommandBufferUVE();
    ASSERT_EQ(commands->MemoryBarrierUVE(MemoryBarrierMaskUVE::All), ResultUVE::Success);
    commands->BeginRenderPassUVE({});
    commands->BindPipelineUVE(pipeline);
    commands->BindVertexBufferUVE(buffer);
    EXPECT_EQ(commands->MemoryBarrierUVE(MemoryBarrierMaskUVE::All), ResultUVE::InvalidState);
    ASSERT_EQ(renderDevice->BeginQueryUVE(visible), ResultUVE::Success);
    ASSERT_EQ(renderDevice->EndQueryUVE(visible), ResultUVE::Success);
    ASSERT_EQ(renderDevice->GetQueryResultUVE(visible, value, true), ResultUVE::Success);
    EXPECT_EQ(value.value, 0U);
    ASSERT_EQ(renderDevice->BeginQueryUVE(visible), ResultUVE::Success);
    commands->DrawUVE(3U);
    ASSERT_EQ(renderDevice->EndQueryUVE(visible), ResultUVE::Success);
    ASSERT_EQ(renderDevice->GetQueryResultUVE(visible, value, true), ResultUVE::Success);
    EXPECT_EQ(value.value, 1U);
    commands->EndRenderPassUVE();
    renderDevice->SubmitUVE(std::move(commands));
    ASSERT_EQ(renderDevice->WriteTimestampUVE(after), ResultUVE::Success);
    RenderQueryResultUVE first, second;
    ASSERT_EQ(renderDevice->GetQueryResultUVE(before, first, true), ResultUVE::Success);
    ASSERT_EQ(renderDevice->GetQueryResultUVE(after, second, true), ResultUVE::Success);
    EXPECT_GE(second.value, first.value);
    EXPECT_EQ(first.kind, RenderQueryKindUVE::Timestamp);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(before), ResultUVE::Success);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(after), ResultUVE::Success);
    EXPECT_EQ(renderDevice->DestroyQueryUVE(visible), ResultUVE::Success);
    EXPECT_EQ(renderDevice->GetQueryResultUVE(visible, value), ResultUVE::InvalidHandle);
    renderDevice->DestroyBufferUVE(buffer);
    renderDevice->DestroyPipelineUVE(pipeline);
    renderDevice->DestroyShaderUVE(vs);
    renderDevice->DestroyShaderUVE(fs);
}

} // namespace UVE::Render::Tests
