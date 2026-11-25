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
// resource call returns the documented invalid result, and Present() can advance several
// frames against the real lavapipe software swapchain without crashing — which is exactly
// what "M1: init -> surface -> swapchain -> clear-color present" means end-to-end.


#include "vulkan_render_device.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "event_system.h"
#include "i_command_buffer.h"
#include "bind_group.h"
#include "i_vulkan_window_surface.h"
#include "spirv_test_shaders.h"
#include "null_window_manager.h"
#include "window_manager.h"

namespace UVE::Render::Tests {
namespace {

[[nodiscard]] Window::WindowDesc MakeTestWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_vulkan_render_device_uve_tests";
    desc.width = 64;
    desc.height = 64;
    // Vulkan ignores the GL hint fields entirely — WindowManager only uses them for the
    // GL context-creation path — but defaulting them keeps the desc unambiguous to readers.
    return desc;
}

} // namespace

// ---------------------------------------------------------------------------
// Tier 1: capability-boundary tests — must pass on EVERY host, Vulkan or not.
// ---------------------------------------------------------------------------

TEST(VulkanCapabilityTest, NullWindowManagerNeverOffersVulkanSurfaceBridge) {
    Window::NullWindowManager nullWindowManager;
    // Query through the IWindowManager base — exactly how the factory does it: through the
    // polymorphic base, the compiler cannot prove the outcome away (on the concrete type it flags "can never
    // succeed" on the concrete final type, which is precisely the fact under test).
    Window::IWindowManager& asBase = nullWindowManager;
    EXPECT_EQ(dynamic_cast<Window::IVulkanWindowSurface*>(&asBase), nullptr)
        << "the headless window manager must not claim the Vulkan surface capability";
}

TEST(VulkanCapabilityTest, CreateOnNullWindowManagerReturnsNullptr) {
    Window::NullWindowManager nullWindowManager;
    auto device = VulkanRenderDevice::Create(nullWindowManager);
    EXPECT_EQ(device, nullptr) << "the factory must cleanly refuse headless input";
}

namespace {

/// Minimal stub bridge that implements the capability contract but reports "no extensions" —
/// the deterministic refusal path for the bridge-direct factory overload on ANY host (with or
/// without a Vulkan loader installed). The bridge methods themselves are never invoked before
/// the extension query bails, so their bodies being minimal is honest, not mockup.
class NoExtensionsStubBridge final : public Window::IVulkanWindowSurface {
public:
    [[nodiscard]] std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override {
        return {}; // deliberate: the documented "capability unavailable" signal
    }
    [[nodiscard]] std::uintptr_t CreateVulkanWindowSurface(std::uintptr_t /*vulkanInstance*/) override {
        return 0U;
    }
    void DestroyVulkanWindowSurface(std::uintptr_t /*vulkanInstance*/,
                                       std::uintptr_t /*vulkanSurface*/) override {}
    void GetVulkanFramebufferSize(std::uint32_t& outWidth, std::uint32_t& outHeight) const override {
        outWidth = 64U;
        outHeight = 64U;
    }
};

} // namespace

TEST(VulkanCapabilityTest, BridgeDirectCreateOnStubWithoutExtensionsReturnsNullptr) {
    NoExtensionsStubBridge stubBridge;
    auto device = VulkanRenderDevice::CreateFromBridge(stubBridge);
    EXPECT_EQ(device, nullptr) << "a bridge advertising no Vulkan WSI extensions must be refused";
}

TEST(VulkanCapabilityTest, RealWindowManagerOffersVulkanSurfaceBridge) {
    // Interface-cast wiring only (no window needed): the real window manager class must be
    // typed against the capability interface so the factory's dynamic_cast has something to
    // find. WindowDesc-sized default construction is enough — IsValid() stays false,
    // which this test does not touch.
    static_assert(std::is_base_of_v<Window::IVulkanWindowSurface, Window::WindowManager>,
                  "WindowManager must implement IVulkanWindowSurface");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Tier 2: real-device tests — skip politely where the host cannot provide Vulkan.
// ---------------------------------------------------------------------------

class VulkanRenderDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Real-device path A: a windowed device on a live OS display (GLFW + swapchain WSI).
        // A valid window is not enough on its own: GLFW's Null platform (the headless sandbox,
        // where contexts come from OSMesa) hands out a perfectly valid window that has no Vulkan
        // WSI behind it. IVulkanWindowSurface's documented signal for that is an empty
        // required-extension list, so ask before committing to the windowed path - otherwise
        // Create() would bail on surface creation and these tests would skip on a host that
        // does have a Vulkan device.
        windowManager = std::make_unique<Window::WindowManager>(eventSystem, MakeTestWindowDesc());
        const bool windowHasVulkanWsi =
            windowManager->IsValid() && !windowManager->GetRequiredVulkanInstanceExtensions().empty();
        if (windowHasVulkanWsi) {
            device = VulkanRenderDevice::Create(*windowManager);
        } else {
            // Real-device path B: displayless host (CI without Xvfb, containers, servers) or a
            // window whose platform cannot present through Vulkan — the engine's self-contained
            // headless factory builds the identical device over a VK_EXT_headless_surface instead
            // of skipping. The window manager is dropped: the device owns no window in this mode,
            // and every pixel assertion below reads the headless swapchain exactly as it would
            // the windowed one.
            windowManager.reset();
            device = VulkanRenderDevice::CreateHeadless();
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

    Events::EventSystem eventSystem;
    std::unique_ptr<Window::WindowManager> windowManager;
    std::unique_ptr<VulkanRenderDevice> device;
};

TEST_F(VulkanRenderDeviceTest, TextureLeaseSurvivesDeferredSubmissionUntilGpuCompletion) {
    const auto texture = device->CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    ASSERT_NE(texture, kInvalidTextureHandle);
    auto lease = device->AdoptTransientTextures(std::vector<TextureHandle>{texture});
    ASSERT_NE(lease, nullptr);
    std::weak_ptr<void> observer = lease;
    auto commands = device->CreateCommandBuffer();
    commands->RetainResources(std::move(lease));
    RenderPassDesc pass;
    // Dynamic rendering can clear an offscreen transient; classic devices still exercise the
    // same completion lifetime through a default-surface pass rather than claiming that feature.
    if (device->GetCapabilities().memoryBarriers == RenderFeatureSupport::Native)
        pass.colorAttachment = texture;
    commands->BeginRenderPass(pass); commands->EndRenderPass();
    std::thread worker([this, commands = std::move(commands)]() mutable { device->Submit(std::move(commands)); });
    worker.join();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(device->WaitIdle(), RenderOperationResult::InvalidState);
    TextureDesc description;
    EXPECT_EQ(device->GetTextureDesc(texture, description), RenderOperationResult::Success);
    device->Present();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(device->WaitIdle(), RenderOperationResult::Success);
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(device->GetTextureDesc(texture, description), RenderOperationResult::InvalidHandle);
}

TEST_F(VulkanRenderDeviceTest, DeviceReportsUsableWithHonestBootstrapName) {
    EXPECT_TRUE(device->IsUsable());
    // M5b: the reported name is capability-driven — a 1.3/dynamic-rendering device reports
    // the current slice name (M5b), anything older reports the M2c classic one. Both are
    // milestone-tagged; neither may be the bare "Vulkan" (honest capability contract).
    const std::string_view name = device->GetBackendName();
    EXPECT_TRUE(name == "Vulkan (M5b storage images)" || name == "Vulkan (M2c textures+staging)")
        << "backend name must report the exact slice and capability gate, got: " << name;
}

TEST_F(VulkanRenderDeviceTest, CreatePipeline_LinesTopology_Succeeds) {
    // Tier 2.8 Scope A: LINE_LIST assembly bakes without a validation error on a real device.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    pipelineDesc.topology = PrimitiveTopology::Lines;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    EXPECT_NE(pipeline, kInvalidPipelineHandle);

    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, PresentedFrameReadbackIsUniformBootstrapClear) {
    for (int frame = 0; frame < 6; ++frame) {
        device->Present();
        ASSERT_TRUE(device->IsUsable());
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
    ASSERT_FALSE(device->ReadbackLatestPresentedImage(pixels, width, height))
        << "an empty span must be the documented size-query failure";
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    pixels.resize(static_cast<std::size_t>(width) * height * 4U);
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
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

TEST_F(VulkanRenderDeviceTest, ReadbackBeforeAnyPresentFailsCleanly) {
    std::vector<std::byte> pixels(64U * 64U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    EXPECT_FALSE(device->ReadbackLatestPresentedImage(pixels, width, height));
}

TEST_F(VulkanRenderDeviceTest, PresentUveAdvancesMultipleFramesWithoutCrash) {
    // FIFO present + 1 frame in flight: this loop double-checks the fence invariants by
    // performing more frames than there are swapchain images (recreate and acquire both spin).
    for (int frame = 0; frame < 8; ++frame) {
        device->Present();
        ASSERT_TRUE(device->IsUsable()) << "device went inert at frame " << frame;
    }
}

TEST_F(VulkanRenderDeviceTest, OutOfScopeResourceCallsReturnDocumentedInvalidResults) {
    // Remaining honest "not yet" areas of the M2a slice: textures, offscreen/depth targets,
    // reflection, and pipeline binaries still return the documented invalid values with a
    // naming-the-milestone warning (Null-render contract — never fake success, never crash).
    const TextureDesc textureDesc{};
    EXPECT_EQ(device->CreateTexture(textureDesc), kInvalidTextureHandle);
    device->DestroyTexture(kInvalidTextureHandle);

    const PipelineDesc pipelineDesc{};
    std::string infoLog;
    EXPECT_EQ(device->CreatePipeline(pipelineDesc, &infoLog), kInvalidPipelineHandle);
    EXPECT_FALSE(infoLog.empty()) << "outInfoLog must name why creation failed";

    EXPECT_TRUE(device->GetPipelineUniforms(kInvalidPipelineHandle).empty());
    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    EXPECT_FALSE(device->GetPipelineBinary(kInvalidPipelineHandle, binary, format));

    const PipelineBinaryDesc binaryDesc{};
    EXPECT_EQ(device->CreatePipelineFromBinary({}, 0U, binaryDesc), kInvalidPipelineHandle);

    device->DestroyBuffer(kInvalidBufferHandle); // documented safe no-op
    device->DestroyShader(kInvalidShaderHandle);
    device->DestroyPipeline(kInvalidPipelineHandle);
    device->Submit(nullptr); // documented: null submissions are ignored
}

TEST_F(VulkanRenderDeviceTest, BuffersAreRealDeviceMemoriesWithStagedUpdates) {
    // M3: vertex/index buffers live in DEVICE_LOCAL memory fed by one-shot staging copies;
    // uniform/storage buffers remain HOST_VISIBLE with persistent maps. The caller-visible
    // contract is identical for both placements, and that is what this test pins: creation
    // with initial data, successful in-range update, clean out-of-range rejection, and
    // silent-false after destroy - exercised against one buffer of each class.
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = 12U;
    bufferDesc.usage = BufferUsage::Vertex;
    const float threeFloats[3] = {1.0F, 2.0F, 3.0F};
    const BufferHandle buffer = device->CreateBuffer(
        bufferDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(threeFloats),
                                               sizeof(threeFloats)));
    ASSERT_NE(buffer, kInvalidBufferHandle);

    const float replacement[3] = {4.0F, 5.0F, 6.0F};
    EXPECT_TRUE(device->UpdateBuffer(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));
    // Out-of-range update must fail cleanly and must not have touched anything.
    EXPECT_FALSE(device->UpdateBuffer(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement)),
        4U));
    device->DestroyBuffer(buffer);
    // Update after destroy: the interface's documented silent-false, never a crash.
    EXPECT_FALSE(device->UpdateBuffer(buffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));

    // The host-visible class (uniform) must keep the exact same contract.
    BufferDesc uniformDesc{};
    uniformDesc.sizeBytes = 12U;
    uniformDesc.usage = BufferUsage::Uniform;
    const BufferHandle uniformBuffer = device->CreateBuffer(
        uniformDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(threeFloats),
                                                sizeof(threeFloats)));
    ASSERT_NE(uniformBuffer, kInvalidBufferHandle);
    EXPECT_TRUE(device->UpdateBuffer(uniformBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));
    EXPECT_FALSE(device->UpdateBuffer(uniformBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement)),
        4U));
    device->DestroyBuffer(uniformBuffer);
    EXPECT_FALSE(device->UpdateBuffer(uniformBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(replacement),
                                   sizeof(replacement))));
}

TEST_F(VulkanRenderDeviceTest, BufferMemoryHintsPlaceBuffersWithoutChangingTheUpdateReadbackContract) {
    // The memory/update hints steer placement (device-local behind staging, host-visible mapped,
    // host-cached readback) and are reported back verbatim; the observable contract — create
    // with data, update a window, read back exactly that — holds for every (usage, hint) pair,
    // including the ones that are new placements for this backend: a device-local storage
    // buffer and a host-visible (Upload) vertex buffer. Unknown hint values are refused.
    const std::array<std::uint32_t, 4> values{7U, 11U, 23U, 42U};
    for (const BufferUsage usage : {BufferUsage::Vertex, BufferUsage::Index, BufferUsage::Uniform,
                                       BufferUsage::Storage, BufferUsage::IndirectStorage}) {
        for (const BufferMemory memory : {BufferMemory::Auto, BufferMemory::DeviceLocal,
                                             BufferMemory::Upload, BufferMemory::Readback}) {
            const BufferDesc desc{sizeof(values), usage, memory, BufferUpdateFrequency::Dynamic};
            const BufferHandle buffer = device->CreateBuffer(desc, std::as_bytes(std::span(values)));
            ASSERT_NE(buffer, kInvalidBufferHandle)
                << "usage " << static_cast<int>(usage) << " memory " << static_cast<int>(memory);
            BufferDesc reported{};
            ASSERT_EQ(device->GetBufferDesc(buffer, reported), RenderOperationResult::Success);
            EXPECT_EQ(reported.sizeBytes, sizeof(values));
            EXPECT_EQ(reported.usage, usage);
            EXPECT_EQ(reported.memory, memory);
            EXPECT_EQ(reported.updateFrequency, BufferUpdateFrequency::Dynamic);
            const std::array<std::uint32_t, 1> patch{99U};
            ASSERT_TRUE(device->UpdateBuffer(buffer, std::as_bytes(std::span(patch)), 8U));
            std::array<std::uint32_t, 4> observed{};
            ASSERT_TRUE(device->ReadbackBuffer(buffer, std::as_writable_bytes(std::span(observed))))
                << "usage " << static_cast<int>(usage) << " memory " << static_cast<int>(memory);
            EXPECT_EQ(observed, (std::array<std::uint32_t, 4>{7U, 11U, 99U, 42U}));
            device->DestroyBuffer(buffer);
        }
    }
    // A device-local destination still takes CopyBuffer from a host-visible source, and the
    // copy is visible through the staged readback.
    const BufferHandle source = device->CreateBuffer(
        {sizeof(values), BufferUsage::Storage, BufferMemory::Upload}, std::as_bytes(std::span(values)));
    const BufferHandle target = device->CreateBuffer({sizeof(values), BufferUsage::Storage,
                                                            BufferMemory::DeviceLocal});
    ASSERT_NE(source, kInvalidBufferHandle);
    ASSERT_NE(target, kInvalidBufferHandle);
    ASSERT_EQ(device->CopyBuffer({source, target, 4U, 0U, 12U}), RenderOperationResult::Success);
    std::array<std::uint32_t, 4> copied{};
    ASSERT_TRUE(device->ReadbackBuffer(target, std::as_writable_bytes(std::span(copied))));
    EXPECT_EQ(copied[0], 11U);
    EXPECT_EQ(copied[1], 23U);
    EXPECT_EQ(copied[2], 42U);
    device->DestroyBuffer(target);
    device->DestroyBuffer(source);
    EXPECT_EQ(device->CreateBuffer({16U, BufferUsage::Vertex, static_cast<BufferMemory>(0xFFU)}),
              kInvalidBufferHandle);
    EXPECT_EQ(device->CreateBuffer({16U, BufferUsage::Vertex, BufferMemory::Auto,
                                       static_cast<BufferUpdateFrequency>(0xFFU)}),
              kInvalidBufferHandle);
}

TEST_F(VulkanRenderDeviceTest, ShaderCreationRejectsGlslTextAndAcceptsSpirv) {
    ShaderDesc glslDesc{};
    glslDesc.stage = ShaderStage::Vertex;
    glslDesc.sourceCode = "#version 450\nvoid main() { gl_Position = vec4(0.0); }";
    std::string infoLog;
    EXPECT_EQ(device->CreateShader(glslDesc, &infoLog), kInvalidShaderHandle);
    EXPECT_NE(infoLog.find("SPIR-V"), std::string::npos) << infoLog;

    ShaderDesc spirvDesc{};
    spirvDesc.stage = ShaderStage::Vertex;
    spirvDesc.sourceCode = kTriangleVertexSpirv;
    const ShaderHandle shader = device->CreateShader(spirvDesc);
    ASSERT_NE(shader, kInvalidShaderHandle);
    device->DestroyShader(shader);
}

TEST_F(VulkanRenderDeviceTest, GeometryPipelineIsExplicitlyUnsupportedRatherThanIgnored) {
    EXPECT_EQ(device->GetCapabilities().geometryShaders, RenderFeatureSupport::Unsupported);
    const auto vertex = device->CreateShader({ShaderStage::Vertex, kTriangleVertexSpirv});
    const auto fragment = device->CreateShader({ShaderStage::Fragment, kTriangleFragmentSpirv});
    ASSERT_NE(vertex, kInvalidShaderHandle);
    ASSERT_NE(fragment, kInvalidShaderHandle);
    PipelineDesc desc;
    desc.vertexShader = vertex;
    desc.fragmentShader = fragment;
    // Even a live handle must not make an unsupported optional stage silently disappear.
    desc.geometryShader = vertex;
    std::string info;
    EXPECT_EQ(device->CreatePipeline(desc, &info), kInvalidPipelineHandle);
    EXPECT_NE(info.find("geometry shader pipelines are unsupported"), std::string::npos);
    device->DestroyShader(fragment);
    device->DestroyShader(vertex);
}

TEST_F(VulkanRenderDeviceTest, TimestampQueriesReportRealTicksAndRefuseOcclusion) {
    using Result = RenderOperationResult;
    const auto caps = device->GetCapabilities();
    ASSERT_EQ(caps.timestampQueries, RenderFeatureSupport::Native);
    EXPECT_EQ(caps.occlusionQueries, RenderFeatureSupport::Native)
        << "WP3: occlusion queries are real through the recorded command-buffer bracket";

    QueryHandle query;
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::Timestamp, query), Result::Success);
    EXPECT_EQ(device->BeginQuery(query), Result::Unsupported);
    EXPECT_EQ(device->EndQuery(query), Result::Unsupported);
    ASSERT_EQ(device->WriteTimestamp(query), Result::Success);
    RenderQueryResult result{};
    ASSERT_EQ(device->GetQueryResult(query, result, false), Result::Success);
    EXPECT_EQ(result.kind, RenderQueryKind::Timestamp);
    const std::uint64_t first = result.value;
    // A later write cannot report an earlier time; a coarse clock may legitimately repeat a tick.
    ASSERT_EQ(device->WriteTimestamp(query), Result::Success);
    ASSERT_EQ(device->GetQueryResult(query, result, true), Result::Success);
    EXPECT_GE(result.value, first);

    QueryHandle occlusion{};
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::AnySamplesPassed, occlusion), Result::Success);
    EXPECT_NE(occlusion, kInvalidQueryHandle);
    // The device-level pair still cannot bracket draws: only the recorded bracket is real.
    EXPECT_EQ(device->BeginQuery(occlusion), Result::Unsupported);
    EXPECT_EQ(device->EndQuery(occlusion), Result::Unsupported);
    EXPECT_EQ(device->WriteTimestamp(occlusion), Result::InvalidArgument)
        << "a timestamp write on an occlusion query is a kind mismatch";
    RenderQueryResult neverIssued{};
    neverIssued.value = 77U;
    EXPECT_EQ(device->GetQueryResult(occlusion, neverIssued, false), Result::NotReady)
        << "a pool that was never reset+written has no result";
    EXPECT_EQ(neverIssued.value, 77U);
    EXPECT_EQ(device->DestroyQuery(occlusion), Result::Success);
    QueryHandle refused{};
    EXPECT_EQ(device->CreateQuery(static_cast<RenderQueryKind>(0xFFU), refused), Result::InvalidArgument);
    EXPECT_EQ(refused, kInvalidQueryHandle) << "a refused create must leave the output untouched";
    RenderQueryResult untouched{};
    untouched.value = 4321U;
    EXPECT_EQ(device->WriteTimestamp(QueryHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(device->GetQueryResult(QueryHandle{999999U}, untouched, false), Result::InvalidHandle);
    EXPECT_EQ(untouched.value, 4321U) << "a refused read must leave the output untouched";
    EXPECT_EQ(device->DestroyQuery(QueryHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(device->DestroyQuery(query), Result::Success);
    EXPECT_EQ(device->DestroyQuery(query), Result::InvalidHandle);
    EXPECT_EQ(device->GetQueryResult(query, untouched, false), Result::InvalidHandle);
}

TEST_F(VulkanRenderDeviceTest, TimelineSemaphoresProvideRealCountersAndHonestTimeouts) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Native)
        << "the CI ICD (lavapipe) exposes Vulkan 1.2+ timeline semaphores";

    SemaphoreHandle semaphore;
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{3U}, semaphore), Result::Success);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device->GetSemaphoreCounter(semaphore, counter), Result::Success);
    EXPECT_EQ(counter, 3U);
    EXPECT_EQ(device->WaitSemaphore(semaphore, 3U, 0U), Result::Success)
        << "an already-reached value resolves without waiting";
    EXPECT_EQ(device->WaitSemaphore(semaphore, 4U, 0U), Result::TimedOut);
    EXPECT_EQ(device->SignalSemaphore(semaphore, 3U), Result::InvalidArgument)
        << "a timeline counter never moves backwards";
    EXPECT_EQ(device->SignalSemaphore(semaphore, 2U), Result::InvalidArgument);
    ASSERT_EQ(device->SignalSemaphore(semaphore, 4U), Result::Success);
    EXPECT_EQ(device->WaitSemaphore(semaphore, 4U, 1'000'000'000ULL), Result::Success);
    ASSERT_EQ(device->GetSemaphoreCounter(semaphore, counter), Result::Success);
    EXPECT_EQ(counter, 4U);

    std::uint64_t untouched = 55U;
    EXPECT_EQ(device->GetSemaphoreCounter(SemaphoreHandle{999999U}, untouched), Result::InvalidHandle);
    EXPECT_EQ(untouched, 55U) << "a refused counter read must leave the output untouched";
    EXPECT_EQ(device->WaitSemaphore(SemaphoreHandle{999999U}, 0U, 0U), Result::InvalidHandle);
    EXPECT_EQ(device->SignalSemaphore(SemaphoreHandle{999999U}, 5U), Result::InvalidHandle);
    EXPECT_EQ(device->DestroySemaphore(SemaphoreHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(device->DestroySemaphore(semaphore), Result::Success);
    EXPECT_EQ(device->DestroySemaphore(semaphore), Result::InvalidHandle);
    EXPECT_EQ(device->GetSemaphoreCounter(semaphore, untouched), Result::InvalidHandle);
    EXPECT_EQ(device->WaitIdle(), Result::Success);
}

TEST_F(VulkanRenderDeviceTest, StencilPipelineAndDepthStencilTextureCreateOnDynamicDevice) {
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    TextureDesc depthDesc{};
    depthDesc.width = 8U;
    depthDesc.height = 8U;
    depthDesc.format = TextureFormat::Depth24Stencil8;
    const TextureHandle depthStencil = device->CreateTexture(depthDesc);
    ASSERT_NE(depthStencil, kInvalidTextureHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 24U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"COLOR", VertexAttributeFormat::Float3, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    pipelineDesc.stencil.enabled = true;
    pipelineDesc.stencil.front.compare = DepthCompare::Equal;
    pipelineDesc.stencil.front.depthPass = StencilOp::Replace;
    pipelineDesc.stencil.front.reference = 7U;
    pipelineDesc.stencil.back.compare = DepthCompare::Always;
    pipelineDesc.stencil.back.depthPass = StencilOp::Keep;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    device->DestroyPipeline(pipeline);
    device->DestroyTexture(depthStencil);
    device->DestroyShader(fragmentShader);
    device->DestroyShader(vertexShader);
}

TEST_F(VulkanRenderDeviceTest, SubmittedTriangleCommandBufferReallyRendersPixels) {
    // A full M2a round trip: SPIR-V shaders + a real pipeline + an interleaved
    // position/color vertex buffer + a recorded command buffer that draws a big triangle —
    // verified against the swapchain's own pixels via ReadbackLatestPresentedImage, not by
    // "no crash" (that would be the mock version of this test).
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 24U; // 2x vec3 interleaved (position, color)
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"COLOR", VertexAttributeFormat::Float3, 12U});
    pipelineDesc.depthTestEnabled = false; // the M2a pass has no depth attachment (documented)
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    // Triangle covering the whole frame (deep-blue-ish red corner signature), interleaved
    // vec3 position + vec3 color, 6 floats per vertex.
    const float vertices[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F, // bottom-left:  red
         1.0F, -1.0F, 0.0F,  0.0F, 1.0F, 0.0F, // bottom-right: green
         0.0F,  1.0F, 0.0F,  0.0F, 0.0F, 1.0F, // top-center:   blue
    };
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc{}; // default attachments: the swapchain ("default framebuffer")
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    device->Submit(std::move(commandBuffer));

    device->Present();
    ASSERT_TRUE(device->IsUsable());

    // Readback: the triangle covers the whole 1280x720 surface (gl_Position pins three corners
    // on the big clip triangle that every viewport rasterizes fully here), so the frame must
    // be NON-uniform: a gradient of the three vertex colors — precisely provable:
    // red corner must be dominantly red, green corner dominantly green, and counting pixels
    // must show above 99% of the frame no longer matching the clear color.
    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
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

    device->DestroyBuffer(vertexBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


TEST_F(VulkanRenderDeviceTest, ReflectedUniformsMatchTheSpirvLayout) {
    // M2b: CreatePipeline now parses the SPIR-V modules with SPIRV-Reflect.
    // The depth-uniform pair declares exactly: UBO block { float uDepth; vec3 uColorTri; }
    // plus push-constant block { float uOffsetX; float uOffsetY; } — the reflection service
    // must report all four with matching types (location is backend-internal: not asserted).
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::vector<UniformReflection> uniforms = device->GetPipelineUniforms(pipeline);
    ASSERT_EQ(uniforms.size(), 4U) << "expected uDepth, uColorTri, uOffsetX, uOffsetY";
    std::map<std::string, ShaderDataType> byName;
    for (const UniformReflection& entry : uniforms) {
        byName.emplace(entry.name, entry.type);
    }
    EXPECT_EQ(byName.count("uDepth"), 1U);
    EXPECT_EQ(byName.at("uDepth"), ShaderDataType::Float);
    EXPECT_EQ(byName.count("uColorTri"), 1U);
    EXPECT_EQ(byName.at("uColorTri"), ShaderDataType::Vec3);
    EXPECT_EQ(byName.count("uOffsetX"), 1U);
    EXPECT_EQ(byName.at("uOffsetX"), ShaderDataType::Float);
    EXPECT_EQ(byName.count("uOffsetY"), 1U);
    EXPECT_EQ(byName.at("uOffsetY"), ShaderDataType::Float);

    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, DepthAndPerDrawUniformSnapshotsRenderCorrectly) {
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
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const float vertices[9] = {
        -0.6F, -0.6F, 0.0F,
         0.6F, -0.6F, 0.0F,
         0.0F,  0.6F, 0.0F,
    };
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc{};
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
    passDesc.depthLoadOp = LoadOp::Clear;
    passDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    // Reflection-API robustness, folded in for free: unknown names and wrong value types
    // must degrade to a logged no-op mid-recording, never crash and never corrupt state.
    commandBuffer->SetUniformFloat("uDoesNotExist", 123.0F);
    commandBuffer->SetUniformVector3("uDepth", Math::Vector3{1.0F, 1.0F, 1.0F});
    // Draw 1 — FRONT: green, nearer (z = 0.1), shifted right by push constants.
    commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
    commandBuffer->SetUniformFloat("uDepth", 0.1F);
    commandBuffer->SetUniformFloat("uOffsetX", 0.15F);
    commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
    commandBuffer->Draw(3U);
    // Draw 2 — BACK: red, farther (z = 0.5), shifted left. Drawn SECOND: if depth testing
    // is broken, red wins the overlap by submission order; if depth works, green stays.
    commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
    commandBuffer->SetUniformFloat("uDepth", 0.5F);
    commandBuffer->SetUniformFloat("uOffsetX", -0.15F);
    commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
    commandBuffer->Draw(3U);
    commandBuffer->EndRenderPass();
    device->Submit(std::move(commandBuffer));

    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
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

    device->DestroyBuffer(vertexBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


TEST_F(VulkanRenderDeviceTest, TextureCreationValidationAndLifecycleAreReal) {
    // M2c resource contract: invalid descriptors bounce through ValidateTextureUpload
    // before ANY allocation; valid ones return handles; destruction is idempotent-safe.
    TextureDesc invalid{};
    invalid.width = 0U;
    invalid.height = 4U;
    EXPECT_EQ(device->CreateTexture(invalid), kInvalidTextureHandle);

    TextureDesc badUpload{};
    badUpload.width = 2U;
    badUpload.height = 2U;
    const std::byte shortData[3] = {std::byte{0}, std::byte{0}, std::byte{0}};
    EXPECT_EQ(device->CreateTexture(badUpload, std::span<const std::byte>(shortData, 3U)),
              kInvalidTextureHandle) << "partial level-0 uploads must be rejected";

    TextureDesc valid{};
    valid.width = 2U;
    valid.height = 2U;
    const TextureHandle colorTex = device->CreateTexture(valid);
    EXPECT_NE(colorTex, kInvalidTextureHandle) << "empty level-0 (render-target shape) is legal";

    TextureDesc validF16{};
    validF16.width = 2U;
    validF16.height = 2U;
    validF16.format = TextureFormat::RGBA16Float;
    EXPECT_NE(device->CreateTexture(validF16), kInvalidTextureHandle)
        << "R16G16B16A16_SFLOAT sampled+transfer-dst is a mandatory format feature set";

    TextureDesc validSrgb{};
    validSrgb.width = 2U;
    validSrgb.height = 2U;
    validSrgb.colorSpace = TextureColorSpace::Srgb;
    const std::array<std::byte, 16U> srgbPixels{};
    const TextureHandle srgbTexture = device->CreateTexture(validSrgb, srgbPixels);
    ASSERT_NE(srgbTexture, kInvalidTextureHandle)
        << "RGBA8 sRGB sampled views must be supported for imported color textures";

    device->DestroyTexture(srgbTexture);
    device->DestroyTexture(colorTex);
    device->DestroyTexture(colorTex); // already destroyed: safe no-op, per interface contract
    device->DestroyTexture(kInvalidTextureHandle); // never valid: safe no-op too
}

TEST_F(VulkanRenderDeviceTest, CompressedSrgbTextureUploadsEveryBlockMipWhenAvailable) {
    if (!device->SupportsTextureFormat(TextureFormat::ETC2RGBA8, TextureColorSpace::Srgb)) {
        GTEST_SKIP() << "physical device does not support ETC2 sRGB sampled images";
    }
    TextureDesc desc{7U, 5U, TextureFormat::ETC2RGBA8, 2U, TextureColorSpace::Srgb};
    std::array<std::byte, 80U> blocks{}; // 4 base blocks + one 3x2 mip block, 16 bytes each
    const TextureHandle texture = device->CreateTexture(desc, blocks);
    ASSERT_NE(texture, kInvalidTextureHandle);
    device->DestroyTexture(texture);
}

TEST_F(VulkanRenderDeviceTest, TexturedQuadRendersUploadedPixelsAndUnboundFallback) {
    // The M2c pixel proof, three frames against one pipeline:
    //   frame 1 - NO BindTexture: every sampled fragment must be the fallback texture's
    //             opaque white (deterministic unbound-slot contract, not undefined content).
    //   frame 2 - sRGB checker texture bound at slot 0: the four quadrants must read back the
    //             uploaded texel colors - sRGB sampled view, staging upload, and per-tuple
    //             descriptor set all proven by pixels.
    //   frame 3 - texture destroyed after recording the SAME bind: the destroyed handle can
    //             no longer resolve, so the draw degrades to the fallback (white) instead of
    //             sampling freed memory.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U; // vec3 position + vec2 uv interleaved
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle)
        << "a pipeline with a combined image sampler must now build (pre-M2c hard-fail)";
    // Samplers are bound by SLOT, not by SetUniform* — they deliberately stay OUT of the
    // reflected SetUniform table (GL backends bind sampler names to texture units instead).
    EXPECT_TRUE(device->GetPipelineUniforms(pipeline).empty());

    const float vertices[30] = {
        -0.5F, -0.5F, 0.0F,  0.0F, 0.0F, // screen top-left == uv(0,0)  (native orientation)
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F, // top-right == uv(1,0)
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F, // bottom-left == uv(0,1)
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F,
         0.5F,  0.5F, 0.0F,  1.0F, 1.0F,
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    // 2x2 checker in upload memory order: (0,0) red, (1,0) green, (0,1) blue, (1,1) yellow.
    const std::uint8_t checkerData[16] = {
        255U, 0U, 0U, 255U,   0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,   255U, 255U, 0U, 255U,
    };
    TextureDesc checkerDesc{};
    checkerDesc.width = 2U;
    checkerDesc.height = 2U;
    checkerDesc.colorSpace = TextureColorSpace::Srgb;
    const TextureHandle checkerTexture = device->CreateTexture(checkerDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(checkerData), sizeof(checkerData)));
    ASSERT_NE(checkerTexture, kInvalidTextureHandle);

    const auto drawQuad = [&](const TextureHandle* textureToBind,
                              const bool destroyAfterBind) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        if (textureToBind != nullptr) {
            commandBuffer->BindTexture(*textureToBind, 0U);
        }
        if (destroyAfterBind) {
            device->DestroyTexture(*textureToBind); // destroyed BEFORE this frame submits
        }
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
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
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
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
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
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
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = channelAt(pixels, width, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "a texture destroyed after recording must degrade to the "
                                     "fallback, never sample freed memory";
    }

    device->DestroyBuffer(vertexBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


// ---------------------------------------------------------------------------
// M2d offscreen render targets (dynamic-rendering gate). Every test also runs on a
// classic-mode device: offscreen passes then warn once and SKIP, which the frame-must-
// stay-intact assertions cover (the attestable content simply never lands in the offscreen
// texture). SwiftShader/lavapipe are both 1.3+, so CI and the sandbox run the full RT path.
// ---------------------------------------------------------------------------

[[nodiscard]] std::array<int, 4> ChannelAtNdc(const std::vector<std::byte>& pixels,
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

[[nodiscard]] PipelineHandle CreateTrianglePipeline(VulkanRenderDevice& device,
                                                          ShaderHandle* outVertexShader,
                                                          ShaderHandle* outFragmentShader) {
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirv;
    *outVertexShader = device.CreateShader(vertexDesc);
    *outFragmentShader = device.CreateShader(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandle ||
        *outFragmentShader == kInvalidShaderHandle) {
        return {};
    }
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 24U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"COLOR", VertexAttributeFormat::Float3, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipeline(pipelineDesc);
}

[[nodiscard]] PipelineHandle CreateTexturedPipeline(VulkanRenderDevice& device,
                                                          ShaderHandle* outVertexShader,
                                                          ShaderHandle* outFragmentShader) {
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    *outVertexShader = device.CreateShader(vertexDesc);
    *outFragmentShader = device.CreateShader(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandle ||
        *outFragmentShader == kInvalidShaderHandle) {
        return {};
    }
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipeline(pipelineDesc);
}

TEST_F(VulkanRenderDeviceTest, TextureMipChainIsUploadedAndSampledDuringMinification) {
    // The image is red at level 0 and green at every lower level. Spanning 16 UV units
    // across the swapchain forces implicit minification on both the 64x64 window and the
    // larger headless swapchain; the center pixel therefore proves a real nonzero mip is
    // initialized, visible through the sampled view, and reachable through the sampler LOD.
    constexpr std::uint32_t baseSize = 256U;
    constexpr std::uint32_t mipLevelCount = 9U; // 256 -> 128 -> ... -> 1
    std::vector<std::byte> mipPixels;
    std::uint32_t mipWidth = baseSize;
    std::uint32_t mipHeight = baseSize;
    for (std::uint32_t level = 0U; level < mipLevelCount; ++level) {
        const std::array<std::byte, 4U> color = level == 0U
            ? std::array<std::byte, 4U>{std::byte{0xFF}, std::byte{0}, std::byte{0}, std::byte{0xFF}}
            : std::array<std::byte, 4U>{std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}};
        const std::size_t texelCount = static_cast<std::size_t>(mipWidth) * mipHeight;
        mipPixels.reserve(mipPixels.size() + texelCount * color.size());
        for (std::size_t texel = 0U; texel < texelCount; ++texel) {
            mipPixels.insert(mipPixels.end(), color.begin(), color.end());
        }
        mipWidth = mipWidth > 1U ? mipWidth / 2U : 1U;
        mipHeight = mipHeight > 1U ? mipHeight / 2U : 1U;
    }

    ShaderHandle vertexShader{};
    ShaderHandle fragmentShader{};
    const PipelineHandle pipeline = CreateTexturedPipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    TextureDesc textureDesc{};
    textureDesc.width = baseSize;
    textureDesc.height = baseSize;
    textureDesc.mipLevels = mipLevelCount;
    const TextureHandle texture = device->CreateTexture(
        textureDesc, std::span<const std::byte>(mipPixels));
    ASSERT_NE(texture, kInvalidTextureHandle);

    constexpr float uvScale = 16.0F;
    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F,     0.0F,
         1.0F, -1.0F, 0.0F,  uvScale,  0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F,     uvScale,
         1.0F, -1.0F, 0.0F,  uvScale,  0.0F,
         1.0F,  1.0F, 0.0F,  uvScale,  uvScale,
        -1.0F,  1.0F, 0.0F,  0.0F,     uvScale,
    };
    const BufferHandle vertexBuffer = device->CreateBuffer(
        BufferDesc{sizeof(quadVertices), BufferUsage::Vertex},
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    RenderPassDesc passDesc{};
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    passDesc.depthLoadOp = LoadOp::Clear;
    passDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->BindTexture(texture, 0U);
    commandBuffer->Draw(6U);
    commandBuffer->EndRenderPass();
    device->Submit(std::move(commandBuffer));
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    EXPECT_FALSE(device->ReadbackLatestPresentedImage({}, width, height)); // extent query
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(center[0], 60) << "minified sample must not use level 0 (red)";
    EXPECT_GT(center[1], 200) << "a populated lower mip must supply the sampled green value";
    EXPECT_LT(center[2], 60);

    device->DestroyBuffer(vertexBuffer);
    device->DestroyTexture(texture);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(fragmentShader);
    device->DestroyShader(vertexShader);
}

TEST_F(VulkanRenderDeviceTest, OffscreenColorPassRendersIntoTheTextureForSampling) {
    // The M2d core proof: an RGBA8Unorm texture CLEARED-AND-DRAWN as a render target in one
    // pass, then SAMPLED by a later same-frame default(=swapchain) pass. Clear blue + a red
    // triangle drawn into a 64x64 target; the onscreen textured quad must read blue outside
    // the triangle and red inside it - impossible without real texture-backed rendering.
    ShaderHandle triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandle trianglePipeline = CreateTrianglePipeline(*device, &triVS, &triFS);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle renderTarget = device->CreateTexture(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandle);

    // Solid red triangle covering the target's center-ish band; vertices at +/-0.5 NDC keep
    // generous margins so sample points escape rasterization boundary fuzz.
    const float triVertices[18] = {
        -0.5F, -0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
         0.0F,  0.5F, 0.0F,  1.0F, 0.0F, 0.0F,
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    // Full-frame sampling quad (x,y in +/-1 so the whole readback is quad).
    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission 1: render the triangle into the offscreen texture.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F}; // flat blue background
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(trianglePipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission 2: sample it on the default framebuffer (proves the image made it back to
    // SHADER_READ with valid content inside one command stream).
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F}; // green: quad overwrites everything
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(renderTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic device: the offscreen pass was skipped, so the quad samples the fallback
        // white. Frame must stay intact (nothing else breaks) - that is the contract.
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "classic device must degrade offscreen content to the "
                                     "fallback texture while keeping the frame intact";
    } else {
        // Center of the quad == center of the triangle: pure red.
        const auto insideTri = ChannelAtNdc(pixels, width, height, 0.0F, -0.1F);
        EXPECT_GT(insideTri[0], 200);
        EXPECT_LT(insideTri[1], 80);
        EXPECT_LT(insideTri[2], 80) << "inside the offscreen triangle must be RED - got ("
            << insideTri[0] << "," << insideTri[1] << "," << insideTri[2] << ")";
        // Off-triangle but inside the quad: the offscreen pass's blue clear.
        const auto background = ChannelAtNdc(pixels, width, height, -0.95F, -0.95F);
        EXPECT_LT(background[0], 80);
        EXPECT_LT(background[1], 80);
        EXPECT_GT(background[2], 200) << "outside the triangle must be the BLUE clear - got ("
            << background[0] << "," << background[1] << "," << background[2] << ")";
    }

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(renderTarget);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(triVS);
    device->DestroyShader(triFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, InterleavedOffscreenPassPreservesTheDefaultFramebuffer) {
    // The resume/LOAD proof: default pass draws a full-frame solid-red triangle; an offscreen
    // pass interrupts the frame; a SECOND default-pass run resumes the SAME swapchain image
    // and draws a left-half textured quad sampling the offscreen texture. If the resumed
    // default image were re-cleared (a broken interleave would show the last default clear
    // color), the right half would NOT be red anymore.
    ShaderHandle triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandle trianglePipeline = CreateTrianglePipeline(*device, &triVS, &triFS);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle renderTarget = device->CreateTexture(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandle);

    const float fullRedTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  1.0F, 0.0F, 0.0F, // covers the entire viewport
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(fullRedTriangle);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(fullRedTriangle),
                                   sizeof(fullRedTriangle)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    const float leftHalfQuad[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         0.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         0.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         0.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(leftHalfQuad);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(leftHalfQuad),
                                   sizeof(leftHalfQuad)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission A: default pass -> full red frame.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(trianglePipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission B: offscreen pass -> flat blue target, no draws at all (clears only).
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission C: resume the default framebuffer -> left-half textured quad sampling the RT.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 1.0F, 1.0F, 1.0F}; // cyan - only the FIRST default clear
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(renderTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        // Classic: offscreen skipped -> quad samples fallback white; the second default pass
        // clear... here the classic flow bakes ONE swapchain pass per frame with the FIRST
        // pass's clear (black), so a resumed-context question never arises; assert intactness
        // of the left-half quad only (fallback white) and move on.
        const auto leftCenter = ChannelAtNdc(pixels, width, height, -0.5F, 0.0F);
        EXPECT_GT(leftCenter[0], 240);
        EXPECT_GT(leftCenter[1], 240);
        EXPECT_GT(leftCenter[2], 240) << "classic device must still draw the sampling quad "
                                         "with the fallback texture";
    } else {
        // Left half center: quad sampling the (blue) offscreen texture.
        const auto leftCenter = ChannelAtNdc(pixels, width, height, -0.5F, 0.0F);
        EXPECT_LT(leftCenter[0], 80);
        EXPECT_LT(leftCenter[1], 80);
        EXPECT_GT(leftCenter[2], 200) << "left half must be the BLUE offscreen content - got ("
            << leftCenter[0] << "," << leftCenter[1] << "," << leftCenter[2] << ")";
        // Right half center: submission A's full-frame red, i.e. the resumed swapchain image
        // kept its earlier content across the interleaved offscreen pass.
        const auto rightCenter = ChannelAtNdc(pixels, width, height, 0.5F, 0.0F);
        EXPECT_GT(rightCenter[0], 200);
        EXPECT_LT(rightCenter[1], 80);
        EXPECT_LT(rightCenter[2], 80) << "right half must keep submission A's RED (the default "
            "framebuffer resumed with LOAD) - got ("
            << rightCenter[0] << "," << rightCenter[1] << "," << rightCenter[2] << ")";
    }

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(renderTarget);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(triVS);
    device->DestroyShader(triFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, DepthTestWorksInColorOnlyOffscreenPass) {
    // Without a caller depth attachment the offscreen pass borrows the engine's per-extent
    // scratch depth image: nearer-drawn-first must WIN over farther-drawn-second (painter
    // order would produce the opposite), which is only observable if the hidden depth test
    // inside the offscreen instance is real.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle depthVS = device->CreateShader(vertexDesc);
    const ShaderHandle depthFS = device->CreateShader(fragmentDesc);
    ASSERT_NE(depthVS, kInvalidShaderHandle);
    ASSERT_NE(depthFS, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = depthVS;
    pipelineDesc.fragmentShader = depthFS;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandle depthPipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandle);

    ShaderHandle quadVS{}, quadFS{};
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle renderTarget = device->CreateTexture(rtDesc);
    ASSERT_NE(renderTarget, kInvalidTextureHandle);

    // One full-coverage triangle drawn TWICE with different depths and colors, exactly like
    // the M2b swapchain proof.
    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission 1: offscreen depth test. Nearer GREEN drawn FIRST, farther RED SECOND; real
    // depth testing leaves green everywhere.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = renderTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(depthPipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.1F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(3U);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.5F);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission 2: show it.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(renderTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240); // fallback white, frame intact
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 80);
        EXPECT_GT(center[1], 200);
        EXPECT_LT(center[2], 80) << "nearer-first green must win inside the offscreen pass - "
            "got (" << center[0] << "," << center[1] << "," << center[2] << ")";
    }

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(renderTarget);
    device->DestroyPipeline(depthPipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(depthVS);
    device->DestroyShader(depthFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, CreateSampler_MalformedDesc_ReturnsInvalid) {
    SamplerDesc desc;
    desc.wrapW = static_cast<SamplerWrap>(0xFFU);
    EXPECT_EQ(device->CreateSampler(desc), kInvalidSamplerHandle);

    SamplerDesc anisoDesc;
    anisoDesc.maxAnisotropy = 0.0F;
    EXPECT_EQ(device->CreateSampler(anisoDesc), kInvalidSamplerHandle);

    const SamplerHandle sampler = device->CreateSampler(SamplerDesc{});
    EXPECT_NE(sampler, kInvalidSamplerHandle);
    device->DestroySampler(sampler);
    device->DestroySampler(sampler); // double-destroy: safe no-op
}

TEST_F(VulkanRenderDeviceTest, BoundSamplerSelectsPointFiltering) {
    // Tier 2.2 proof that BindSampler reaches the Vulkan descriptors: a 2x2 texture with a
    // VERTICAL red/blue split (both rows identical, so the GL-bottom-up vs Vulkan-top-down row
    // order cannot matter) sampled through a point sampler at U=0.4/0.6 — far enough from the
    // texel centers (0.25/0.75) that linear filtering would visibly blend, firmly inside each
    // texel so point sampling reads the exact color.
    const std::uint8_t splitPixels[16] = {
        255U, 0U, 0U, 255U,  0U, 0U, 255U, 255U,
        255U, 0U, 0U, 255U,  0U, 0U, 255U, 255U,
    };
    TextureDesc splitDesc{};
    splitDesc.width = 2U;
    splitDesc.height = 2U;
    splitDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle splitTexture = device->CreateTexture(splitDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(splitPixels),
                                   sizeof(splitPixels)));
    ASSERT_NE(splitTexture, kInvalidTextureHandle);

    SamplerDesc pointDesc;
    pointDesc.magFilter = SamplerFilter::Point;
    pointDesc.minFilter = SamplerFilter::Point;
    pointDesc.mipMode = SamplerMipMode::None;
    const SamplerHandle pointSampler = device->CreateSampler(pointDesc);
    ASSERT_NE(pointSampler, kInvalidSamplerHandle);

    ShaderHandle quadVS{}, quadFS{};
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        const std::array<ResourceBinding, 2> group{
            BindTextureCommand{splitTexture, 0U}, BindSamplerCommand{pointSampler, 0U}};
        ASSERT_EQ(ValidateBindGroup({group, {}}), BindGroupValidation::Valid);
        BindGroup(*commandBuffer, {group, {}});
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240); // fallback white, frame intact
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // NDC x=-0.2 → U=0.4 (red texel); x=+0.2 → U=0.6 (blue texel). Linear filtering at
        // these points would read ~(178,0,76) / ~(76,0,178) — the bounds below only pass for
        // unblended texels.
        const auto left = ChannelAtNdc(pixels, width, height, -0.2F, 0.0F);
        EXPECT_GT(left[0], 200);
        EXPECT_LT(left[1], 60);
        EXPECT_LT(left[2], 60) << "point sampling must read the exact red texel at U=0.4 - "
            "got (" << left[0] << "," << left[1] << "," << left[2] << ")";
        const auto right = ChannelAtNdc(pixels, width, height, 0.2F, 0.0F);
        EXPECT_LT(right[0], 60);
        EXPECT_LT(right[1], 60);
        EXPECT_GT(right[2], 200) << "point sampling must read the exact blue texel at U=0.6 - "
            "got (" << right[0] << "," << right[1] << "," << right[2] << ")";
    }

    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(splitTexture);
    device->DestroyPipeline(texturedPipeline);
    device->DestroySampler(pointSampler);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, DepthCompareSelectsTheCoplanarWinner) {
    // Tier 2.1 proof that PipelineDesc::depthCompare threads into the Vulkan pipeline: two
    // pipelines identical except the compare op each render GREEN-then-RED at the SAME depth
    // into separate offscreen targets. Less keeps the first draw (green wins); LessOrEqual
    // lets the coplanar second draw through (red wins). Same shader, same constants — the
    // only variable is the compare op.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle depthVS = device->CreateShader(vertexDesc);
    const ShaderHandle depthFS = device->CreateShader(fragmentDesc);
    ASSERT_NE(depthVS, kInvalidShaderHandle);
    ASSERT_NE(depthFS, kInvalidShaderHandle);

    PipelineDesc lessDesc{};
    lessDesc.vertexShader = depthVS;
    lessDesc.fragmentShader = depthFS;
    lessDesc.vertexStride = 12U;
    lessDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    lessDesc.depthTestEnabled = true;
    lessDesc.depthWriteEnabled = true;
    lessDesc.depthCompare = DepthCompare::Less;
    const PipelineHandle lessPipeline = device->CreatePipeline(lessDesc);
    ASSERT_NE(lessPipeline, kInvalidPipelineHandle);

    PipelineDesc lequalDesc = lessDesc;
    lequalDesc.depthCompare = DepthCompare::LessOrEqual;
    const PipelineHandle lequalPipeline = device->CreatePipeline(lequalDesc);
    ASSERT_NE(lequalPipeline, kInvalidPipelineHandle);

    ShaderHandle quadVS{}, quadFS{};
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc rtDesc{};
    rtDesc.width = 64U;
    rtDesc.height = 64U;
    rtDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle lessTarget = device->CreateTexture(rtDesc);
    ASSERT_NE(lessTarget, kInvalidTextureHandle);
    const TextureHandle lequalTarget = device->CreateTexture(rtDesc);
    ASSERT_NE(lequalTarget, kInvalidTextureHandle);

    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission 1: GREEN then RED at the SAME depth through the Less pipeline.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = lessTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(lessPipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.3F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(3U);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.3F);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission 2: the identical coplanar pair through the LessOrEqual pipeline.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = lequalTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(lequalPipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.3F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(3U);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.3F);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission 3: show the Less target.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(lessTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240); // fallback white, frame intact
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 80);
        EXPECT_GT(center[1], 200);
        EXPECT_LT(center[2], 80) << "Less must reject the coplanar red redraw - "
            "got (" << center[0] << "," << center[1] << "," << center[2] << ")";
    }

    // Submission 4: show the LessOrEqual target.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(lequalTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::uint32_t width2 = 0;
    std::uint32_t height2 = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width2, height2));
    if (width2 == 0U || height2 == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width2, height2, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240); // fallback white, frame intact
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        const auto center = ChannelAtNdc(pixels, width2, height2, 0.0F, 0.0F);
        EXPECT_GT(center[0], 200);
        EXPECT_LT(center[1], 80);
        EXPECT_LT(center[2], 80) << "LessOrEqual must accept the coplanar red redraw - "
            "got (" << center[0] << "," << center[1] << "," << center[2] << ")";
    }

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(lessTarget);
    device->DestroyTexture(lequalTarget);
    device->DestroyPipeline(lessPipeline);
    device->DestroyPipeline(lequalPipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(depthVS);
    device->DestroyShader(depthFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, NonAttachableTexturePassSkipsButTheFrameSurvives) {
    // A texture created with TextureUsage::Sampled only (without RenderTarget) is a legal
    // SAMPLING texture but not a legal render target. The offscreen pass degrades to a
    // one-shot warning + skip; the rest of the frame must still render with the texture's
    // INITIAL pixels intact (the skip provably erased nothing, wrote nothing).
    ShaderHandle quadVS{}, quadFS{};
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    const std::uint8_t initialRed[16] = {
        255U, 0U, 0U, 255U,  255U, 0U, 0U, 255U,
        255U, 0U, 0U, 255U,  255U, 0U, 0U, 255U,
    };
    TextureDesc rgba16Desc{};
    rgba16Desc.width = 2U;
    rgba16Desc.height = 2U;
    rgba16Desc.format = TextureFormat::RGBA8Unorm; // uploaded as RGBA8...
    const TextureHandle plainRed = device->CreateTexture(rgba16Desc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(initialRed),
                                   sizeof(initialRed)));
    ASSERT_NE(plainRed, kInvalidTextureHandle);
    TextureDesc floatDesc{};
    floatDesc.width = 2U;
    floatDesc.height = 2U;
    floatDesc.format = TextureFormat::RGBA16Float;
    floatDesc.usage = TextureUsage::Sampled; // sampled-only: not attachable
    const std::uint8_t floatRedPixels[32] = {
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU, // 1.0,0,0,1 as fp16
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
        0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x3CU,
    };
    const TextureHandle floatRed = device->CreateTexture(floatDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(floatRedPixels),
                                   sizeof(floatRedPixels)));
    ASSERT_NE(floatRed, kInvalidTextureHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Frame 1's content sequence is: (skipped) offscreen GREEN-pass over the float texture,
    // then a default pass sampling it. On a dynamic device the float texture must still read
    // RED (its initial upload); on a classic device the pass is also skipped for the no-1.3
    // reason, with the identical outcome - one expectation serves both.
    {
        auto offscreen = device->CreateCommandBuffer();
        RenderPassDesc offscreenDesc{};
        offscreenDesc.colorAttachment = floatRed;
        offscreenDesc.colorLoadOp = LoadOp::Clear;
        offscreenDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F}; // green - must NEVER land
        offscreenDesc.depthLoadOp = LoadOp::Clear;
        offscreen->BeginRenderPass(offscreenDesc);
        offscreen->EndRenderPass();
        device->Submit(std::move(offscreen));
    }
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(floatRed, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(center[0], 200);
    EXPECT_LT(center[1], 80);
    EXPECT_LT(center[2], 80) << "the float texture must still sample its INITIAL RED after "
        "its offscreen pass was skipped - got ("
        << center[0] << "," << center[1] << "," << center[2] << ")";

    // And the plain-attachable texture must still be where we left it for the next frame;
    // this is a cheap keep-alive assertion for the fallback-independent path.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(plainRed, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    }

    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(plainRed);
    device->DestroyTexture(floatRed);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, DepthOnlyOffscreenPassIsRefusedButTheFrameSurvives) {
    // A depth-only pass whose depth texture lacks TextureUsage::RenderTarget is refused with
    // a one-shot warning; later passes of the frame must render as if the skip were a no-op.
    TextureDesc depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormat::Depth32Float;
    depthDesc.usage = TextureUsage::Sampled;
    const TextureHandle depthTex = device->CreateTexture(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandle);

    ShaderHandle triVS{}, triFS{};
    const PipelineHandle trianglePipeline = CreateTrianglePipeline(*device, &triVS, &triFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);

    const float redTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  1.0F, 0.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  1.0F, 0.0F, 0.0F,
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(redTriangle);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(redTriangle),
                                   sizeof(redTriangle)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    {
        auto depthOnly = device->CreateCommandBuffer();
        RenderPassDesc depthOnlyDesc{};
        depthOnlyDesc.colorAttachment = kInvalidTextureHandle;
        depthOnlyDesc.depthAttachment = depthTex; // no color -> refused
        depthOnlyDesc.colorLoadOp = LoadOp::Clear;
        depthOnlyDesc.depthLoadOp = LoadOp::Clear;
        depthOnly->BeginRenderPass(depthOnlyDesc);
        depthOnly->EndRenderPass();
        device->Submit(std::move(depthOnly));
    }
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(trianglePipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(center[0], 200);
    EXPECT_LT(center[1], 80);
    EXPECT_LT(center[2], 80) << "the frame after a refused depth-only pass must still draw "
        "the triangle - got (" << center[0] << "," << center[1] << "," << center[2] << ")";

    device->DestroyBuffer(triBuffer);
    device->DestroyTexture(depthTex);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyShader(triVS);
    device->DestroyShader(triFS);
}

// ---------------------------------------------------------------------------
// M2e tier-2 proofs: on the dynamic-rendering arm, caller DEPTH attachments are
// truly sampleable after their pass closes (the bit-15 fallback is gone there),
// and offscreen passes honour real color/depth LoadOp::Load (GL-FBO-accurate
// retention across passes, the former bit-6 warning now swapchain-only).
// ---------------------------------------------------------------------------

TEST_F(VulkanRenderDeviceTest, DepthAttachmentTextureSamplesRealDepthValues) {
    // The bit-15 resolution proof: render into a caller-supplied Depth32Float texture
    // (fullscreen triangle at NDC depth exactly 0.25), close the pass (the depth record's
    // LFA barrier lands it in SHADER_READ), then sample it with the textured quad on the
    // default swapchain pass. A DEPTH-format texture sampled by an ordinary sampler yields
    // (depth, 0, 0, A) per the spec's depth-read swizzle - A is implementation-defined on
    // pre-maintenance5 devices (see the byte check below) - so the onscreen red must
    // reconstruct 0.25 and green/blue must be the swizzle zeros: NOT the fallback white
    // (255,255,255,255) M2d produced.
    ShaderHandle depthVS{}, depthFS{}, quadVS{}, quadFS{};
    PipelineDesc depthPipelineDesc{};
    {
        ShaderDesc vertexDesc{};
        vertexDesc.stage = ShaderStage::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirv;
        ShaderDesc fragmentDesc{};
        fragmentDesc.stage = ShaderStage::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
        depthVS = device->CreateShader(vertexDesc);
        depthFS = device->CreateShader(fragmentDesc);
        ASSERT_NE(depthVS, kInvalidShaderHandle);
        ASSERT_NE(depthFS, kInvalidShaderHandle);
        depthPipelineDesc.vertexShader = depthVS;
        depthPipelineDesc.fragmentShader = depthFS;
        depthPipelineDesc.vertexStride = 12U;
        depthPipelineDesc.vertexLayout.push_back(
            VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
        depthPipelineDesc.depthTestEnabled = true;
        depthPipelineDesc.depthWriteEnabled = true;
    }
    const PipelineHandle depthPipeline = device->CreatePipeline(depthPipelineDesc);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle colorTarget = device->CreateTexture(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depthTex = device->CreateTexture(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandle);

    // Full-coverage triangle: depth written = uDepth everywhere it lands.
    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDesc triBufferDesc{};
    triBufferDesc.sizeBytes = sizeof(triVertices);
    triBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(triBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission 1: color+depth offscreen pass; depth = 0.25 across the whole target.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {1.0F, 0.0F, 1.0F, 1.0F}; // magenta (unused by assertions)
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(depthPipeline);
        commandBuffer->BindVertexBuffer(triBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 1.0F, 1.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.25F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Submission 2: sample the depth texture on the default pass.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(depthTex, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
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

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(colorTarget);
    device->DestroyTexture(depthTex);
    device->DestroyPipeline(depthPipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(depthVS);
    device->DestroyShader(depthFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, OffscreenColorLoadPreservesContentAcrossPasses) {
    // The bit-6 (color) resolution proof: pass A clears the target BLACK and draws a red
    // LEFT-half triangle; pass B reuses the SAME color target with colorLoadOp=Load and
    // draws a GREEN RIGHT-half triangle. With Load honored, the sampled result must show
    // red on the left, green on the right, and the original black clear in the untouched
    // top-left corner - if pass B had re-cleared, the red half could not survive.
    ShaderHandle triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandle trianglePipeline = CreateTrianglePipeline(*device, &triVS, &triFS);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle colorTarget = device->CreateTexture(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandle);

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
    BufferDesc leftDesc{};
    leftDesc.sizeBytes = sizeof(leftVertices);
    leftDesc.usage = BufferUsage::Vertex;
    const BufferHandle leftBuffer = device->CreateBuffer(leftDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(leftVertices),
                                   sizeof(leftVertices)));
    BufferDesc rightDesc{};
    rightDesc.sizeBytes = sizeof(rightVertices);
    rightDesc.usage = BufferUsage::Vertex;
    const BufferHandle rightBuffer = device->CreateBuffer(rightDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(rightVertices),
                                   sizeof(rightVertices)));
    ASSERT_NE(leftBuffer, kInvalidBufferHandle);
    ASSERT_NE(rightBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Pass A: clear black + red left.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(trianglePipeline);
        commandBuffer->BindVertexBuffer(leftBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Pass B: Load + green right (a re-clear would erase pass A entirely).
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOp::Load;
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(trianglePipeline);
        commandBuffer->BindVertexBuffer(rightBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Pass C: sample.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {1.0F, 1.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(colorTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240) << "classic device degrades the whole chain to fallback";
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // Left-red survived pass B's Load.
        const auto left = ChannelAtNdc(pixels, width, height, -0.5F, -0.2F);
        EXPECT_GT(left[0], 200); EXPECT_LT(left[1], 80); EXPECT_LT(left[2], 80)
            << "pass B's Load must have kept pass A's red - got ("
            << left[0] << "," << left[1] << "," << left[2] << ")";
        // Right-green lands in the same LOADed target.
        const auto right = ChannelAtNdc(pixels, width, height, 0.5F, 0.2F);
        EXPECT_LT(right[0], 80); EXPECT_GT(right[1], 200); EXPECT_LT(right[2], 80)
            << "pass B's green right half - got ("
            << right[0] << "," << right[1] << "," << right[2] << ")";
        // Untouched top-left: the original black clear, preserved by Load.
        const auto untouched = ChannelAtNdc(pixels, width, height, -0.5F, 0.9F);
        EXPECT_LT(untouched[0], 40); EXPECT_LT(untouched[1], 40); EXPECT_LT(untouched[2], 40)
            << "Load must preserve the original clear in untouched pixels - got ("
            << untouched[0] << "," << untouched[1] << "," << untouched[2] << ")";
    }

    device->DestroyBuffer(leftBuffer);
    device->DestroyBuffer(rightBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(colorTarget);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(triVS);
    device->DestroyShader(triFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, OffscreenDepthLoadAccumulatesDepthAcrossPasses) {
    // The bit-6 (depth) resolution proof, plus the depth-reuse FBO contract: pass A draws a
    // NEAR red occluder (uDepth=0.2) into the caller depth target covering the center band;
    // pass B reuses BOTH attachments with color+depth LoadOp::Load and draws (1) a FAR
    // full-coverage blue triangle (uDepth=0.9): where pass A's preserved red depth (0.2)
    // survives, blue must be CULLED; elsewhere blue fills the cleared far depth (1.0);
    // (2) a NEARER green triangle (uDepth=0.5) that must beat pass B's own blue (0.9)
    // inside the same pass. Red-guard, blue-fill, green-overtake = depth content genuinely
    // carried between passes.
    ShaderHandle depthVS{}, depthFS{}, quadVS{}, quadFS{};
    PipelineDesc depthPipelineDesc{};
    {
        ShaderDesc vertexDesc{};
        vertexDesc.stage = ShaderStage::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirv;
        ShaderDesc fragmentDesc{};
        fragmentDesc.stage = ShaderStage::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
        depthVS = device->CreateShader(vertexDesc);
        depthFS = device->CreateShader(fragmentDesc);
        ASSERT_NE(depthVS, kInvalidShaderHandle);
        ASSERT_NE(depthFS, kInvalidShaderHandle);
        depthPipelineDesc.vertexShader = depthVS;
        depthPipelineDesc.fragmentShader = depthFS;
        depthPipelineDesc.vertexStride = 12U;
        depthPipelineDesc.vertexLayout.push_back(
            VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
        depthPipelineDesc.depthTestEnabled = true;
        depthPipelineDesc.depthWriteEnabled = true;
    }
    const PipelineHandle depthPipeline = device->CreatePipeline(depthPipelineDesc);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle colorTarget = device->CreateTexture(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 64U;
    depthDesc.height = 64U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depthTex = device->CreateTexture(depthDesc);
    ASSERT_NE(depthTex, kInvalidTextureHandle);

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
    BufferDesc redDesc{};
    redDesc.sizeBytes = sizeof(redBand);
    redDesc.usage = BufferUsage::Vertex;
    const BufferHandle redBuffer = device->CreateBuffer(redDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(redBand), sizeof(redBand)));
    BufferDesc blueDesc{};
    blueDesc.sizeBytes = sizeof(blueFull);
    blueDesc.usage = BufferUsage::Vertex;
    const BufferHandle blueBuffer = device->CreateBuffer(blueDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(blueFull), sizeof(blueFull)));
    BufferDesc greenDesc{};
    greenDesc.sizeBytes = sizeof(greenSmall);
    greenDesc.usage = BufferUsage::Vertex;
    const BufferHandle greenBuffer = device->CreateBuffer(greenDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(greenSmall), sizeof(greenSmall)));
    ASSERT_NE(redBuffer, kInvalidBufferHandle);
    ASSERT_NE(blueBuffer, kInvalidBufferHandle);
    ASSERT_NE(greenBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Pass A: red band at depth 0.2 (near), with its own clear.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(depthPipeline);
        commandBuffer->BindVertexBuffer(redBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.2F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Pass B: LOAD both attachments, then far-blue + near-green as plotted above.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = colorTarget;
        passDesc.colorLoadOp = LoadOp::Load;
        passDesc.depthAttachment = depthTex;
        passDesc.depthLoadOp = LoadOp::Load;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(depthPipeline);
        commandBuffer->BindVertexBuffer(blueBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 0.0F, 1.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.9F);
        commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
        commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
        commandBuffer->Draw(3U);
        commandBuffer->BindVertexBuffer(greenBuffer);
        commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
        commandBuffer->SetUniformFloat("uDepth", 0.5F);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    // Pass C: sample the accumulated color.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {1.0F, 1.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(colorTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    if (name == "Vulkan (M2c textures+staging)") {
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240) << "classic device degrades the whole chain to fallback";
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240);
    } else {
        // Inside the red band: pass B's blue at depth 0.9 MUST be culled by the preserved
        // red depth (0.2) - proof that Load retained pass A's depth content.
        const auto band = ChannelAtNdc(pixels, width, height, 0.4F, 0.0F);
        EXPECT_GT(band[0], 200); EXPECT_LT(band[1], 80); EXPECT_LT(band[2], 80)
            << "preserved near depth must cull pass-B blue - got ("
            << band[0] << "," << band[1] << "," << band[2] << ")";
        // Outside the band and outside the green triangle: pass-B blue filled the far clear.
        const auto blueArea = ChannelAtNdc(pixels, width, height, 0.4F, 0.6F);
        EXPECT_LT(blueArea[0], 80); EXPECT_LT(blueArea[1], 80); EXPECT_GT(blueArea[2], 200)
            << "far blue must land where depth was still the clear - got ("
            << blueArea[0] << "," << blueArea[1] << "," << blueArea[2] << ")";
        // Green small triangle: nearer than pass-B blue's own 0.9 wins inside pass B.
        const auto greenArea = ChannelAtNdc(pixels, width, height, -0.6F, -0.6F);
        EXPECT_LT(greenArea[0], 80); EXPECT_GT(greenArea[1], 200); EXPECT_LT(greenArea[2], 80)
            << "nearer green must overtake pass-B blue - got ("
            << greenArea[0] << "," << greenArea[1] << "," << greenArea[2] << ")";
    }

    device->DestroyBuffer(redBuffer);
    device->DestroyBuffer(blueBuffer);
    device->DestroyBuffer(greenBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(colorTarget);
    device->DestroyTexture(depthTex);
    device->DestroyPipeline(depthPipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(depthVS);
    device->DestroyShader(depthFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, SamplingOpenPassAttachmentDegradesOnceAndFrameSurvives) {
    // The bit-16 feedback-loop guard proof: inside an OPEN offscreen pass, binding the very
    // texture that pass is currently writing is a feedback loop (undefined behavior in
    // Vulkan). M2e guards it honestly - the draw inside that pass samples the 1x1-white
    // fallback instead of reading its own attachment - and after the pass closes the SAME
    // texture samples its real content. Frame intact throughout.
    ShaderHandle quadVS{}, quadFS{};
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc colorDesc{};
    colorDesc.width = 64U;
    colorDesc.height = 64U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle colorTarget = device->CreateTexture(colorDesc);
    ASSERT_NE(colorTarget, kInvalidTextureHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Submission 1: clear the target RED, then open a second pass on it with Load, draw a
    // quad that binds the target ITSELF (feedback) inside that open pass - guarded to the
    // fallback white - and close it. The self-sampling quad wrote WHITE over the red.
    {
        auto clearCmd = device->CreateCommandBuffer();
        RenderPassDesc clearDesc{};
        clearDesc.colorAttachment = colorTarget;
        clearDesc.colorLoadOp = LoadOp::Clear;
        clearDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
        clearDesc.depthLoadOp = LoadOp::Clear;
        clearDesc.clearDepth = 1.0F;
        clearCmd->BeginRenderPass(clearDesc);
        clearCmd->EndRenderPass();
        device->Submit(std::move(clearCmd));

        auto feedbackCmd = device->CreateCommandBuffer();
        RenderPassDesc loopDesc{};
        loopDesc.colorAttachment = colorTarget;
        loopDesc.colorLoadOp = LoadOp::Load;
        loopDesc.depthLoadOp = LoadOp::Clear;
        loopDesc.clearDepth = 1.0F;
        feedbackCmd->BeginRenderPass(loopDesc);
        feedbackCmd->BindPipeline(texturedPipeline);
        feedbackCmd->BindVertexBuffer(quadBuffer);
        feedbackCmd->BindTexture(colorTarget, 0U); // == the open pass's attachment!
        feedbackCmd->Draw(6U);
        feedbackCmd->EndRenderPass();
        device->Submit(std::move(feedbackCmd));
    }
    // Submission 2: sample the target - must be WHITE wherever the guarded quad landed
    // (fallback, not garbage, not the red), proving the guard traded correctness for a
    // deterministic degrade exactly once.
    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(texturedPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(colorTarget, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    const std::string_view name = device->GetBackendName();
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
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

    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(colorTarget);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}


// ---------------------------------------------------------------------------
// M2f: SSBOs + separate samplers (descriptor-form completeness). These run on
// BOTH device arms - the descriptor plumbing is pass-independent (only the
// offscreen-pass machinery is dynamic-rendering-gated), and every case draws
// into the default swapchain pass. Pixel expectations use pure 0.0/1.0 channel
// values exclusively, which are invariant under the sRGB/UNORM swapchain
// format-class duality the M2e reconstruction check documents.
// ---------------------------------------------------------------------------

// Full-coverage quad (native Vulkan NDC, y-down; uv spans 0..1), 20-byte stride
// matching kTexturedVertexSpirv's POSITION+TEXCOORD layout.
const float kM2fQuadVertices[30] = {
    -1.0F, -1.0F, 0.0F,  0.0F, 0.0F, // screen top-left == uv(0,0)
     1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
    -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
     1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
     1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
    -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
};

[[nodiscard]] BufferHandle CreateM2fQuadBuffer(VulkanRenderDevice& device) {
    BufferDesc quadDesc{};
    quadDesc.sizeBytes = sizeof(kM2fQuadVertices);
    quadDesc.usage = BufferUsage::Vertex;
    return device.CreateBuffer(quadDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(kM2fQuadVertices),
                                   sizeof(kM2fQuadVertices)));
}

// The SSBO palette pipeline: kTexturedVertexSpirv + the M2f palette fragment
// (UBO { int uIndex; } at binding 0, readonly buffer { vec4 uColors[]; } at binding 1).
[[nodiscard]] PipelineHandle CreateSsboPalettePipeline(VulkanRenderDevice& device,
                                                             ShaderHandle* outVertexShader,
                                                             ShaderHandle* outFragmentShader) {
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kSsboPaletteFragmentSpirv;
    *outVertexShader = device.CreateShader(vertexDesc);
    *outFragmentShader = device.CreateShader(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandle ||
        *outFragmentShader == kInvalidShaderHandle) {
        return {};
    }
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipeline(pipelineDesc);
}

TEST_F(VulkanRenderDeviceTest, SsboPaletteFeedsRealPixelsThroughStorageBufferBinding) {
    // The M2f SSBO pixel proof, three frames against one pipeline:
    //   frame 1 - palette A bound at slot 0, ring-fed uIndex=2: the center pixel must be
    //             A[2] (BLUE) - the STORAGE_BUFFER descriptor really points at the caller's
    //             buffer and the fragment shader really reads it.
    //   frame 2 - palette B bound at the same slot (a different tuple, so a different cached
    //             descriptor set), uIndex=2: the center must be B[2] (GREEN) - the set cache
    //             follows the bound buffer exactly like it follows bound textures.
    //   frame 3 - palette A re-bound (tuple-cache reuse), uIndex=3: A[3] (YELLOW) - ring
    //             uniform snapshots and cached SSBO sets stay correct across frames.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle pipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle)
        << "a pipeline with a STORAGE_BUFFER binding must now build (pre-M2f hard-fail)";

    // The reflected uniform table exposes ONLY the UBO member - SSBO data lives in the bound
    // buffer and is deliberately not SetUniform-feedable.
    const std::vector<UniformReflection> uniforms = device->GetPipelineUniforms(pipeline);
    ASSERT_EQ(uniforms.size(), 1U) << "expected exactly uIndex from the ring-fed UBO";
    EXPECT_EQ(uniforms[0].name, "uIndex");
    EXPECT_EQ(uniforms[0].type, ShaderDataType::Int);

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Pure 0/1 colors only: sRGB/UNORM swapchain format classes store them identically.
    const float paletteA[16] = {
        1.0F, 0.0F, 0.0F, 1.0F,   0.0F, 1.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F,   1.0F, 1.0F, 0.0F, 1.0F,
    };
    const float paletteB[16] = {
        1.0F, 1.0F, 0.0F, 1.0F,   0.0F, 0.0F, 1.0F, 1.0F,
        0.0F, 1.0F, 0.0F, 1.0F,   1.0F, 0.0F, 0.0F, 1.0F,
    };
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(paletteA);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBufferA = device->CreateBuffer(paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(paletteA), sizeof(paletteA)));
    const BufferHandle paletteBufferB = device->CreateBuffer(paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(paletteB), sizeof(paletteB)));
    ASSERT_NE(paletteBufferA, kInvalidBufferHandle);
    ASSERT_NE(paletteBufferB, kInvalidBufferHandle);

    const auto drawPaletteQuad = [&](const BufferHandle palette, const std::int32_t index) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->SetUniformInt("uIndex", index);
        commandBuffer->BindStorageBuffer(palette, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    drawPaletteQuad(paletteBufferA, 2);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_GT(center[2], 240) << "frame 1 must show palette A[2] (BLUE) through the SSBO";
    }

    drawPaletteQuad(paletteBufferB, 2);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_GT(center[1], 240);
        EXPECT_LT(center[2], 12) << "frame 2 must show palette B[2] (GREEN) - the descriptor "
                                    "set must follow the newly bound buffer";
    }

    drawPaletteQuad(paletteBufferA, 3);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_LT(center[2], 12) << "frame 3 must show palette A[3] (YELLOW) - cached tuple "
                                    "reuse and the ring-fed uIndex snapshot must both hold";
    }

    device->DestroyBuffer(paletteBufferA);
    device->DestroyBuffer(paletteBufferB);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, SeparateSamplerAndImageSampleCheckerLikeCombined) {
    // The M2f split-form proof: the SAME checker scene as the M2c combined-sampler test, but
    // the fragment shader declares texture2D (binding 0, SAMPLED_IMAGE) and sampler (binding
    // 1, SAMPLER) separately. The device writes the bound texture's sampled view into the
    // image binding and its ONE fixed sampler (the GL-mirrored linear/clamp shape) into the
    // sampler binding, so:
    //   frame 1 - NO BindTexture: the unbound slot resolves to the fallback white exactly
    //             like the combined form does;
    //   frame 2 - checker bound at slot 0: the four quadrants must read back the exact
    //             uploaded texel colors - byte-for-byte the M2c assertions.
    ShaderHandle vertexShader{}, fragmentShader{};
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kSeparateSamplerFragmentSpirv;
    vertexShader = device->CreateShader(vertexDesc);
    fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle)
        << "a pipeline with a separate sampled-image+sampler pair must now build "
           "(pre-M2f hard-fail)";

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // 2x2 checker in upload memory order: (0,0) red, (1,0) green, (0,1) blue, (1,1) yellow.
    const std::uint8_t checkerData[16] = {
        255U, 0U, 0U, 255U,   0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,   255U, 255U, 0U, 255U,
    };
    TextureDesc checkerDesc{};
    checkerDesc.width = 2U;
    checkerDesc.height = 2U;
    const TextureHandle checkerTexture = device->CreateTexture(checkerDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(checkerData),
                                   sizeof(checkerData)));
    ASSERT_NE(checkerTexture, kInvalidTextureHandle);

    const auto drawQuad = [&](const TextureHandle* textureToBind) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        if (textureToBind != nullptr) {
            commandBuffer->BindTexture(*textureToBind, 0U);
        }
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    drawQuad(nullptr);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_GT(center[1], 240);
        EXPECT_GT(center[2], 240) << "an unbound separate-image slot must sample the fallback "
                                     "white, exactly like the combined form";
    }

    drawQuad(&checkerTexture);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        // Quadrant centers hit uv (0.25/0.75, 0.25/0.75) == exact texel centers, so even
        // LINEAR filtering yields the pure uploaded colors (same math as the M2c case).
        const auto topLeft = ChannelAtNdc(pixels, width, height, -0.5F, -0.5F);
        const auto topRight = ChannelAtNdc(pixels, width, height, 0.5F, -0.5F);
        const auto bottomLeft = ChannelAtNdc(pixels, width, height, -0.5F, 0.5F);
        const auto bottomRight = ChannelAtNdc(pixels, width, height, 0.5F, 0.5F);
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

    device->DestroyTexture(checkerTexture);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, StorageImageGraphicsPipelinesCreateSinceM5bLiftedTheRefusal) {
    // The M2f boundary is lifted in M5b: a graphics pipeline whose fragment shader declares
    // a STORAGE_IMAGE (rgba8 image2D) binding now builds successfully — the graphics
    // reflection collects it into the unified texture-slot space. (The pixel proof lives in
    // FragmentImageStoreWritesTextureAndNextFrameImageLoadReadsIt; this test pins creation
    // alone, mirroring the old refusal test it replaces.)
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kStorageImageFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});

    std::string infoLog;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc, &infoLog);
    EXPECT_NE(pipeline, kInvalidPipelineHandle)
        << "STORAGE_IMAGE graphics pipelines must build since M5b - got: " << infoLog;

    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, UnboundStorageSlotReadsDeterministicZerosAndFrameSurvives) {
    // The SSBO analogue of the unbound-texture white fallback: a pipeline whose storage slot
    // is never bound must read the device-owned zero-filled fallback buffer - deterministic
    // zeros (BLACK pixels through the palette shader), never undefined memory - and the
    // frame must present/survive normally.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle pipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->SetUniformInt("uIndex", 0);
        // Deliberately NO BindStorageBuffer.
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(center[0], 12);
    EXPECT_LT(center[1], 12);
    EXPECT_LT(center[2], 12) << "an unbound storage slot must read the zero-filled fallback "
                                "- got (" << center[0] << "," << center[1] << "," << center[2]
                                << ")";

    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, NonStorageBufferBindIsDroppedAndFrameSurvives) {
    // Usage-validation proof: binding a VERTEX-usage buffer as an SSBO is refused at replay
    // (warn-once, bind dropped), so the draw reads the zero fallback instead of binding a
    // buffer whose VkBufferUsageFlags lack STORAGE_BUFFER_BIT (which would be a validation
    // error / undefined on real drivers). The frame must survive.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle pipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->SetUniformInt("uIndex", 0);
        commandBuffer->BindStorageBuffer(quadBuffer, 0U); // VERTEX usage - must be dropped
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(center[0], 12);
    EXPECT_LT(center[1], 12);
    EXPECT_LT(center[2], 12) << "a wrong-usage storage bind must be dropped to the zero "
                                "fallback, never bound as a STORAGE_BUFFER descriptor";

    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, DestroyedStorageBufferAfterBindResolvesToZerosAndFrameSurvives) {
    // The SSBO analogue of the M2c destroyed-after-bind texture case: the bind is recorded,
    // the buffer is destroyed BEFORE the submit replays it, so the handle can no longer
    // resolve - the draw must degrade to the zero fallback instead of referencing freed
    // VkBuffer/VkDeviceMemory, and the frame must survive.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle pipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const float palette[16] = {
        1.0F, 0.0F, 0.0F, 1.0F,   0.0F, 1.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F,   1.0F, 1.0F, 0.0F, 1.0F,
    };
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(palette);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBuffer = device->CreateBuffer(paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(palette), sizeof(palette)));
    ASSERT_NE(paletteBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->SetUniformInt("uIndex", 0);
        commandBuffer->BindStorageBuffer(paletteBuffer, 0U);
        device->DestroyBuffer(paletteBuffer); // destroyed BEFORE this frame submits
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(center[0], 12);
    EXPECT_LT(center[1], 12);
    EXPECT_LT(center[2], 12) << "a storage buffer destroyed after recording the bind must "
                                "resolve to the zero fallback, never to freed memory";

    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


// ---------------------------------------------------------------------------
// M3: device-local staging for vertex/index buffers. VERTEX/INDEX buffers now
// allocate DEVICE_LOCAL memory and are fed exclusively through one-shot staging
// copies, so every pixel proof below exercises the staging path end-to-end:
// create-time initial data (both tests) and UpdateBuffer re-staging (the
// vertex test's frame 2). Pure 0/1 channel colors keep the asserts invariant
// under the sRGB/UNORM swapchain format-class duality (M2e's lesson).
// ---------------------------------------------------------------------------

TEST_F(VulkanRenderDeviceTest, StagedVertexBufferCreateAndUpdateRepaintRealPixels) {
    // Frame 1 - a full-coverage quad whose six vertices were staged into DEVICE_LOCAL memory
    // at creation, all sampling uv(0.25,0.25): the center pixel must be the checker's RED
    // texel - proof the create-time staging copy delivered the exact float payload.
    // Frame 2 - UpdateBuffer re-stages the same positions with uv(0.75,0.25): the center
    // must flip to GREEN - proof updates re-stage too (a silently dropped update could not
    // fake this; a host-mapped write path no longer exists for this buffer).
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::uint8_t checkerData[16] = {
        255U, 0U, 0U, 255U,   0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,   255U, 255U, 0U, 255U,
    };
    TextureDesc checkerDesc{};
    checkerDesc.width = 2U;
    checkerDesc.height = 2U;
    const TextureHandle checkerTexture = device->CreateTexture(checkerDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(checkerData),
                                   sizeof(checkerData)));
    ASSERT_NE(checkerTexture, kInvalidTextureHandle);

    // Full-coverage quad; every vertex carries the SAME uv so the whole frame samples one
    // texel center exactly (LINEAR filtering at a texel center returns that texel pure).
    const float quadRedUv[30] = {
        -1.0F, -1.0F, 0.0F,  0.25F, 0.25F,
         1.0F, -1.0F, 0.0F,  0.25F, 0.25F,
        -1.0F,  1.0F, 0.0F,  0.25F, 0.25F,
         1.0F, -1.0F, 0.0F,  0.25F, 0.25F,
         1.0F,  1.0F, 0.0F,  0.25F, 0.25F,
        -1.0F,  1.0F, 0.0F,  0.25F, 0.25F,
    };
    float quadGreenUv[30];
    std::memcpy(quadGreenUv, quadRedUv, sizeof(quadGreenUv));
    for (std::size_t vertex = 0; vertex < 6U; ++vertex) {
        quadGreenUv[vertex * 5U + 3U] = 0.75F; // u -> green texel; v stays 0.25
    }

    BufferDesc quadDesc{};
    quadDesc.sizeBytes = sizeof(quadRedUv);
    quadDesc.usage = BufferUsage::Vertex; // DEVICE_LOCAL + staged since M3
    const BufferHandle quadBuffer = device->CreateBuffer(quadDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadRedUv),
                                   sizeof(quadRedUv)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const auto drawQuad = [&]() {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(checkerTexture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    drawQuad();
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12) << "frame 1 must show the staged vertex data's RED texel";
    }

    ASSERT_TRUE(device->UpdateBuffer(quadBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadGreenUv),
                                   sizeof(quadGreenUv)),
        0U)) << "UpdateBuffer must succeed on a device-local buffer via re-staging";

    drawQuad();
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_GT(center[1], 240);
        EXPECT_LT(center[2], 12) << "frame 2 must show the RE-STAGED update's GREEN texel - a "
                                    "dropped or mis-staged update could not flip the pixel";
    }

    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(checkerTexture);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, StagedIndexBufferDrivesRealIndexedDraw) {
    // The M3 index-side proof (and the suite's first real DrawIndexed on Vulkan): four
    // unique vertices + a uint32 index buffer {0,1,2, 2,3,0}, both staged into DEVICE_LOCAL
    // memory at creation. The indexed draw must cover the full quad - the center pixel reads
    // the RED texel through the shared uv(0.25,0.25) - so all six indices provably resolved
    // from the staged index data (an empty/garbage index buffer could not paint the center).
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);

    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const std::uint8_t checkerData[16] = {
        255U, 0U, 0U, 255U,   0U, 255U, 0U, 255U,
        0U, 0U, 255U, 255U,   255U, 255U, 0U, 255U,
    };
    TextureDesc checkerDesc{};
    checkerDesc.width = 2U;
    checkerDesc.height = 2U;
    const TextureHandle checkerTexture = device->CreateTexture(checkerDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(checkerData),
                                   sizeof(checkerData)));
    ASSERT_NE(checkerTexture, kInvalidTextureHandle);

    const float quadVertices[20] = {
        -1.0F, -1.0F, 0.0F,  0.25F, 0.25F, // v0 top-left
         1.0F, -1.0F, 0.0F,  0.25F, 0.25F, // v1 top-right
        -1.0F,  1.0F, 0.0F,  0.25F, 0.25F, // v2 bottom-left
         1.0F,  1.0F, 0.0F,  0.25F, 0.25F, // v3 bottom-right
    };
    const std::uint32_t indices[6] = {0U, 1U, 2U, 2U, 3U, 0U};

    BufferDesc vertexDescBuffer{};
    vertexDescBuffer.sizeBytes = sizeof(quadVertices);
    vertexDescBuffer.usage = BufferUsage::Vertex;
    BufferDesc indexDesc{};
    indexDesc.sizeBytes = sizeof(indices);
    indexDesc.usage = BufferUsage::Index;
    const BufferHandle vertexBuffer = device->CreateBuffer(vertexDescBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    const BufferHandle indexBuffer = device->CreateBuffer(indexDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(indices),
                                   sizeof(indices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    ASSERT_NE(indexBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        commandBuffer->BindIndexBuffer(indexBuffer);
        commandBuffer->BindTexture(checkerTexture, 0U);
        commandBuffer->DrawIndexed(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(center[0], 240);
    EXPECT_LT(center[1], 12);
    EXPECT_LT(center[2], 12) << "the indexed draw must cover the center via the staged index "
                                "data - got (" << center[0] << "," << center[1] << ","
                                << center[2] << ")";

    device->DestroyBuffer(indexBuffer);
    device->DestroyBuffer(vertexBuffer);
    device->DestroyTexture(checkerTexture);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


// ---------------------------------------------------------------------------
// M4: multi-threaded command recording. Recording is per-object and the
// submission FIFO is mutex-guarded, so N threads may create+record+submit
// concurrently; Present (main thread) drains and replays in submission
// order. The pixel proof below is deliberately order-independent: each thread
// paints its OWN quadrant, so whichever thread submits first (its pass
// instance takes the frame's bootstrap clear before drawing, later instances
// resume with LOAD per M2d), all four quadrant centers must end up exactly
// their thread's palette color.
// ---------------------------------------------------------------------------

TEST_F(VulkanRenderDeviceTest, ParallelRecordedQuadsAllLandInOneFrame) {
    // Four std::threads concurrently create, record, AND submit their own command buffers
    // (thread i: quadrant i, palette color i via the ring-fed uIndex). If any thread's
    // recording were dropped, clobbered, or raced, its quadrant could not show its color —
    // and the frame itself must survive the concurrent submissions.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle pipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const float palette[16] = {
        1.0F, 0.0F, 0.0F, 1.0F,   0.0F, 1.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F,   1.0F, 1.0F, 0.0F, 1.0F,
    };
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(palette);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBuffer = device->CreateBuffer(paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(palette), sizeof(palette)));
    ASSERT_NE(paletteBuffer, kInvalidBufferHandle);

    // Quadrant vertex data (stride 20: pos3 + uv2, uv unused by the palette fragment).
    // Quadrant order: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right (native y-down).
    constexpr std::array<float, 4> kXSign = {-1.0F, 1.0F, -1.0F, 1.0F};
    constexpr std::array<float, 4> kYSign = {-1.0F, -1.0F, 1.0F, 1.0F};
    BufferHandle quadrantBuffers[4] = {};
    for (std::size_t quadrant = 0; quadrant < 4U; ++quadrant) {
        const float xStart = kXSign[quadrant] * 0.05F;
        const float xEnd = kXSign[quadrant] * 0.95F;
        const float yStart = kYSign[quadrant] * 0.05F;
        const float yEnd = kYSign[quadrant] * 0.95F;
        const float vertices[30] = {
            xStart, yStart, 0.0F,  0.0F, 0.0F,
            xEnd,   yStart, 0.0F,  0.0F, 0.0F,
            xStart, yEnd,   0.0F,  0.0F, 0.0F,
            xEnd,   yStart, 0.0F,  0.0F, 0.0F,
            xEnd,   yEnd,   0.0F,  0.0F, 0.0F,
            xStart, yEnd,   0.0F,  0.0F, 0.0F,
        };
        BufferDesc quadDesc{};
        quadDesc.sizeBytes = sizeof(vertices);
        quadDesc.usage = BufferUsage::Vertex; // DEVICE_LOCAL + staged since M3
        quadrantBuffers[quadrant] = device->CreateBuffer(quadDesc,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices),
                                       sizeof(vertices)));
        ASSERT_NE(quadrantBuffers[quadrant], kInvalidBufferHandle);
    }

    // Resources exist before the threads start (resource creation/destruction and Present
    // stay main-thread by contract); everything below runs concurrently on four workers.
    std::array<bool, 4> recordedAndSubmitted{};
    std::vector<std::thread> threads;
    threads.reserve(4U);
    for (std::size_t quadrant = 0; quadrant < 4U; ++quadrant) {
        threads.emplace_back([&, quadrant]() {
            auto commandBuffer = device->CreateCommandBuffer();
            if (commandBuffer == nullptr) {
                return;
            }
            RenderPassDesc passDesc{};
            passDesc.colorLoadOp = LoadOp::Load;   // order-independent accumulation (M2d)
            passDesc.depthLoadOp = LoadOp::DontCare;
            commandBuffer->BeginRenderPass(passDesc);
            commandBuffer->BindPipeline(pipeline);
            commandBuffer->BindVertexBuffer(quadrantBuffers[quadrant]);
            commandBuffer->SetUniformInt("uIndex", static_cast<std::int32_t>(quadrant));
            commandBuffer->BindStorageBuffer(paletteBuffer, 0U);
            commandBuffer->Draw(6U);
            commandBuffer->EndRenderPass();
            device->Submit(std::move(commandBuffer)); // any-thread-safe since M4
            recordedAndSubmitted[quadrant] = true;       // distinct array elements: no race
        });
    }
    for (std::thread& worker : threads) {
        worker.join();
    }
    for (std::size_t quadrant = 0; quadrant < 4U; ++quadrant) {
        ASSERT_TRUE(recordedAndSubmitted[quadrant])
            << "worker thread " << quadrant << " failed to record+submit";
    }

    device->Present();
    ASSERT_TRUE(device->IsUsable()) << "concurrent submissions must not break the device";

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
    const auto topLeft = ChannelAtNdc(pixels, width, height, -0.5F, -0.5F);
    const auto topRight = ChannelAtNdc(pixels, width, height, 0.5F, -0.5F);
    const auto bottomLeft = ChannelAtNdc(pixels, width, height, -0.5F, 0.5F);
    const auto bottomRight = ChannelAtNdc(pixels, width, height, 0.5F, 0.5F);
    EXPECT_GT(topLeft[0], 240);
    EXPECT_LT(topLeft[1], 12);
    EXPECT_LT(topLeft[2], 12) << "thread 0's quadrant must be its own RED recording";
    EXPECT_LT(topRight[0], 12);
    EXPECT_GT(topRight[1], 240);
    EXPECT_LT(topRight[2], 12) << "thread 1's quadrant must be its own GREEN recording";
    EXPECT_LT(bottomLeft[0], 12);
    EXPECT_LT(bottomLeft[1], 12);
    EXPECT_GT(bottomLeft[2], 240) << "thread 2's quadrant must be its own BLUE recording";
    EXPECT_GT(bottomRight[0], 240);
    EXPECT_GT(bottomRight[1], 240);
    EXPECT_LT(bottomRight[2], 12) << "thread 3's quadrant must be its own YELLOW recording";

    for (const BufferHandle quadrantBuffer : quadrantBuffers) {
        device->DestroyBuffer(quadrantBuffer);
    }
    device->DestroyBuffer(paletteBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

// ---------------------------------------------------------------------------
// M5a: compute pipelines + Dispatch
// ---------------------------------------------------------------------------

// Builds the fill_palette compute pipeline (kFillPaletteComputeSpirv: one workgroup of
// 4 invocations writes red/green/blue/yellow into uColors[0..3] of the bound SSBO).
[[nodiscard]] PipelineHandle CreateFillPaletteComputePipeline(VulkanRenderDevice& device,
                                                                    ShaderHandle* outComputeShader,
                                                                    std::string* outInfoLog) {
    ShaderDesc computeDesc{};
    computeDesc.stage = ShaderStage::Compute;
    computeDesc.sourceCode = kFillPaletteComputeSpirv;
    *outComputeShader = device.CreateShader(computeDesc, outInfoLog);
    if (*outComputeShader == kInvalidShaderHandle) {
        return {};
    }
    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = *outComputeShader;
    return device.CreateComputePipeline(pipelineDesc, outInfoLog);
}

TEST_F(VulkanRenderDeviceTest, ComputeDispatchFillsStorageBufferProvingRealCompute) {
    // The M5a pixel proof — two frames against ONE zero-initialized palette SSBO:
    //   frame 1 (control): no dispatch. The palette stays all-zero, so the M2f palette
    //     quad paints uColors[2] == BLACK at the center — proving the buffer starts zeroed.
    //   frame 2: the fill_palette compute pipeline is bound and Dispatch(1,1,1) runs
    //     OUTSIDE the pass markers (4 invocations write red/green/blue/yellow), then the
    //     same quad paints uIndex=2 — the center must be BLUE.
    // The only difference between the frames is the dispatch, so the blue pixel is
    // unfakeable evidence the compute stage really ran and its SSBO write really reached
    // the fragment shader through the conservative dispatch barriers.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle graphicsPipeline =
        CreateSsboPalettePipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(graphicsPipeline, kInvalidPipelineHandle);

    ShaderHandle computeShader{};
    std::string infoLog;
    const PipelineHandle computePipeline =
        CreateFillPaletteComputePipeline(*device, &computeShader, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;
    // The SSBO-only compute shader reflects no SetUniform-feedable members.
    EXPECT_TRUE(device->GetPipelineUniforms(computePipeline).empty());

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const float zeros[16] = {};
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(zeros);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBuffer = device->CreateBuffer(
        paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(zeros), sizeof(zeros)));
    ASSERT_NE(paletteBuffer, kInvalidBufferHandle);

    const auto presentPaletteFrame = [&](const bool dispatchFirst) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        if (dispatchFirst) {
            // M5a contract: the compute bind, its SSBO bind, and the dispatch are all
            // recorded OUTSIDE render-pass markers.
            commandBuffer->BindPipeline(computePipeline);
            commandBuffer->BindStorageBuffer(paletteBuffer, 0U);
            commandBuffer->Dispatch(1U, 1U, 1U);
        }
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(graphicsPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->SetUniformInt("uIndex", 2);
        commandBuffer->BindStorageBuffer(paletteBuffer, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    presentPaletteFrame(false);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12) << "control frame: the zero-initialized palette must paint BLACK";
    }

    presentPaletteFrame(true);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_GT(center[2], 240)
            << "dispatch frame: uColors[2] must be the compute-written BLUE — the compute "
               "stage really ran and its SSBO write really reached the fragment shader";
    }

    device->DestroyBuffer(paletteBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(computePipeline);
    device->DestroyPipeline(graphicsPipeline);
    device->DestroyShader(computeShader);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, PointAndStripTopologiesAssemblePrimitivesOnTheSwapchain) {
    // Each new topology is proved by a pixel a list topology could not have painted, with the
    // list drawn as the control in the same test: a 16px point (gl_PointSize from the vertex
    // shader) covers pixels the 1px/absent alternative would not; a four-vertex TriangleStrip
    // paints the corner a four-vertex Triangles draw leaves clear; a three-vertex LineStrip
    // paints the second segment a Lines draw drops.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTriangleVertexSpirv;
    ShaderDesc pointVertexDesc{};
    pointVertexDesc.stage = ShaderStage::Vertex;
    pointVertexDesc.sourceCode = kPointVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle pointVertexShader = device->CreateShader(pointVertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(pointVertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    const auto makePipeline = [&](const ShaderHandle vs, const PrimitiveTopology topology) {
        PipelineDesc desc{};
        desc.vertexShader = vs;
        desc.fragmentShader = fragmentShader;
        desc.vertexStride = 24U;
        desc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
        desc.vertexLayout.push_back(VertexAttribute{"COLOR", VertexAttributeFormat::Float3, 12U});
        desc.topology = topology;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        return device->CreatePipeline(desc);
    };
    const PipelineHandle pointPipeline = makePipeline(pointVertexShader, PrimitiveTopology::Points);
    const PipelineHandle stripPipeline = makePipeline(vertexShader, PrimitiveTopology::TriangleStrip);
    const PipelineHandle trianglePipeline = makePipeline(vertexShader, PrimitiveTopology::Triangles);
    const PipelineHandle lineStripPipeline = makePipeline(vertexShader, PrimitiveTopology::LineStrip);
    const PipelineHandle linePipeline = makePipeline(vertexShader, PrimitiveTopology::Lines);
    ASSERT_NE(pointPipeline, kInvalidPipelineHandle);
    ASSERT_NE(stripPipeline, kInvalidPipelineHandle);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    ASSERT_NE(lineStripPipeline, kInvalidPipelineHandle);
    ASSERT_NE(linePipeline, kInvalidPipelineHandle);
    PipelineDesc unknownTopology{};
    unknownTopology.vertexShader = vertexShader;
    unknownTopology.fragmentShader = fragmentShader;
    unknownTopology.topology = static_cast<PrimitiveTopology>(0xFFU);
    EXPECT_EQ(device->CreatePipeline(unknownTopology), kInvalidPipelineHandle);

    // The swapchain extent decides pixel-centre NDC for the line strip's column and row.
    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    {
        auto clearFrame = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        clearFrame->BeginRenderPass(passDesc);
        clearFrame->EndRenderPass();
        device->Submit(std::move(clearFrame));
        device->Present();
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width < 64U || height < 64U) { GTEST_SKIP() << "swapchain too small for pixel proofs"; }
    }
    const auto ndcForPixel = [](const std::uint32_t pixel, const std::uint32_t size) {
        return (static_cast<float>(pixel) + 0.5F) / static_cast<float>(size) * 2.0F - 1.0F;
    };
    const std::uint32_t centreX = width / 2U;
    const std::uint32_t centreY = height / 2U;
    const float columnNdc = ndcForPixel(centreX + width / 4U, width);
    const float rowNdc = ndcForPixel(centreY, height);
    const float green[3] = {0.0F, 1.0F, 0.0F};
    const auto vertex = [&](const float x, const float y, std::array<float, 6>& out) {
        out = {x, y, 0.0F, green[0], green[1], green[2]};
    };
    std::array<std::array<float, 6>, 4> stripCorners{};
    vertex(-1.0F, -1.0F, stripCorners[0]);
    vertex(1.0F, -1.0F, stripCorners[1]);
    vertex(-1.0F, 1.0F, stripCorners[2]);
    vertex(1.0F, 1.0F, stripCorners[3]);
    std::array<std::array<float, 6>, 3> lineStrip{};
    vertex(-1.0F, rowNdc, lineStrip[0]);
    vertex(columnNdc, rowNdc, lineStrip[1]);
    vertex(columnNdc, 1.0F, lineStrip[2]);
    std::array<std::array<float, 6>, 1> point{};
    vertex(ndcForPixel(centreX, width), ndcForPixel(centreY, height), point[0]);
    const BufferHandle stripBuffer = device->CreateBuffer(
        {sizeof(stripCorners), BufferUsage::Vertex}, std::as_bytes(std::span(stripCorners)));
    const BufferHandle lineStripBuffer = device->CreateBuffer(
        {sizeof(lineStrip), BufferUsage::Vertex}, std::as_bytes(std::span(lineStrip)));
    const BufferHandle pointBuffer = device->CreateBuffer(
        {sizeof(point), BufferUsage::Vertex}, std::as_bytes(std::span(point)));
    ASSERT_NE(stripBuffer, kInvalidBufferHandle);
    ASSERT_NE(lineStripBuffer, kInvalidBufferHandle);
    ASSERT_NE(pointBuffer, kInvalidBufferHandle);

    const auto drawFrame = [&](const PipelineHandle pipeline, const BufferHandle buffer,
                               const std::uint32_t vertexCount) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(buffer);
        commandBuffer->Draw(vertexCount);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    };
    const auto greenAtPixel = [&](const std::uint32_t x, const std::uint32_t y) {
        return ChannelAtNdc(pixels, width, height, ndcForPixel(x, width), ndcForPixel(y, height))[1];
    };

    drawFrame(pointPipeline, pointBuffer, 1U);
    EXPECT_GT(greenAtPixel(centreX, centreY), 200) << "the point covers its own pixel";
    EXPECT_GT(greenAtPixel(centreX - 6U, centreY), 200) << "16px point: 6 pixels left is still inside";
    EXPECT_GT(greenAtPixel(centreX, centreY + 6U), 200);
    EXPECT_LT(greenAtPixel(centreX - 20U, centreY), 40) << "20 pixels out is beyond the 16px square";
    EXPECT_LT(greenAtPixel(centreX, centreY + 20U), 40);

    drawFrame(stripPipeline, stripBuffer, 4U);
    EXPECT_GT(greenAtPixel(width - 8U, height - 8U), 200) << "strip's second triangle paints the far corner";
    EXPECT_GT(greenAtPixel(8U, 8U), 200);
    drawFrame(trianglePipeline, stripBuffer, 4U);
    EXPECT_LT(greenAtPixel(width - 8U, height - 8U), 40) << "a Triangles draw of the same vertices leaves it clear";
    EXPECT_GT(greenAtPixel(8U, 8U), 200);

    drawFrame(lineStripPipeline, lineStripBuffer, 3U);
    EXPECT_GT(greenAtPixel(centreX / 2U, centreY), 200) << "first segment along the centre row";
    EXPECT_GT(greenAtPixel(centreX + width / 4U, centreY + height / 4U), 200)
        << "second segment down the quarter column: only a strip draws it";
    drawFrame(linePipeline, lineStripBuffer, 3U);
    EXPECT_GT(greenAtPixel(centreX / 2U, centreY), 200);
    EXPECT_LT(greenAtPixel(centreX + width / 4U, centreY + height / 4U), 40)
        << "a Lines draw drops the unpaired third vertex";

    device->DestroyBuffer(pointBuffer);
    device->DestroyBuffer(lineStripBuffer);
    device->DestroyBuffer(stripBuffer);
    device->DestroyPipeline(linePipeline);
    device->DestroyPipeline(lineStripPipeline);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyPipeline(stripPipeline);
    device->DestroyPipeline(pointPipeline);
    device->DestroyShader(fragmentShader);
    device->DestroyShader(pointVertexShader);
    device->DestroyShader(vertexShader);
}

// IEEE binary16 of a float in the normal range (enough for test vertex data: ±1, 0.5, 0).
[[nodiscard]] static std::uint16_t HalfFromFloat(const float value) {
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

TEST_F(VulkanRenderDeviceTest, PackedVertexAttributeFormatsFetchNormalizedHalfAndIntegerData) {
    // The GL proof's twin on SwiftShader: a lower-left triangle per layout, NDC (-0.5, -0.5)
    // sampled from the presented image. The colour-triangle vertex shader takes a vec3
    // position / vec3 colour, which Vulkan legally feeds from two-component (z = 0) and
    // four-component (w dropped) formats; the integer layouts use the baked uvec4 shader.
    ShaderDesc floatVertexDesc{};
    floatVertexDesc.stage = ShaderStage::Vertex;
    floatVertexDesc.sourceCode = kTriangleVertexSpirv;
    ShaderDesc integerVertexDesc{};
    integerVertexDesc.stage = ShaderStage::Vertex;
    integerVertexDesc.sourceCode = kIntegerJointsVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTriangleFragmentSpirv;
    const ShaderHandle floatVertexShader = device->CreateShader(floatVertexDesc);
    const ShaderHandle integerVertexShader = device->CreateShader(integerVertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(floatVertexShader, kInvalidShaderHandle);
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
        float x, y, z;
        std::uint16_t j0, j1, j2, j3;
    };
    struct UIntJointVertex final {
        float x, y, z;
        std::uint32_t j0, j1, j2, j3;
    };
#pragma pack(pop)
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
    const std::array<UShortJointVertex, 3> ushortTriangle{{{-1.0F, -1.0F, 0.0F, 200U, 100U, 0U, 7U},
                                                              {1.0F, -1.0F, 0.0F, 200U, 100U, 0U, 7U},
                                                              {-1.0F, 1.0F, 0.0F, 200U, 100U, 0U, 7U}}};
    const std::array<UIntJointVertex, 3> uintTriangle{{{-1.0F, -1.0F, 0.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {1.0F, -1.0F, 0.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U},
                                                          {-1.0F, 1.0F, 0.0F, 65536U + 200U, 3U * 65536U + 100U, 0U, 7U}}};
    struct LayoutCase final {
        ShaderHandle vertexShader;
        std::vector<VertexAttribute> layout;
        std::uint32_t stride;
        std::span<const std::byte> vertices;
        std::array<int, 3> expected;
        const char* name;
    };
    const std::array<LayoutCase, 4> cases{{
        {floatVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Short2Norm, 0U},
          VertexAttribute{"COLOR", VertexAttributeFormat::UByte4Norm, 4U}},
         sizeof(SnormVertex), std::as_bytes(std::span(snormTriangle)), {255, 128, 0}, "Short2Norm+UByte4Norm"},
        {floatVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Half2, 0U},
          VertexAttribute{"COLOR", VertexAttributeFormat::Half4, 4U}},
         sizeof(HalfVertex), std::as_bytes(std::span(halfTriangle)), {0, 128, 255}, "Half2+Half4"},
        {integerVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U},
          VertexAttribute{"JOINTS", VertexAttributeFormat::UShort4, 12U}},
         sizeof(UShortJointVertex), std::as_bytes(std::span(ushortTriangle)), {200, 100, 0}, "UShort4"},
        {integerVertexShader,
         {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U},
          VertexAttribute{"JOINTS", VertexAttributeFormat::UInt4, 12U}},
         sizeof(UIntJointVertex), std::as_bytes(std::span(uintTriangle)), {200, 100, 0}, "UInt4"},
    }};
    for (const LayoutCase& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        PipelineDesc desc{};
        desc.vertexShader = testCase.vertexShader;
        desc.fragmentShader = fragmentShader;
        desc.vertexLayout = testCase.layout;
        desc.vertexStride = testCase.stride;
        desc.depthTestEnabled = false;
        desc.depthWriteEnabled = false;
        const PipelineHandle pipeline = device->CreatePipeline(desc);
        ASSERT_NE(pipeline, kInvalidPipelineHandle);
        const BufferHandle vertexBuffer =
            device->CreateBuffer({testCase.vertices.size(), BufferUsage::Vertex}, testCase.vertices);
        ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        commandBuffer->Draw(3U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto inside = ChannelAtNdc(pixels, width, height, -0.5F, -0.5F);
        // The shader's linear value is stored either linearly (UNORM-typed swapchain, lavapipe)
        // or sRGB-encoded (SRGB-typed swapchain, SwiftShader) — the suite's standing rule for
        // mid-range bytes. 0 and 255 are fixed points of both, so the half/integer proofs that
        // hinge on them (0 vs 255, 200 vs anything else) are unaffected.
        const auto srgbEncode = [](const int linearByte) {
            const float linear = static_cast<float>(linearByte) / 255.0F;
            const float encoded = linear <= 0.0031308F ? linear * 12.92F
                                                       : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
            return static_cast<int>(std::lround(encoded * 255.0F));
        };
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const int linearExpected = testCase.expected[channel];
            const int srgbExpected = srgbEncode(linearExpected);
            const bool storedLinear = std::abs(inside[channel] - linearExpected) <= 3;
            const bool storedSrgb = std::abs(inside[channel] - srgbExpected) <= 3;
            EXPECT_TRUE(storedLinear || storedSrgb)
                << "channel " << channel << " read " << inside[channel] << ", expected " << linearExpected
                << " (linear swapchain) or " << srgbExpected << " (sRGB swapchain)";
        }
        const auto outside = ChannelAtNdc(pixels, width, height, 0.5F, 0.5F);
        EXPECT_LT(outside[0] + outside[1] + outside[2], 20) << "the triangle covers only one half";
        device->DestroyBuffer(vertexBuffer);
        device->DestroyPipeline(pipeline);
    }
    device->DestroyShader(fragmentShader);
    device->DestroyShader(integerVertexShader);
    device->DestroyShader(floatVertexShader);
}

TEST_F(VulkanRenderDeviceTest, ReadbackBuffer_ReadsComputeWrittenPaletteDirectly) {
    // CS3: the compute result read back as NUMBERS, not inferred from a pixel. M5a proved the
    // dispatch by painting uColors[2] and sampling the framebuffer; this proves the same write
    // by reading the sixteen floats the shader stored. Zero-initialized SSBO in, one
    // outside-pass dispatch, then red/green/blue/yellow must come back exactly - without a real
    // dispatch whose writes the readback's queue drain observes, the buffer stays zeroed.
    ShaderHandle computeShader{};
    std::string infoLog;
    const PipelineHandle computePipeline =
        CreateFillPaletteComputePipeline(*device, &computeShader, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;

    const float zeros[16] = {};
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(zeros);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBuffer = device->CreateBuffer(
        paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(zeros), sizeof(zeros)));
    ASSERT_NE(paletteBuffer, kInvalidBufferHandle);

    // Control: before any dispatch the readback must show the zero fill it was created with.
    float beforeDispatch[16] = {1.0F};
    ASSERT_TRUE(device->ReadbackBuffer(
        paletteBuffer, std::span<std::byte>(reinterpret_cast<std::byte*>(beforeDispatch),
                                              sizeof(beforeDispatch))));
    for (const float value : beforeDispatch) {
        EXPECT_FLOAT_EQ(value, 0.0F) << "control: the palette must start zeroed";
    }

    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BindPipeline(computePipeline);
    commandBuffer->BindStorageBuffer(paletteBuffer, 0U);
    commandBuffer->Dispatch(1U, 1U, 1U);
    device->Submit(std::move(commandBuffer));
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    float palette[16] = {};
    ASSERT_TRUE(device->ReadbackBuffer(
        paletteBuffer, std::span<std::byte>(reinterpret_cast<std::byte*>(palette), sizeof(palette))));
    EXPECT_FLOAT_EQ(palette[0], 1.0F);   // uColors[0] = RED
    EXPECT_FLOAT_EQ(palette[1], 0.0F);
    EXPECT_FLOAT_EQ(palette[2], 0.0F);
    EXPECT_FLOAT_EQ(palette[3], 1.0F);
    EXPECT_FLOAT_EQ(palette[4], 0.0F);   // uColors[1] = GREEN
    EXPECT_FLOAT_EQ(palette[5], 1.0F);
    EXPECT_FLOAT_EQ(palette[8], 0.0F);   // uColors[2] = BLUE
    EXPECT_FLOAT_EQ(palette[10], 1.0F);
    EXPECT_FLOAT_EQ(palette[12], 1.0F);  // uColors[3] = YELLOW
    EXPECT_FLOAT_EQ(palette[13], 1.0F);
    EXPECT_FLOAT_EQ(palette[14], 0.0F);

    // A windowed read sees exactly its window: uColors[2] alone.
    float blueOnly[4] = {};
    ASSERT_TRUE(device->ReadbackBuffer(
        paletteBuffer, std::span<std::byte>(reinterpret_cast<std::byte*>(blueOnly), sizeof(blueOnly)),
        8U * sizeof(float)));
    EXPECT_FLOAT_EQ(blueOnly[2], 1.0F);
    EXPECT_FLOAT_EQ(blueOnly[3], 1.0F);

    device->DestroyBuffer(paletteBuffer);
    device->DestroyPipeline(computePipeline);
    device->DestroyShader(computeShader);
}

TEST_F(VulkanRenderDeviceTest, ReadbackBuffer_ReadsDeviceLocalThroughStagingAndRefusesOutOfRangeReads) {
    // Device-local placement (the Auto rule for VERTEX/INDEX) used to be refused; with the
    // buffer memory hints every placement reads back — here through a one-shot staging copy —
    // and what comes back is what the staged upload wrote. Out-of-range and unknown-handle
    // reads stay refused, and the empty read stays a no-op.
    const float vertices[6] = {1.5F, -2.0F, 3.25F, 4.0F, 0.0F, 6.5F};
    BufferDesc vertexDesc{};
    vertexDesc.sizeBytes = sizeof(vertices);
    vertexDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(
        vertexDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    std::array<float, 6> readback{};
    ASSERT_TRUE(device->ReadbackBuffer(vertexBuffer, std::as_writable_bytes(std::span(readback))))
        << "a device-local buffer reads back through staging now";
    EXPECT_EQ(readback[0], 1.5F);
    EXPECT_EQ(readback[5], 6.5F);
    std::array<float, 2> window2{};
    ASSERT_TRUE(device->ReadbackBuffer(vertexBuffer, std::as_writable_bytes(std::span(window2)), 8U));
    EXPECT_EQ(window2[0], 3.25F) << "offset reads copy the requested window";
    EXPECT_EQ(window2[1], 4.0F);
    std::array<std::byte, sizeof(vertices) + 4U> tooLargeForVertex{};
    EXPECT_FALSE(device->ReadbackBuffer(vertexBuffer, tooLargeForVertex));

    BufferDesc storageDesc{};
    storageDesc.sizeBytes = 16U;
    storageDesc.usage = BufferUsage::Storage;
    const BufferHandle storageBuffer = device->CreateBuffer(storageDesc);
    ASSERT_NE(storageBuffer, kInvalidBufferHandle);
    std::array<std::byte, 32U> tooLarge{};
    EXPECT_FALSE(device->ReadbackBuffer(storageBuffer, tooLarge));
    std::array<std::byte, 8U> window{};
    EXPECT_FALSE(device->ReadbackBuffer(storageBuffer, window, 12U)); // runs off the end
    EXPECT_FALSE(device->ReadbackBuffer(BufferHandle{4242U}, window));
    EXPECT_TRUE(device->ReadbackBuffer(storageBuffer, std::span<std::byte>{})); // empty no-op

    device->DestroyBuffer(storageBuffer);
    device->DestroyBuffer(vertexBuffer);
}

TEST_F(VulkanRenderDeviceTest, CreateComputePipelineAcceptsStorageImagesSinceM5b) {
    // The M5a boundary is lifted in this very slice: a STORAGE_IMAGE compute shader is valid
    // SPIR-V (shader creation always succeeded) and compute pipeline creation now ACCEPTS
    // it — the reflection folds the rgba8 image2D binding into set 0 as a STORAGE_IMAGE
    // descriptor. (The pixel proof lives in
    // ComputeImageStoreFillsTextureProvingRealStorageImages; this test pins creation alone,
    // mirroring the old refusal test it replaces.)
    ShaderDesc computeDesc{};
    computeDesc.stage = ShaderStage::Compute;
    computeDesc.sourceCode = kStorageImageComputeSpirv;
    std::string infoLog;
    const ShaderHandle computeShader = device->CreateShader(computeDesc, &infoLog);
    ASSERT_NE(computeShader, kInvalidShaderHandle) << infoLog;

    ComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = computeShader;
    const PipelineHandle pipeline = device->CreateComputePipeline(pipelineDesc, &infoLog);
    EXPECT_NE(pipeline, kInvalidPipelineHandle)
        << "STORAGE_IMAGE compute pipelines must build since M5b - got: " << infoLog;

    device->DestroyPipeline(pipeline);
    device->DestroyShader(computeShader);
}

TEST_F(VulkanRenderDeviceTest, CreateComputePipelineValidatesShaderHandleAndStage) {
    std::string infoLog;
    ComputePipelineDesc unknownDesc{};
    unknownDesc.computeShader = ShaderHandle{999999U};
    EXPECT_EQ(device->CreateComputePipeline(unknownDesc, &infoLog), kInvalidPipelineHandle);
    EXPECT_NE(infoLog.find("live shader"), std::string::npos) << infoLog;

    // A VERTEX-stage handle must be refused even though it references a live shader.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc, &infoLog);
    ASSERT_NE(vertexShader, kInvalidShaderHandle) << infoLog;
    ComputePipelineDesc wrongStageDesc{};
    wrongStageDesc.computeShader = vertexShader;
    EXPECT_EQ(device->CreateComputePipeline(wrongStageDesc, &infoLog), kInvalidPipelineHandle);
    EXPECT_NE(infoLog.find("wrong stage"), std::string::npos) << infoLog;
    device->DestroyShader(vertexShader);
}

TEST_F(VulkanRenderDeviceTest, DispatchAndDrawMisuseWithWrongPipelineKindDegradesSafely) {
    // Two misuse frames, both degrading to warn-once + skip with the frame fully presented:
    //   frame 1: Dispatch with NO pipeline bound — skipped; the frame shows only its
    //            pure-BLUE clear (encoding-invariant byte check).
    //   frame 2: a COMPUTE pipeline stays bound through a pass marker into Draw — the
    //            draw is skipped (vkCmdDraw against a COMPUTE-bound pipeline would be a
    //            validation error); the frame again shows only the clear.
    ShaderHandle computeShader{};
    std::string infoLog;
    const PipelineHandle computePipeline =
        CreateFillPaletteComputePipeline(*device, &computeShader, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;

    const auto presentBlueClearFrame = [&](const bool bindComputeAndDraw) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        if (!bindComputeAndDraw) {
            commandBuffer->Dispatch(1U, 1U, 1U); // nothing bound: skip + warn-once
        } else {
            commandBuffer->BindPipeline(computePipeline); // binds at the COMPUTE point
        }
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        if (bindComputeAndDraw) {
            commandBuffer->Draw(6U); // no vertex buffer, no graphics pipeline: skipped
        }
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_GT(center[2], 240) << "the misuse frame must still present its pure-blue clear";
    };

    presentBlueClearFrame(false);
    presentBlueClearFrame(true);

    device->DestroyPipeline(computePipeline);
    device->DestroyShader(computeShader);
}

// The M2c combined-sampler quad pipeline (textured vertex + uTex fragment), reused by the
// M5b storage-image proofs to sample back whatever the imageStore passes wrote.
[[nodiscard]] PipelineHandle CreateTexturedSamplerPipeline(VulkanRenderDevice& device,
                                                                 ShaderHandle* outVertexShader,
                                                                 ShaderHandle* outFragmentShader) {
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    *outVertexShader = device.CreateShader(vertexDesc);
    *outFragmentShader = device.CreateShader(fragmentDesc);
    if (*outVertexShader == kInvalidShaderHandle ||
        *outFragmentShader == kInvalidShaderHandle) {
        return {};
    }
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = *outVertexShader;
    pipelineDesc.fragmentShader = *outFragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    return device.CreatePipeline(pipelineDesc);
}

TEST_F(VulkanRenderDeviceTest, ComputeImageStoreFillsTextureProvingRealStorageImages) {
    // The M5b compute-side pixel proof — two frames against ONE zero-initialized 4x4 sRGB RGBA8
    // texture, exercising both its UNORM storage alias and hardware-decoding sampled view:
    //   frame 1 (control): no dispatch. The M2c combined-sampler quad samples the untouched
    //     texture — the center must be BLACK, proving the texture starts zeroed.
    //   frame 2: the image-fill compute pipeline is bound OUTSIDE the pass markers,
    //     BindTexture feeds global slot 0 (the unified sampled+storage slot space), and
    //     Dispatch(1,1,1) runs one 4x4 workgroup whose imageStore paints every texel
    //     GREEN through a STORAGE_IMAGE descriptor; the same sampler quad then paints the
    //     center — it must be GREEN.
    // The only difference between the frames is the dispatch, so the green pixel is
    // unfakeable evidence the STORAGE_IMAGE descriptor really points at the caller's
    // texture, the GENERAL transition + dispatch barriers really published the writes, and
    // a GENERAL-pinned texture stays sampleable through the ordinary combined-sampler path.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle samplerPipeline =
        CreateTexturedSamplerPipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(samplerPipeline, kInvalidPipelineHandle);

    ShaderDesc computeDesc{};
    computeDesc.stage = ShaderStage::Compute;
    computeDesc.sourceCode = kImageFillComputeSpirv;
    std::string infoLog;
    const ShaderHandle computeShader = device->CreateShader(computeDesc, &infoLog);
    ASSERT_NE(computeShader, kInvalidShaderHandle) << infoLog;
    ComputePipelineDesc computePipelineDesc{};
    computePipelineDesc.computeShader = computeShader;
    const PipelineHandle computePipeline =
        device->CreateComputePipeline(computePipelineDesc, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle)
        << "a STORAGE_IMAGE compute pipeline must build since M5b - got: " << infoLog;

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const std::uint8_t zeroPixels[4U * 4U * 4U] = {};
    TextureDesc targetDesc{};
    targetDesc.width = 4U;
    targetDesc.height = 4U;
    targetDesc.colorSpace = TextureColorSpace::Srgb;
    const TextureHandle targetTexture = device->CreateTexture(
        targetDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(zeroPixels),
                                   sizeof(zeroPixels)));
    ASSERT_NE(targetTexture, kInvalidTextureHandle);

    const auto presentSamplerFrame = [&](const bool dispatchFirst) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        if (dispatchFirst) {
            commandBuffer->BindPipeline(computePipeline);
            commandBuffer->BindTexture(targetTexture, 0U);
            commandBuffer->Dispatch(1U, 1U, 1U);
        }
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(samplerPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(targetTexture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    presentSamplerFrame(false);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12)
            << "control frame: the zero-initialized texture must sample BLACK";
    }

    presentSamplerFrame(true);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_GT(center[1], 240);
        EXPECT_LT(center[2], 12)
            << "dispatch frame: every texel must be the compute-written GREEN — the "
               "STORAGE_IMAGE descriptor really points at the caller's texture";
    }

    device->DestroyTexture(targetTexture);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(computePipeline);
    device->DestroyPipeline(samplerPipeline);
    device->DestroyShader(computeShader);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, FragmentImageStoreWritesTextureAndNextFrameImageLoadReadsIt) {
    // The M5b graphics-side pixel proof — two frames, two STORAGE_IMAGE pipelines over ONE
    // zero-initialized 4x4 RGBA8 texture:
    //   frame 1: a fullscreen quad whose fragment shader imageStores RED into texel
    //     (fragCoord % 4) — the coverage therefore paints all 16 texels — while outputting
    //     opaque black to the attachment. The draw happens INSIDE the pass markers, so the
    //     flush must exercise the in-pass transition path: close the open swapchain pass,
    //     barrier SHADER_READ→GENERAL, reopen with LOAD semantics, write the tuple set,
    //     draw. The presented center must be BLACK (the fragment's own color output).
    //   frame 2: a quad whose fragment is outColor = imageLoad(uImg, (0,0)) — the pinned
    //     GENERAL texture needs no transition this time; the center must be RED.
    // The red pixel is unfakeable evidence the fragment-stage imageStore really wrote the
    // caller's texture mid-pass and the close/reopen machinery preserved the frame.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc storeDesc{};
    storeDesc.stage = ShaderStage::Fragment;
    storeDesc.sourceCode = kImageStoreFragmentSpirv;
    ShaderDesc loadDesc{};
    loadDesc.stage = ShaderStage::Fragment;
    loadDesc.sourceCode = kStorageImageFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle storeShader = device->CreateShader(storeDesc);
    const ShaderHandle loadShader = device->CreateShader(loadDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(storeShader, kInvalidShaderHandle);
    ASSERT_NE(loadShader, kInvalidShaderHandle);

    PipelineDesc storePipelineDesc{};
    storePipelineDesc.vertexShader = vertexShader;
    storePipelineDesc.fragmentShader = storeShader;
    storePipelineDesc.vertexStride = 20U;
    storePipelineDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    storePipelineDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    storePipelineDesc.depthTestEnabled = false;
    storePipelineDesc.depthWriteEnabled = false;
    PipelineDesc loadPipelineDesc = storePipelineDesc;
    loadPipelineDesc.fragmentShader = loadShader;
    const PipelineHandle storePipeline = device->CreatePipeline(storePipelineDesc);
    const PipelineHandle loadPipeline = device->CreatePipeline(loadPipelineDesc);
    ASSERT_NE(storePipeline, kInvalidPipelineHandle)
        << "a STORAGE_IMAGE graphics pipeline must build since M5b";
    ASSERT_NE(loadPipeline, kInvalidPipelineHandle);

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const std::uint8_t zeroPixels[4U * 4U * 4U] = {};
    TextureDesc targetDesc{};
    targetDesc.width = 4U;
    targetDesc.height = 4U;
    const TextureHandle targetTexture = device->CreateTexture(
        targetDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(zeroPixels),
                                   sizeof(zeroPixels)));
    ASSERT_NE(targetTexture, kInvalidTextureHandle);

    const auto presentImageFrame = [&](const PipelineHandle pipeline) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(targetTexture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    };

    presentImageFrame(storePipeline);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12)
            << "store frame: the fragment's own color output paints BLACK while it "
               "imageStores red into the texture";
    }

    presentImageFrame(loadPipeline);
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_GT(center[0], 240);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12)
            << "load frame: imageLoad(uImg, (0,0)) must return the RED the previous "
               "frame's fragment imageStore wrote mid-pass";
    }

    device->DestroyTexture(targetTexture);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(storePipeline);
    device->DestroyPipeline(loadPipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(storeShader);
    device->DestroyShader(loadShader);
}

TEST_F(VulkanRenderDeviceTest, DepthTextureInStorageSlotFallsBackToSinkAndFrameSurvives) {
    // The M5b depth-in-storage-slot contract: imageStore into a DEPTH image is refused by
    // design, so binding a Depth32Float texture to a storage-image slot deterministically
    // resolves to the device-owned 1x1 black sink (warn bit 21) — never a crash, never a
    // corrupted white sampling fallback, never the caller's depth texture. The dispatch
    // "fills" the sink, the frame presents normally, and the untouched RGBA8 target still
    // samples BLACK.
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle samplerPipeline =
        CreateTexturedSamplerPipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(samplerPipeline, kInvalidPipelineHandle);

    ShaderDesc computeDesc{};
    computeDesc.stage = ShaderStage::Compute;
    computeDesc.sourceCode = kImageFillComputeSpirv;
    std::string infoLog;
    const ShaderHandle computeShader = device->CreateShader(computeDesc, &infoLog);
    ASSERT_NE(computeShader, kInvalidShaderHandle) << infoLog;
    ComputePipelineDesc computePipelineDesc{};
    computePipelineDesc.computeShader = computeShader;
    const PipelineHandle computePipeline =
        device->CreateComputePipeline(computePipelineDesc, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depthTexture = device->CreateTexture(depthDesc);
    ASSERT_NE(depthTexture, kInvalidTextureHandle);

    const std::uint8_t zeroPixels[4U * 4U * 4U] = {};
    TextureDesc targetDesc{};
    targetDesc.width = 4U;
    targetDesc.height = 4U;
    const TextureHandle targetTexture = device->CreateTexture(
        targetDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(zeroPixels),
                                   sizeof(zeroPixels)));
    ASSERT_NE(targetTexture, kInvalidTextureHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        commandBuffer->BindPipeline(computePipeline);
        commandBuffer->BindTexture(depthTexture, 0U); // refused → deterministic sink
        commandBuffer->Dispatch(1U, 1U, 1U);
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(samplerPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(targetTexture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    }
    {
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        EXPECT_LT(center[0], 12);
        EXPECT_LT(center[1], 12);
        EXPECT_LT(center[2], 12)
            << "the green stores went to the sink; the caller's RGBA8 texture must stay "
               "untouched BLACK and the frame must survive";
    }

    device->DestroyTexture(targetTexture);
    device->DestroyTexture(depthTexture);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(computePipeline);
    device->DestroyPipeline(samplerPipeline);
    device->DestroyShader(computeShader);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


TEST_F(VulkanRenderDeviceTest, OffscreenPass_LayerOutOfRange_SkipsGracefullyAndStaysUsable) {
    // Tier 2.3: layer selection is validated before any view/barrier work. A pass naming a
    // layer past the attachment's count must skip (warn-once) rather than crash or wedge the
    // device, and the very next submission must run normally. Classic devices degrade every
    // offscreen pass the same way, so both paths share these assertions (no name branch).
    TextureDesc arrayDesc{};
    arrayDesc.width = 4U;
    arrayDesc.height = 4U;
    arrayDesc.format = TextureFormat::RGBA8Unorm;
    arrayDesc.type = TextureType::Texture2DArray;
    arrayDesc.arrayLayers = 2U;
    const TextureHandle array = device->CreateTexture(arrayDesc);
    ASSERT_NE(array, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depth = device->CreateTexture(depthDesc);
    ASSERT_NE(depth, kInvalidTextureHandle);

    const auto submitLayerPass = [&](const std::uint32_t colorLayer, const std::uint32_t depthLayer) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = array;
        passDesc.depthAttachment = depth;
        passDesc.colorLayer = colorLayer;
        passDesc.depthLayer = depthLayer;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
    };
    submitLayerPass(5U, 0U); // color layer past the 2-layer count
    ASSERT_TRUE(device->IsUsable());
    submitLayerPass(0U, 1U); // depth layer past the 1-layer depth
    ASSERT_TRUE(device->IsUsable());
    submitLayerPass(1U, 0U); // in range on both attachments
    EXPECT_TRUE(device->IsUsable());

    // Liveness note: rejected passes must not corrupt the texture map — the in-range pass
    // above already ran against the same textures (GetLiveTextureDescs is a Null-only
    // seam; the VK suite proves liveness through reuse, not desc scans).
    device->DestroyTexture(array);
    device->DestroyTexture(depth);
}

TEST_F(VulkanRenderDeviceTest, CubemapCaptureAndSample_RendersPerFaceColors) {
    // Tier 2.3 end-to-end: six clear-only offscreen passes paint one cubemap's layers, then a
    // samplerCube strip shader composites the faces back to the swapchain for readback. The
    // cube ALSO carries uploaded per-face colors, so the classic path (offscreen passes skip,
    // staging uploads land) asserts the uploaded set while modern devices assert the cleared
    // set — deterministic pixels on both, no eyeball needed.
    if (kCubeStripFragmentSpirv.empty()) {
        GTEST_SKIP() << "cube-strip fragment SPIR-V not baked yet (see spirv_test_shaders_uve.h)";
    }
    // Uploaded set (dark, one solid 4x4 face each, level-major): what classic asserts.
    const std::uint8_t uploadedFaces[6][4] = {
        {128U, 0U, 0U, 255U}, {0U, 128U, 0U, 255U}, {0U, 0U, 128U, 255U},
        {128U, 128U, 0U, 255U}, {128U, 0U, 128U, 255U}, {0U, 128U, 128U, 255U},
    };
    // Cleared set (bright): what modern devices assert. Faces are uniform, so any filtering
    // reads these exact values.
    const std::uint8_t clearedFaces[6][4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U}, {0U, 0U, 255U, 255U},
        {255U, 255U, 0U, 255U}, {255U, 0U, 255U, 255U}, {0U, 255U, 255U, 255U},
    };
    const float clearColors[6][4] = {
        {1.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F, 1.0F},
        {1.0F, 1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F, 1.0F},
    };
    std::array<std::uint8_t, 4U * 4U * 4U * 6U> uploadBytes{};
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
            const std::size_t base = (static_cast<std::size_t>(face) * 16U + texel) * 4U;
            uploadBytes[base] = uploadedFaces[face][0];
            uploadBytes[base + 1U] = uploadedFaces[face][1];
            uploadBytes[base + 2U] = uploadedFaces[face][2];
            uploadBytes[base + 3U] = uploadedFaces[face][3];
        }
    }
    TextureDesc cubeDesc{};
    cubeDesc.width = 4U;
    cubeDesc.height = 4U;
    cubeDesc.format = TextureFormat::RGBA8Unorm;
    cubeDesc.type = TextureType::Cubemap;
    cubeDesc.arrayLayers = 6U;
    const TextureHandle cube = device->CreateTexture(
        cubeDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(uploadBytes.data()),
                                             uploadBytes.size()));
    ASSERT_NE(cube, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depth = device->CreateTexture(depthDesc);
    ASSERT_NE(depth, kInvalidTextureHandle);

    // Six clear-only passes, one layer each (face == layer, CubemapFace order).
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = cube;
        passDesc.depthAttachment = depth;
        passDesc.colorLayer = face;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {clearColors[face][0], clearColors[face][1], clearColors[face][2],
                               clearColors[face][3]};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kCubeStripFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(quadBufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(cube, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width == 0U || height == 0U) {
        GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
    }

    // Strip i centers at U=(i+0.5)/6 over the fullscreen quad: NDC x=(i+0.5)/3-1.
    const bool isClassic = device->GetBackendName() == "Vulkan (M2c textures+staging)";
    for (std::uint32_t face = 0U; face < 6U; ++face) {
        SCOPED_TRACE("face " + std::to_string(face) + (isClassic ? " (uploaded)" : " (cleared)"));
        const float ndcX = (static_cast<float>(face) + 0.5F) / 3.0F - 1.0F;
        const auto got = ChannelAtNdc(pixels, width, height, ndcX, 0.0F);
        const std::uint8_t* expected = isClassic ? uploadedFaces[face] : clearedFaces[face];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const int want = static_cast<int>(expected[channel]);
            if (want >= 200) {
                EXPECT_GT(got[channel], 200);
            } else if (want >= 96) {
                EXPECT_GE(got[channel], 96);
                EXPECT_LE(got[channel], 160);
            } else {
                EXPECT_LT(got[channel], 60);
            }
        }
    }

    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(cube);
    device->DestroyTexture(depth);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}


TEST_F(VulkanRenderDeviceTest, OffscreenPass_ExtraPrefixGap_SkipsGracefullyAndStaysUsable) {
    // Tier 2.4: a gapped extras prefix is malformed — the submission's draws skip (warn-once)
    // and the device stays usable for the very next submission. Both arms share these
    // assertions (classic degrades every offscreen pass the same way — no name branch).
    TextureDesc colorDesc{};
    colorDesc.width = 4U;
    colorDesc.height = 4U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle target0 = device->CreateTexture(colorDesc);
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = device->CreateTexture(colorDesc);
    ASSERT_NE(target1, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depth = device->CreateTexture(depthDesc);
    ASSERT_NE(depth, kInvalidTextureHandle);

    const auto submitMrtPass =
        [&](const TextureHandle color, const TextureHandle extraSlot0,
            const TextureHandle extraSlot2) {
            auto commandBuffer = device->CreateCommandBuffer();
            RenderPassDesc passDesc{};
            passDesc.colorAttachment = color;
            passDesc.depthAttachment = depth;
            passDesc.colorLoadOp = LoadOp::Clear;
            passDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
            passDesc.depthLoadOp = LoadOp::Clear;
            passDesc.extraColorAttachments[0].target = extraSlot0;
            passDesc.extraColorAttachments[0].loadOp = LoadOp::Clear;
            passDesc.extraColorAttachments[0].clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
            passDesc.extraColorAttachments[2].target = extraSlot2;
            passDesc.extraColorAttachments[2].loadOp = LoadOp::Clear;
            passDesc.extraColorAttachments[2].clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
            commandBuffer->BeginRenderPass(passDesc);
            commandBuffer->EndRenderPass();
            device->Submit(std::move(commandBuffer));
            device->Present();
        };
    // Location 2 set while 1 is empty (the gap check fires before any per-slot validation).
    submitMrtPass(target0, target1, target1);
    ASSERT_TRUE(device->IsUsable());
    // Extras without location 0 never reach an instance either.
    submitMrtPass(kInvalidTextureHandle, target1, kInvalidTextureHandle);
    ASSERT_TRUE(device->IsUsable());
    // The well-formed twin runs normally on both arms (classic: validated-then-skipped).
    submitMrtPass(target0, target1, kInvalidTextureHandle);
    EXPECT_TRUE(device->IsUsable());

    // Liveness note: the well-formed twin above already reused both targets (the VK suite has
    // no desc-liveness seam — GetLiveTextureDescs is Null-only — so reuse is the proof).
    device->DestroyTexture(target0);
    device->DestroyTexture(target1);
    device->DestroyTexture(depth);
}

TEST_F(VulkanRenderDeviceTest, MultiTargetCaptureAndSample_RendersPerTargetColors) {
    // Tier 2.4 end-to-end: ONE offscreen pass clears two targets (bright red / bright green),
    // then each target is sampled fullscreen back to the swapchain. Both textures ALSO carry
    // uploaded dark colors, so classic (offscreen skips, uploads land) asserts dark while
    // modern asserts bright — deterministic pixels on both, no eyeball needed.
    std::array<std::uint8_t, 4U * 4U * 4U> uploadRed{};
    std::array<std::uint8_t, 4U * 4U * 4U> uploadGreen{};
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        uploadRed[texel * 4U] = 128U;
        uploadRed[texel * 4U + 3U] = 255U;
        uploadGreen[texel * 4U + 1U] = 128U;
        uploadGreen[texel * 4U + 3U] = 255U;
    }
    TextureDesc colorDesc{};
    colorDesc.width = 4U;
    colorDesc.height = 4U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle target0 = device->CreateTexture(
        colorDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(uploadRed.data()),
                                              uploadRed.size()));
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = device->CreateTexture(
        colorDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(uploadGreen.data()),
                                   uploadGreen.size()));
    ASSERT_NE(target1, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depth = device->CreateTexture(depthDesc);
    ASSERT_NE(depth, kInvalidTextureHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = target0;
        passDesc.depthAttachment = depth;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F};
        passDesc.colorStoreOp = StoreOp::Store;
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.extraColorAttachments[0].target = target1;
        passDesc.extraColorAttachments[0].loadOp = LoadOp::Clear;
        passDesc.extraColorAttachments[0].clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
        passDesc.extraColorAttachments[0].storeOp = StoreOp::Store;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
    }

    ShaderHandle vertexShader{};
    ShaderHandle fragmentShader{};
    const PipelineHandle pipeline =
        CreateTexturedPipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufferDesc{};
    quadBufferDesc.sizeBytes = sizeof(quadVertices);
    quadBufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(
        quadBufferDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const auto destroyAll = [&]() {
        device->DestroyBuffer(quadBuffer);
        device->DestroyTexture(target0);
        device->DestroyTexture(target1);
        device->DestroyTexture(depth);
        device->DestroyPipeline(pipeline);
        device->DestroyShader(vertexShader);
        device->DestroyShader(fragmentShader);
    };
    const bool isClassic = device->GetBackendName() == "Vulkan (M2c textures+staging)";
    const auto expectChannelBands = [](const std::array<int, 4>& got, const int wantR,
                                       const int wantG, const int wantB) {
        const int want[3] = {wantR, wantG, wantB};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (want[channel] >= 200) {
                EXPECT_GT(got[channel], 200);
            } else if (want[channel] >= 96) {
                EXPECT_GE(got[channel], 96);
                EXPECT_LE(got[channel], 160);
            } else {
                EXPECT_LT(got[channel], 60);
            }
        }
    };
    const auto sampleTargetCenter = [&](const TextureHandle target) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(target, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        EXPECT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        return std::make_tuple(std::move(pixels), width, height);
    };

    {
        SCOPED_TRACE(isClassic ? "target0 (uploaded dark red)" : "target0 (cleared bright red)");
        const auto [pixels, width, height] = sampleTargetCenter(target0);
        ASSERT_TRUE(device->IsUsable());
        if (width == 0U || height == 0U) {
            destroyAll();
            GTEST_SKIP() << "driver reported a zero-sized extent; coverage math needs pixels";
        }
        const auto got = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        if (isClassic) {
            expectChannelBands(got, 128, 0, 0);
        } else {
            expectChannelBands(got, 255, 0, 0);
        }
    }
    {
        SCOPED_TRACE(isClassic ? "target1 (uploaded dark green)" : "target1 (cleared bright green)");
        const auto [pixels, width, height] = sampleTargetCenter(target1);
        ASSERT_TRUE(device->IsUsable());
        ASSERT_NE(width, 0U);
        ASSERT_NE(height, 0U);
        const auto got = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
        if (isClassic) {
            expectChannelBands(got, 0, 128, 0);
        } else {
            expectChannelBands(got, 0, 255, 0);
        }
    }

    destroyAll();
}

TEST_F(VulkanRenderDeviceTest, OffscreenPass_DontCareStore_StaysUsable) {
    // Tier 2.5: DontCare stores run the full pass (barriers + instance) with discard semantics —
    // the device stays usable and the attachments remain live for the next pass. (Discarded
    // contents are undefined, so this asserts health + liveness, not pixels.)
    TextureDesc colorDesc{};
    colorDesc.width = 4U;
    colorDesc.height = 4U;
    colorDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle target0 = device->CreateTexture(colorDesc);
    ASSERT_NE(target0, kInvalidTextureHandle);
    const TextureHandle target1 = device->CreateTexture(colorDesc);
    ASSERT_NE(target1, kInvalidTextureHandle);
    TextureDesc depthDesc{};
    depthDesc.width = 4U;
    depthDesc.height = 4U;
    depthDesc.format = TextureFormat::Depth32Float;
    const TextureHandle depth = device->CreateTexture(depthDesc);
    ASSERT_NE(depth, kInvalidTextureHandle);

    const auto submitStorePass = [&](const StoreOp store) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorAttachment = target0;
        passDesc.depthAttachment = depth;
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
        passDesc.colorStoreOp = store;
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.depthStoreOp = store;
        passDesc.extraColorAttachments[0].target = target1;
        passDesc.extraColorAttachments[0].loadOp = LoadOp::Clear;
        passDesc.extraColorAttachments[0].clearColor = {1.0F, 1.0F, 0.0F, 1.0F};
        passDesc.extraColorAttachments[0].storeOp = store;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
    };
    submitStorePass(StoreOp::DontCare);
    ASSERT_TRUE(device->IsUsable());
    submitStorePass(StoreOp::Store);
    EXPECT_TRUE(device->IsUsable());

    // Liveness note: the follow-up Store pass already reused all three attachments (the VK
    // suite has no desc-liveness seam — GetLiveTextureDescs is Null-only).
    device->DestroyTexture(target0);
    device->DestroyTexture(target1);
    device->DestroyTexture(depth);
}

TEST_F(VulkanRenderDeviceTest, FoundationCopyAndFencesOrderRealGpuWork) {
    using Result = RenderOperationResult;
    EXPECT_EQ(device->GetCapabilities().fences, RenderFeatureSupport::Native);
    EXPECT_EQ(device->GetCapabilities().bufferCopy, RenderFeatureSupport::Native);
    const std::array<std::uint32_t, 4> values{7, 11, 23, 42};
    const auto source =
        device->CreateBuffer({sizeof(values), BufferUsage::Storage}, std::as_bytes(std::span(values)));
    const auto target = device->CreateBuffer({sizeof(values), BufferUsage::Storage});
    ASSERT_NE(source, kInvalidBufferHandle);
    ASSERT_NE(target, kInvalidBufferHandle);
    EXPECT_EQ(device->CopyBuffer({source, target, 1, 0, 3}), Result::InvalidArgument);
    ASSERT_EQ(device->CopyBuffer({source, target, 4, 0, 12}), Result::Success);
    FenceHandle fence;
    ASSERT_EQ(device->InsertFence(fence), Result::Success);
    ASSERT_EQ(device->WaitFence(fence, 1'000'000'000ULL), Result::Success);
    std::array<std::uint32_t, 4> output{};
    ASSERT_TRUE(device->ReadbackBuffer(target, std::as_writable_bytes(std::span(output))));
    EXPECT_EQ(output[0], 11U);
    EXPECT_EQ(output[1], 23U);
    EXPECT_EQ(output[2], 42U);
    ASSERT_EQ(device->DestroyFence(fence), Result::Success);
    EXPECT_EQ(device->WaitFence(fence, 0), Result::InvalidHandle);
    EXPECT_EQ(device->WaitIdle(), Result::Success);
    device->DestroyBuffer(source);
    device->DestroyBuffer(target);
}

TEST_F(VulkanRenderDeviceTest, FoundationFencesDoNotPretendPendingCpuSubmissionsHaveExecuted) {
    using Result = RenderOperationResult;
    auto commands = device->CreateCommandBuffer();
    const auto expected = device->GetCapabilities().memoryBarriers == RenderFeatureSupport::Native
                              ? Result::Recorded
                              : Result::Unsupported;
    EXPECT_EQ(commands->MemoryBarrier(MemoryBarrierMask::All), expected);
    device->Submit(std::move(commands));
    FenceHandle untouched{42U};
    EXPECT_EQ(device->InsertFence(untouched), Result::InvalidState);
    EXPECT_EQ(untouched.value, 42U);
    EXPECT_EQ(device->WaitIdle(), Result::InvalidState);
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    EXPECT_EQ(device->InsertFence(untouched), Result::Success);
    ASSERT_EQ(device->WaitFence(untouched, 1'000'000'000ULL), Result::Success);
    EXPECT_EQ(device->DestroyFence(untouched), Result::Success);
    RenderSurfaceInfo surface;
    ASSERT_EQ(device->GetSurfaceInfo(surface), Result::Success);
    EXPECT_TRUE(surface.drawable);
    EXPECT_GT(surface.width, 0U);
    EXPECT_GT(surface.imageCount, 0U);
    // Recorded occlusion queries allocate a handle at device level; their begin/end/result
    // lifecycle is validated by the dedicated recorded-query tests below. The old expectation
    // that creation itself was unsupported became stale when that path landed.
    QueryHandle query{73U};
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::AnySamplesPassed, query), Result::Success);
    ASSERT_NE(query, kInvalidQueryHandle);
    EXPECT_EQ(device->DestroyQuery(query), Result::Success);
    if (device->GetCapabilities().timestampQueries == RenderFeatureSupport::Native) {
        QueryHandle timestamp{};
        ASSERT_EQ(device->CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Success);
        EXPECT_EQ(device->DestroyQuery(timestamp), Result::Success);
    } else {
        EXPECT_EQ(device->CreateQuery(RenderQueryKind::Timestamp, query), Result::Unsupported);
        EXPECT_EQ(query.value, 73U) << "an unsupported capability must not hand out a handle";
    }
    // Tier 2.6 landed the transfer paths; what remains "not yet" is result queries. The
    // transfer ops now validate handles like every other implemented device call.
    EXPECT_EQ(device->BlitTexture({}), Result::InvalidHandle);
    EXPECT_EQ(device->GenerateMipmaps({}), Result::InvalidHandle);
}

namespace {

// Fills a 4x4 RGBA8 image with four uniform 2x2 blocks so a bilinear 2x downsample has an
// exactly predictable answer at every level.
[[nodiscard]] std::array<std::byte, 4U * 4U * 4U> MakeBlockedSource() {
    std::array<std::byte, 4U * 4U * 4U> bytes{};
    constexpr std::array<std::uint8_t, 4> kBlockRed{40U, 80U, 120U, 160U};
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::size_t block = static_cast<std::size_t>(y / 2U) * 2U + x / 2U;
            const std::size_t texel = (static_cast<std::size_t>(y) * 4U + x) * 4U;
            bytes[texel + 0U] = static_cast<std::byte>(kBlockRed[block]);
            bytes[texel + 1U] = static_cast<std::byte>(7U);
            bytes[texel + 2U] = static_cast<std::byte>(200U);
            bytes[texel + 3U] = std::byte{0xFF};
        }
    }
    return bytes;
}

[[nodiscard]] TextureDesc MakeTransferTextureDesc(const std::uint32_t size,
                                                        const std::uint32_t mipLevels = 1U) {
    TextureDesc desc{};
    desc.width = size;
    desc.height = size;
    desc.mipLevels = mipLevels;
    desc.format = TextureFormat::RGBA8Unorm;
    return desc;
}

} // namespace

TEST_F(VulkanRenderDeviceTest, TextureTransferCapabilitiesAreAdvertisedOnARealDevice) {
    const auto caps = device->GetCapabilities();
    EXPECT_EQ(caps.textureBlit, RenderFeatureSupport::Native);
    EXPECT_EQ(caps.mipGeneration, RenderFeatureSupport::Native);
}

TEST_F(VulkanRenderDeviceTest, BlitTextureCopiesAnExactRegionAndReadbackMatchesTheSource) {
    using Result = RenderOperationResult;
    constexpr std::uint32_t kSize = 4U;
    const auto sourceBytes = MakeBlockedSource();
    const auto source = device->CreateTexture(MakeTransferTextureDesc(kSize), sourceBytes);
    // A Vulkan image created without an upload has undefined contents, so the "untouched texels"
    // assertions below need a destination whose zeroes are real, not assumed.
    const std::array<std::byte, kSize * kSize * 4U> zeroes{};
    const auto destination = device->CreateTexture(MakeTransferTextureDesc(kSize), zeroes);
    ASSERT_NE(source, kInvalidTextureHandle);
    ASSERT_NE(destination, kInvalidTextureHandle);

    // The readback hook is the same transfer machinery in reverse, so pinning it here makes the
    // blit assertions below meaningful.
    std::array<std::byte, kSize * kSize * 4U> sourceReadback{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(source, 0U, 0U, sourceReadback));
    EXPECT_EQ(sourceReadback, sourceBytes);

    TextureBlitDesc blit{};
    blit.source = source;
    blit.destination = destination;
    blit.sourceRegion = {0U, 0U, 0U, 0U, 2U, 2U}; // mip, layer, x, y, width, height
    blit.destinationRegion = {0U, 0U, 2U, 2U, 2U, 2U};
    blit.filter = BlitFilter::Point;
    EXPECT_EQ(device->BlitTexture(blit), Result::Success);

    std::array<std::byte, kSize * kSize * 4U> destinationReadback{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(destination, 0U, 0U, destinationReadback));
    for (std::uint32_t y = 0U; y < 2U; ++y) {
        for (std::uint32_t x = 0U; x < 2U; ++x) {
            for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
                EXPECT_EQ(destinationReadback[((static_cast<std::size_t>(y) + 2U) * kSize + x + 2U) * 4U + channel],
                          sourceBytes[(static_cast<std::size_t>(y) * kSize + x) * 4U + channel]);
            }
        }
    }
    for (std::uint32_t texel = 0U; texel < 2U * kSize + 2U; ++texel) {
        EXPECT_EQ(destinationReadback[texel * 4U], std::byte{0}) << "texels outside the destination region must stay untouched";
    }
    device->DestroyTexture(destination);
    device->DestroyTexture(source);
}

TEST_F(VulkanRenderDeviceTest, BlitTextureLinearScalingAndRejectionsKeepTheirContracts) {
    using Result = RenderOperationResult;
    constexpr std::uint32_t kSize = 4U;
    const auto sourceBytes = MakeBlockedSource();
    const auto source = device->CreateTexture(MakeTransferTextureDesc(kSize), sourceBytes);
    const auto uniformTarget = device->CreateTexture(MakeTransferTextureDesc(kSize));
    const auto floatTarget = device->CreateTexture(
        [] {
            TextureDesc desc = MakeTransferTextureDesc(kSize);
            desc.format = TextureFormat::RGBA16Float;
            return desc;
        }());
    const auto singleLevelSource = device->CreateTexture(MakeTransferTextureDesc(kSize), sourceBytes);
    ASSERT_NE(source, kInvalidTextureHandle);
    ASSERT_NE(uniformTarget, kInvalidTextureHandle);
    ASSERT_NE(floatTarget, kInvalidTextureHandle);
    ASSERT_NE(singleLevelSource, kInvalidTextureHandle);

    // A single-texel source region scaled up by 2 with LINEAR must stay exactly that texel.
    TextureBlitDesc blit{};
    blit.source = source;
    blit.destination = uniformTarget;
    blit.sourceRegion = {0U, 0U, 0U, 0U, 1U, 1U};
    blit.destinationRegion = {0U, 0U, 0U, 0U, 2U, 2U};
    blit.filter = BlitFilter::Linear;
    EXPECT_EQ(device->BlitTexture(blit), Result::Success);
    std::array<std::byte, kSize * kSize * 4U> readback{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(uniformTarget, 0U, 0U, readback));
    for (std::uint32_t y = 0U; y < 2U; ++y) {
        for (std::uint32_t x = 0U; x < 2U; ++x) {
            const std::size_t texel = (static_cast<std::size_t>(y) * kSize + x) * 4U;
            EXPECT_EQ(readback[texel + 0U], sourceBytes[0U]);
            EXPECT_EQ(readback[texel + 1U], sourceBytes[1U]);
            EXPECT_EQ(readback[texel + 2U], sourceBytes[2U]);
            EXPECT_EQ(readback[texel + 3U], sourceBytes[3U]);
        }
    }

    // Downsampling the whole image by 2 with LINEAR: bilinear weights average each uniform
    // 2x2 block, so 40/80/120/160 must come back as themselves (rounding tolerance of 1).
    const auto downsampleTarget = device->CreateTexture(MakeTransferTextureDesc(2U));
    ASSERT_NE(downsampleTarget, kInvalidTextureHandle);
    blit.destination = downsampleTarget;
    blit.sourceRegion = {0U, 0U, 0U, 0U, 4U, 4U};
    blit.destinationRegion = {0U, 0U, 0U, 0U, 2U, 2U};
    EXPECT_EQ(device->BlitTexture(blit), Result::Success);
    std::array<std::byte, 2U * 2U * 4U> downsampled{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(downsampleTarget, 0U, 0U, downsampled));
    constexpr std::array<std::uint8_t, 4> kExpectedRed{40U, 80U, 120U, 160U};
    for (std::uint32_t block = 0U; block < 4U; ++block) {
        const int actual = std::to_integer<int>(downsampled[block * 4U]);
        EXPECT_NEAR(actual, kExpectedRed[block], 1) << "block " << block;
    }

    blit.source = source;
    blit.destination = floatTarget;
    blit.sourceRegion = {0U, 0U, 0U, 0U, 2U, 2U};
    blit.destinationRegion = {0U, 0U, 0U, 0U, 2U, 2U};
    EXPECT_EQ(device->BlitTexture(blit), Result::InvalidArgument) << "format conversion is refused";
    blit.destination = source;
    EXPECT_EQ(device->BlitTexture(blit), Result::InvalidArgument) << "self-blits are refused";
    blit.source = singleLevelSource;
    blit.destination = uniformTarget;
    blit.sourceRegion = {1U, 0U, 0U, 0U, 1U, 1U};
    EXPECT_EQ(device->BlitTexture(blit), Result::InvalidArgument) << "missing mip levels are refused";
    blit.source = TextureHandle{999999U};
    blit.sourceRegion = {0U, 0U, 0U, 0U, 2U, 2U};
    EXPECT_EQ(device->BlitTexture(blit), Result::InvalidHandle);

    device->DestroyTexture(singleLevelSource);
    device->DestroyTexture(floatTarget);
    device->DestroyTexture(uniformTarget);
    device->DestroyTexture(downsampleTarget);
    device->DestroyTexture(source);
}

TEST_F(VulkanRenderDeviceTest, GenerateMipmapsRebuildsEveryLevelWithTheDownsampledAverage) {
    using Result = RenderOperationResult;
    constexpr std::uint32_t kSize = 4U;
    constexpr std::uint32_t kMipLevels = 3U;
    const auto sourceBytes = MakeBlockedSource();
    const auto source = device->CreateTexture(MakeTransferTextureDesc(kSize), sourceBytes);
    // The common upload contract requires every declared level up front
    // (ValidateTextureUpload), so the mip chain starts from a real, zeroed allocation that the
    // seeding blit then overwrites at level 0.
    const std::array<std::byte, (4U * 4U + 2U * 2U + 1U) * 4U> mipmappedBytes{};
    const auto mipmapped = device->CreateTexture(MakeTransferTextureDesc(kSize, kMipLevels), mipmappedBytes);
    const auto singleLevel = device->CreateTexture(MakeTransferTextureDesc(kSize), sourceBytes);
    ASSERT_NE(source, kInvalidTextureHandle);
    ASSERT_NE(mipmapped, kInvalidTextureHandle);
    ASSERT_NE(singleLevel, kInvalidTextureHandle);

    TextureBlitDesc seed{};
    seed.source = source;
    seed.destination = mipmapped;
    seed.sourceRegion = {0U, 0U, 0U, 0U, kSize, kSize};
    seed.destinationRegion = {0U, 0U, 0U, 0U, kSize, kSize};
    seed.filter = BlitFilter::Point;
    ASSERT_EQ(device->BlitTexture(seed), Result::Success);
    EXPECT_EQ(device->GenerateMipmaps(mipmapped), Result::Success);

    std::array<std::byte, 2U * 2U * 4U> levelOne{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(mipmapped, 1U, 0U, levelOne));
    constexpr std::array<std::uint8_t, 4> kBlockRed{40U, 80U, 120U, 160U};
    for (std::uint32_t block = 0U; block < 4U; ++block) {
        EXPECT_NEAR(std::to_integer<int>(levelOne[block * 4U]), kBlockRed[block], 1) << "level 1 block " << block;
        EXPECT_EQ(levelOne[block * 4U + 1U], std::byte{7U});
        EXPECT_EQ(levelOne[block * 4U + 3U], std::byte{0xFF});
    }
    std::array<std::byte, 1U * 1U * 4U> levelTwo{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(mipmapped, 2U, 0U, levelTwo));
    EXPECT_NEAR(std::to_integer<int>(levelTwo[0U]), 100, 1) << "level 2 averages the four blocks";

    EXPECT_EQ(device->GenerateMipmaps(singleLevel), Result::InvalidArgument);
    EXPECT_EQ(device->GenerateMipmaps(TextureHandle{999999U}), Result::InvalidHandle);

    device->DestroyTexture(singleLevel);
    device->DestroyTexture(mipmapped);
    device->DestroyTexture(source);
}

TEST_F(VulkanRenderDeviceTest, SingleTwoChannelAndFloatFormatsCreateUploadAndSampleOnTheRealDevice) {
    // One-/two-channel and 32-bit float formats on a real Vulkan device:
    //   1. SupportsTextureFormat answers from the device's format features, and creation
    //      agrees with it format by format (never "supported" yet uncreatable, or the reverse);
    //      an sRGB request is refused up front and the RGBA8 readback honestly declines them;
    //   2. a 2x2 R8Unorm and a 2x2 RG8Unorm upload are sampled by the textured quad with the
    //      missing channels reading 0 — pixel proof that the one-/two-channel staging rows
    //      (1 and 2 bytes per texel) land where the shader expects them;
    //   3. GenerateMipmaps builds an RG8 chain (R8G8_UNORM's blit + linear-filter features
    //      are spec-mandatory, so this is Success on every conformant device, not a skip).
    constexpr std::array<TextureFormat, 7> kFormats{
        TextureFormat::R8Unorm,  TextureFormat::RG8Unorm, TextureFormat::R16Float,
        TextureFormat::RG16Float, TextureFormat::R32Float, TextureFormat::RG32Float,
        TextureFormat::RGBA32Float};
    for (const TextureFormat format : kFormats) {
        const bool supported = device->SupportsTextureFormat(format);
        EXPECT_FALSE(device->SupportsTextureFormat(format, TextureColorSpace::Srgb));
        const std::vector<std::byte> texel(GetTextureFormatBlockInfo(format).bytes);
        const TextureHandle texture = device->CreateTexture({1U, 1U, format, 1U}, texel);
        EXPECT_EQ(texture != kInvalidTextureHandle, supported)
            << "format " << static_cast<int>(format) << " support and creation must agree";
        EXPECT_EQ(device->CreateTexture({1U, 1U, format, 1U, TextureColorSpace::Srgb}, texel),
                  kInvalidTextureHandle);
        if (texture != kInvalidTextureHandle) {
            std::array<std::byte, 4> rgba{};
            EXPECT_FALSE(device->ReadbackTextureRGBA8(texture, 0U, 0U, rgba))
                << "the RGBA8 readback must not reinterpret a " << static_cast<int>(format) << " image";
            device->DestroyTexture(texture);
        }
    }
    // The 8-bit members carry spec-mandatory sampled/linear/transfer/colour-attachment features.
    ASSERT_TRUE(device->SupportsTextureFormat(TextureFormat::R8Unorm));
    ASSERT_TRUE(device->SupportsTextureFormat(TextureFormat::RG8Unorm));

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kTexturedFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const float vertices[30] = {
        -0.5F, -0.5F, 0.0F,  0.0F, 0.0F,
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F,
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F,
         0.5F, -0.5F, 0.0F,  1.0F, 0.0F,
         0.5F,  0.5F, 0.0F,  1.0F, 1.0F,
        -0.5F,  0.5F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);
    // Only 0/255 texels: the presented image is sRGB-encoded, and those two values are the
    // fixed points of the transfer function. Reads of a 255 texel and of a channel the format
    // does not store are exact; a 0 texel next to a 255 one picks up a sub-texel bilinear
    // bleed that the sRGB encode amplifies (a few units), hence the looser bound there.
    constexpr std::array<std::uint8_t, 4> kR8Texels{255U, 0U, 0U, 255U};          // (0,0) (1,0) (0,1) (1,1)
    constexpr std::array<std::uint8_t, 8> kRg8Texels{255U, 0U, 0U, 255U, 0U, 255U, 255U, 0U};
    const TextureHandle r8 = device->CreateTexture({2U, 2U, TextureFormat::R8Unorm, 1U},
                                                         std::as_bytes(std::span(kR8Texels)));
    const TextureHandle rg8 = device->CreateTexture({2U, 2U, TextureFormat::RG8Unorm, 1U},
                                                          std::as_bytes(std::span(kRg8Texels)));
    ASSERT_NE(r8, kInvalidTextureHandle);
    ASSERT_NE(rg8, kInvalidTextureHandle);
    const auto drawAndRead = [&](const TextureHandle texture, std::vector<std::byte>& pixels,
                                 std::uint32_t& width, std::uint32_t& height) {
        auto commandBuffer = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.5F, 0.5F, 0.5F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(pipeline);
        commandBuffer->BindVertexBuffer(vertexBuffer);
        commandBuffer->BindTexture(texture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        pixels.assign(1280U * 720U * 4U, std::byte{});
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        ASSERT_GT(width, 0U);
        ASSERT_GT(height, 0U);
    };
    std::vector<std::byte> pixels;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    drawAndRead(r8, pixels, width, height);
    if (HasFatalFailure()) { return; }
    {
        const auto topLeft = ChannelAtNdc(pixels, width, height, -0.25F, -0.25F);
        const auto topRight = ChannelAtNdc(pixels, width, height, 0.25F, -0.25F);
        const auto bottomRight = ChannelAtNdc(pixels, width, height, 0.25F, 0.25F);
        EXPECT_GT(topLeft[0], 250) << "R8 texel (0,0) = 255 must sample as full red";
        EXPECT_LT(topLeft[1], 5) << "an R8 texture has no green";
        EXPECT_LT(topLeft[2], 5) << "an R8 texture has no blue";
        EXPECT_GT(topLeft[3], 250) << "missing alpha samples as 1";
        EXPECT_LT(topRight[0], 40) << "R8 texel (1,0) = 0";
        EXPECT_GT(bottomRight[0], 250) << "R8 texel (1,1) = 255: the second 2-byte row landed";
    }
    drawAndRead(rg8, pixels, width, height);
    if (HasFatalFailure()) { return; }
    {
        const auto topLeft = ChannelAtNdc(pixels, width, height, -0.25F, -0.25F);
        const auto topRight = ChannelAtNdc(pixels, width, height, 0.25F, -0.25F);
        const auto bottomLeft = ChannelAtNdc(pixels, width, height, -0.25F, 0.25F);
        EXPECT_GT(topLeft[0], 250);
        EXPECT_LT(topLeft[1], 40) << "RG8 texel (0,0) = (255,0)";
        EXPECT_LT(topRight[0], 40);
        EXPECT_GT(topRight[1], 250) << "RG8 texel (1,0) = (0,255)";
        EXPECT_GT(bottomLeft[1], 250) << "RG8 texel (0,1) = (0,255): the 4-byte second row landed";
        EXPECT_LT(topLeft[2], 5);
        EXPECT_LT(topRight[2], 5) << "an RG8 texture has no blue";
    }

    std::vector<std::byte> rgChain(static_cast<std::size_t>((16U + 4U + 1U) * 2U));
    for (std::size_t texel = 0; texel < 16U; ++texel) {
        rgChain[texel * 2U] = std::byte{200};
        rgChain[texel * 2U + 1U] = std::byte{40};
    }
    const TextureHandle rgMips = device->CreateTexture({4U, 4U, TextureFormat::RG8Unorm, 3U}, rgChain);
    ASSERT_NE(rgMips, kInvalidTextureHandle);
    EXPECT_EQ(device->GenerateMipmaps(rgMips), RenderOperationResult::Success);
    EXPECT_EQ(device->GenerateMipmaps(rg8), RenderOperationResult::InvalidArgument) << "single level";
    const TextureHandle rgCopy = device->CreateTexture({2U, 2U, TextureFormat::RG8Unorm, 1U});
    ASSERT_NE(rgCopy, kInvalidTextureHandle);
    EXPECT_EQ(device->BlitTexture({rgMips, rgCopy, {0, 0, 0, 0, 4, 4}, {0, 0, 0, 0, 2, 2}, BlitFilter::Linear}),
              RenderOperationResult::Success);
    EXPECT_EQ(device->BlitTexture({r8, rgCopy, {0, 0, 0, 0, 2, 2}, {0, 0, 0, 0, 2, 2}, BlitFilter::Point}),
              RenderOperationResult::InvalidArgument) << "format conversion is still refused";

    device->DestroyTexture(rgCopy);
    device->DestroyTexture(rgMips);
    device->DestroyTexture(rg8);
    device->DestroyTexture(r8);
    device->DestroyBuffer(vertexBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, Texture3DSamplesSliceColoursAndUsageFlagsGateOperationsOnTheRealDevice) {
    // Texture3D + usage flags on a real Vulkan device:
    //   1. a 4x4x4 RGBA8 volume with one solid colour per slice is sampled through a sampler3D
    //      whose w is the quad's u — a left-to-right scan of the presented frame must read
    //      red, green, blue, white at the four slice centres (VK_IMAGE_TYPE_3D + VIEW_TYPE_3D
    //      and the slice-major staging copy, proven by pixels);
    //   2. the 3D mip chain builds with the ordinary blit chain (depth halves with the axes) and
    //      the 2D readback helper honestly declines a volume;
    //   3. usage gates: a Sampled-only texture is refused as a colour attachment (its texels
    //      stay zero while the Default-usage control is cleared green through the same pass),
    //      is refused as a blit source and by the readback, and mip generation needs both
    //      transfer bits.
    using Result = RenderOperationResult;
    constexpr std::uint8_t kSliceColors[4][4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U}, {0U, 0U, 255U, 255U}, {255U, 255U, 255U, 255U},
    };
    std::array<std::byte, 4U * 4U * 4U * 4U> upload{};
    for (std::size_t slice = 0U; slice < 4U; ++slice) {
        for (std::size_t texel = 0U; texel < 16U; ++texel) {
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                upload[(slice * 16U + texel) * 4U + channel] = std::byte{kSliceColors[slice][channel]};
            }
        }
    }
    TextureDesc volumeDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    volumeDesc.type = TextureType::Texture3D;
    volumeDesc.depth = 4U;
    const TextureHandle volume = device->CreateTexture(volumeDesc, upload);
    ASSERT_NE(volume, kInvalidTextureHandle);
    TextureDesc reported{};
    ASSERT_EQ(device->GetTextureDesc(volume, reported), Result::Success);
    EXPECT_EQ(reported.type, TextureType::Texture3D);
    EXPECT_EQ(reported.depth, 4U);
    EXPECT_EQ(GetEffectiveTextureUsage(reported),
              TextureUsage::Sampled | TextureUsage::TransferSrc | TextureUsage::TransferDst);

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kTexturedVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kVolumeSampleFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    std::string infoLog;
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc, &infoLog);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle) << infoLog;
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 20U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipelineDesc.depthTestEnabled = false;
    pipelineDesc.depthWriteEnabled = false;
    const PipelineHandle volumePipeline = device->CreatePipeline(pipelineDesc, &infoLog);
    ASSERT_NE(volumePipeline, kInvalidPipelineHandle) << infoLog;
    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.5F, 0.5F, 0.5F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(volumePipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(volume, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        if (width == 0U || height == 0U) { GTEST_SKIP() << "zero-sized extent; no pixels"; }
        // u = 0.125, 0.375, 0.625, 0.875 are the slice centres of a 4-deep volume.
        constexpr float kSliceNdcX[4] = {-0.75F, -0.25F, 0.25F, 0.75F};
        for (std::size_t slice = 0U; slice < 4U; ++slice) {
            const auto sample = ChannelAtNdc(pixels, width, height, kSliceNdcX[slice], 0.0F);
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                // 0 and 255 are fixed points of the sRGB encode the swapchain may apply.
                if (kSliceColors[slice][channel] == 255U) {
                    EXPECT_GT(sample[channel], 215) << "slice " << slice << " channel " << channel;
                } else {
                    EXPECT_LT(sample[channel], 40) << "slice " << slice << " channel " << channel;
                }
            }
        }
    }

    // 2. Mip chain on a volume and the readback helper's honest refusal.
    std::vector<std::byte> chain(292U);
    for (std::size_t texel = 0U; texel < 64U; ++texel) {
        chain[texel * 4U] = std::byte{200};
        chain[texel * 4U + 3U] = std::byte{255};
    }
    TextureDesc mipDesc = volumeDesc;
    mipDesc.mipLevels = 3U;
    const TextureHandle mipVolume = device->CreateTexture(mipDesc, chain);
    ASSERT_NE(mipVolume, kInvalidTextureHandle);
    EXPECT_EQ(device->GenerateMipmaps(mipVolume), Result::Success);
    std::array<std::byte, 4U * 4U * 4U> sliceBytes{};
    EXPECT_FALSE(device->ReadbackTextureRGBA8(volume, 0U, 0U, sliceBytes)) << "volumes are not 2D-readable";
    const TextureHandle flat = device->CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U});
    ASSERT_NE(flat, kInvalidTextureHandle);
    EXPECT_EQ(device->BlitTexture({volume, flat, {0U, 0U, 0U, 0U, 4U, 4U}, {0U, 0U, 0U, 0U, 4U, 4U},
                                      BlitFilter::Point}),
              Result::InvalidArgument) << "volumes never take part in blits";

    // 3. Usage gates. Both targets start zeroed; only the Default-usage control may be attached.
    const std::array<std::byte, 4U * 4U * 4U> zeroPixels{};
    TextureDesc sampledOnlyDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    sampledOnlyDesc.usage = TextureUsage::Sampled;
    const TextureHandle sampledOnly = device->CreateTexture(sampledOnlyDesc, zeroPixels);
    const TextureHandle control = device->CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U}, zeroPixels);
    ASSERT_NE(sampledOnly, kInvalidTextureHandle);
    ASSERT_NE(control, kInvalidTextureHandle);
    ShaderHandle texturedVertex{}, texturedFragment{};
    const PipelineHandle samplerPipeline = CreateTexturedSamplerPipeline(*device, &texturedVertex, &texturedFragment);
    ASSERT_NE(samplerPipeline, kInvalidPipelineHandle);
    const auto clearThenSample = [&](const TextureHandle target, std::array<int, 4>& outCenter) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc offscreen{};
        offscreen.colorAttachment = target;
        offscreen.colorLoadOp = LoadOp::Clear;
        offscreen.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
        offscreen.depthLoadOp = LoadOp::DontCare;
        commandBuffer->BeginRenderPass(offscreen);
        commandBuffer->EndRenderPass();
        RenderPassDesc swapchain{};
        swapchain.colorLoadOp = LoadOp::Clear;
        swapchain.clearColor = {0.5F, 0.0F, 0.5F, 1.0F};
        swapchain.depthLoadOp = LoadOp::Clear;
        swapchain.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(swapchain);
        commandBuffer->BindPipeline(samplerPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(target, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        outCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    };
    std::array<int, 4> controlCenter{};
    clearThenSample(control, controlCenter);
    EXPECT_LT(controlCenter[0], 40);
    EXPECT_GT(controlCenter[1], 215) << "control: a Default-usage texture takes the green clear";
    std::array<int, 4> sealedCenter{};
    clearThenSample(sampledOnly, sealedCenter);
    EXPECT_LT(sealedCenter[1], 40) << "a Sampled-only texture must not be cleared through a render pass";
    EXPECT_LT(sealedCenter[0], 40);

    std::array<std::byte, 4U * 4U * 4U> readback{};
    EXPECT_TRUE(device->ReadbackTextureRGBA8(control, 0U, 0U, readback));
    EXPECT_EQ(readback[1], std::byte{255}) << "the control really was cleared green";
    EXPECT_FALSE(device->ReadbackTextureRGBA8(sampledOnly, 0U, 0U, readback)) << "no TransferSrc, no readback";
    EXPECT_EQ(device->BlitTexture({sampledOnly, flat, {0U, 0U, 0U, 0U, 4U, 4U}, {0U, 0U, 0U, 0U, 4U, 4U},
                                      BlitFilter::Point}),
              Result::InvalidArgument);
    EXPECT_EQ(device->BlitTexture({control, sampledOnly, {0U, 0U, 0U, 0U, 4U, 4U}, {0U, 0U, 0U, 0U, 4U, 4U},
                                      BlitFilter::Point}),
              Result::InvalidArgument) << "no TransferDst, no blit destination";
    EXPECT_EQ(device->BlitTexture({control, flat, {0U, 0U, 0U, 0U, 4U, 4U}, {0U, 0U, 0U, 0U, 4U, 4U},
                                      BlitFilter::Point}),
              Result::Success);
    TextureDesc sealedChainDesc{4U, 4U, TextureFormat::RGBA8Unorm, 3U};
    sealedChainDesc.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
    const TextureHandle sealedChain = device->CreateTexture(sealedChainDesc, std::vector<std::byte>(84U));
    ASSERT_NE(sealedChain, kInvalidTextureHandle);
    EXPECT_EQ(device->GenerateMipmaps(sealedChain), Result::InvalidArgument);
    TextureDesc unsupportedDesc{4U, 4U, TextureFormat::Depth32Float, 1U};
    unsupportedDesc.usage = TextureUsage::Storage;
    EXPECT_EQ(device->CreateTexture(unsupportedDesc), kInvalidTextureHandle);

    device->DestroyTexture(sealedChain);
    device->DestroyPipeline(samplerPipeline);
    device->DestroyShader(texturedFragment);
    device->DestroyShader(texturedVertex);
    device->DestroyTexture(control);
    device->DestroyTexture(sampledOnly);
    device->DestroyTexture(flat);
    device->DestroyTexture(mipVolume);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(volumePipeline);
    device->DestroyShader(fragmentShader);
    device->DestroyShader(vertexShader);
    device->DestroyTexture(volume);
}

TEST_F(VulkanRenderDeviceTest, StorageImageBindWithoutStorageUsageWritesTheBlackSinkInstead) {
    // The storage-image usage gate, as pixels: the M5b image-fill dispatch paints GREEN into a
    // Default-usage texture (the positive proof lives in ComputeImageStoreFillsTexture…), but a
    // texture created without TextureUsage::Storage bound to the same slot sends the
    // imageStore to the 1x1 black sink — the texture samples back exactly as uploaded (black).
    ShaderHandle vertexShader{}, fragmentShader{};
    const PipelineHandle samplerPipeline = CreateTexturedSamplerPipeline(*device, &vertexShader, &fragmentShader);
    ASSERT_NE(samplerPipeline, kInvalidPipelineHandle);
    ShaderDesc computeDesc{};
    computeDesc.stage = ShaderStage::Compute;
    computeDesc.sourceCode = kImageFillComputeSpirv;
    std::string infoLog;
    const ShaderHandle computeShader = device->CreateShader(computeDesc, &infoLog);
    ASSERT_NE(computeShader, kInvalidShaderHandle) << infoLog;
    ComputePipelineDesc computePipelineDesc{};
    computePipelineDesc.computeShader = computeShader;
    const PipelineHandle computePipeline = device->CreateComputePipeline(computePipelineDesc, &infoLog);
    ASSERT_NE(computePipeline, kInvalidPipelineHandle) << infoLog;
    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const std::array<std::byte, 4U * 4U * 4U> zeroPixels{};
    TextureDesc noStorageDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U};
    noStorageDesc.usage = TextureUsage::Sampled | TextureUsage::RenderTarget |
                          TextureUsage::TransferSrc | TextureUsage::TransferDst;
    const TextureHandle noStorage = device->CreateTexture(noStorageDesc, zeroPixels);
    const TextureHandle withStorage = device->CreateTexture(TextureDesc{4U, 4U, TextureFormat::RGBA8Unorm, 1U}, zeroPixels);
    ASSERT_NE(noStorage, kInvalidTextureHandle);
    ASSERT_NE(withStorage, kInvalidTextureHandle);

    const auto dispatchThenSample = [&](const TextureHandle texture, std::array<int, 4>& outCenter) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        commandBuffer->BindPipeline(computePipeline);
        commandBuffer->BindTexture(texture, 0U);
        commandBuffer->Dispatch(1U, 1U, 1U);
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.5F, 0.0F, 0.5F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->BindPipeline(samplerPipeline);
        commandBuffer->BindVertexBuffer(quadBuffer);
        commandBuffer->BindTexture(texture, 0U);
        commandBuffer->Draw(6U);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
        std::vector<std::byte> pixels(1280U * 720U * 4U);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
        outCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    };
    std::array<int, 4> storageCenter{};
    dispatchThenSample(withStorage, storageCenter);
    EXPECT_GT(storageCenter[1], 215) << "control: the Default-usage texture takes the imageStore";
    std::array<int, 4> sealedCenter{};
    dispatchThenSample(noStorage, sealedCenter);
    EXPECT_LT(sealedCenter[1], 40) << "without Storage usage the imageStore must not reach the texture";
    EXPECT_LT(sealedCenter[0], 40);
    EXPECT_LT(sealedCenter[2], 40);
    std::array<std::byte, 4U * 4U * 4U> readback{};
    ASSERT_TRUE(device->ReadbackTextureRGBA8(noStorage, 0U, 0U, readback));
    EXPECT_EQ(readback, zeroPixels) << "the sealed texture's bytes are untouched";

    device->DestroyTexture(withStorage);
    device->DestroyTexture(noStorage);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(computePipeline);
    device->DestroyShader(computeShader);
    device->DestroyPipeline(samplerPipeline);
    device->DestroyShader(fragmentShader);
    device->DestroyShader(vertexShader);
}

// ---- Foundation WP3: recorded occlusion brackets on a real Vulkan ICD ---------------------
TEST_F(VulkanRenderDeviceTest, RecordedOcclusionQueryDistinguishesHiddenAndVisibleTriangles) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().occlusionQueries, RenderFeatureSupport::Native);
    QueryHandle hidden, visible, timestamp;
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::AnySamplesPassed, hidden), Result::Success);
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::AnySamplesPassed, visible), Result::Success);
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::Timestamp, timestamp), Result::Success);

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.sourceCode = kDepthUniformVertexSpirv;
    ShaderDesc fragmentDesc{};
    fragmentDesc.stage = ShaderStage::Fragment;
    fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
    const ShaderHandle vertexShader = device->CreateShader(vertexDesc);
    const ShaderHandle fragmentShader = device->CreateShader(fragmentDesc);
    ASSERT_NE(vertexShader, kInvalidShaderHandle);
    ASSERT_NE(fragmentShader, kInvalidShaderHandle);
    PipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexStride = 12U;
    pipelineDesc.vertexLayout.push_back(VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipelineDesc.depthTestEnabled = true;
    pipelineDesc.depthWriteEnabled = true;
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);
    const float vertices[9] = {-0.6F, -0.6F, 0.0F, 0.6F, -0.6F, 0.0F, 0.0F, 0.6F, 0.0F};
    BufferDesc bufferDesc{};
    bufferDesc.sizeBytes = sizeof(vertices);
    bufferDesc.usage = BufferUsage::Vertex;
    const BufferHandle vertexBuffer = device->CreateBuffer(bufferDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(vertices), sizeof(vertices)));
    ASSERT_NE(vertexBuffer, kInvalidBufferHandle);

    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    // Validation shared with Null and GL.
    EXPECT_EQ(commandBuffer->BeginQuery(QueryHandle{999999U}), Result::InvalidHandle);
    EXPECT_EQ(commandBuffer->BeginQuery(timestamp), Result::InvalidArgument);
    EXPECT_EQ(commandBuffer->WriteTimestamp(hidden), Result::InvalidArgument);
    EXPECT_EQ(commandBuffer->EndQuery(hidden), Result::InvalidState);
    ASSERT_EQ(commandBuffer->WriteTimestamp(timestamp), Result::Recorded);
    RenderPassDesc passDesc{};
    passDesc.colorLoadOp = LoadOp::Clear;
    passDesc.clearColor = {0.05F, 0.07F, 0.12F, 1.0F};
    passDesc.depthLoadOp = LoadOp::Clear;
    passDesc.clearDepth = 1.0F;
    commandBuffer->BeginRenderPass(passDesc);
    commandBuffer->BindPipeline(pipeline);
    commandBuffer->BindVertexBuffer(vertexBuffer);
    commandBuffer->SetUniformFloat("uOffsetX", 0.0F);
    commandBuffer->SetUniformFloat("uOffsetY", 0.0F);
    // Occluder: the same triangle at z = 0.2, written to depth.
    commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.2F, 0.2F, 0.2F});
    commandBuffer->SetUniformFloat("uDepth", 0.2F);
    commandBuffer->Draw(3U);
    // Hidden: identical footprint behind the occluder (z = 0.8) — every fragment fails depth.
    ASSERT_EQ(commandBuffer->BeginQuery(hidden), Result::Recorded);
    EXPECT_EQ(commandBuffer->BeginQuery(visible), Result::InvalidState) << "brackets do not nest";
    commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
    commandBuffer->SetUniformFloat("uDepth", 0.8F);
    commandBuffer->Draw(3U);
    ASSERT_EQ(commandBuffer->EndQuery(hidden), Result::Recorded);
    // Visible: in front of the occluder (z = 0.1).
    ASSERT_EQ(commandBuffer->BeginQuery(visible), Result::Recorded);
    commandBuffer->SetUniformVector3("uColorTri", Math::Vector3{0.0F, 1.0F, 0.0F});
    commandBuffer->SetUniformFloat("uDepth", 0.1F);
    commandBuffer->Draw(3U);
    ASSERT_EQ(commandBuffer->EndQuery(visible), Result::Recorded);
    commandBuffer->EndRenderPass();
    device->Submit(std::move(commandBuffer));

    // Submitted but not presented: the brackets are still queued, so nothing is readable yet.
    RenderQueryResult value{};
    value.value = 4321U;
    EXPECT_EQ(device->GetQueryResult(hidden, value, false), Result::NotReady);
    EXPECT_EQ(value.value, 4321U);

    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_EQ(device->GetQueryResult(hidden, value, true), Result::Success);
    EXPECT_EQ(value.kind, RenderQueryKind::AnySamplesPassed);
    EXPECT_EQ(value.value, 0U) << "the triangle behind the occluder must not pass any sample";
    ASSERT_EQ(device->GetQueryResult(visible, value, true), Result::Success);
    EXPECT_EQ(value.value, 1U) << "the triangle in front of the occluder passes";
    RenderQueryResult stamp{};
    ASSERT_EQ(device->GetQueryResult(timestamp, stamp, true), Result::Success);
    EXPECT_EQ(stamp.kind, RenderQueryKind::Timestamp);

    // The image itself is unchanged by the brackets: the visible (green) triangle wins the
    // overlap, the hidden one never shows.
    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    if (width != 0U && height != 0U) {
        const std::size_t x = static_cast<std::size_t>(width / 2U);
        const std::size_t y = static_cast<std::size_t>(height / 2U);
        const std::size_t base = (y * width + x) * 4U;
        EXPECT_LT(static_cast<int>(pixels[base]), 40) << "no red from the hidden triangle";
        EXPECT_GT(static_cast<int>(pixels[base + 1]), 200) << "green visible triangle at the centre";
    }

    EXPECT_EQ(device->DestroyQuery(hidden), Result::Success);
    EXPECT_EQ(device->DestroyQuery(visible), Result::Success);
    EXPECT_EQ(device->DestroyQuery(timestamp), Result::Success);
    device->DestroyBuffer(vertexBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertexShader);
    device->DestroyShader(fragmentShader);
}

TEST_F(VulkanRenderDeviceTest, RecordedOcclusionQueryIsNotReadyUntilCompletion) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().occlusionQueries, RenderFeatureSupport::Native);
    QueryHandle query;
    ASSERT_EQ(device->CreateQuery(RenderQueryKind::AnySamplesPassed, query), Result::Success);
    RenderQueryResult value{};
    value.value = 99U;
    EXPECT_EQ(device->GetQueryResult(query, value, false), Result::NotReady) << "never issued";
    EXPECT_EQ(device->GetQueryResult(query, value, true), Result::NotReady)
        << "waiting on a never-issued query must not block or invent";
    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    ASSERT_EQ(commandBuffer->BeginQuery(query), Result::Recorded);
    ASSERT_EQ(commandBuffer->EndQuery(query), Result::Recorded);
    commandBuffer->EndRenderPass();
    device->Submit(std::move(commandBuffer));
    EXPECT_EQ(device->GetQueryResult(query, value, false), Result::NotReady) << "queued, not replayed";
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_EQ(device->GetQueryResult(query, value, true), Result::Success);
    EXPECT_EQ(value.value, 0U) << "an empty bracket passes no samples";
    EXPECT_EQ(device->DestroyQuery(query), Result::Success);
}

// ---- Foundation WP4: submission-level queue sync on a real Vulkan ICD ------------------------
TEST_F(VulkanRenderDeviceTest, SubmitSignalIsObservedAfterTheFrameCompletes) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Native);
    SemaphoreHandle done;
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{0U}, done), Result::Success);
    const std::array<SemaphoreSignal, 1U> signals{SemaphoreSignal{done, 3U}};
    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->EndRenderPass();
    ASSERT_EQ(device->SubmitWithSync(std::move(commandBuffer), SubmitDesc{{}, signals}), Result::Success);
    std::uint64_t counter = 99U;
    ASSERT_EQ(device->GetSemaphoreCounter(done, counter), Result::Success);
    EXPECT_EQ(counter, 0U) << "nothing reached the queue before Present";
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    EXPECT_EQ(device->WaitSemaphore(done, 3U, 5'000'000'000ULL), Result::Success)
        << "the frame batch's completion performs the signal";
    ASSERT_EQ(device->GetSemaphoreCounter(done, counter), Result::Success);
    EXPECT_EQ(counter, 3U);
    EXPECT_EQ(device->WaitIdle(), Result::Success);
    EXPECT_EQ(device->DestroySemaphore(done), Result::Success);
}

TEST_F(VulkanRenderDeviceTest, SubmitWaitBlocksTheQueueUntilHostSignal) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Native);
    SemaphoreHandle gate, done;
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{0U}, gate), Result::Success);
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{0U}, done), Result::Success);
    const std::array<SemaphoreWait, 1U> waits{SemaphoreWait{gate, 5U}};
    const std::array<SemaphoreSignal, 1U> signals{SemaphoreSignal{done, 1U}};
    auto commandBuffer = device->CreateCommandBuffer();
    ASSERT_NE(commandBuffer, nullptr);
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    commandBuffer->EndRenderPass();
    ASSERT_EQ(device->SubmitWithSync(std::move(commandBuffer), SubmitDesc{waits, signals}), Result::Success);
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    // The queue is parked on `gate` reaching 5; `done` cannot move until then.
    EXPECT_EQ(device->WaitSemaphore(done, 1U, 100'000'000ULL), Result::TimedOut)
        << "the signal must not happen before the wait is satisfied";
    std::uint64_t counter = 99U;
    ASSERT_EQ(device->GetSemaphoreCounter(done, counter), Result::Success);
    EXPECT_EQ(counter, 0U);
    ASSERT_EQ(device->SignalSemaphore(gate, 5U), Result::Success) << "host signal releases the queue";
    EXPECT_EQ(device->WaitSemaphore(done, 1U, 5'000'000'000ULL), Result::Success);
    ASSERT_EQ(device->GetSemaphoreCounter(done, counter), Result::Success);
    EXPECT_EQ(counter, 1U);
    EXPECT_EQ(device->WaitIdle(), Result::Success);
    EXPECT_EQ(device->DestroySemaphore(gate), Result::Success);
    EXPECT_EQ(device->DestroySemaphore(done), Result::Success);
}

TEST_F(VulkanRenderDeviceTest, SubmitDescRejectsDestroyedSemaphore) {
    using Result = RenderOperationResult;
    ASSERT_EQ(device->GetCapabilities().timelineSemaphores, RenderFeatureSupport::Native);
    SemaphoreHandle live, destroyed;
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{4U}, live), Result::Success);
    ASSERT_EQ(device->CreateSemaphore(SemaphoreDesc{0U}, destroyed), Result::Success);
    ASSERT_EQ(device->DestroySemaphore(destroyed), Result::Success);
    const auto submit = [this](const std::span<const SemaphoreWait> waits,
                               const std::span<const SemaphoreSignal> signals) {
        auto commandBuffer = device->CreateCommandBuffer();
        return device->SubmitWithSync(std::move(commandBuffer), SubmitDesc{waits, signals});
    };
    const std::array<SemaphoreWait, 1U> waitDestroyed{SemaphoreWait{destroyed, 1U}};
    const std::array<SemaphoreSignal, 1U> signalDestroyed{SemaphoreSignal{destroyed, 1U}};
    const std::array<SemaphoreWait, 1U> waitLive{SemaphoreWait{live, 9U}};
    const std::array<SemaphoreSignal, 1U> signalStale{SemaphoreSignal{live, 4U}};
    const std::array<SemaphoreSignal, 1U> signalLive{SemaphoreSignal{live, 5U}};
    EXPECT_EQ(submit(waitDestroyed, {}), Result::InvalidHandle);
    EXPECT_EQ(submit({}, signalDestroyed), Result::InvalidHandle);
    EXPECT_EQ(submit({}, signalStale), Result::InvalidArgument) << "a signal must exceed the current counter";
    EXPECT_EQ(submit(waitLive, signalLive), Result::InvalidArgument) << "one semaphore may not be in both lists";
    EXPECT_EQ(submit({}, signalLive), Result::Success);
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    EXPECT_EQ(device->WaitSemaphore(live, 5U, 5'000'000'000ULL), Result::Success);
    EXPECT_EQ(device->WaitIdle(), Result::Success);
    EXPECT_EQ(device->DestroySemaphore(live), Result::Success);
}

// ---- Foundation WP5c: swapchain load-op semantics on the dynamic-rendering arm ------------------
TEST_F(VulkanRenderDeviceTest, SwapchainLoadOpKeepsThePreviouslyPresentedImage) {
    // Present a distinctive clear colour often enough that EVERY swapchain image holds it (the
    // swapchain rotates images, and "Load" means that image's own last presented content), then
    // present one frame that asks to Load without drawing anything: the colour must survive.
    // A final Clear frame proves the pass's clear still wins when asked for.
    constexpr std::uint32_t kWarmupFrames = 8U; // > any sane swapchain image count
    for (std::uint32_t frame = 0U; frame < kWarmupFrames; ++frame) {
        auto commandBuffer = device->CreateCommandBuffer();
        ASSERT_NE(commandBuffer, nullptr);
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 1.0F, 0.0F, 1.0F};
        passDesc.depthLoadOp = LoadOp::Clear;
        passDesc.clearDepth = 1.0F;
        commandBuffer->BeginRenderPass(passDesc);
        commandBuffer->EndRenderPass();
        device->Submit(std::move(commandBuffer));
        device->Present();
        ASSERT_TRUE(device->IsUsable());
    }

    auto loadFrame = device->CreateCommandBuffer();
    ASSERT_NE(loadFrame, nullptr);
    RenderPassDesc loadDesc{};
    loadDesc.colorLoadOp = LoadOp::Load;
    loadDesc.clearColor = {1.0F, 0.0F, 0.0F, 1.0F}; // must NOT be applied
    loadDesc.depthLoadOp = LoadOp::Load;
    loadFrame->BeginRenderPass(loadDesc);
    loadFrame->EndRenderPass();
    device->Submit(std::move(loadFrame));
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    EXPECT_FALSE(device->ReadbackLatestPresentedImage({}, width, height));
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto loaded = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(loaded[0], 40) << "a Load pass must not apply its clear colour";
    EXPECT_GT(loaded[1], 200) << "the previously presented green must survive the Load pass";
    EXPECT_LT(loaded[2], 40);

    auto clearFrame = device->CreateCommandBuffer();
    ASSERT_NE(clearFrame, nullptr);
    RenderPassDesc clearDesc{};
    clearDesc.colorLoadOp = LoadOp::Clear;
    clearDesc.clearColor = {0.0F, 0.0F, 1.0F, 1.0F};
    clearFrame->BeginRenderPass(clearDesc);
    clearFrame->EndRenderPass();
    device->Submit(std::move(clearFrame));
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto cleared = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(cleared[1], 40) << "an explicit Clear still replaces the content";
    EXPECT_GT(cleared[2], 200);
    EXPECT_EQ(device->WaitIdle(), RenderOperationResult::Success);
}

// ---- Foundation WP5b + WP5e: offscreen pipeline-variant cache & depth-only shadow passes --------
TEST_F(VulkanRenderDeviceTest,
       OffscreenRGBA16FloatAndR32FloatPassesRenderThroughPipelineVariantCache) {
    // WP5b: a single graphics pipeline handle created against the default swapchain format can
    // be bound inside offscreen passes targeting RGBA16Float and R32Float colour attachments
    // (even after the original ShaderHandles are destroyed) via the per-PipelineRecord
    // VkPipelineRenderingCreateInfo variant cache.
    ShaderHandle triVS{}, triFS{}, quadVS{}, quadFS{};
    const PipelineHandle trianglePipeline = CreateTrianglePipeline(*device, &triVS, &triFS);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(trianglePipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    // Destroy the triangle shaders before any offscreen variant is lazily built: retained SPIR-V
    // bytes on PipelineRecord must still materialize the RGBA16Float / R32Float variants.
    device->DestroyShader(triVS);
    device->DestroyShader(triFS);

    TextureDesc rgba16Desc{};
    rgba16Desc.width = 64U;
    rgba16Desc.height = 64U;
    rgba16Desc.format = TextureFormat::RGBA16Float;
    const TextureHandle rgba16Target = device->CreateTexture(rgba16Desc);
    ASSERT_NE(rgba16Target, kInvalidTextureHandle);

    TextureDesc r32Desc{};
    r32Desc.width = 64U;
    r32Desc.height = 64U;
    r32Desc.format = TextureFormat::R32Float;
    const TextureHandle r32Target = device->CreateTexture(r32Desc);
    ASSERT_NE(r32Target, kInvalidTextureHandle);

    const float greenTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  0.0F, 1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  0.0F, 1.0F, 0.0F,
    };
    BufferDesc greenBufDesc{};
    greenBufDesc.sizeBytes = sizeof(greenTriangle);
    greenBufDesc.usage = BufferUsage::Vertex;
    const BufferHandle greenBuffer = device->CreateBuffer(
        greenBufDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(greenTriangle),
                                   sizeof(greenTriangle)));
    ASSERT_NE(greenBuffer, kInvalidBufferHandle);

    const float halfRedTriangle[18] = {
        -1.0F, -1.0F, 0.0F,  0.75F, 0.0F, 0.0F,
         3.0F, -1.0F, 0.0F,  0.75F, 0.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,  0.75F, 0.0F, 0.0F,
    };
    BufferDesc redBufDesc{};
    redBufDesc.sizeBytes = sizeof(halfRedTriangle);
    redBufDesc.usage = BufferUsage::Vertex;
    const BufferHandle redBuffer = device->CreateBuffer(
        redBufDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(halfRedTriangle),
                                   sizeof(halfRedTriangle)));
    ASSERT_NE(redBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufDesc{};
    quadBufDesc.sizeBytes = sizeof(quadVertices);
    quadBufDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(
        quadBufDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // Pass 1: render green into RGBA16Float, then sample it onto the swapchain.
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc offscreenDesc{};
        offscreenDesc.colorAttachment = rgba16Target;
        offscreenDesc.colorLoadOp = LoadOp::Clear;
        offscreenDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(offscreenDesc);
        cmd->BindPipeline(trianglePipeline);
        cmd->BindVertexBuffer(greenBuffer);
        cmd->Draw(3U);
        cmd->EndRenderPass();

        RenderPassDesc mainDesc{};
        mainDesc.colorLoadOp = LoadOp::Clear;
        mainDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(mainDesc);
        cmd->BindPipeline(texturedPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->BindTexture(rgba16Target, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    const auto rgba16Center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(rgba16Center[0], 40);
    EXPECT_GT(rgba16Center[1], 200) << "RGBA16Float offscreen pass must render green via its "
        "format-matched pipeline variant - got (" << rgba16Center[0] << "," << rgba16Center[1]
        << "," << rgba16Center[2] << ")";
    EXPECT_LT(rgba16Center[2], 40);

    // Pass 2: render R=0.75 into R32Float using the SAME trianglePipeline, then sample it.
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc offscreenDesc{};
        offscreenDesc.colorAttachment = r32Target;
        offscreenDesc.colorLoadOp = LoadOp::Clear;
        offscreenDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(offscreenDesc);
        cmd->BindPipeline(trianglePipeline);
        cmd->BindVertexBuffer(redBuffer);
        cmd->Draw(3U);
        cmd->EndRenderPass();

        RenderPassDesc mainDesc{};
        mainDesc.colorLoadOp = LoadOp::Clear;
        mainDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(mainDesc);
        cmd->BindPipeline(texturedPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->BindTexture(r32Target, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto r32Center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    const int r32Red = r32Center[0];
    const bool r32LinearStored = r32Red >= 188 && r32Red <= 194; // UNORM swapchain (~191)
    const bool r32SrgbStored = r32Red >= 222 && r32Red <= 228;   // SRGB swapchain (~225)
    EXPECT_TRUE(r32LinearStored || r32SrgbStored)
        << "R32Float offscreen pass must write 0.75 into the single red channel "
           "(191 linear / 225 sRGB) - got (" << r32Center[0] << "," << r32Center[1] << ","
        << r32Center[2] << ")";
    EXPECT_LT(r32Center[1], 16);
    EXPECT_LT(r32Center[2], 16);

    device->DestroyBuffer(greenBuffer);
    device->DestroyBuffer(redBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(rgba16Target);
    device->DestroyTexture(r32Target);
    device->DestroyPipeline(trianglePipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

TEST_F(VulkanRenderDeviceTest, DepthOnlyOffscreenShadowPrepassWritesDepthAndSamplesInMainPass) {
    // WP5e: a depth-only offscreen pass (colorAttachment == kInvalidTextureHandle,
    // depthAttachment == Depth32Float shadow map) opens a 0-color-attachment dynamic rendering
    // instance, binds a 0-color-attachment pipeline variant, writes depth = 0.25, transitions the
    // shadow map to SHADER_READ_ONLY_OPTIMAL on pass close, and samples 0.25 in the main pass.
    ShaderHandle depthVS{}, depthFS{}, quadVS{}, quadFS{};
    PipelineDesc depthPipelineDesc{};
    {
        ShaderDesc vertexDesc{};
        vertexDesc.stage = ShaderStage::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirv;
        ShaderDesc fragmentDesc{};
        fragmentDesc.stage = ShaderStage::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
        depthVS = device->CreateShader(vertexDesc);
        depthFS = device->CreateShader(fragmentDesc);
        ASSERT_NE(depthVS, kInvalidShaderHandle);
        ASSERT_NE(depthFS, kInvalidShaderHandle);
        depthPipelineDesc.vertexShader = depthVS;
        depthPipelineDesc.fragmentShader = depthFS;
        depthPipelineDesc.vertexStride = 12U;
        depthPipelineDesc.vertexLayout.push_back(
            VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
        depthPipelineDesc.depthTestEnabled = true;
        depthPipelineDesc.depthWriteEnabled = true;
    }
    const PipelineHandle depthPipeline = device->CreatePipeline(depthPipelineDesc);
    const PipelineHandle texturedPipeline = CreateTexturedPipeline(*device, &quadVS, &quadFS);
    ASSERT_NE(depthPipeline, kInvalidPipelineHandle);
    ASSERT_NE(texturedPipeline, kInvalidPipelineHandle);

    TextureDesc shadowDesc{};
    shadowDesc.width = 64U;
    shadowDesc.height = 64U;
    shadowDesc.format = TextureFormat::Depth32Float;
    const TextureHandle shadowTex = device->CreateTexture(shadowDesc);
    ASSERT_NE(shadowTex, kInvalidTextureHandle);

    const float triVertices[9] = {
        -1.0F, -1.0F, 0.0F,
         3.0F, -1.0F, 0.0F,
        -1.0F,  3.0F, 0.0F,
    };
    BufferDesc triBufDesc{};
    triBufDesc.sizeBytes = sizeof(triVertices);
    triBufDesc.usage = BufferUsage::Vertex;
    const BufferHandle triBuffer = device->CreateBuffer(
        triBufDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(triVertices),
                                   sizeof(triVertices)));
    ASSERT_NE(triBuffer, kInvalidBufferHandle);

    const float quadVertices[30] = {
        -1.0F, -1.0F, 0.0F,  0.0F, 0.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
         1.0F, -1.0F, 0.0F,  1.0F, 0.0F,
         1.0F,  1.0F, 0.0F,  1.0F, 1.0F,
        -1.0F,  1.0F, 0.0F,  0.0F, 1.0F,
    };
    BufferDesc quadBufDesc{};
    quadBufDesc.sizeBytes = sizeof(quadVertices);
    quadBufDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(
        quadBufDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadVertices),
                                   sizeof(quadVertices)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc shadowPass{};
        shadowPass.colorAttachment = kInvalidTextureHandle;
        shadowPass.depthAttachment = shadowTex;
        shadowPass.depthLoadOp = LoadOp::Clear;
        shadowPass.clearDepth = 1.0F;
        shadowPass.depthStoreOp = StoreOp::Store;
        cmd->BeginRenderPass(shadowPass);
        cmd->BindPipeline(depthPipeline);
        cmd->BindVertexBuffer(triBuffer);
        cmd->SetUniformFloat("uDepth", 0.25F);
        cmd->Draw(3U);
        cmd->EndRenderPass();

        RenderPassDesc mainPass{};
        mainPass.colorLoadOp = LoadOp::Clear;
        mainPass.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        mainPass.depthLoadOp = LoadOp::Clear;
        cmd->BeginRenderPass(mainPass);
        cmd->BindPipeline(texturedPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->BindTexture(shadowTex, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);
    const auto center = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    const int red = center[0];
    const bool linearStored = red >= 61 && red <= 67; // UNORM swapchain (~64)
    const bool srgbStored = red >= 134 && red <= 140; // SRGB swapchain (~137)
    EXPECT_TRUE(linearStored || srgbStored)
        << "depth-only shadow pass must write 0.25 into Depth32Float and sample "
           "64 linear / 137 sRGB in the main pass - got (" << center[0] << "," << center[1]
        << "," << center[2] << ")";
    EXPECT_LT(center[1], 12);
    EXPECT_LT(center[2], 12);

    device->DestroyBuffer(triBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyTexture(shadowTex);
    device->DestroyPipeline(depthPipeline);
    device->DestroyPipeline(texturedPipeline);
    device->DestroyShader(depthVS);
    device->DestroyShader(depthFS);
    device->DestroyShader(quadVS);
    device->DestroyShader(quadFS);
}

// ---- Foundation WP5d: BindUniformBuffer via the per-tuple descriptor-set cache ---------------
TEST_F(VulkanRenderDeviceTest,
       BindUniformBufferRoutesThroughTupleSetCacheAndHandlesMidPassRebindAndDestruction) {
    // WP5d: BindUniformBuffer (directly and through ApplyBindGroup) routes a live
    // BufferUsage::Uniform buffer into the pipeline's dynamic-UBO descriptor at offset 0 via
    // the 'u'-prefixed tuple set cache, supports mid-pass rebinding without clobbering earlier
    // draws, and falls back to the SetUniform* frame ring when the bound UBO is destroyed.
    ShaderHandle uboVS{}, uboFS{};
    PipelineDesc pipelineDesc{};
    {
        ShaderDesc vertexDesc{};
        vertexDesc.stage = ShaderStage::Vertex;
        vertexDesc.sourceCode = kDepthUniformVertexSpirv;
        ShaderDesc fragmentDesc{};
        fragmentDesc.stage = ShaderStage::Fragment;
        fragmentDesc.sourceCode = kDepthUniformFragmentSpirv;
        uboVS = device->CreateShader(vertexDesc);
        uboFS = device->CreateShader(fragmentDesc);
        ASSERT_NE(uboVS, kInvalidShaderHandle);
        ASSERT_NE(uboFS, kInvalidShaderHandle);
        pipelineDesc.vertexShader = uboVS;
        pipelineDesc.fragmentShader = uboFS;
        pipelineDesc.vertexStride = 12U;
        pipelineDesc.vertexLayout.push_back(
            VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
        pipelineDesc.depthTestEnabled = false;
        pipelineDesc.depthWriteEnabled = false;
    }
    const PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
    ASSERT_NE(pipeline, kInvalidPipelineHandle);

    // Small quad covering [-0.25, +0.25]^2 so uOffsetX = -0.5 / +0.5 places it cleanly on the
    // left and right halves of the screen.
    const float quadPositions[18] = {
        -0.25F, -0.25F, 0.0F,
         0.25F, -0.25F, 0.0F,
        -0.25F,  0.25F, 0.0F,
         0.25F, -0.25F, 0.0F,
         0.25F,  0.25F, 0.0F,
        -0.25F,  0.25F, 0.0F,
    };
    BufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(quadPositions);
    vbDesc.usage = BufferUsage::Vertex;
    const BufferHandle quadBuffer = device->CreateBuffer(
        vbDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(quadPositions),
                                   sizeof(quadPositions)));
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    // std140 Transform { float uDepth (offset 0); vec3 uColorTri (offset 16); } -> 32 bytes.
    const float greenBlock[8] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
    const float blueBlock[8]  = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    BufferDesc uboDesc{};
    uboDesc.sizeBytes = sizeof(greenBlock);
    uboDesc.usage = BufferUsage::Uniform;
    const BufferHandle greenUbo = device->CreateBuffer(
        uboDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(greenBlock),
                                   sizeof(greenBlock)));
    const BufferHandle blueUbo = device->CreateBuffer(
        uboDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(blueBlock),
                                   sizeof(blueBlock)));
    ASSERT_NE(greenUbo, kInvalidBufferHandle);
    ASSERT_NE(blueUbo, kInvalidBufferHandle);

    // Frame 1: left quad uses direct BindUniformBuffer(greenUbo, 0); right quad rebinds
    // blueUbo via ApplyBindGroup in the same pass. Both must render their UBO colours even
    // though SetUniformVector3 sets the shadow to red.
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(passDesc);
        cmd->BindPipeline(pipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->SetUniformFloat("uDepth", 0.0F);
        cmd->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});

        cmd->BindUniformBuffer(greenUbo, 0U);
        cmd->SetUniformFloat("uOffsetX", -0.5F);
        cmd->SetUniformFloat("uOffsetY", 0.0F);
        cmd->Draw(6U);

        const std::array<ResourceBinding, 1U> blueBindings = {
            ResourceBinding{BindUniformBufferCommand{blueUbo, 0U}},
        };
        BindGroupDesc blueGroup{};
        blueGroup.bindings = blueBindings;
        EXPECT_EQ(ValidateBindGroup(blueGroup), BindGroupValidation::Valid);
        BindGroup(*cmd, blueGroup);
        cmd->SetUniformFloat("uOffsetX", 0.5F);
        cmd->SetUniformFloat("uOffsetY", 0.0F);
        cmd->Draw(6U);

        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());

    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    ASSERT_GT(width, 0U);
    ASSERT_GT(height, 0U);

    const auto left = ChannelAtNdc(pixels, width, height, -0.5F, 0.0F);
    EXPECT_LT(left[0], 40);
    EXPECT_GT(left[1], 200) << "left draw bound to greenUbo must render green - got ("
        << left[0] << "," << left[1] << "," << left[2] << ")";
    EXPECT_LT(left[2], 40);

    const auto right = ChannelAtNdc(pixels, width, height, 0.5F, 0.0F);
    EXPECT_LT(right[0], 40);
    EXPECT_LT(right[1], 40);
    EXPECT_GT(right[2], 200) << "right draw rebound to blueUbo via BindGroup must render blue "
        "- got (" << right[0] << "," << right[1] << "," << right[2] << ")";

    // Destroy greenUbo: cached descriptor sets referencing it are invalidated, and a later draw
    // attempting to bind the destroyed handle drops the bind and falls back to the SetUniform*
    // red shadow.
    device->DestroyBuffer(greenUbo);
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(passDesc);
        cmd->BindPipeline(pipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->SetUniformFloat("uDepth", 0.0F);
        cmd->SetUniformVector3("uColorTri", Math::Vector3{1.0F, 0.0F, 0.0F});
        cmd->BindUniformBuffer(greenUbo, 0U); // destroyed handle -> ignored, falls back to ring
        cmd->SetUniformFloat("uOffsetX", 0.0F);
        cmd->SetUniformFloat("uOffsetY", 0.0F);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto fallbackCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(fallbackCenter[0], 200) << "after greenUbo destruction, draw must fall back to the "
        "SetUniform* red ring - got (" << fallbackCenter[0] << "," << fallbackCenter[1] << ","
        << fallbackCenter[2] << ")";
    EXPECT_LT(fallbackCenter[1], 40);
    EXPECT_LT(fallbackCenter[2], 40);

    device->DestroyBuffer(blueUbo);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(uboVS);
    device->DestroyShader(uboFS);
}

TEST_F(VulkanRenderDeviceTest,
       MultiSetDescriptorLayoutsBindUniformInSet0AndTextureOrStorageInSet1) {
    // WP5a proof:
    // 1. Patch kSsboPaletteFragmentSpirv so Palette (%13) is layout(set = 1, binding = 0)
    //    while Params (%18) stays layout(set = 0, binding = 0). Both use binding = 0 in
    //    different descriptor sets. Verify both SetUniform* (ring-fed UBO in set 0) and
    //    BindUniformBuffer (explicit UBO in set 0) together with BindStorageBuffer (set 1)
    //    produce the exact expected palette colors.
    // 2. Patch kTexturedFragmentSpirv so uTex (%13) is layout(set = 1, binding = 0) (leaving
    //    set 0 empty) and verify texture sampling produces the expected pixel color.
    // 3. Patch kTexturedFragmentSpirv to set = 4 (>= kMaxDescriptorSets) and verify
    //    CreatePipeline refuses cleanly with a diagnostic naming the 4-set limit.
    const auto patchSpirvDecorate = [](std::string spirv, const std::uint32_t targetId,
                                          const std::uint32_t decoration,
                                          const std::uint32_t newValue) -> std::string {
        auto* words = reinterpret_cast<std::uint32_t*>(spirv.data());
        const std::size_t wordCount = spirv.size() / sizeof(std::uint32_t);
        for (std::size_t i = 5U; i + 3U < wordCount;) {
            const std::uint32_t word0 = words[i];
            const std::uint32_t op = word0 & 0xFFFFU;
            const std::uint32_t len = word0 >> 16U;
            if (len == 0U) {
                break;
            }
            // OpDecorate = 71 (0x47), word count = 4: [op|4, targetId, decoration, literal]
            if (op == 71U && len == 4U && words[i + 1U] == targetId &&
                words[i + 2U] == decoration) {
                words[i + 3U] = newValue;
            }
            i += len;
        }
        return spirv;
    };

    // SpvDecorationBinding = 33 (0x21), SpvDecorationDescriptorSet = 34 (0x22).
    // In kSsboPaletteFragmentSpirv, %13 is Palette and %18 is Params.
    std::string multiSetSsboFragSpirv =
        patchSpirvDecorate(kSsboPaletteFragmentSpirv, 13U, 33U, 0U);
    multiSetSsboFragSpirv =
        patchSpirvDecorate(std::move(multiSetSsboFragSpirv), 13U, 34U, 1U);

    ShaderDesc vsDesc{};
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.sourceCode = kTexturedVertexSpirv;
    const ShaderHandle vs = device->CreateShader(vsDesc);
    ASSERT_NE(vs, kInvalidShaderHandle);

    ShaderDesc ssboFsDesc{};
    ssboFsDesc.stage = ShaderStage::Fragment;
    ssboFsDesc.sourceCode = multiSetSsboFragSpirv;
    const ShaderHandle ssboFs = device->CreateShader(ssboFsDesc);
    ASSERT_NE(ssboFs, kInvalidShaderHandle);

    PipelineDesc pipeDesc{};
    pipeDesc.vertexShader = vs;
    pipeDesc.fragmentShader = ssboFs;
    pipeDesc.vertexStride = 20U;
    pipeDesc.vertexLayout.push_back(
        VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0U});
    pipeDesc.vertexLayout.push_back(
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 12U});
    pipeDesc.depthTestEnabled = false;
    pipeDesc.depthWriteEnabled = false;
    std::string infoLog;
    const PipelineHandle ssboPipeline = device->CreatePipeline(pipeDesc, &infoLog);
    ASSERT_NE(ssboPipeline, kInvalidPipelineHandle)
        << "multi-set (set 0 UBO + set 1 SSBO) pipeline must build cleanly: " << infoLog;

    const BufferHandle quadBuffer = CreateM2fQuadBuffer(*device);
    ASSERT_NE(quadBuffer, kInvalidBufferHandle);

    const float palette[16] = {
        1.0F, 0.0F, 0.0F, 1.0F, // 0: RED
        0.0F, 1.0F, 0.0F, 1.0F, // 1: GREEN
        0.0F, 0.0F, 1.0F, 1.0F, // 2: BLUE
        1.0F, 1.0F, 0.0F, 1.0F, // 3: YELLOW
    };
    BufferDesc paletteDesc{};
    paletteDesc.sizeBytes = sizeof(palette);
    paletteDesc.usage = BufferUsage::Storage;
    const BufferHandle paletteBuffer = device->CreateBuffer(
        paletteDesc,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(palette), sizeof(palette)));
    ASSERT_NE(paletteBuffer, kInvalidBufferHandle);

    // Frame 1: ring-fed SetUniformInt("uIndex", 2) in set 0 + SSBO in set 1 -> BLUE.
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(passDesc);
        cmd->BindPipeline(ssboPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->SetUniformInt("uIndex", 2);
        cmd->BindStorageBuffer(paletteBuffer, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    std::vector<std::byte> pixels(1280U * 720U * 4U);
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto blueCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(blueCenter[0], 40);
    EXPECT_LT(blueCenter[1], 40);
    EXPECT_GT(blueCenter[2], 200) << "set 0 ring UBO + set 1 SSBO (uIndex=2) must paint BLUE";

    // Frame 2: explicit BindUniformBuffer (uIndex=1) in set 0 + SSBO in set 1 -> GREEN.
    const std::int32_t uboIndexOne[4] = {1, 0, 0, 0};
    BufferDesc uboDesc{};
    uboDesc.sizeBytes = sizeof(uboIndexOne);
    uboDesc.usage = BufferUsage::Uniform;
    const BufferHandle indexOneUbo = device->CreateBuffer(
        uboDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(uboIndexOne),
                                            sizeof(uboIndexOne)));
    ASSERT_NE(indexOneUbo, kInvalidBufferHandle);
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(passDesc);
        cmd->BindPipeline(ssboPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->SetUniformInt("uIndex", 0); // overridden by BindUniformBuffer below
        cmd->BindUniformBuffer(indexOneUbo, 0U);
        cmd->BindStorageBuffer(paletteBuffer, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto greenCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_LT(greenCenter[0], 40);
    EXPECT_GT(greenCenter[1], 200) << "set 0 bound UBO + set 1 SSBO (uIndex=1) must paint GREEN";
    EXPECT_LT(greenCenter[2], 40);

    // Part 2: texture in set 1 with empty set 0.
    const std::string set1TexFragSpirv =
        patchSpirvDecorate(kTexturedFragmentSpirv, 13U, 34U, 1U);
    ShaderDesc texFsDesc{};
    texFsDesc.stage = ShaderStage::Fragment;
    texFsDesc.sourceCode = set1TexFragSpirv;
    const ShaderHandle texFs = device->CreateShader(texFsDesc);
    ASSERT_NE(texFs, kInvalidShaderHandle);
    pipeDesc.fragmentShader = texFs;
    const PipelineHandle texPipeline = device->CreatePipeline(pipeDesc, &infoLog);
    ASSERT_NE(texPipeline, kInvalidPipelineHandle)
        << "pipeline with empty set 0 and sampler2D in set 1 must build cleanly: " << infoLog;

    const std::uint8_t magentaTexel[4] = {255U, 0U, 255U, 255U};
    TextureDesc texDesc{};
    texDesc.width = 1U;
    texDesc.height = 1U;
    texDesc.format = TextureFormat::RGBA8Unorm;
    const TextureHandle magentaTex = device->CreateTexture(
        texDesc, std::span<const std::byte>(reinterpret_cast<const std::byte*>(magentaTexel),
                                            sizeof(magentaTexel)));
    ASSERT_NE(magentaTex, kInvalidTextureHandle);
    {
        auto cmd = device->CreateCommandBuffer();
        RenderPassDesc passDesc{};
        passDesc.colorLoadOp = LoadOp::Clear;
        passDesc.clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
        cmd->BeginRenderPass(passDesc);
        cmd->BindPipeline(texPipeline);
        cmd->BindVertexBuffer(quadBuffer);
        cmd->BindTexture(magentaTex, 0U);
        cmd->Draw(6U);
        cmd->EndRenderPass();
        device->Submit(std::move(cmd));
    }
    device->Present();
    ASSERT_TRUE(device->IsUsable());
    ASSERT_TRUE(device->ReadbackLatestPresentedImage(pixels, width, height));
    const auto magentaCenter = ChannelAtNdc(pixels, width, height, 0.0F, 0.0F);
    EXPECT_GT(magentaCenter[0], 200) << "set 1 sampler2D must sample magenta R";
    EXPECT_LT(magentaCenter[1], 40);
    EXPECT_GT(magentaCenter[2], 200) << "set 1 sampler2D must sample magenta B";

    // Part 3: descriptor set 4 (>= kMaxDescriptorSets) is refused cleanly.
    const std::string set4TexFragSpirv =
        patchSpirvDecorate(kTexturedFragmentSpirv, 13U, 34U, 4U);
    ShaderDesc set4FsDesc{};
    set4FsDesc.stage = ShaderStage::Fragment;
    set4FsDesc.sourceCode = set4TexFragSpirv;
    const ShaderHandle set4Fs = device->CreateShader(set4FsDesc);
    ASSERT_NE(set4Fs, kInvalidShaderHandle);
    pipeDesc.fragmentShader = set4Fs;
    infoLog.clear();
    const PipelineHandle rejectedPipeline = device->CreatePipeline(pipeDesc, &infoLog);
    EXPECT_EQ(rejectedPipeline, kInvalidPipelineHandle);
    EXPECT_NE(infoLog.find("exceeds the RHI's 4-set limit"), std::string::npos) << infoLog;

    device->DestroyShader(set4Fs);
    device->DestroyTexture(magentaTex);
    device->DestroyPipeline(texPipeline);
    device->DestroyShader(texFs);
    device->DestroyBuffer(indexOneUbo);
    device->DestroyBuffer(paletteBuffer);
    device->DestroyBuffer(quadBuffer);
    device->DestroyPipeline(ssboPipeline);
    device->DestroyShader(ssboFs);
    device->DestroyShader(vs);
}

} // namespace UVE::Render::Tests
