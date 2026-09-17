// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VulkanRenderDeviceUVE — the engine's second real IRenderDeviceUVE backend and the milestone-1
// (M1) "bootstrap" of the approved modern-GPU path (AUDIT.md section 8, item 2). M1 scope is
// deliberately narrow and honest: instance -> physical/logical device -> window surface ->
// swapchain -> per-frame clear-color present. Synchronisation is the documented-simple form —
// one frame in flight plus vkQueueWaitIdle before every swapchain teardown call — matching the
// "small real thing over big fake thing" rule that governs this repo's milestone slicing.
//
// Slice M2a ("draw slice") added 2026-09-17: buffers (host-visible memory policy), SPIR-V
// shaders, fixed-function pipelines built on the swapchain render pass, recorded command
// buffers, and Draw/DrawIndexed replay inside the frame pass are all REAL implementations —
// the device renders actual geometry into the presented image, replaying submitted command
// buffers exactly the way GlRenderDeviceUVE plays its recordings.
//
// Slice M2b ("uniforms + depth") added 2026-09-17: SPIRV-Reflect parses each pipeline's
// SPIR-V at creation; set-0 uniform-buffer blocks become dynamic-offset descriptor bindings
// against one shared 1 MiB-per-frame uniform ring (always-snapshot-per-draw, never per-draw
// descriptor writes), push-constant blocks become VkPushConstantRange state, and every
// SetUniform* op resolves by reflected member name into CPU-side shadows that the draw path
// snapshots into the ring. The swapchain render pass grew a real depth attachment
// (D32_SFLOAT preferred, X8_D24 fallback), so depthTestEnabled/depthWriteEnabled are honored
// exactly as GlRenderDeviceUVE documents them (LESS_OR_EQUAL), and the caller's clearDepth
// feeds the frame's depth clear. Still honest "not yet" areas (documented at each method,
// warnings emitted once, no silent lies): textures/images/SSBOs (pipeline creation FAILS
// loudly with an M2c message rather than building a broken layout), more-than-one
// descriptor set, offscreen render targets, loadOp semantics beyond clear, per-op depth
// compare funcs, device-local staging, pipeline binaries. Those keep returning the
// documented invalid/false/empty results with a warning naming the missing milestone.
//
// Slice M2d ("offscreen render targets") added 2026-09-17: BeginRenderPassUVE with texture
// attachments is now REAL on 1.3-capable devices. The engineering shortcut the classic
/// render-pass route could not survive: VkRenderPass compatibility requires EXACT format
// identity per attachment, the swapchain is sRGB-typed, and RHI RGBA8 textures are unorm —
// so the device switches wholesale to core-1.3 dynamic rendering (probed via
// vkGetPhysicalDeviceFeatures2 + VkPhysicalDeviceVulkan13Features::dynamicRendering, enabled
// in the device-create feature chain; the instance apiVersion has been clamped at 1.3 since
// bring-up). In dynamic mode there is no VkRenderPass and no framebuffers: pipelines declare
// their one-color(RGBA8)+depth contract through VkPipelineRenderingCreateInfo, passes open
// lazily with vkCmdBeginRendering at the first marker of a run, and interleaved
// default/offscreen passes are legal (a re-opened swapchain instance resumes with LOAD —
// GL's FBO semantics preserved exactly). Textures allocate in the swapchain's own format via
// MUTABLE_FORMAT + an unorm-sibling SAMPLED view and an image-native ATTACHMENT view, so
// every RGBA8 texture is attachable by construction; color-only offscreen passes borrow an
// extent-keyed scratch depth image; caller Depth32Float textures attach when the device
// depth is D32. Honest boundaries (one-shot warns + skip/degrade, never silent wrong
// pixels): classic-mode devices (no 1.3 entry points) keep byte-identical M2c behavior and
// skip offscreen passes; non-attachable textures (RGBA16Float; anything when the swapchain
// is not a 4x8 RGBA format) skip their pass; D24 devices fall back to the invisible scratch
// depth; sampling a Depth32Float texture is the M2e slice and returns the 1x1 white
// fallback until then. The latent M1 readback-fence hazard is fixed here too: the readback
// submission rides its own transient fence created around it (the presented-frame fence is
// strictly PresentUVE-owned).
//
// Capability reporting is honest and upstream-visible: GetBackendNameUVE() says
// "Vulkan (M2e depth+Load policies)" on dynamic-rendering devices, "Vulkan (M2c textures+staging)"
// on classic ones — never "Vulkan" unqualified — so logs, editor overlays, and bug reports
// cannot mistake the current slice for the finished backend, and IsUsableUVE() reflects the
// real instance/device/swapchain bring-up result.


#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi/recorded_command_uve.h"
#include "uve/window/i_vulkan_window_surface_uve.h"
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

    /// Same factory contract as the window-manager overload above (nullptr on any host-level
    /// failure, never throws), but takes the surface capability *directly* — for Vulkan
    /// consumers that own no OS window yet still satisfy the bridge contract: server-side
    /// rendering, offscreen verification harnesses, CI screenshot tooling. Such callers answer
    /// GetRequiredVulkanInstanceExtensionsUVE() with headless-appropriate extensions (e.g.
    /// VK_KHR_surface + VK_EXT_headless_surface) and implement CreateVulkanWindowSurfaceUVE()
    /// through the matching WSI entry points; everything else (present-support physical-device
    /// pickup, swapchain, present loop) is identical, because the window-manager path funnels
    /// through this same bridge on entry. `surfaceBridge` must outlive the returned device.
    [[nodiscard]] static std::unique_ptr<VulkanRenderDeviceUVE> CreateFromBridgeUVE(
        Window::IVulkanWindowSurfaceUVE& surfaceBridge);

    /// Fully self-contained headless construction: the device itself requests
    /// VK_EXT_headless_surface, creates its own VkHeadlessSurfaceEXT, and sizes the swapchain
    /// at 1280x720 — no window manager, no bridge object, no OS display needed. This is the
    /// sanctioned factory for CI and any other execution host without X/Wayland (the tier-2
    /// real-device tests use it whenever GLFW cannot create a window); on hosts where the
    /// ICD does not advertise VK_EXT_headless_surface (SwiftShader does; lavapipe currently
    /// does not) it cleanly refuses with nullptr and a one-line explanation. The rendering
    /// and present path itself is identical to the windowed one — only surface provenance
    /// and extent reporting differ. Failure policy is unchanged: nullptr on any host-level
    /// failure, never throws.
    [[nodiscard]] static std::unique_ptr<VulkanRenderDeviceUVE> CreateHeadlessUVE();

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

    // --- M1 diagnostics beyond IRenderDeviceUVE (Vulkan bootstrap device only) ---
    /// Copies the most recently presented swapchain image's pixel contents into `outRGBA8`,
    /// byte order R,G,B,A regardless of the surface's native channel swizzle. The caller sizes
    /// `outRGBA8` to exactly width*height*4 bytes of the current framebuffer extent; this method
    /// fills `outWidth`/`outHeight` with that extent on success. Deliberately a cold-path API for
    /// editor screenshots and automated visual verification — internally it drains the queue,
    /// records a transient layout-transition + image-to-buffer copy into a host-visible staging
    /// buffer, and CPU-swizzles B8G8R8A8 surfaces to RGBA byte order. Returns false (logging
    /// the reason) when no frame has been presented yet, the output span is mis-sized, or any
    /// Vulkan call fails; never throws.
    [[nodiscard]] bool ReadbackLatestPresentedImageUVE(std::span<std::byte> outRGBA8,
                                                       std::uint32_t& outWidth,
                                                       std::uint32_t& outHeight);

private:
    /// Creates the 1x1 white fallback texture after device initialisation (M2c sampler
    /// bindings always have a legal image). Returns false — with a logged reason — when
    /// the upload path failed; both factories then abort like any other init failure.
    [[nodiscard]] bool CreateFallbackTextureUVE();

    VulkanRenderDeviceUVE(Window::IWindowManagerUVE* windowManager,
                          Window::IVulkanWindowSurfaceUVE* bridge);

    /// Replays one submitted recorded command buffer inside the frame render pass (see the
    /// .cpp for the M2a integration contract).
    void ReplayRecordedCommandsUVE(const std::vector<RecordedCommandUVE>& commands);

    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Render
