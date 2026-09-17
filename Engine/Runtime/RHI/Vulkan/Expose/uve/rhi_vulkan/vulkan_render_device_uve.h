// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VulkanRenderDeviceUVE — the engine's second real IRenderDeviceUVE backend and the milestone-1
// (M1) "bootstrap" of the approved modern-GPU path (AUDIT.md section 8, item 2). M1 scope is
// deliberately narrow and honest: instance -> physical/logical device -> window surface ->
// swapchain -> per-frame clear-color present. Synchronisation is the documented-simple form —
// one frame in flight plus vkQueueWaitIdle before every swapchain teardown call — matching the
// "small real thing over big fake thing" rule that governs this repo's milestone slicing.
//
// What M1 does NOT implement: every resource-bearing method (buffers, textures, shaders,
// pipelines, reflection, pipeline binaries, command buffers). Those return the documented
// invalid-handle/false/empty/nullptr values with an explicit UVE_WARNING naming the missing
// milestone, exactly as Null-render did before it (and which the whole headless test-suite
// proved safe: the engine runs end-to-end on a device that services no draw path). Higher
// milestones will fill these in, behind the same interface, without touching callers.
//
// Capability reporting is honest and upstream-visible: GetBackendNameUVE() says
// "Vulkan (M1 bootstrap)" — never "Vulkan" unqualified — so logs, editor overlays, and bug
// reports cannot mistake the bootstrap for the finished backend, and IsUsableUVE() reflects
// the real instance/device/swapchain bring-up result.


#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "uve/rhi/i_render_device_uve.h"
#include "uve/window/i_window_manager_uve.h"

namespace UVE::Render {

/// Vulkan M1 bootstrap device; see the file banner above for the full scope contract.
/// Thread-safety: not thread-safe, matching IRenderDeviceUVE's own documented contract; every
/// method is intended to be called only from the main engine/render thread.
class VulkanRenderDeviceUVE final : public IRenderDeviceUVE {
public:
    /// Factory — not a constructor — because Vulkan bring-up has many normal, host-dependent
    /// failure modes (no Vulkan loader shared library installed, no Vulkan-capable driver/ICD
    /// such as Mesas lavapipe, the OS window created with GLFW lacking the platform WSI
    /// surface support the instance needs). Every one of those logs at least one UVE_WARNING
    /// naming what was missing and returns nullptr here; it never throws, never aborts, and
    /// never returns a half-initialised device. This is exactly how EngineCoreUVE probes the
    /// OpenGL backend today (`IsUsableUVE()` on a constructed GlRenderDeviceUVE), adapted to
    /// creation-failure: Vulkan failures happen strictly *before* any object exists.
    ///
    /// `windowManager` must be the engine's real window manager (headless
    /// NullWindowManagerUVE deliberately cannot satisfy this — Vulkan is a windowed backend
    /// in this build). It is queried via RTTI for the Window::IVulkanWindowSurfaceUVE
    /// capability (see i_vulkan_window_surface_uve.h for why an interface + dynamic_cast and
    /// not a virtual on IWindowManagerUVE itself): implementations without it — the Android
    /// window manager, stubs, test doubles — immediately disqualify the backend. Pass a
    /// reference instead of the raw IVulkanWindowSurfaceUVE to preserve the established
    /// "render device takes IWindowManagerUVE&" call-shape precedent (audit #30 naming).
    /// `windowManager` must outlive the returned device. VkInstance, VkDevice, and the
    /// surface/swapchain are all owned by the returned device and destroyed in strict LIFO
    /// order from its destructor, while windowManager's window is still alive.
    [[nodiscard]] static std::unique_ptr<VulkanRenderDeviceUVE> CreateUVE(
        Window::IWindowManagerUVE& windowManager);

    ~VulkanRenderDeviceUVE() override;

    VulkanRenderDeviceUVE(const VulkanRenderDeviceUVE&) = delete;
    VulkanRenderDeviceUVE& operator=(const VulkanRenderDeviceUVE&) = delete;

    // --- M1 honest "not yet" methods: invalid handle / false / empty / nullptr + a warning ---
    [[nodiscard]] BufferHandleUVE CreateBufferUVE(const BufferDescUVE& desc,
                                                  std::span<const std::byte> initialData = {}) override;
    void DestroyBufferUVE(BufferHandleUVE buffer) override;
    [[nodiscard]] bool UpdateBufferUVE(BufferHandleUVE buffer, std::span<const std::byte> data,
                                       std::size_t offset = 0) override;
    [[nodiscard]] TextureHandleUVE CreateTextureUVE(const TextureDescUVE& desc,
                                                    std::span<const std::byte> initialData = {}) override;
    void DestroyTextureUVE(TextureHandleUVE texture) override;
    [[nodiscard]] ShaderHandleUVE CreateShaderUVE(const ShaderDescUVE& desc,
                                                  std::string* outInfoLog = nullptr) override;
    void DestroyShaderUVE(ShaderHandleUVE shader) override;
    [[nodiscard]] PipelineHandleUVE CreatePipelineUVE(const PipelineDescUVE& desc,
                                                      std::string* outInfoLog = nullptr) override;
    void DestroyPipelineUVE(PipelineHandleUVE pipeline) override;
    [[nodiscard]] std::vector<UniformReflectionUVE> GetPipelineUniformsUVE(
        PipelineHandleUVE pipeline) const override;
    [[nodiscard]] bool GetPipelineBinaryUVE(PipelineHandleUVE pipeline, std::vector<std::byte>& outBinary,
                                            std::uint32_t& outFormat) const override;
    [[nodiscard]] PipelineHandleUVE CreatePipelineFromBinaryUVE(std::span<const std::byte> binary,
                                                                std::uint32_t format,
                                                                const PipelineBinaryDescUVE& desc) override;
    [[nodiscard]] std::unique_ptr<ICommandBufferUVE> CreateCommandBufferUVE() override;
    void SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) override;

    // --- M1 real behaviour ---
    /// Advances one frame: waits for the previous frame's fence (one frame in flight),
    /// acquires the next swapchain image, records and submits a one-shot command buffer
    /// whose single render pass clears the acquired image, and queues the present. A swapchain
    /// invalidated by resize is rebuilt here against the window's current framebuffer size;
    /// a minimized (zero-size) framebuffer skips rendering for the frame entirely — a documented
    /// no-op, never an error, mirroring the GL device's minimization contract.
    void PresentUVE() override;

    [[nodiscard]] bool IsUsableUVE() const noexcept override;
    [[nodiscard]] std::string_view GetBackendNameUVE() const noexcept override;

private:
    explicit VulkanRenderDeviceUVE(Window::IWindowManagerUVE& windowManager);

    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Render
