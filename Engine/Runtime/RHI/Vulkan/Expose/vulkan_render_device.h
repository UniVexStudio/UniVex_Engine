// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VulkanRenderDevice — the engine's second real IRenderDevice backend and the milestone-1
// (M1) "bootstrap" of the approved modern-GPU path. M1 scope is
// deliberately narrow and honest: instance -> physical/logical device -> window surface ->
// swapchain -> per-frame clear-color present. Synchronisation is the documented-simple form —
// one frame in flight plus vkQueueWaitIdle before every swapchain teardown call — matching the
// "small real thing over big fake thing" rule that governs this repo's milestone slicing.
//
// Slice M2a ("draw slice") added 2026-09-17: buffers (host-visible memory policy), SPIR-V
// shaders, fixed-function pipelines built on the swapchain render pass, recorded command
// buffers, and Draw/DrawIndexed replay inside the frame pass are all REAL implementations —
// the device renders actual geometry into the presented image, replaying submitted command
// buffers exactly the way GlRenderDevice plays its recordings.
//
// Slice M2b ("uniforms + depth") added 2026-09-17: SPIRV-Reflect parses each pipeline's
// SPIR-V at creation; set-0 uniform-buffer blocks become dynamic-offset descriptor bindings
// against one shared 1 MiB-per-frame uniform ring (always-snapshot-per-draw, never per-draw
// descriptor writes), push-constant blocks become VkPushConstantRange state, and every
// SetUniform* op resolves by reflected member name into CPU-side shadows that the draw path
// snapshots into the ring. The swapchain render pass grew a real depth attachment
// (D32_SFLOAT preferred, X8_D24 fallback), so depthTestEnabled/depthWriteEnabled are honored
// exactly as GlRenderDevice documents them (LESS_OR_EQUAL), and the caller's clearDepth
// feeds the frame's depth clear. Still honest "not yet" areas (documented at each method,
// warnings emitted once, no silent lies): textures/images/SSBOs (pipeline creation FAILS
// loudly with an M2c message rather than building a broken layout), more-than-one
// descriptor set, offscreen render targets, loadOp semantics beyond clear, per-op depth
// compare funcs, device-local staging, pipeline binaries. Those keep returning the
// documented invalid/false/empty results with a warning naming the missing milestone.
//
// Slice M2d ("offscreen render targets") added 2026-09-17: BeginRenderPass with texture
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
// MUTABLE_FORMAT + a color-space-matched SAMPLED view (UNORM for linear, sRGB for decoded color)
// and an image-native ATTACHMENT view, so every RGBA8 texture is attachable by construction;
// color-only offscreen passes borrow an
// extent-keyed scratch depth image; caller Depth32Float textures attach when the device
// depth is D32. Honest boundaries (one-shot warns + skip/degrade, never silent wrong
// pixels): classic-mode devices (no 1.3 entry points) keep byte-identical M2c behavior and
// skip offscreen passes; non-attachable textures (RGBA16Float; anything when the swapchain
// is not a 4x8 RGBA format) skip their pass; D24 devices fall back to the invisible scratch
// depth; sampling a Depth32Float texture is the M2e slice and returns the 1x1 white
// fallback until then. The latent M1 readback-fence hazard is fixed here too: the readback
// submission rides its own transient fence created around it (the presented-frame fence is
// strictly Present-owned).
//
// Slice M2f ("SSBOs + separate samplers") added 2026-09-18: CreatePipeline reflection now
// accepts STORAGE_BUFFER bindings (bound through the new ICommandBuffer::BindStorageBuffer
// — GL shader-storage binding-point semantics, i-th reflected storage binding reads global slot
// i) and separate SAMPLED_IMAGE + SAMPLER pairs (fed from the same BindTexture slots as
// combined samplers; every standalone SAMPLER binding is written with ONE device-owned fixed
// sampler — the RHI's single GL-mirrored linear/trilinear/clamp shape, bounded by the image view).
// Storage buffers live in
// real per-handle VkBuffers (the M2a host-visible policy), join the per-tuple descriptor-set
// cache under an 's'-prefixed key section, and are invalidated from it at DestroyBuffer
// exactly like textures. Unbound or destroyed-after-bind storage slots read a device-owned
// zero-filled fallback buffer — deterministic zeros, the buffer analogue of the 1x1 white
// fallback texture. Storage IMAGES (and any other descriptor type) still fail pipeline creation
// loudly: they land with the compute milestone (ComputeSystem, Part 7.2). External uniform
// buffers (BindUniformBuffer) remain the documented later-slice no-op; SetUniform* + the
// reflected ring is the uniform path.
//
// Slice M3 ("device-local staging") added 2026-09-18: VERTEX and INDEX buffers now allocate
// DEVICE_LOCAL memory (the performance shape M2a's host-visible policy explicitly deferred)
// and carry TRANSFER_DST; their initial data and every UpdateBuffer re-stage through a
// one-shot staging copy that mirrors the M2c texture-upload discipline exactly — transient
// HOST_VISIBLE|COHERENT TRANSFER_SRC buffer, one-time command buffer on the present queue,
// buffer barriers around vkCmdCopyBuffer (TRANSFER_WRITE published to VERTEX_ATTRIBUTE_READ /
// INDEX_READ), full-idle wait before teardown. UNIFORM buffers stay HOST_VISIBLE with the
// persistent map (host writes ARE the SetUniform ring mechanism) and STORAGE buffers stay
// host-visible in this slice (SSBO traffic benefits least from device placement; the M2f
// zero-fallback contract is untouched). The caller-visible copy-on-update contract is
// identical for every usage — only placement/traffic differs. The memory policy is
// arm-independent (both classic and dynamic devices get device-local vertex/index buffers);
// the reported name follows the established rule that the badge tracks the newest slice.
//
// Slice M4 ("parallel recording") added 2026-09-18: multi-threaded command recording is a
// real, contract-documented capability. VulkanCommandBuffer objects carry NO device state
// (recording appends to a per-object retained list), so any number of threads may create,
// record, and submit their own command buffers concurrently; Submit() pushes into a
// mutex-guarded submission FIFO from any thread, and Present() drains that FIFO into a
// local snapshot under the same lock before replaying strictly main-thread (the GPU timeline
// keeps one owner; a submit racing a present lands in the NEXT frame's FIFO — honest queue
// order). Replay stays the M2a one-primary-buffer model: this slice parallelizes RECORDING
// (the explicit-API capability the roadmap names), not the replay translation. Resource
// creation/destruction and Present remain main-thread calls.
//
// Slice M5a ("compute dispatch") added 2026-09-18: CreateComputePipeline() builds real
// VkPipelines at the COMPUTE bind point (compact compute-only SPIR-V reflection: uniform
// blocks flow through the M2b ring-dynamic machinery, STORAGE_BUFFER slots through the M2f
// slot/tuple-cache machinery; every other descriptor form refuses loudly — storage images
// name the M5b slice), and ICommandBuffer::Dispatch() replays OUTSIDE render-pass
// markers as Vulkan requires: the handler closes any lazily-open dynamic-rendering instance
// first (a later Begin re-opens it with LOAD, content preserved), brackets vkCmdDispatch in
// conservative global memory barriers (prior shader writes visible to compute; compute
// writes visible to every later shader reader and the queue-idle readback), and flushes
// descriptors at the COMPUTE bind point. The classic pre-1.3 arm cannot dispatch at all —
// its whole replay is wrapped in ONE native render pass instance where compute is illegal —
// so it warns once and skips (bit 19); draws reaching replay with a compute pipeline bound
// skip warn-once too (bit 20). The replay's outside-pass ignore rule now relaxes while a
// compute pipeline is bound (outside-pass storage binds and uniform writes are the dispatch
// feed).
//
// Slice M5b ("storage images") added 2026-09-18: STORAGE_IMAGE descriptors are accepted by
// BOTH pipeline reflections (the M2f loud refusal is lifted) and join the ONE texture-slot
// space — the pipeline's i-th texture-family binding (sampled and storage forms together,
// ascending by binding number) reads global BindTexture slot i. Storage slots write
// STORAGE_IMAGE descriptors with VK_IMAGE_LAYOUT_GENERAL: the first tuple-set write
// transitions the texture to GENERAL (a legal — if unoptimal — sampling and attachment-entry
// layout, so it becomes the texture's permanent rest state; CloseCurrentPassDynamic
// restores pinned offscreen attachments to GENERAL instead of SHADER_READ_ONLY). The
// transition barrier is only legal OUTSIDE a rendering instance, so the flush closes an open
// pass and reopens it with LOAD semantics (content preserved) around the barrier — on the
// classic pre-1.3 arm, whose single native pass spans the whole replay, storage-image draws
// warn once (bit 22) and skip. Cached descriptor sets that baked the pre-pin layout are
// invalidated at pin time and park in a deferred-free list (this frame's recorded binds may
// still reference them; the next Present's post-fence point frees them). Depth textures
// are never storage-bound: such slots deterministically write a device-owned 1x1 black sink
// (bit 21) — imageStore into the shared white sampling fallback would corrupt its invariant,
// and unbound/destroyed storage-image slots resolve to the same sink. Sampled descriptors of
// a GENERAL-pinned texture rewrite with the GENERAL layout so cached sets never contradict
// the tracked state. The M5a dispatch post-barrier widened to also publish compute writes to
// COLOR_ATTACHMENT_OUTPUT (a storage-written texture may be ATTACHED with Load next).
// Same-day fix-forward on the slice's first red CI: the IMAGE side of the contract —
// storage descriptors written against images created without VK_IMAGE_USAGE_STORAGE_BIT,
// and through views whose format cannot match a SPIR-V image-format qualifier, are
// undefined behavior that segfaulted lavapipe at execution in all three pixel proofs.
// Every color texture now carries VK_IMAGE_USAGE_STORAGE_BIT (depth textures never
// storage-bind — the sink substitution stands), and RGBA8 textures gain a dedicated
// R8G8B8A8_UNORM storage alias view when the M2d swapchain-format policy aliased the
// image into the B,G,R,A or sRGB family: a STORAGE_IMAGE descriptor's view format must
// EXACTLY equal the shader's qualifier (rgba8), SPIR-V has no B,G,R,A storage format at
// all, and the mutable-format 4x8 compatibility class makes the alias legal. Sampling
// and attachment views are untouched.
//
// Capability reporting is honest and upstream-visible: GetBackendName() says
// "Vulkan (M5b storage images)" on dynamic-rendering devices, "Vulkan (M2c textures+staging)"
// on classic ones — never "Vulkan" unqualified — so logs, editor overlays, and bug reports
// cannot mistake the current slice for the finished backend, and IsUsable() reflects the
// real instance/device/swapchain bring-up result.


#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "i_render_device.h"
#include "recorded_command.h"
#include "i_vulkan_window_surface.h"
#include "i_window_manager.h"

namespace UVE::Render {

/// Vulkan M1 bootstrap device; see the file banner above for the full scope contract.
/// Thread-safety: not thread-safe, matching IRenderDevice's own documented contract; every
/// method is intended to be called only from the main engine/render thread.
class VulkanRenderDevice final : public IRenderDevice {
public:
    /// Factory — not a constructor — because Vulkan bring-up has many normal, host-dependent
    /// failure modes (no Vulkan loader shared library installed, no Vulkan-capable driver/ICD
    /// such as Mesas lavapipe, the OS window created with GLFW lacking the platform WSI
    /// surface support the instance needs). Every one of those logs at least one UVE_WARNING
    /// naming what was missing and returns nullptr here; it never throws, never aborts, and
    /// never returns a half-initialised device. This is exactly how EngineCore probes the
    /// OpenGL backend today (`IsUsable()` on a constructed GlRenderDevice), adapted to
    /// creation-failure: Vulkan failures happen strictly *before* any object exists.
    ///
    /// `windowManager` must be the engine's real window manager (headless
    /// NullWindowManager deliberately cannot satisfy this — Vulkan is a windowed backend
    /// in this build). It is queried via RTTI for the Window::IVulkanWindowSurface
    /// capability (see i_vulkan_window_surface_uve.h for why an interface + dynamic_cast and
    /// not a virtual on IWindowManager itself): implementations without it — the Android
    /// window manager, stubs, test doubles — immediately disqualify the backend. Pass a
    /// reference instead of the raw IVulkanWindowSurface to preserve the established
    /// "render device takes IWindowManager&" call-shape precedent (audit #30 naming).
    /// `windowManager` must outlive the returned device. VkInstance, VkDevice, and the
    /// surface/swapchain are all owned by the returned device and destroyed in strict LIFO
    /// order from its destructor, while windowManager's window is still alive. When requested,
    /// VK_LAYER_KHRONOS_validation is enabled only if installed; missing validation tooling warns
    /// and does not prevent the ordinary backend fallback chain from running.
    [[nodiscard]] static std::unique_ptr<VulkanRenderDevice> Create(
        Window::IWindowManager& windowManager, bool enableValidationLayers = false);

    /// Same factory contract as the window-manager overload above (nullptr on any host-level
    /// failure, never throws), but takes the surface capability *directly* — for Vulkan
    /// consumers that own no OS window yet still satisfy the bridge contract: server-side
    /// rendering, offscreen verification harnesses, CI screenshot tooling. Such callers answer
    /// GetRequiredVulkanInstanceExtensions() with headless-appropriate extensions (e.g.
    /// VK_KHR_surface + VK_EXT_headless_surface) and implement CreateVulkanWindowSurface()
    /// through the matching WSI entry points; everything else (present-support physical-device
    /// pickup, swapchain, present loop) is identical, because the window-manager path funnels
    /// through this same bridge on entry. `surfaceBridge` must outlive the returned device.
    [[nodiscard]] static std::unique_ptr<VulkanRenderDevice> CreateFromBridge(
        Window::IVulkanWindowSurface& surfaceBridge, bool enableValidationLayers = false);

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
    [[nodiscard]] static std::unique_ptr<VulkanRenderDevice> CreateHeadless(
        bool enableValidationLayers = false);

    ~VulkanRenderDevice() override;
    [[nodiscard]] RenderDeviceCapabilities GetCapabilities() const noexcept override;
    [[nodiscard]] RenderOperationResult GetTextureDesc(TextureHandle,
                                                             TextureDesc &) const override;
    [[nodiscard]] RenderOperationResult GetBufferDesc(BufferHandle,
                                                            BufferDesc &) const override;
    [[nodiscard]] RenderOperationResult CopyBuffer(const BufferCopyDesc &) override;
    /// Tier 2.6 color blit: one recorded vkCmdBlitImage on a transient command buffer, gated on
    /// the per-format blit/filter feature bits, with both subresources restored to the layout the
    /// backend samples from. Rejects mid-pass calls and open CPU submission FIFOs like CopyBuffer.
    [[nodiscard]] RenderOperationResult BlitTexture(const TextureBlitDesc &) override;
    /// Rebuilds levels 1..n-1 by linear-blitting each level from the one above, for every array
    /// layer. Requires mipLevels >= 2 plus BLIT_SRC|BLIT_DST|LINEAR_FILTER on the image format.
    [[nodiscard]] RenderOperationResult GenerateMipmaps(TextureHandle) override;
    [[nodiscard]] RenderOperationResult InsertFence(FenceHandle &) override;
    [[nodiscard]] RenderOperationResult WaitFence(FenceHandle, std::uint64_t) override;
    [[nodiscard]] RenderOperationResult DestroyFence(FenceHandle) override;
    /// Timestamp queries: one single-sample `VK_QUERY_TYPE_TIMESTAMP` pool per handle.
    /// `WriteTimestamp` rides a transient reset+write submission (the queue-ordered point in
    /// time), and results are converted from the device's tick period to nanoseconds. Occlusion
    /// queries stay Unsupported: `vkCmdBeginQuery`/`vkCmdEndQuery` must bracket the caller's own
    /// draws inside ONE command buffer, which this device-level API cannot promise.
    [[nodiscard]] RenderOperationResult CreateQuery(RenderQueryKind, QueryHandle &) override;
    [[nodiscard]] RenderOperationResult BeginQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult EndQuery(QueryHandle) override;
    [[nodiscard]] RenderOperationResult WriteTimestamp(QueryHandle) override;
    [[nodiscard]] RenderOperationResult GetQueryResult(QueryHandle, RenderQueryResult &,
                                                             bool = false) override;
    [[nodiscard]] RenderOperationResult DestroyQuery(QueryHandle) override;
    /// Timeline semaphores (Vulkan 1.2 feature): real device counters with CPU-visible waits.
    /// Every call reports Unsupported until the feature is enabled and its entry points resolve,
    /// so a 1.0/1.1 host never half-implements the contract.
    [[nodiscard]] RenderOperationResult CreateSemaphore(const SemaphoreDesc &,
                                                              SemaphoreHandle &) override;
    [[nodiscard]] RenderOperationResult GetSemaphoreCounter(SemaphoreHandle,
                                                                  std::uint64_t &) const override;
    [[nodiscard]] RenderOperationResult SignalSemaphore(SemaphoreHandle, std::uint64_t) override;
    [[nodiscard]] RenderOperationResult WaitSemaphore(SemaphoreHandle, std::uint64_t,
                                                            std::uint64_t) override;
    [[nodiscard]] RenderOperationResult DestroySemaphore(SemaphoreHandle) override;
    [[nodiscard]] RenderOperationResult WaitIdle() override;
    [[nodiscard]] RenderOperationResult GetSurfaceInfo(RenderSurfaceInfo &) const override;
    // Occlusion queries retain the interface's explicit Unsupported contract; the capability
    // never advertises them.

    VulkanRenderDevice(const VulkanRenderDevice&) = delete;
    VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

    // --- M1 honest "not yet" methods: invalid handle / false / empty / nullptr + a warning ---
    [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& desc,
                                                  std::span<const std::byte> initialData = {}) override;
    void DestroyBuffer(BufferHandle buffer) override;
    [[nodiscard]] bool UpdateBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                       std::size_t offset = 0) override;
    [[nodiscard]] bool ReadbackBuffer(BufferHandle buffer, std::span<std::byte> outData,
                                         std::uint64_t offsetBytes = 0) override;
    [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& desc,
                                                    std::span<const std::byte> initialData = {}) override;
    [[nodiscard]] bool SupportsTextureFormat(
        TextureFormat format, TextureColorSpace colorSpace = TextureColorSpace::Linear) const noexcept override;
    void DestroyTexture(TextureHandle texture) override;
    [[nodiscard]] std::uint32_t RegisterBindlessTexture(TextureHandle texture) override;
    void UnregisterBindlessTexture(std::uint32_t bindlessIndex) override;
    [[nodiscard]] SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle sampler) override;
    [[nodiscard]] ShaderHandle CreateShader(const ShaderDesc& desc,
                                                  std::string* outInfoLog = nullptr) override;
    void DestroyShader(ShaderHandle shader) override;
    [[nodiscard]] PipelineHandle CreatePipeline(const PipelineDesc& desc,
                                                      std::string* outInfoLog = nullptr) override;
    [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc,
                                                              std::string* outInfoLog = nullptr) override;
    void DestroyPipeline(PipelineHandle pipeline) override;
    [[nodiscard]] std::vector<UniformReflection> GetPipelineUniforms(
        PipelineHandle pipeline) const override;
    [[nodiscard]] bool GetPipelineBinary(PipelineHandle pipeline, std::vector<std::byte>& outBinary,
                                            std::uint32_t& outFormat) const override;
    [[nodiscard]] PipelineHandle CreatePipelineFromBinary(std::span<const std::byte> binary,
                                                                std::uint32_t format,
                                                                const PipelineBinaryDesc& desc) override;
    [[nodiscard]] std::unique_ptr<ICommandBuffer> CreateCommandBuffer() override;
    void Submit(std::unique_ptr<ICommandBuffer> commandBuffer) override;
    /// Foundation WP4: Submit plus timeline waits/signals, honoured at this backend's real
    /// queue granularity - the frame batch. All waits of a frame's submissions gate that frame's
    /// single vkQueueSubmit (VkTimelineSemaphoreSubmitInfo); all signals are performed when that
    /// batch completes. Validated at submit time against the live semaphore table; see the
    /// interface comment for the result contract.
    [[nodiscard]] RenderOperationResult SubmitWithSync(std::unique_ptr<ICommandBuffer> commandBuffer,
                                                            const SubmitDesc &desc) override;

    // --- M1 real behaviour ---
    /// Advances one frame: waits for the previous frame's fence (one frame in flight),
    /// acquires the next swapchain image, records and submits a one-shot command buffer
    /// whose single render pass clears the acquired image, and queues the present. A swapchain
    /// invalidated by resize is rebuilt here against the window's current framebuffer size;
    /// a minimized (zero-size) framebuffer skips rendering for the frame entirely — a documented
    /// no-op, never an error, mirroring the GL device's minimization contract.
    void Present() override;

    [[nodiscard]] bool IsUsable() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;
    [[nodiscard]] bool IsPresentationSurfaceSrgb() const noexcept override;
    [[nodiscard]] bool UsesZeroToOneDepthBuffer() const noexcept override;

    // --- M1 diagnostics beyond IRenderDevice (Vulkan bootstrap device only) ---
    /// Copies the most recently presented swapchain image's pixel contents into `outRGBA8`,
    /// byte order R,G,B,A regardless of the surface's native channel swizzle. The caller sizes
    /// `outRGBA8` to exactly width*height*4 bytes of the current framebuffer extent; this method
    /// fills `outWidth`/`outHeight` with that extent on success. Deliberately a cold-path API for
    /// editor screenshots and automated visual verification — internally it drains the queue,
    /// records a transient layout-transition + image-to-buffer copy into a host-visible staging
    /// buffer, and CPU-swizzles B8G8R8A8 surfaces to RGBA byte order. Returns false (logging
    /// the reason) when no frame has been presented yet, the output span is mis-sized, or any
    /// Vulkan call fails; never throws.
    [[nodiscard]] bool ReadbackLatestPresentedImage(std::span<std::byte> outRGBA8,
                                                       std::uint32_t& outWidth,
                                                       std::uint32_t& outHeight);

    /// Copies one mip/layer of a byte-per-channel color texture into `outRGBA8` (R,G,B,A byte
    /// order, like the surface readback). The caller sizes the span to
    /// max(1, width >> mip) * max(1, height >> mip) * 4 bytes. Cold path: drains the queue and
    /// rides the same transient-command-buffer transfer path as the blit; returns false with a
    /// logged reason on any validation or Vulkan failure.
    [[nodiscard]] bool ReadbackTextureRGBA8(TextureHandle texture, std::uint32_t mipLevel,
                                               std::uint32_t layer, std::span<std::byte> outRGBA8);

private:
    /// Creates the 1x1 white fallback texture after device initialisation (M2c sampler
    /// bindings always have a legal image). Returns false — with a logged reason — when
    /// the upload path failed; both factories then abort like any other init failure.
    [[nodiscard]] bool CreateFallbackTexture();

    VulkanRenderDevice(Window::IWindowManager* windowManager,
                          Window::IVulkanWindowSurface* bridge, bool enableValidationLayers);

    /// Replays one submitted recorded command buffer inside the frame render pass (see the
    /// .cpp for the M2a integration contract).
    void ReplayRecordedCommands(const std::vector<RecordedCommand>& commands);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Render
