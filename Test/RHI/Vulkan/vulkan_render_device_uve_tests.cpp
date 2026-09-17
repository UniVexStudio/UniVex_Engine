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

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "uve/events/event_system_uve.h"
#include "uve/window/i_vulkan_window_surface_uve.h"
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
    EXPECT_EQ(device->GetBackendNameUVE(), "Vulkan (M1 bootstrap)")
        << "capability reporting must never call the bootstrap the finished Vulkan backend";
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
    // M1's contract: never fake success, never crash — every resource path returns the RHI's
    // documented invalid/false/empty value (see vulkan_render_device_uve.h and the exact
    // NullRenderDeviceUVE precedent the headless suite relies on).
    const BufferDescUVE bufferDesc{};
    EXPECT_EQ(device->CreateBufferUVE(bufferDesc), kInvalidBufferHandleUVE);
    EXPECT_FALSE(device->UpdateBufferUVE(kInvalidBufferHandleUVE, {}));
    device->DestroyBufferUVE(kInvalidBufferHandleUVE); // documented safe no-op

    const TextureDescUVE textureDesc{};
    EXPECT_EQ(device->CreateTextureUVE(textureDesc), kInvalidTextureHandleUVE);
    device->DestroyTextureUVE(kInvalidTextureHandleUVE);

    const ShaderDescUVE shaderDesc{};
    std::string infoLog;
    EXPECT_EQ(device->CreateShaderUVE(shaderDesc, &infoLog), kInvalidShaderHandleUVE);
    EXPECT_FALSE(infoLog.empty()) << "outInfoLog must name the milestone gap";

    const PipelineDescUVE pipelineDesc{};
    infoLog.clear();
    EXPECT_EQ(device->CreatePipelineUVE(pipelineDesc, &infoLog), kInvalidPipelineHandleUVE);
    device->DestroyPipelineUVE(kInvalidPipelineHandleUVE);

    EXPECT_TRUE(device->GetPipelineUniformsUVE(kInvalidPipelineHandleUVE).empty());
    std::vector<std::byte> binary;
    std::uint32_t format = 0;
    EXPECT_FALSE(device->GetPipelineBinaryUVE(kInvalidPipelineHandleUVE, binary, format));

    const PipelineBinaryDescUVE binaryDesc{};
    EXPECT_EQ(device->CreatePipelineFromBinaryUVE({}, 0U, binaryDesc), kInvalidPipelineHandleUVE);

    EXPECT_EQ(device->CreateCommandBufferUVE(), nullptr);
    device->SubmitUVE(nullptr); // documented: the M1 device services no submitted command buffer
}

} // namespace UVE::Render::Tests
