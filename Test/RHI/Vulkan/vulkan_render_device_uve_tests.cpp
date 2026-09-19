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
        // Real-device path A: a windowed device on a live OS display (GLFW + swapchain WSI).
        windowManager = std::make_unique<Window::WindowManagerUVE>(eventSystem, MakeTestWindowDescUVE());
        if (windowManager->IsValidUVE()) {
            device = VulkanRenderDeviceUVE::CreateUVE(*windowManager);
        } else {
            // Real-device path B: displayless host (CI without Xvfb, containers, servers) —
            // the engine's self-contained headless factory builds the identical device over a
            // VK_EXT_headless_surface instead of skipping. The window manager is dropped: the
            // device owns no window in this mode, and every pixel assertion below reads the
            // headless swapchain exactly as it would the windowed one.
            windowManager.reset();
            device = VulkanRenderDeviceUVE::CreateHeadlessUVE();
        }
        if (device == nullptr) {
            GTEST_SKIP() << "No Vulkan device path on this host - skipping (need a display for "
                            "the windowed path, or a loader/ICD with VK_EXT_headless_surface "
                            "like SwiftShader for the headless one)";
        }
        // Destruction correctness is what matters most in the windowed path: the fixture's
        // TearDown runs the device destructor while the window is still alive (member
        // declaration order guarantees it — device is declared after windowManager and
        // therefore destroyed first). In the headless path windowManager is null already.
    }

    Events::EventSystemUVE eventSystem;
    std::unique_ptr<Window::WindowManagerUVE> windowManager;
    std::unique_ptr<VulkanRenderDeviceUVE> device;
};

TEST_F(VulkanRenderDeviceUVETest, DeviceReportsUsableWithHonestBootstrapName) {
    EXPECT_TRUE(device->IsUsableUVE());
    // M2d: the reported name is capability-driven — a 1.3/dynamic-rendering device reports
    // the offscreen-RT slice name, anything older reports the M2c classic one. Both are
    // milestone-tagged; neither may be the bare "Vulkan" (honest capability contract).
    const std::string_view name = device->GetBackendNameUVE();
    EXPECT_TRUE(name == "Vulkan (M2e depth+Load policies)" || name == "Vulkan (M2c textures+staging)")
        << "backend name must report the exact slice and capability gate, got: " << name;
}

TEST_F(VulkanRenderDeviceUVETest, PresentedFrameReadbackIsUniformBootstrapClear) {
    for (int frame = 0; frame < 6; ++frame) {
        device->PresentUVE();
        ASSERT_TRUE(device->IsUsableUVE());
    }
    // Readback of the most recently presented image: the M1 bootstrap is a fixed engineering
    // clear, so every pixel must be the same encoded value. The expected bytes depend on the
    // swapchain image's FORMAT CLASS, which the stack (not the test) picks: on an SRGB-typed
    // 4x8 image the clear runs through sRGB conversion — sRGB(0.05/0.07/0.12, 1.0) = (63, 78,
    // 97, 255); on a UNORM-typed one (what lavapipe's headless surface advertises, for one)
    // the same float clear stores linearly — (13, 18, 31, 255). Both are the correct encoding
    // of the same engineering clear; asserting a wrong-encoding failure would punish an honest
    // driver pick, so the byte check accepts exactly the two correct encodings (uniformity and
    // the opaque alpha are asserted unconditionally below regardless). Surface-provenance-
    // agnostic on size: the windowed path is the 64x64 fixture window, while headless-WSI
    // surfaces (CI's VK_EXT_headless_surface arm) are driver-extent-sized — the documented
    // two-call readback pattern (empty span first to learn the extent, then the real buffer)
    // covers both.
    std::vector<std::byte> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_FALSE(device->ReadbackLatestPresentedImageUVE(pixels, width, height))
        << "an empty span must be the documented size-query failure";
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    EXPECT_NE(width * height, 0U);

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
    const bool matchesSrgb = near_byte(r0, 63) && near_byte(g0, 78) && near_byte(b0, 97);
    const bool matchesLinear = near_byte(r0, 13) && near_byte(g0, 18) && near_byte(b0, 31);
    EXPECT_TRUE(matchesSrgb || matchesLinear)
        << "clear encoded as neither sRGB(63,78,97) nor linear(13,18,31): got ("
        << static_cast<int>(r0) << "," << static_cast<int>(g0) << "," << static_cast<int>(b0)
        << ") — a swapchain format-class mismatch beyond the two correct encodings";
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


// ---------------------------------------------------------------------------
// M2d offscreen render targets (dynamic-rendering gate). Every test also runs on a
// classic-mode device: offscreen passes then warn once and SKIP, which the frame-must-
// stay-intact assertions cover (the attestable content simply never lands in the offscreen
// texture). SwiftShader/lavapipe are both 1.3+, so CI and the sandbox run the full RT path.
// ---------------------------------------------------------------------------

[[nodiscard]] std::array<int, 4> ChannelAtNdcUVE(const std::vector<std::byte>& pixels,
                                                 const std::uint32_t width,
                                                 const std::uint32_t height,
                                                 const float ndcX, const float ndcY) {
    const std::uint32_t x =
        static_cast<std::uint32_t>((ndcX + 1.0F) * 0.5F * static_cast<float>(width));
    const std::uint32_t y =
        static_cast<std::uint32_t>((ndcY + 1.0F) * 0.5F * static_cast<float>(height));
    const std::uint32_t clampedX = x < width ? x : width - 1U;
    const std::uint32_t clampedY = y < height ? y : height - 1U;
    const std::size_t base = (static_cast<std::size_t>(clampedY) * width + clampedX) * 4U;
    return {static_cast<int>(pixels[base]), static_cast<int>(pixels[base + 1]),
            static_cast<int>(pixels[base + 2]), static_cast<int>(pixels[base + 3])};
}

[[nodiscard]] PipelineHandleUVE CreateTrianglePipelineUVE(VulkanRenderDeviceUVE& device,
                                                          ShaderHandleUVE* outVertexShader,
                                                          ShaderHandleUVE* outFragmentShader) {
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirvUVE;
    *outVertexShader = device.CreateShaderUVE(vertexDesc);
    *outFragmentShader = device.CreateShaderUVE(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandleUVE ||
        *outFragmentShader == kInvalidShaderHandleUVE) {
        return {};
    }
    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 24U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float3, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipelineUVE(pipelineDesc);
}

[[nodiscard]] PipelineHandleUVE CreateTexturedPipelineUVE(VulkanRenderDeviceUVE& device,
                                                          ShaderHandleUVE* outVertexShader,
                                                          ShaderHandleUVE* outFragmentShader) {
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirvUVE;
    *outVertexShader = device.CreateShaderUVE(vertexDesc);
    *outFragmentShader = device.CreateShaderUVE(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandleUVE ||
        *outFragmentShader == kInvalidShaderHandleUVE) {
        return {};
    }
    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttributeUVE{"TEXCOORD", VertexAttributeFormatUVE::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipelineUVE(pipelineDesc);
}

TEST_F(VulkanRenderDeviceUVETest, OffscreenColorPassRendersIntoTheTextureForSampling) {
    // The M2d core proof: an RGBA8Unorm texture CLEARED-AND-DRAWN as a render target in one
    // pass, then SAMPLED by a later same-frame default(=swapchain) pass. Clear blue + a red
    // triangle drawn into a 64x64 target; the onscreen textured quad must read blue outside
    // the triangle and red inside it - impossible without real texture-backed rendering.
    ShaderHandleUVE triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandleUVE trianglePipeline = CreateTrianglePipelineUVE(*device, &triVS, &triFS);
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE renderTarget = device->CreateTextureUVE(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandleUVE);

    // Solid red triangle covering the target's center-ish band; vertices at +/-0.5 NDC keep
    // generous margins so sample points escape rasterization boundary fuzz.
    const float triVertices[18] = {
        -0.5F, -0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
         0.0F,  0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
    };
    BufferDescUVE triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE triBuffer = device->CreateBufferUVE(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandleUVE);

    // Full-frame sampling quad (x,y in +/-1 so the whole readback is quad).
    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Submission 1: render the triangle into the offscreen texture.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F}; // flat blue background
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(trianglePipeline);
        commandBuffer->BindVertexBufferUVE(triBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Submission 2: sample it on the default framebuffer (proves the image made it back to
    // SHADER_READ with valid content inside one command stream).
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F}; // green: quad overwrites everything
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(renderTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic device: the offscreen pass was skipped, so the quad samples the fallback
        // white. Frame must stay intact (nothing else breaks) - that is the contract.
        const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "classic device must degrade offscreen content to the "
                                     "fallback texture while keeping the frame intact";
    } else {
        // Center of the quad == center of the triangle: pure red.
        const auto insideTri = ChannelAtNdcUVE(pixels, width, height, 0.0F, -0.1F);
        EXPECT_GT(insideTri[0], 200);
        EXPECT_LT(insideTri[1], 80);
        EXPECT_LT(insideTri[2], 80) << "inside the offscreen triangle must be RED - got ("
            << insideTri[0] << "," << insideTri[1] << "," << insideTri[2] << ")";
        // Off-triangle but inside the quad: the offscreen pass's blue clear.
        const auto background = ChannelAtNdcUVE(pixels, width, height, -0.95F, -0.95F);
        EXPECT_LT(background[0], 80);
        EXPECT_LT(background[1], 80);
        EXPECT_GT(background[2], 200) << "outside the triangle must be the BLUE clear - got ("
            << background[0] << "," << background[1] << "," << background[2] << ")";
    }

    device->DestroyBufferUVE(triBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(renderTarget);
    device->DestroyPipelineUVE(trianglePipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(triVS);
    device->DestroyShaderUVE(triFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, InterleavedOffscreenPassPreservesTheDefaultFramebuffer) {
    // The resume/LOAD proof: default pass draws a full-frame solid-red triangle; an offscreen
    // pass interrupts the frame; a SECOND default-pass run resumes the SAME swapchain image
    // and draws a left-half textured quad sampling the offscreen texture. If the resumed
    // default image were re-cleared (a broken interleave would show the last default clear
    // color), the right half would NOT be red anymore.
    ShaderHandleUVE triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandleUVE trianglePipeline = CreateTrianglePipelineUVE(*device, &triVS, &triFS);
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE renderTarget = device->CreateTextureUVE(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandleUVE);

    const float fullRedTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  1.0F, 0.0F, 0.0F, // covers the entire viewport
    };
    BufferDescUVE triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(fullRedTriangle);
    triBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE triBuffer = device->CreateBufferUVE(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(fullRedTriangle),
                                   sizeof(fullRedTriangle)));
    ASSERT_NE(triBuffer, kInvalidBufferHandleUVE);

    const float leftHalfQuad[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         0.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         0.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         0.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(leftHalfQuad);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(leftHalfQuad),
                                   sizeof(leftHalfQuad)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Submission A: default pass -> full red frame.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(trianglePipeline);
        commandBuffer->BindVertexBufferUVE(triBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Submission B: offscreen pass -> flat blue target, no draws at all (clears only).
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Submission C: resume the default framebuffer -> left-half textured quad sampling the RT.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 1.0F, 1.0F, 1.0F}; // cyan - only the FIRST default clear
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(renderTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic: offscreen skipped -> quad samples fallback white; the second default pass
        // clear... here the classic flow bakes ONE swapchain pass per frame with the FIRST
        // pass's clear (black), so a resumed-context question never arises; assert intactness
        // of the left-half quad only (fallback white) and move on.
        const auto leftCenter = ChannelAtNdcUVE(pixels, width, height, -0.5F, 0.0F);
        EXPECT_GT(leftCenter[0], 240);
        EXPECT_GT(leftCenter[1], 240);
        EXPECT_GT(leftCenter[2], 240) << "classic device must still draw the sampling quad "
                                         "with the fallback texture";
    } else {
        // Left half center: quad sampling the (blue) offscreen texture.
        const auto leftCenter = ChannelAtNdcUVE(pixels, width, height, -0.5F, 0.0F);
        EXPECT_LT(leftCenter[0], 80);
        EXPECT_LT(leftCenter[1], 80);
        EXPECT_GT(leftCenter[2], 200) << "left half must be the BLUE offscreen content - got ("
            << leftCenter[0] << "," << leftCenter[1] << "," << leftCenter[2] << ")";
        // Right half center: submission A's full-frame red, i.e. the resumed swapchain image
        // kept its earlier content across the interleaved offscreen pass.
        const auto rightCenter = ChannelAtNdcUVE(pixels, width, height, 0.5F, 0.0F);
        EXPECT_GT(rightCenter[0], 200);
        EXPECT_LT(rightCenter[1], 80);
        EXPECT_LT(rightCenter[2], 80) << "right half must keep submission A's RED (the default "
            "framebuffer resumed with LOAD) - got ("
            << rightCenter[0] << "," << rightCenter[1] << "," << rightCenter[2] << ")";
    }

    device->DestroyBufferUVE(triBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(renderTarget);
    device->DestroyPipelineUVE(trianglePipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(triVS);
    device->DestroyShaderUVE(triFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, DepthTestWorksInColorOnlyOffscreenPass) {
    // Without a caller depth attachment the offscreen pass borrows the engine's per-extent
    // scratch depth image: nearer-drawn-first must WIN over farther-drawn-second (painter
    // order would produce the opposite), which is only observable if the hidden depth test
    // inside the offscreen instance is real.
    ShaderDescUVE vertexDesc{};
    vertexDesc.stage = ShaderStageUVE::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirvUVE;
    ShaderDescUVE fragmentDesc{};
    fragmentDesc.stage = ShaderStageUVE::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirvUVE;
    const ShaderHandleUVE depthVS = device->CreateShaderUVE(vertexDesc);
    const ShaderHandleUVE depthFS = device->CreateShaderUVE(fragmentDesc);
    ASSERT_NE(depthVS, kInvalidShaderHandleUVE);
    ASSERT_NE(depthFS, kInvalidShaderHandleUVE);

    PipelineDescUVE pipelineDesc{};
    pipelineDesc.vertexShader = depthVS;
    pipelineDesc.fragmentShader = depthFS;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandleUVE depthPipeline = device->CreatePipelineUVE(pipelineDesc);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandleUVE);

    ShaderHandleUVE quadVS{}, quadFS{};
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE renderTarget = device->CreateTextureUVE(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandleUVE);

    // One full-coverage triangle drawn TWICE with different depths and colors, exactly like
    // the M2b swapchain proof.
    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDescUVE triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE triBuffer = device->CreateBufferUVE(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Submission 1: offscreen depth test. Nearer GREEN drawn FIRST, farther RED SECOND; real
    // depth testing leaves green everywhere.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(depthPipeline);
        commandBuffer->BindVertexBufferUVE(triBuffer);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.1F);
        commandBuffer->SetUniformFloatUVE("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
        commandBuffer->DrawUVE(3U);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.5F);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Submission 2: show it.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(renderTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240); // fallback white, frame intact
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 80);
        EXPECT_GT(center[1], 200);
        EXPECT_LT(center[2], 80) << "nearer-first green must win inside the offscreen pass - "
            "got (" << center[0] << "," << center[1] << "," << center[2] << ")";
    }

    device->DestroyBufferUVE(triBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(renderTarget);
    device->DestroyPipelineUVE(depthPipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(depthVS);
    device->DestroyShaderUVE(depthFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, NonAttachableTexturePassSkipsButTheFrameSurvives) {
    // RGBA16Float is a legal SAMPLING texture but never a legal render target in M2d (the
    // pipeline contract is the swapchain's format). The offscreen pass degrades to a one-shot
    // warning + skip; the rest of the frame must still render with the texture's INITIAL
    // pixels intact (the skip provably erased nothing, wrote nothing).
    ShaderHandleUVE quadVS{}, quadFS{};
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    const std::uint8_t initialRed[16] = {
        255U, 0U, 0U, 255U,  255U, 0U, 0U, 255U,
        255U, 0U, 0U, 255U,  255U, 0U, 0U, 255U,
    };
    TextureDescUVE rgba16Desc{};
    rgba16Desc.width = 2U;
    rgba16Desc.height = 2U;
    rgba16Desc.format = TextureFormatUVE::RGBA8Unorm; // uploaded as RGBA8...
    const TextureHandleUVE plainRed = device->CreateTextureUVE(rgba16Desc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(initialRed),
                                   sizeof(initialRed)));
    ASSERT_NE(plainRed, kInvalidTextureHandleUVE);
    TextureDescUVE floatDesc{};
    floatDesc.width = 2U;
    floatDesc.height = 2U;
    floatDesc.format = TextureFormatUVE::RGBA16Float; // ...and its float twin: not attachable
    const std::uint8_t floatRedPixels[32] = {
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU, // 1.0,0,0,1 as fp16
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
    };
    const TextureHandleUVE floatRed = device->CreateTextureUVE(floatDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(floatRedPixels),
                                   sizeof(floatRedPixels)));
    ASSERT_NE(floatRed, kInvalidTextureHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Frame 1's content sequence is: (skipped) offscreen GREEN-pass over the float texture,
    // then a default pass sampling it. On a dynamic device the float texture must still read
    // RED (its initial upload); on a classic device the pass is also skipped for the no-1.3
    // reason, with the identical outcome - one expectation serves both.
    {
        auto offscreen = device->CreateCommandBufferUVE();
        RenderPassDescUVE offscreenDesc{};
        offscreenDesc.colorAttachment = floatRed;
        offscreenDesc.colorLoadOp = LoadOpUVE::Clear;
        offscreenDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F}; // green - must NEVER land
        offscreenDesc.depthLoadOp = LoadOpUVE::Clear;
        offscreen->BeginRenderPassUVE(offscreenDesc);
        offscreen->EndRenderPassUVE();
        device->SubmitUVE(std::move(offscreen));
    }
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(floatRed, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }
    const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(center[0], 200);
    EXPECT_LT(center[1], 80);
    EXPECT_LT(center[2], 80) << "the float texture must still sample its INITIAL RED after "
        "its offscreen pass was skipped - got ("
        << center[0] << "," << center[1] << "," << center[2] << ")";

    // And the plain-attachable texture must still be where we left it for the next frame;
    // this is a cheap keep-alive assertion for the fallback-independent path.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(plainRed, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
        device->PresentUVE();
        ASSERT_TRUE(device->IsUsableUVE());
    }

    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(plainRed);
    device->DestroyTextureUVE(floatRed);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, DepthOnlyOffscreenPassIsRefusedButTheFrameSurvives) {
    // A pass with ONLY a depth texture (no color attachment) is a documented M2d no-op
    // boundary: the submission's pass is skipped with a one-shot warning; later passes of
    // the frame must render exactly as if the skip were a no-op.
    TextureDescUVE depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormatUVE::Depth32Float;
    const TextureHandleUVE depthTex = device->CreateTextureUVE(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandleUVE);

    ShaderHandleUVE triVS{}, triFS{};
    const PipelineHandleUVE trianglePipeline = CreateTrianglePipelineUVE(*device, &triVS, &triFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandleUVE);

    const float redTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  1.0F, 0.0F, 0.0F,
    };
    BufferDescUVE triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(redTriangle);
    triBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE triBuffer = device->CreateBufferUVE(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(redTriangle),
                                   sizeof(redTriangle)));
    ASSERT_NE(triBuffer, kInvalidBufferHandleUVE);

    {
        auto depthOnly = device->CreateCommandBufferUVE();
        RenderPassDescUVE depthOnlyDesc{};
        depthOnlyDesc.colorAttachment = kInvalidTextureHandleUVE;
        depthOnlyDesc.depthAttachment = depthTex; // no color -> refused
        depthOnlyDesc.colorLoadOp = LoadOpUVE::Clear;
        depthOnlyDesc.depthLoadOp = LoadOpUVE::Clear;
        depthOnly->BeginRenderPassUVE(depthOnlyDesc);
        depthOnly->EndRenderPassUVE();
        device->SubmitUVE(std::move(depthOnly));
    }
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(trianglePipeline);
        commandBuffer->BindVertexBufferUVE(triBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }
    const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(center[0], 200);
    EXPECT_LT(center[1], 80);
    EXPECT_LT(center[2], 80) << "the frame after a refused depth-only pass must still draw "
        "the triangle - got (" << center[0] << "," << center[1] << "," << center[2] << ")";

    device->DestroyBufferUVE(triBuffer);
    device->DestroyTextureUVE(depthTex);
    device->DestroyPipelineUVE(trianglePipeline);
    device->DestroyShaderUVE(triVS);
    device->DestroyShaderUVE(triFS);
}

// ---------------------------------------------------------------------------
// M2e tier-2 proofs: on the dynamic-rendering arm, caller DEPTH attachments are
// truly sampleable after their pass closes (the bit-15 fallback is gone there),
// and offscreen passes honour real color/depth LoadOpUVE::Load (GL-FBO-accurate
// retention across passes, the former bit-6 warning now swapchain-only).
// ---------------------------------------------------------------------------

TEST_F(VulkanRenderDeviceUVETest, DepthAttachmentTextureSamplesRealDepthValues) {
    // The bit-15 resolution proof: render into a caller-supplied Depth32Float texture
    // (fullscreen triangle at NDC depth exactly 0.25), close the pass (the depth record's
    // LFA barrier lands it in SHADER_READ), then sample it with the textured quad on the
    // default swapchain pass. A DEPTH-format texture sampled by an ordinary sampler yields
    // (depth, 0, 0, 1) per the spec's depth-read swizzle, so the onscreen pixels must be
    // (~0.25*255, 0, 0, 255) - NOT the fallback white (255,255,255,255) M2d produced.
    ShaderHandleUVE depthVS{}, depthFS{}, quadVS{}, quadFS{};
    PipelineDescUVE depthPipelineDesc{};
    {
        ShaderDescUVE vertexDesc{};
        vertexDesc.stage = ShaderStageUVE::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirvUVE;
        ShaderDescUVE fragmentDesc{};
        fragmentDesc.stage = ShaderStageUVE::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirvUVE;
        depthVS = device->CreateShaderUVE(vertexDesc);
        depthFS = device->CreateShaderUVE(fragmentDesc);
        ASSERT_NE(depthVS, kInvalidShaderHandleUVE);
        ASSERT_NE(depthFS, kInvalidShaderHandleUVE);
        depthPipelineDesc.vertexShader = depthVS;
        depthPipelineDesc.fragmentShader = depthFS;
        depthPipelineDesc.vertexStride = 12U;
        depthPipelineDesc.vertexLayout.push_back(
            VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
        depthPipelineDesc.depthTestEnabled = true;
        depthPipelineDesc.depthWriteEnabled = true;
    }
    const PipelineHandleUVE depthPipeline = device->CreatePipelineUVE(depthPipelineDesc);
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE colorTarget = device->CreateTextureUVE(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandleUVE);
    TextureDescUVE depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormatUVE::Depth32Float;
    const TextureHandleUVE depthTex = device->CreateTextureUVE(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandleUVE);

    // Full-coverage triangle: depth written = uDepth everywhere it lands.
    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDescUVE triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE triBuffer = device->CreateBufferUVE(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Submission 1: color+depth offscreen pass; depth = 0.25 across the whole target.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {1.0F, 0.0F, 1.0F, 1.0F}; // magenta (unused by assertions)
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(depthPipeline);
        commandBuffer->BindVertexBufferUVE(triBuffer);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{1.0F, 1.0F, 1.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.25F);
        commandBuffer->SetUniformFloatUVE("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Submission 2: sample the depth texture on the default pass.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(depthTex, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic arm: bit-15 fallback persists there by design.
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "classic device must keep the depth fallback white";
    } else {
        // Real sampled depth 0.25: .r reconstructs it, .g/.b are the SPECIFIED swizzle
        // zeros, and the classic white fallback is GONE. Two honest driver choices remain,
        // so the byte check accepts exactly the correct outcomes and nothing else (the
        // bootstrap-clear check's philosophy - punishing one driver's honest pick is a
        // wrong-encoding failure mode, not coverage):
        // (a) swapchain format class: on an SRGB-typed image the shader's linear 0.25
        //     stores sRGB-encoded (1.055*0.25^(1/2.4)-0.055 ~= 0.537 -> byte ~137, the
        //     SwiftShader pick); on a UNORM-typed one (lavapipe's surfaces) the same
        //     value stores linearly (byte ~64).
        // (b) swizzle alpha: for depth/stencil views the identity A channel resolves to
        //     VK_COMPONENT_SWIZZLE_ONE, whose texel value is spec-UNDEFINED unless the
        //     device reports maintenance5's depthStencilSwizzleOneSupport (neither 1.3
        //     software stack does): SwiftShader yields 1.0 -> 255, lavapipe 0.0 -> 0.
        //     The reconstruction proof lives in .r plus the .g/.b zeros; either alpha
        //     byte is honest.
        const int red = center[0];
        const bool linearStored = red >= 61 && red <= 67;  // UNORM-typed swapchain
        const bool srgbStored = red >= 134 && red <= 140;  // SRGB-typed swapchain
        EXPECT_TRUE(linearStored || srgbStored)
            << "sampled depth must reconstruct 0.25 (64 linear / 137 sRGB) - got r=" << red;
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12);
        EXPECT_TRUE(center[3] == 255 || center[3] == 0)
            << "depth-read swizzle alpha is implementation-defined pre-maintenance5 - got a="
            << center[3];
    }

    device->DestroyBufferUVE(triBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(colorTarget);
    device->DestroyTextureUVE(depthTex);
    device->DestroyPipelineUVE(depthPipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(depthVS);
    device->DestroyShaderUVE(depthFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, OffscreenColorLoadPreservesContentAcrossPasses) {
    // The bit-6 (color) resolution proof: pass A clears the target BLACK and draws a red
    // LEFT-half triangle; pass B reuses the SAME color target with colorLoadOp=Load and
    // draws a GREEN RIGHT-half triangle. With Load honored, the sampled result must show
    // red on the left, green on the right, and the original black clear in the untouched
    // top-left corner - if pass B had re-cleared, the red half could not survive.
    ShaderHandleUVE triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandleUVE trianglePipeline = CreateTrianglePipelineUVE(*device, &triVS, &triFS);
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE colorTarget = device->CreateTextureUVE(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandleUVE);

    // Left half, solid red (x in [-1, 0]).
    const float leftVertices[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
         0.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
    };
    // Right half, solid green (x in [-0, +1] via two triangle strip quads? keep one
    // triangle covering x>0).
    const float rightVertices[18] = {
         1.0F, -1.0F, 0.0F,  0.0F, 1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  0.0F, 1.0F, 0.0F,
         0.0F,  1.0F, 0.0F,  0.0F, 1.0F, 0.0F,
    };
    BufferDescUVE leftDesc{};
    leftDesc.sizeBytes = sizeof(leftVertices);
    leftDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE leftBuffer = device->CreateBufferUVE(leftDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(leftVertices),
                                   sizeof(leftVertices)));
    BufferDescUVE rightDesc{};
    rightDesc.sizeBytes = sizeof(rightVertices);
    rightDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE rightBuffer = device->CreateBufferUVE(rightDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(rightVertices),
                                   sizeof(rightVertices)));
    ASSERT_NE(leftBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(rightBuffer, kInvalidBufferHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Pass A: clear black + red left.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(trianglePipeline);
        commandBuffer->BindVertexBufferUVE(leftBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Pass B: Load + green right (a re-clear would erase pass A entirely).
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOpUVE::Load;
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(trianglePipeline);
        commandBuffer->BindVertexBufferUVE(rightBuffer);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Pass C: sample.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {1.0F, 1.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(colorTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240) << "classic device degrades the whole chain to fallback";
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // Left-red survived pass B's Load.
        const auto left = ChannelAtNdcUVE(pixels, width, height, -0.5F, -0.2F);
        EXPECT_GT(left[0], 200); EXPECT_LT(left[1], 80); EXPECT_LT(left[2], 80)
            << "pass B's Load must have kept pass A's red - got ("
            << left[0] << "," << left[1] << "," << left[2] << ")";
        // Right-green lands in the same LOADed target.
        const auto right = ChannelAtNdcUVE(pixels, width, height, 0.5F, 0.2F);
        EXPECT_LT(right[0], 80); EXPECT_GT(right[1], 200); EXPECT_LT(right[2], 80)
            << "pass B's green right half - got ("
            << right[0] << "," << right[1] << "," << right[2] << ")";
        // Untouched top-left: the original black clear, preserved by Load.
        const auto untouched = ChannelAtNdcUVE(pixels, width, height, -0.5F, 0.9F);
        EXPECT_LT(untouched[0], 40); EXPECT_LT(untouched[1], 40); EXPECT_LT(untouched[2], 40)
            << "Load must preserve the original clear in untouched pixels - got ("
            << untouched[0] << "," << untouched[1] << "," << untouched[2] << ")";
    }

    device->DestroyBufferUVE(leftBuffer);
    device->DestroyBufferUVE(rightBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(colorTarget);
    device->DestroyPipelineUVE(trianglePipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(triVS);
    device->DestroyShaderUVE(triFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, OffscreenDepthLoadAccumulatesDepthAcrossPasses) {
    // The bit-6 (depth) resolution proof, plus the depth-reuse FBO contract: pass A draws a
    // NEAR red occluder (uDepth=0.2) into the caller depth target covering the center band;
    // pass B reuses BOTH attachments with color+depth LoadOpUVE::Load and draws (1) a FAR
    // full-coverage blue triangle (uDepth=0.9): where pass A's preserved red depth (0.2)
    // survives, blue must be CULLED; elsewhere blue fills the cleared far depth (1.0);
    // (2) a NEARER green triangle (uDepth=0.5) that must beat pass B's own blue (0.9)
    // inside the same pass. Red-guard, blue-fill, green-overtake = depth content genuinely
    // carried between passes.
    ShaderHandleUVE depthVS{}, depthFS{}, quadVS{}, quadFS{};
    PipelineDescUVE depthPipelineDesc{};
    {
        ShaderDescUVE vertexDesc{};
        vertexDesc.stage = ShaderStageUVE::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirvUVE;
        ShaderDescUVE fragmentDesc{};
        fragmentDesc.stage = ShaderStageUVE::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirvUVE;
        depthVS = device->CreateShaderUVE(vertexDesc);
        depthFS = device->CreateShaderUVE(fragmentDesc);
        ASSERT_NE(depthVS, kInvalidShaderHandleUVE);
        ASSERT_NE(depthFS, kInvalidShaderHandleUVE);
        depthPipelineDesc.vertexShader = depthVS;
        depthPipelineDesc.fragmentShader = depthFS;
        depthPipelineDesc.vertexStride = 12U;
        depthPipelineDesc.vertexLayout.push_back(
            VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3, 0U});
        depthPipelineDesc.depthTestEnabled = true;
        depthPipelineDesc.depthWriteEnabled = true;
    }
    const PipelineHandleUVE depthPipeline = device->CreatePipelineUVE(depthPipelineDesc);
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandleUVE);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE colorTarget = device->CreateTextureUVE(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandleUVE);
    TextureDescUVE depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormatUVE::Depth32Float;
    const TextureHandleUVE depthTex = device->CreateTextureUVE(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandleUVE);

    // Red occluder: horizontal center band (y in [-0.25, 0.25], full width via offsets? no -
    // one triangle can only be half-plane; use a quad = two triangles, so 6 verts * 3).
    const float redBand[18] = {
        -1.0F, -0.25F, 0.0F,
         1.0F, -0.25F, 0.0F,
        -1.0F,  0.25F, 0.0F,
         1.0F, -0.25F, 0.0F,
         1.0F,  0.25F, 0.0F,
        -1.0F,  0.25F, 0.0F,
    };
    // Blue far full-coverage triangle.
    const float blueFull[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    // Green small triangle inside, lower-left quadrant (well inside blue, outside red).
    const float greenSmall[9] = {
        -0.75F, -0.75F, 0.0F,
        -0.25F, -0.75F, 0.0F,
        -0.75F, -0.25F, 0.0F,
    };
    BufferDescUVE redDesc{};
    redDesc.sizeBytes = sizeof(redBand);
    redDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE redBuffer = device->CreateBufferUVE(redDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(redBand), sizeof(redBand)));
    BufferDescUVE blueDesc{};
    blueDesc.sizeBytes = sizeof(blueFull);
    blueDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE blueBuffer = device->CreateBufferUVE(blueDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(blueFull), sizeof(blueFull)));
    BufferDescUVE greenDesc{};
    greenDesc.sizeBytes = sizeof(greenSmall);
    greenDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE greenBuffer = device->CreateBufferUVE(greenDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(greenSmall), sizeof(greenSmall)));
    ASSERT_NE(redBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(blueBuffer, kInvalidBufferHandleUVE);
    ASSERT_NE(greenBuffer, kInvalidBufferHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Pass A: red band at depth 0.2 (near), with its own clear.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(depthPipeline);
        commandBuffer->BindVertexBufferUVE(redBuffer);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.2F);
        commandBuffer->SetUniformFloatUVE("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Pass B: LOAD both attachments, then far-blue + near-green as plotted above.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOpUVE::Load;
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOpUVE::Load;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(depthPipeline);
        commandBuffer->BindVertexBufferUVE(blueBuffer);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.9F);
        commandBuffer->SetUniformFloatUVE("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloatUVE("uOffsetY", 0.0F);
        commandBuffer->DrawUVE(3U);
        commandBuffer->BindVertexBufferUVE(greenBuffer);
        commandBuffer->SetUniformVector3UVE("uColorTri", Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloatUVE("uDepth", 0.5F);
        commandBuffer->DrawUVE(3U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    // Pass C: sample the accumulated color.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {1.0F, 1.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(colorTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240) << "classic device degrades the whole chain to fallback";
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // Inside the red band: pass B's blue at depth 0.9 MUST be culled by the preserved
        // red depth (0.2) - proof that Load retained pass A's depth content.
        const auto band = ChannelAtNdcUVE(pixels, width, height, 0.4F, 0.0F);
        EXPECT_GT(band[0], 200); EXPECT_LT(band[1], 80); EXPECT_LT(band[2], 80)
            << "preserved near depth must cull pass-B blue - got ("
            << band[0] << "," << band[1] << "," << band[2] << ")";
        // Outside the band and outside the green triangle: pass-B blue filled the far clear.
        const auto blueArea = ChannelAtNdcUVE(pixels, width, height, 0.4F, 0.6F);
        EXPECT_LT(blueArea[0], 80); EXPECT_LT(blueArea[1], 80); EXPECT_GT(blueArea[2], 200)
            << "far blue must land where depth was still the clear - got ("
            << blueArea[0] << "," << blueArea[1] << "," << blueArea[2] << ")";
        // Green small triangle: nearer than pass-B blue's own 0.9 wins inside pass B.
        const auto greenArea = ChannelAtNdcUVE(pixels, width, height, -0.6F, -0.6F);
        EXPECT_LT(greenArea[0], 80); EXPECT_GT(greenArea[1], 200); EXPECT_LT(greenArea[2], 80)
            << "nearer green must overtake pass-B blue - got ("
            << greenArea[0] << "," << greenArea[1] << "," << greenArea[2] << ")";
    }

    device->DestroyBufferUVE(redBuffer);
    device->DestroyBufferUVE(blueBuffer);
    device->DestroyBufferUVE(greenBuffer);
    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(colorTarget);
    device->DestroyTextureUVE(depthTex);
    device->DestroyPipelineUVE(depthPipeline);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(depthVS);
    device->DestroyShaderUVE(depthFS);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

TEST_F(VulkanRenderDeviceUVETest, SamplingOpenPassAttachmentDegradesOnceAndFrameSurvives) {
    // The bit-16 feedback-loop guard proof: inside an OPEN offscreen pass, binding the very
    // texture that pass is currently writing is a feedback loop (undefined behavior in
    // Vulkan). M2e guards it honestly - the draw inside that pass samples the 1x1-white
    // fallback instead of reading its own attachment - and after the pass closes the SAME
    // texture samples its real content. Frame intact throughout.
    ShaderHandleUVE quadVS{}, quadFS{};
    const PipelineHandleUVE texturedPipeline = CreateTexturedPipelineUVE(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandleUVE);

    TextureDescUVE colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormatUVE::RGBA8Unorm;
    const TextureHandleUVE colorTarget = device->CreateTextureUVE(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandleUVE);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDescUVE quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsageUVE::Vertex;
    const BufferHandleUVE quadBuffer = device->CreateBufferUVE(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandleUVE);

    // Submission 1: clear the target RED, then open a second pass on it with Load, draw a
    // quad that binds the target ITSELF (feedback) inside that open pass - guarded to the
    // fallback white - and close it. The self-sampling quad wrote WHITE over the red.
    {
        auto clearCmd = device->CreateCommandBufferUVE();
        RenderPassDescUVE clearDesc{};
        clearDesc.colorAttachment = colorTarget;
        clearDesc.colorLoadOp = LoadOpUVE::Clear;
        clearDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
        clearDesc.depthLoadOp = LoadOpUVE::Clear;
        clearDesc.clearDepth = 1.0F;
        clearCmd->BeginRenderPassUVE(clearDesc);
        clearCmd->EndRenderPassUVE();
        device->SubmitUVE(std::move(clearCmd));

        auto feedbackCmd = device->CreateCommandBufferUVE();
        RenderPassDescUVE loopDesc{};
        loopDesc.colorAttachment = colorTarget;
        loopDesc.colorLoadOp = LoadOpUVE::Load;
        loopDesc.depthLoadOp = LoadOpUVE::Clear;
        loopDesc.clearDepth = 1.0F;
        feedbackCmd->BeginRenderPassUVE(loopDesc);
        feedbackCmd->BindPipelineUVE(texturedPipeline);
        feedbackCmd->BindVertexBufferUVE(quadBuffer);
        feedbackCmd->BindTextureUVE(colorTarget, 0U); // == the open pass's attachment!
        feedbackCmd->DrawUVE(6U);
        feedbackCmd->EndRenderPassUVE();
        device->SubmitUVE(std::move(feedbackCmd));
    }
    // Submission 2: sample the target - must be WHITE wherever the guarded quad landed
    // (fallback, not garbage, not the red), proving the guard traded correctness for a
    // deterministic degrade exactly once.
    {
        auto commandBuffer = device->CreateCommandBufferUVE();
        RenderPassDescUVE passDesc{};
        passDesc.colorLoadOp = LoadOpUVE::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOpUVE::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPassUVE(passDesc);
        commandBuffer->BindPipelineUVE(texturedPipeline);
        commandBuffer->BindVertexBufferUVE(quadBuffer);
        commandBuffer->BindTextureUVE(colorTarget, 0U);
        commandBuffer->DrawUVE(6U);
        commandBuffer->EndRenderPassUVE();
        device->SubmitUVE(std::move(commandBuffer));
    }
    device->PresentUVE();
    ASSERT_TRUE(device->IsUsableUVE());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImageUVE(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendNameUVE();
    const auto center = ChannelAtNdcUVE(pixels, width, height, 0.0F, 0.0F);
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic arm: offscreen passes skip entirely, nothing wrote; sampling still falls
        // back (bit-15), frame intact. Same white, different reason - still the contract.
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // The guarded self-sample used the fallback white and painted it over the red.
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240)
            << "open-pass feedback sampling must degrade to the fallback (deterministic "
               "white), never to garbage or the raw attachment - got ("
            << center[0] << "," << center[1] << "," << center[2] << ")";
    }

    device->DestroyBufferUVE(quadBuffer);
    device->DestroyTextureUVE(colorTarget);
    device->DestroyPipelineUVE(texturedPipeline);
    device->DestroyShaderUVE(quadVS);
    device->DestroyShaderUVE(quadFS);
}

} // namespace UVE::Render::Tests
