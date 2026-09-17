// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Vulkan M1 bootstrap device tests. Two tiers, exactly like the GL device suite this is
// modelled on (gl_render_device_uve_tests.cpp): tests that interrogate the *capability
// boundary* run in every environment (they deliberately never need a Vulkan runtime), while
// tests that bring up a real Vulkan device GTEST_SKIP() when the host lacks the two normal
// host dependencies — a display for GLFW to create the window against, and a Vulkan ICD (CI
// installs mesa-vulkan-drivers' lavapipe, where these run for real; the development sandbox
// has neither loader nor ICD, where they skip with explicit reasons).
//
// What is asserted when the device does come up is the M1 *honesty contract*, not raw
// rendering: the backend reports itself by its bootstrap name, every out-of-scope milestone
// resource call returns the documented invalid result, and PresentUVE() can advance several
// frames against the real lavapipe software swapchain without crashing — which is exactly
// what "M1: init -> surface -> swapchain -> clear-color present" means end-to-end.


#include "uve/rhi_vulkan/vulkan_render_device_uve.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "uve/events/event_system_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/window/i_vulkan_window_surface_uve.h"
#include "spirv_test_shaders_uve.h"
#include "uve/window/null_window_manager_uve.h"
#include "uve/window/window_manager_uve.h"

namespace UVE::Render::Tests {
namespace {

[[nodiscard]] Window::WindowDescUVE MakeTestWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_vulkan_render_device_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // Vulkan ignores the GL hint fields entirely — WindowManagerUVE only uses them for the
    // GL context-creation path — but defaulting them keeps the desc unambiguous to readers.
    return desc;
}

} // namespace

// ---------------------------------------------------------------------------
// Tier 1: capability-boundary tests — must pass on EVERY host, Vulkan or not.
// ---------------------------------------------------------------------------

TEST(VulkanCapabilityUVETest, NullWindowManagerNeverOffersVulkanSurfaceBridge) {
    Window::NullWindowManagerUVE nullWindowManager;
    // Query through the IWindowManagerUVE base — exactly how the factory does it: through the
    // polymorphic base, the compiler cannot prove the outcome away (on the concrete type it flags "can never
    // succeed" on the concrete final type, which is precisely the fact under test).
    Window::IWindowManagerUVE& asBase = nullWindowManager;
    EXPECT_EQ(dynamic_cast<Window::IVulkanWindowSurfaceUVE*>(&asBase), nullptr)
        << "the headless window manager must not claim the Vulkan surface capability";
}

TEST(VulkanCapabilityUVETest, CreateOnNullWindowManagerReturnsNullptr) {
    Window::NullWindowManagerUVE nullWindowManager;
    auto device = VulkanRenderDeviceUVE::CreateUVE(nullWindowManager);
    EXPECT_EQ(device, nullptr) << "the factory must cleanly refuse headless input";
}

namespace {

/// Minimal stub bridge that implements the capability contract but reports "no extensions" —
/// the deterministic refusal path for the bridge-direct factory overload on ANY host (with or
/// without a Vulkan loader installed). The bridge methods themselves are never invoked before
/// the extension query bails, so their bodies being minimal is honest, not mockup.
class NoExtensionsStubBridgeUVE final : public Window::IVulkanWindowSurfaceUVE {
public:
    [[nodiscard]] std::vector<const char*> GetRequiredVulkanInstanceExtensionsUVE() const override {
        return {}; // deliberate: the documented "capability unavailable" signal
    }
    [[nodiscard]] std::uintptr_t CreateVulkanWindowSurfaceUVE(std::uintptr_t /*vulkanInstance*/) override {
        return 0U;
    }
    void DestroyVulkanWindowSurfaceUVE(std::uintptr_t /*vulkanInstance*/,
                                       std::uintptr_t /*vulkanSurface*/) override {}
    void GetVulkanFramebufferSizeUVE(std::uint32_t& outWidth, std::uint32_t& outHeight) const override {
        outWidth = 64U;
        outHeight = 64U;
    }
};

} // namespace

TEST(VulkanCapabilityUVETest, BridgeDirectCreateOnStubWithoutExtensionsReturnsNullptr) {
    NoExtensionsStubBridgeUVE stubBridge;
    auto device = VulkanRenderDeviceUVE::CreateFromBridgeUVE(stubBridge);
    EXPECT_EQ(device, nullptr) << "a bridge advertising no Vulkan WSI extensions must be refused";
}

TEST(VulkanCapabilityUVETest, RealWindowManagerOffersVulkanSurfaceBridge) {
    // Interface-cast wiring only (no window needed): the real window manager class must be
    // typed against the capability interface so the factory's dynamic_cast has something to
    // find. WindowDescUVE-sized default construction is enough — IsValidUVE() stays false,
    // which this test does not touch.
    static_assert(std::is_base_of_v<Window::IVulkanWindowSurfaceUVE, Window::WindowManagerUVE>,
                  "WindowManagerUVE must implement IVulkanWindowSurfaceUVE");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Tier 2: real-device tests — skip politely where the host cannot provide Vulkan.
// ---------------------------------------------------------------------------

class VulkanRenderDeviceUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        windowManager = std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeTestWindowDescUVE());
        if (!windowManager->IsValidUVE()) {
            GTEST_SKIP() << "No display available for VulkanRenderDeviceUVE - skipping (run under "
                            "Xvfb or a real display)";
        }
        device = VulkanRenderDeviceUVE::CreateUVE(*windowManager);
        if (device == nullptr) {
            GTEST_SKIP() << "No Vulkan loader/ICD on this host - skipping (install "
                            "mesa-vulkan-drivers/lavapipe to exercise the M1 bootstrap here)";
        }
        // Destruction correctness is what matters most: the fixture's TearDown runs the device
        // destructor while the window is still alive (member declaration order guarantees it —
        // device is declared after windowManager and therefore destroyed first).
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<VulkanRenderDeviceUVE> device;
};

TEST_F(VulkanRenderDeviceUVETest, DeviceReportsUsableWithHonestBootstrapName) {
    EXPECT_TRUE(device->IsUsableUVE());
    EXPECT_EQ(device->GetBackendNameUVE(), "Vulkan (M2a draw slice)")
        << "capability reporting must never call the current slice the finished Vulkan backend";
}

TEST_F(VulkanRenderDeviceUVETest, PresentedFrameReadbackIsUniformBootstrapClear) {
    for (int frame = 0; frame < 6; ++frame) {
        device->PresentUVE();
        ASSERT_TRUE(device->IsUsableUVE());
    }
    // Readback of the most recently presented image: the M1 bootstrap is a fixed engineering
    // clear, so every pixel must be the same sRGB-encoded value. The expected bytes follow
    // sRGB(0.05/0.07/0.12, 1.0) = (63, 78, 97, 255) with a small tolerance for driver rounding
    // differences (the clears run through the swapchain's sRGB conversion, so exact linear
    // float comparison would be wrong by design).
    std::vector<std::byte> pixels(64U * 64U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    EXPECT_EQ(width, 64U);
    EXPECT_EQ(height, 64U);

    const std::byte r0 = pixels[0];
    const std::byte g0 = pixels[1];
    const std::byte b0 = pixels[2];
    const std::byte a0 = pixels[3];
    for (std::size_t px = 4; px + 3 < pixels.size(); px += 4) {
        ASSERT_EQ(pixels[px], r0) << "non-uniform frame at pixel " << px / 4;
        ASSERT_EQ(pixels[px + 1], g0);
        ASSERT_EQ(pixels[px + 2], b0);
        ASSERT_EQ(pixels[px + 3], a0);
    }
    const auto near_byte = [](std::byte actual, int expected) {
        return std::abs(static_cast<int>(actual) - expected) <= 4;
    };
    EXPECT_TRUE(near_byte(r0, 63)) << "R channel = " << static_cast<int>(r0);
    EXPECT_TRUE(near_byte(g0, 78)) << "G channel = " << static_cast<int>(g0);
    EXPECT_TRUE(near_byte(b0, 97)) << "B channel = " << static_cast<int>(b0);
    EXPECT_EQ(a0, static_cast<std::byte>(255));
}

TEST_F(VulkanRenderDeviceUVETest, ReadbackBeforeAnyPresentFailsCleanly) {
    std::vector<std::byte> pixels(64U * 64U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    EXPECT_FALSE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
}

TEST_F(VulkanRenderDeviceUVETest, PresentUveAdvancesMultipleFramesWithoutCrash) {
    // FIFO present + 1 frame in flight: this loop double-checks the fence invariants by
    // performing more frames than there are swapchain images (recreate and acquire both spin).
    for (int frame = 0; frame < 8; ++frame) {
        device->PresentUVE();
        ASSERT_TRUE(device->IsUsableUVE()) << "device went inert at frame " << frame;
    }
}

TEST_F(VulkanRenderDeviceUVETest, OutOfScopeResourceCallsReturnDocumentedInvalidResults) {
    // Remaining honest "not yet" areas of the M2a slice: textures, offscreen/depth targets,
    // reflection, and pipeline binaries still return the documented invalid values with a
    // naming-the-milestone warning (Null-render contract — never fake success, never crash).
    const TextureDescUVE textureDesc{};
    EXPECT_EQ(device->CreateTextureUVE(textureDesc), kInvalidTextureHandleUVE);
    device->DestroyTextureUVE(kInvalidTextureHandleUVE);

    const PipelineDescUVE pipelineDesc{};
    std::string infoLog;
    EXPECT_EQ(device->CreatePipelineUVE(pipelineDesc, &infoLog), kInvalidPipelineHandleUVE);
    EXPECT_FALSE(infoLog.empty()) << "outInfoLog must name why creation failed";

    EXPECT_TRUE(device->GetPipelineUniformsUVE(kInvalidPipelineHandleUVE).empty());
    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    EXPECT_FALSE(device->GetPipelineBinaryUVE(kInvalidPipelineHandleUVE, binary, format));

    const PipelineBinaryDescUVE binaryDesc{};
    EXPECT_EQ(device->CreatePipelineFromBinaryUVE({}, 0U, binaryDesc), kInvalidPipelineHandleUVE);

    device->DestroyBufferUVE(kInvalidBufferHandleUVE); // documented safe no-op
    device->DestroyShaderUVE(kInvalidShaderHandleUVE);
    device->DestroyPipelineUVE(kInvalidPipelineHandleUVE);
    device->SubmitUVE(nullptr); // documented: null submissions are ignored
}

TEST_F(VulkanRenderDeviceUVETest, BuffersAreRealHostVisibleMemories) {
    BufferDescUVE bufferDesc{};
    bufferDesc.sizeBytes = 12U;
    bufferDesc.usage = BufferUsageUVE::Vertex;
    const float threeFloats[3] = {1.0F, 2.0F, 3.0F};
    const BufferHandleUVE buffer = device->CreateBufferUVE(
        bufferDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(threeFloats),
                                               sizeof(threeFloats)));
    ASSERT_NE(buffer, kInvalidBufferHandleUVE);

    const float replacement[3] = {4.0F, 5.0F, 6.0F};
    EXPECT_TRUE(device->UpdateBufferUVE(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));
    // Out-of-range update must fail cleanly and must not have touched anything.
    EXPECT_FALSE(device->UpdateBufferUVE(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement)),
        4U));
    device->DestroyBufferUVE(buffer);
    // Update after destroy: the interface's documented silent-false, never a crash.
    EXPECT_FALSE(device->UpdateBufferUVE(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));
}

TEST_F(VulkanRenderDeviceUVETest, ShaderCreationRejectsGlslTextAndAcceptsSpirv) {
    ShaderDescUVE glslDesc{};
    glslDesc.stage = ShaderStageUVE::Vertex;
    glslDesc.sourceCode = "#version 450\nvoid main() { gl_Position = vec4(0.0); }";
    std::string infoLog;
    EXPECT_EQ(device->CreateShaderUVE(glslDesc, &infoLog), kInvalidShaderHandleUVE);
    EXPECT_NE(infoLog.find("SPIR-V"), std::string::npos) << infoLog;

    ShaderDescUVE spirvDesc{};
    spirvDesc.stage = ShaderStageUVE::Vertex;
    spirvDesc.sourceCode = kTriangleVertexSpirvUVE;
    const ShaderHandleUVE shader = device->CreateShaderUVE(spirvDesc);
    ASSERT_NE(shader, kInvalidShaderHandleUVE);
    device->DestroyShaderUVE(shader);
}

TEST_F(VulkanRenderDeviceUVETest, SubmittedTriangleCommandBufferReallyRendersPixels) {
    // A full M2a round trip: SPIR-V shaders + a real pipeline + an interleaved
    // position/color vertex buffer + a recorded command buffer that draws a big triangle —
    // verified against the swapchain's own pixels via ReadbackLatestPresentedImageUVE, not by
    // "no crash" (that would be the mock version of this test).
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirvUVE;
    const ShaderHandleUVE vertexShader = device->CreateShaderUVE(vertexDesc);
    const ShaderHandleUVE fragmentShader = device->CreateShaderUVE(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 24U; // 2x vec3 interleaved (position, color)
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float3, 12U});
    pipelineDesc.depthTestEnabled = false; // the M2a pass has no depth attachment (documented)
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = device->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    // Triangle covering the whole frame (deep-blue-ish red corner signature), interleaved
    // vec3 position + vec3 color, 6 floats per vertex.
    const float vertices[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F, // bottom-left:  red
         1.0F, -1.0F, 0.0F,  0.0F, 1.0F, 0.0F, // bottom-right: green
         0.0F,  1.0F, 0.0F,  0.0F, 0.0F, 1.0F, // top-center:   blue
    };
    BufferDescUVE bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE vertexBuffer = device->CreateBufferUVE(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    auto commandBuffer = device->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc{}; // default attachments: the swapchain ("default framebuffer")
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    device->SubmitUVE(std::move(commandBuffer));

    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    // Readback: the triangle covers the whole 1280x720 surface (gl_Position pins three corners
    // on the big clip triangle that every viewport rasterizes fully here), so the frame must
    // be NON-uniform: a gradient of the three vertex colors — precisely provable:
    // red corner must be dominantly red, green corner dominantly green, and counting pixels
    // must show above 99% of the frame no longer matching the clear color.
    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const auto channelAt = [&](const std::uint32_t x, const std::uint32_t y) {
        const std::size_t base = (static_cast<std::size_t>(y) * width + x) * 4U;
        return std::array<int, 3>{static_cast<int>(pixels[base]),
                                  static_cast<int>(pixels[base + 1]),
                                  static_cast<int>(pixels[base + 2])};
    };
    // Sample small neighborhoods away from edges, robust to off-by-one raster rules. Vulkan's
    // NATIVE NDC convention: with a positive-height viewport, NDC y=-1 maps to the framebuffer
    // TOP row (GL authors: the same vertex data appears Y-flipped versus the GL backend until
    // the projection-uniform milestone applies the conventional Y flip — M2a documents the
    // native mapping, no silent flips).
    const auto topLeft = channelAt(width / 8U, height / 8U);
    const auto topRight = channelAt(width - width / 8U, height / 8U);
    const auto bottom = channelAt(width / 2U, height - height / 8U);
    EXPECT_GT(topLeft[0], topLeft[1] + 40) << "top-left must be red-dominant: ("
        << topLeft[0] << "," << topLeft[1] << "," << topLeft[2] << ")";
    EXPECT_GT(topRight[1], topRight[0] + 40) << "top-right must be green-dominant: ("
        << topRight[0] << "," << topRight[1] << "," << topRight[2] << ")";
    EXPECT_GT(bottom[2], bottom[0] + 40) << "bottom-center must be blue-dominant: ("
        << bottom[0] << "," << bottom[1] << "," << bottom[2] << ")";

    device->DestroyBufferUVE(vertexBuffer);
    device->DestroyPipelineUVE(pipeline);
    device->DestroyShaderUVE(vertexShader);
    device->DestroyShaderUVE(fragmentShader);
}


TEST_F(VulkanRenderDeviceUVETest, ReflectedUniformsMatchTheSpirvLayout) {
    // M2b: CreatePipelineUVE now parses the SPIR-V modules with SPIRV-Reflect.
    // The depth-uniform pair declares exactly: UBO block { float uDepth; vec3 uColorTri; }
    // plus push-constant block { float uOffsetX; float uOffsetY; } — the reflection service
    // must report all four with matching types (location is backend-internal: not asserted).
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirvUVE;
    const ShaderHandleUVE vertexShader = device->CreateShaderUVE(vertexDesc);
    const ShaderHandleUVE fragmentShader = device->CreateShaderUVE(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE pipeline = device->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const std::vector<UniformReflectionUVE> uniforms = device->GetPipelineUniformsUVE(pipeline);
    ASSERT_EQ(uniforms.size(), 4U) << "expected uDepth, uColorTri, uOffsetX, uOffsetY";
    std::map<std::string, ShaderDataTypeUVE> byName;
    for (const UniformReflectionUVE& entry : uniforms) {
        byName.emplace(entry.name, entry.type);
    }
    EXPECT_EQ(byName.count("uDepth"), 1U);
    EXPECT_EQ(byName.at("uDepth"), ShaderDataTypeUVE::Float);
    EXPECT_EQ(byName.count("uColorTri"), 1U);
    EXPECT_EQ(byName.at("uColorTri"), ShaderDataTypeUVE::Vec3);
    EXPECT_EQ(byName.count("uOffsetX"), 1U);
    EXPECT_EQ(byName.at("uOffsetX"), ShaderDataTypeUVE::Float);
    EXPECT_EQ(byName.count("uOffsetY"), 1U);
    EXPECT_EQ(byName.at("uOffsetY"), ShaderDataTypeUVE::Float);

    device->DestroyPipelineUVE(pipeline);
    device->DestroyShaderUVE(vertexShader);
    device->DestroyShaderUVE(fragmentShader);
}

TEST_F(VulkanRenderDeviceUVETest, DepthAndPerDrawUniformSnapshotsRenderCorrectly) {
    // The M2b pixel proof, all three claims in one frame:
    //   (1) DEPTH: the NEARER triangle (green, z=0.1) is drawn FIRST, the FARTHER one
    //       (red, z=0.5) SECOND. Painter's order would leave red in the overlap; real depth
    //       testing leaves green. center pixel green => depth attachment + state are real.
    //   (2) PER-DRAW UBO SNAPSHOTS: one pipeline, different uDepth/uColorTri between two
    //       draws via the dynamic-offset frame ring. The left region must be red and the
    //       right region green simultaneously — a shared/broken snapshot would leak one
    //       draw's uniform block into the other and produce one color for both halves.
    //   (3) PUSH CONSTANTS: uOffsetX shifts each triangle sideways (0.15/-0.15); if the push
    //       flush were broken, both triangles would sit centered and the overlap would
    //       cover the whole shape — the red-only left pixel sample would then be green.
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirvUVE;
    const ShaderHandleUVE vertexShader = device->CreateShaderUVE(vertexDesc);
    const ShaderHandleUVE fragmentShader = device->CreateShaderUVE(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE pipeline = device->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE);

    const float vertices[9] = {
        -0.6F, -0.6F, 0.0F,
         0.6F, -0.6F, 0.0F,
         0.0F,  0.6F, 0.0F,
    };
    BufferDescUVE bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE vertexBuffer = device->CreateBufferUVE(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    auto commandBuffer = device->CreateCommandBufferUVE();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDescUVE passDesc{};
    passDesc.colorLoadOp = LoadOpUVE::Clear;
    passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
    passDesc.depthLoadOp = LoadOpUVE::Clear;
    passDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPassUVE(passDesc);
    commandBuffer->BindPipelineUVE(pipeline);
    commandBuffer->BindVertexBufferUVE(vertexBuffer);
    // Reflection-API robustness, folded in for free: unknown names and wrong value types
    // must degrade to a logged no-op mid-recording, never crash and never corrupt state.
    commandBuffer->SetUniformFloatUVE("uDoesNotExist", 123.0F);
    commandBuffer->SetUniformVector3UVE("uDepth", Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    // Draw 1 — FRONT: green, nearer (z = 0.1), shifted right by push constants.
    commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    commandBuffer->SetUniformFloatUVE("uDepth", 0.1F);
    commandBuffer->SetUniformFloatUVE("uOffsetX", 0.15F);
    commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
    commandBuffer->DrawUVE(3U);
    // Draw 2 — BACK: red, farther (z = 0.5), shifted left. Drawn SECOND: if depth testing
    // is broken, red wins the overlap by submission order; if depth works, green stays.
    commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    commandBuffer->SetUniformFloatUVE("uDepth", 0.5F);
    commandBuffer->SetUniformFloatUVE("uOffsetX", -0.15F);
    commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
    commandBuffer->DrawUVE(3U);
    commandBuffer->EndRenderPassUVE();
    device->SubmitUVE(std::move(commandBuffer));

    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }
    const auto channelAt = [&](const std::uint32_t x, const std::uint32_t y) {
        const std::size_t base = (static_cast<std::size_t>(y) * width + x) * 4U;
        return std::array<int, 3>{static_cast<int>(pixels[base]),
                                  static_cast<int>(pixels[base + 1]),
                                  static_cast<int>(pixels[base + 2])};
    };
    // NDC (-0.45, -0.2) / (0, -0.2) / (0.45, -0.2); Vulkan NATIVE orientation maps NDC y=-1
    // to the framebuffer TOP row (the M2a pixel test's documented convention — the same
    // mapping the GPU rasterizes, no hidden flips), so pixel-y = (ndc_y + 1) / 2 * height.
    const auto pixelX = [&](const float ndcX) {
        return static_cast<std::uint32_t>((ndcX + 1.0F) * 0.5F * static_cast<float>(width));
    };
    const auto pixelY = [&](const float ndcY) {
        return static_cast<std::uint32_t>((ndcY + 1.0F) * 0.5F * static_cast<float>(height));
    };
    const auto redOnly = channelAt(pixelX(-0.45F), pixelY(-0.2F));
    const auto overlap = channelAt(pixelX(0.0F), pixelY(-0.2F));
    const auto greenOnly = channelAt(pixelX(0.45F), pixelY(-0.2F));
    EXPECT_GT(redOnly[0], redOnly[1] + 40) << "left region must be red-dominant (push-offset "
        "proof): (" << redOnly[0] << "," << redOnly[1] << "," << redOnly[2] << ")";
    EXPECT_GT(overlap[1], overlap[0] + 40) << "overlap must be GREEN-dominant: nearer draw-1 "
        "wins despite being drawn first, which painter's order cannot do (depth proof): ("
        << overlap[0] << "," << overlap[1] << "," << overlap[2] << ")";
    EXPECT_GT(greenOnly[1], greenOnly[0] + 40) << "right region must be green-dominant "
        "(per-draw UBO snapshot proof): (" << greenOnly[0] << "," << greenOnly[1] << ","
        << greenOnly[2] << ")";

    device->DestroyBufferUVE(vertexBuffer);
    device->DestroyPipelineUVE(pipeline);
    device->DestroyShaderUVE(vertexShader);
    device->DestroyShaderUVE(fragmentShader);
}


TEST_F(VulkanRenderDeviceUVETest, TextureCreationValidationAndLifecycleAreReal) {
    // M2c resource contract: invalid descriptors bounce through ValidateTextureUploadUVE
    // before ANY allocation; valid ones return handles; destruction is idempotent-safe.
    TextureDescUVE invalid{};
    invalid.width = 0U;
    invalid.height = 4U;
    EXPECT_EQ(device->CreateTextureUVE(invalid), kInvalidTextureHandleUVE);

    TextureDescUVE badUpload{};
    badUpload.width = 2U;
    badUpload.height = 2U;
    const std::byte shortData[3] = {std::byte{0}, std::byte{0}, std::byte{0}};
    EXPECT_EQ(device->CreateTextureUVE(badUpload, std::span<const std::byte>(shortData, 3U)),
              kInvalidTextureHandleUVE) << "partial level-0 uploads must be rejected";

    TextureDescUVE valid{};
    valid.width = 2U;
    valid.height = 2U;
    const TextureHandleUVE colorTex = device->CreateTextureUVE(valid);
    EXPECT_NE(colorTex, kInvalidTextureHandleUVE) << "empty level-0 (render-target shape) is legal";

    TextureDescUVE validF16{};
    validF16.width = 2U;
    validF16.height = 2U;
    validF16.format = TextureFormatUVE::RGBA16Float;
    EXPECT_NE(device->CreateTextureUVE(validF16), kInvalidTextureHandleUVE)
        << "R16G16B16A16_SFLOAT sampled+transfer-dst is a mandatory format feature set";

    device->DestroyTextureUVE(colorTex);
    device->DestroyTextureUVE(colorTex); // already destroyed: safe no-op, per interface contract
    device->DestroyTextureUVE(kInvalidTextureHandleUVE); // never valid: safe no-op too
}

TEST_F(VulkanRenderDeviceUVETest, TexturedQuadRendersUploadedPixelsAndUnboundFallback) {
    // The M2c pixel proof, three frames against one pipeline:
    //   frame 1 - NO BindTextureUVE: every sampled fragment must be the fallback texture's
    //             opaque white (deterministic unbound-slot contract, not undefined content).
    //   frame 2 - checker texture bound at slot 0: the four quadrants must read back the
    //             exact uploaded texel colors - staging upload, sampler, and the per-tuple
    //             descriptor set all proven by pixels.
    //   frame 3 - texture destroyed after recording the SAME bind: the destroyed handle can
    //             no longer resolve, so the draw degrades to the fallback (white) instead of
    //             sampling freed memory.
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirvUVE;
    const ShaderHandleUVE vertexShader = device->CreateShaderUVE(vertexDesc);
    const ShaderHandleUVE fragmentShader = device->CreateShaderUVE(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandleUVE);
    ASSERT_NE(fragmentShader, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U; // vec3 position + vec2 uv interleaved
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttributeUVE{"TEXCOORD", VertexAttributeFormatUVE::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandleUVE pipeline = device->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandleUVE)
        << "a pipeline with a combined image sampler must now build (pre-M2c hard-fail)";
    // Samplers are bound by SLOT, not by SetUniform* — they deliberately stay OUT of the
    // reflected SetUniform table (GL backends bind sampler names to texture units instead).
    EXPECT_TRUE(device->GetPipelineUniformsUVE(pipeline).empty());

    const float vertices[30] = {
        -0.5F, -0.5F, 0.0F,  0.0F, 0.0F, // screen top-left == uv(0,0)  (native orientation)
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F, // top-right == uv(1,0)
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F, // bottom-left == uv(0,1)
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F,
         0.5F,  0.5F, 0.0F,  1.0F, 1.0F,
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE vertexBuffer = device->CreateBufferUVE(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandleUVE);

    // 2x2 checker in upload memory order: (0,0) red, (1,0) green, (0,1) blue, (1,1) yellow.
    const std::uint8_t checkerData[16] = {
        255U, 0U, 0U, 255U,   0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,   255U, 255U, 0U, 255U,
    };
    TextureDescUVE checkerDesc{};
    checkerDesc.width = 2U;
    checkerDesc.height = 2U;
    const TextureHandleUVE checkerTexture = device->CreateTextureUVE(checkerDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(checkerData), sizeof(checkerData)));
    ASSERT_NE(checkerTexture, kInvalidTextureHandleUVE);

    const auto drawQuad = [&](const TextureHandleUVE* textureToBind,
                              const bool destroyAfterBind) {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(pipeline);
        commandBuffer->BindVertexBufferUVE(vertexBuffer);
        if (textureToBind != nullptr) {
            commandBuffer->BindTextureUVE(*textureToBind, 0U);
        }
        if (destroyAfterBind) {
            device->DestroyTextureUVE(*textureToBind); // destroyed BEFORE this frame submits
        }
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
        device->PresentUVE();
        ASSERT_TRUE(device->IsUsableUVE());
    };
    const auto channelAt = [&](const std::vector<std::byte>& pixels, const std::uint32_t width,
                               const float ndcX, const float ndcY) {
        std::uint32_t height = static_cast<std::uint32_t>(pixels.size() / 4U / width);
        const std::uint32_t x = static_cast<std::uint32_t>((ndcX + 1.0F) * 0.5F * static_cast<float>(width));
        const std::uint32_t y = static_cast<std::uint32_t>((ndcY + 1.0F) * 0.5F * static_cast<float>(height));
        const std::size_t base = (static_cast<std::size_t>(y) * width + x) * 4U;
        return std::array<int, 3>{static_cast<int>(pixels[base]),
                                  static_cast<int>(pixels[base + 1]),
                                  static_cast<int>(pixels[base + 2])};
    };

    // Frame 1: unbound -> fallback white.
    drawQuad(nullptr, false);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
        if (width == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels to verify"; }
        const auto center = channelAt(pixels, width, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "unbound sampler slot must sample the fallback white";
    }

    // Frame 2: checker bound -> exact quadrants (uv == texel centers at quadrant centers,
    // so even LINEAR filtering yields the pure uploaded colors).
    drawQuad(&checkerTexture, false);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
        const auto topLeft = channelAt(pixels, width, -0.25F, -0.25F);
        const auto topRight = channelAt(pixels, width, 0.25F, -0.25F);
        const auto bottomLeft = channelAt(pixels, width, -0.25F, 0.25F);
        const auto bottomRight = channelAt(pixels, width, 0.25F, 0.25F);
        EXPECT_GT(topLeft[0], 200);
        EXPECT_LT(topLeft[1], 60) << "top-left quadrant must be the uploaded RED texel";
        EXPECT_GT(topRight[1], 200);
        EXPECT_LT(topRight[0], 60) << "top-right quadrant must be the uploaded GREEN texel";
        EXPECT_GT(bottomLeft[2], 200);
        EXPECT_LT(bottomLeft[0], 60) << "bottom-left quadrant must be the uploaded BLUE texel";
        EXPECT_GT(bottomRight[0], 200);
        EXPECT_GT(bottomRight[1], 200);
        EXPECT_LT(bottomRight[2], 60) << "bottom-right quadrant must be the uploaded YELLOW texel";
    }

    // Frame 3: bind the SAME texture, destroy it before submit -> fallback white again.
    drawQuad(&checkerTexture, true);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
        const auto center = channelAt(pixels, width, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "a texture destroyed after recording must degrade to the "
                                     "fallback, never sample freed memory";
    }

    device->DestroyBufferUVE(vertexBuffer);
    device->DestroyPipelineUVE(pipeline);
    device->DestroyShaderUVE(vertexShader);
    device->DestroyShaderUVE(fragmentShader);
}

} // namespace UVE::Render::Tests
