// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "buffer_handle.h"
#include "device_operations.h"
#include "i_command_buffer.h"
#include "pipeline_handle.h"
#include "render_resource_descs.h"
#include "sampler_handle.h"
#include "shader_handle.h"
#include "texture_handle.h"
#include "uniform_reflection.h"

namespace UVE::Render {

/// IRenderDevice is the engine's backend-agnostic RHI (render hardware interface): the
/// "modern explicit" style (per the approved architecture decision), mirroring Vulkan/D3D12/
/// Metal directly — pipeline state objects, explicit render passes, and resources created/
/// destroyed by handle rather than through implicit global state. The only implementation this
/// sandbox can build and test is NullRenderDevice (engine/render); a real Vulkan/Metal/D3D12
/// backend needs SDK headers, a GPU, and windowing this environment doesn't have (see
/// docs/CODING_STANDARDS.md) and is future work once it does. Every future backend implements
/// exactly this interface, so nothing above the RHI (CameraSystem, MeshRenderer,
/// Renderer3D — later increments) needs to change when one arrives.
/// Thread-safety: implementation-defined; NullRenderDevice documents its own contract. Callers
/// should assume a render device is only safe to use from the main engine/render thread unless a
/// concrete implementation states otherwise.
class IRenderDevice {
public:
    IRenderDevice();
    virtual ~IRenderDevice();
    IRenderDevice(const IRenderDevice&) = delete;
    IRenderDevice& operator=(const IRenderDevice&) = delete;

    /// Takes exclusive retirement responsibility for newly allocated, distinct live textures.
    /// Attach the returned lease to every command buffer that uses them. The input vector stays
    /// owned by the caller on rejection/exception. Releasing a lease on any thread only enqueues
    /// retirement; native destruction occurs on the owner thread at a safe backend boundary.
    /// No device pointer survives device destruction. Requires retainedTextures capability.
    [[nodiscard]] std::shared_ptr<void> AdoptTransientTextures(std::vector<TextureHandle>&& textures);
    /// Owner-thread drain; only backend-completed/discarded leases arrive here. Present/WaitIdle
    /// call it automatically. Clients may also drain discarded, unsubmitted command-buffer leases.
    std::size_t CollectRetiredTextures();

    /// Optional capabilities are explicit. The default is unsupported for third-party adapters;
    /// it never fabricates success or changes output. Model recording is distinct from GPU work.
    [[nodiscard]] virtual RenderDeviceCapabilities GetCapabilities() const noexcept { return {}; }
    [[nodiscard]] virtual RenderOperationResult GetTextureDesc(TextureHandle,
                                                                     TextureDesc &) const {
        return RenderOperationResult::Unsupported;
    }
    /// The descriptor a live buffer was created with — size, usage and the memory/update hints
    /// exactly as requested (not the placement the backend chose). Output untouched on failure.
    [[nodiscard]] virtual RenderOperationResult GetBufferDesc(BufferHandle,
                                                                    BufferDesc &) const {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult CopyBuffer(const BufferCopyDesc &) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult BlitTexture(const TextureBlitDesc &) {
        return RenderOperationResult::Unsupported;
    }
    /// Rebuilds already allocated lower levels from level zero. Does not change texture shape.
    [[nodiscard]] virtual RenderOperationResult GenerateMipmaps(TextureHandle) {
        return RenderOperationResult::Unsupported;
    }
    /// Owner-thread fence after ALREADY SUBMITTED GPU work. A retained backend rejects a fence
    /// while its CPU submission FIFO is nonempty; Present must drain that FIFO first. Fences
    /// are one-shot, device-owned and non-recycled. Destroy may return NotReady until completion.
    [[nodiscard]] virtual RenderOperationResult InsertFence(FenceHandle &) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult WaitFence(FenceHandle, std::uint64_t) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult DestroyFence(FenceHandle) {
        return RenderOperationResult::Unsupported;
    }
    /// Does not pretend that deferred CPU command lists have executed. Retained backends return
    /// InvalidState until they have been presented; owner-thread resource lifetime boundary.
    [[nodiscard]] virtual RenderOperationResult WaitIdle() {
        return RenderOperationResult::Unsupported;
    }
    /// Creates a timeline semaphore object. Its counter starts at `desc.initialValue`; the
    /// backend refuses (Unsupported) when it cannot provide a real counter. Output is untouched
    /// on failure.
    [[nodiscard]] virtual RenderOperationResult CreateSemaphore(const SemaphoreDesc &,
                                                                     SemaphoreHandle &) {
        return RenderOperationResult::Unsupported;
    }
    /// Reads the current counter value. Owner-thread, non-blocking, never invents a value:
    /// failures leave the output untouched.
    [[nodiscard]] virtual RenderOperationResult GetSemaphoreCounter(SemaphoreHandle,
                                                                         std::uint64_t &) const {
        return RenderOperationResult::Unsupported;
    }
    /// Advances the counter. A value not strictly greater than the current one is an
    /// `InvalidArgument` (a timeline counter never moves backwards). A CPU-side signal is real
    /// device state, not a promise that any GPU work has finished.
    [[nodiscard]] virtual RenderOperationResult SignalSemaphore(SemaphoreHandle,
                                                                     std::uint64_t) {
        return RenderOperationResult::Unsupported;
    }
    /// Blocks until the counter reaches `value` (immediately, if it already has) or the timeout
    /// expires, returning `TimedOut` on expiry. Never claims unrelated GPU work completed. May be
    /// called from a thread other than the one that signals, exactly like a real timeline wait.
    [[nodiscard]] virtual RenderOperationResult WaitSemaphore(SemaphoreHandle, std::uint64_t,
                                                                   std::uint64_t) {
        return RenderOperationResult::Unsupported;
    }
    /// Destroys the object. Callers must guarantee no submission still references it, exactly as
    /// for buffers and textures.
    [[nodiscard]] virtual RenderOperationResult DestroySemaphore(SemaphoreHandle) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult CreateQuery(RenderQueryKind, QueryHandle &) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult BeginQuery(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult EndQuery(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult WriteTimestamp(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    /// Nonblocking by default; NotReady/Unsupported leave output untouched. Null never returns
    /// invented zero timings or visibility results. wait=true is an explicitly blocking cold path.
    [[nodiscard]] virtual RenderOperationResult GetQueryResult(QueryHandle, RenderQueryResult &,
                                                                     bool = false) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult DestroyQuery(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    /// Describes the existing device-owned default surface/swapchain; no second window owner.
    [[nodiscard]] virtual RenderOperationResult GetSurfaceInfo(RenderSurfaceInfo &) const {
        return RenderOperationResult::Unsupported;
    }

    /// Creates a GPU buffer per `desc`, optionally uploading `initialData` (must be no larger
    /// than `desc.sizeBytes`). Never returns kInvalidBufferHandle on success.
    [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDesc& desc,
                                                           std::span<const std::byte> initialData = {}) = 0;

    /// Destroys `buffer`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyBuffer(BufferHandle buffer) = 0;

    /// Overwrites `buffer`'s contents at `offsetBytes` with `data`. Returns false (logging the
    /// reason) if `buffer` is unknown or the write would exceed the buffer's size.
    [[nodiscard]] virtual bool UpdateBuffer(BufferHandle buffer, std::span<const std::byte> data,
                                                std::uint64_t offsetBytes = 0) = 0;

    /// Copies `buffer`'s current contents at `offsetBytes` into `outData`, filling it exactly.
    /// This is the read direction of UpdateBuffer and the only way engine code can observe
    /// what the GPU wrote — the capability GPU compute needs to be verifiable rather than
    /// merely dispatched (a compute result nobody can read back cannot be asserted on).
    ///
    /// Cold path by construction: the call synchronizes with the device (each backend drains
    /// the work that could still be writing the buffer) before copying, so it belongs in
    /// tooling, tests, and deliberate CPU-readback steps — never in a per-frame hot loop.
    ///
    /// Returns false (logging the reason) if `buffer` is unknown, the range would exceed the
    /// buffer's size, or the backend cannot read that buffer's memory. Every usage and every
    /// `BufferMemory` placement reads back on the shipped backends (device-local memory goes
    /// through a staging copy); `BufferMemory::Readback` is the hint for buffers the CPU reads
    /// routinely. This is a cold path — it drains the queue so what the GPU last wrote is what
    /// the host sees. An empty `outData` is a successful no-op.
    [[nodiscard]] virtual bool ReadbackBuffer(BufferHandle buffer, std::span<std::byte> outData,
                                                  std::uint64_t offsetBytes = 0) = 0;

    /// Creates a GPU texture per `desc`, optionally uploading `initialData`. A non-empty upload
    /// is tightly packed in mip-level order (level 0 first, then levels 1..N) and must contain
    /// exactly the bytes required by every declared level. Array/cubemap uploads pack every
    /// level's layers 0..N-1 contiguously (cube faces in `CubemapFace` order) before the
    /// next level starts. Empty data is legal only for a one-level render target. The backend
    /// must reject invalid descriptors or partial uploads through `ValidateTextureUpload`
    /// before allocating a resource; valid creation never returns kInvalidTextureHandle.
    [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDesc& desc,
                                                             std::span<const std::byte> initialData = {}) = 0;

    /// Reports whether this device can sample a texture using `format`. RenderSystems uses this
    /// capability to choose a native Basis transcode target; compressed formats unsupported by
    /// the active GPU fall back to RGBA8. Null deliberately reports no compressed support.
    [[nodiscard]] virtual bool SupportsTextureFormat(
        TextureFormat format, TextureColorSpace colorSpace = TextureColorSpace::Linear) const noexcept = 0;

    /// Destroys `texture`.  A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyTexture(TextureHandle texture) = 0;

    /// True when this backend exposes a bindless / descriptor-indexed texture table.
    [[nodiscard]] virtual bool SupportsBindlessTextures() const noexcept {
        return GetCapabilities().bindlessTextures != RenderFeatureSupport::Unsupported;
    }

    /// Registers a live texture in the device's bindless texture table, returning a 32-bit shader
    /// index or `0xFFFFFFFFU` (`kInvalidBindlessTextureIndex`) if unsupported or invalid.
    [[nodiscard]] virtual std::uint32_t RegisterBindlessTexture(TextureHandle) {
        return 0xFFFFFFFFU;
    }

    /// Releases a previously registered bindless texture slot. Safe no-op if invalid or unused.
    virtual void UnregisterBindlessTexture(std::uint32_t) {}

    /// Creates a sampler object per `desc` (Tier 2.2). The backend rejects malformed descs
    /// (see IsSamplerDescValid) with kInvalidSamplerHandle; above-limit anisotropy is
    /// clamped to the device maximum (warn-once), never a creation failure. Valid creation
    /// never returns kInvalidSamplerHandle.
    [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;

    /// Destroys `sampler`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroySampler(SamplerHandle sampler) = 0;

    /// Creates a shader per `desc`. Never returns kInvalidShaderHandle on success. If
    /// `outInfoLog` is non-null, the backend's raw compile info log is written to it regardless
    /// of success/failure (Increment 21 — Shader::ShaderManager parses this into structured,
    /// line-numbered diagnostics; a successful compile can still produce non-fatal warning text).
    /// NullRenderDevice never populates `outInfoLog` (leaves it untouched) — it never really
    /// compiles anything, so it has no log to give.
    [[nodiscard]] virtual ShaderHandle CreateShader(const ShaderDesc& desc,
                                                           std::string* outInfoLog = nullptr) = 0;

    /// Destroys `shader`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyShader(ShaderHandle shader) = 0;

    /// Creates a pipeline state object per `desc`. Returns kInvalidPipelineHandle (logging the
    /// reason) if `desc.vertexShader`/`desc.fragmentShader` don't reference live shaders. Same
    /// `outInfoLog` contract as CreateShader(), but for the link step's info log.
    [[nodiscard]] virtual PipelineHandle CreatePipeline(const PipelineDesc& desc,
                                                               std::string* outInfoLog = nullptr) = 0;

    /// Creates a COMPUTE pipeline (M5a) per `desc` — one compute-stage shader, no fixed-function
    /// state. Returns kInvalidPipelineHandle (logging the reason into `outInfoLog`, same
    /// contract as CreatePipeline) when the shader handle is invalid, is not a compute-stage
    /// shader, or the backend cannot build the pipeline. The returned handle shares the graphics
    /// pipeline handle domain: BindPipeline binds it, and ICommandBuffer::Dispatch
    /// executes it. Since M5b, STORAGE_IMAGE bindings are accepted by both compute and graphics
    /// reflection — they share the one texture-slot space fed by BindTexture (see its doc).
    /// Backends without compute (GL contexts older than 4.3, the fixed ES 3.0
    /// Android baseline) fail creation loudly rather than returning a handle that could never
    /// dispatch.
    [[nodiscard]] virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc,
                                                                      std::string* outInfoLog = nullptr) = 0;

    /// Destroys `pipeline`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

    /// Returns every uniform `pipeline`'s shaders declare, reflected once at link time (Increment
    /// 21). Empty for an unknown handle. NullRenderDevice always returns empty — it never
    /// really links anything, so it has nothing to reflect.
    [[nodiscard]] virtual std::vector<UniformReflection> GetPipelineUniforms(
        PipelineHandle pipeline) const = 0;

    /// Retrieves `pipeline`'s compiled GL program binary (Increment 21's on-disk shader cache),
    /// writing it to `outBinary` and the backend-specific binary format to `outFormat`. Returns
    /// false (leaving both out-params untouched) if `pipeline` is unknown or the backend/driver
    /// can't produce a binary — always false for NullRenderDevice, since it never compiles
    /// anything real.
    [[nodiscard]] virtual bool GetPipelineBinary(PipelineHandle pipeline, std::vector<std::byte>& outBinary,
                                                     std::uint32_t& outFormat) const = 0;

    /// Creates a pipeline directly from a previously retrieved GetPipelineBinary() blob and
    /// its `format`, skipping shader compilation/linking entirely — the fast path Shader::
    /// ShaderManager's on-disk cache uses on a cache hit. Returns kInvalidPipelineHandle
    /// (logging the reason) if the backend/driver rejects `binary` — e.g. a stale cache entry
    /// from before a driver update — which callers must treat as an ordinary cache miss, never a
    /// hard failure. NullRenderDevice always succeeds, bookkeeping `desc`'s fixed-function
    /// state exactly like CreatePipeline() does (with invalid shader handles, since none was
    /// ever compiled).
    [[nodiscard]] virtual PipelineHandle CreatePipelineFromBinary(std::span<const std::byte> binary,
                                                                         std::uint32_t format,
                                                                         const PipelineBinaryDesc& desc) = 0;

    /// Creates a new, empty ICommandBuffer ready for recording.
    /// M4 threading contract: each ICommandBuffer is fully independent — the Vulkan and
    /// Null backends record into per-object retained lists that touch no device state — so
    /// distinct threads may create and record into their OWN command buffers concurrently
    /// (one object is never shared between threads). The GL backend executes every command
    /// during recording (an inherent GL-context property), so GL recording must happen on
    /// the context's thread.
    [[nodiscard]] virtual std::unique_ptr<ICommandBuffer> CreateCommandBuffer() = 0;

    /// Submits a finished command buffer for execution. Consumes `commandBuffer` — it must not be
    /// used again after this call.
    /// M4 threading contract: safe to call from any thread on every backend — the Vulkan
    /// backend enqueues the recorded commands into a mutex-guarded submission FIFO that the
    /// next Present() drains in submission order, the Null backend stores its spy list
    /// under the same discipline, and GL simply releases the object. Present() itself
    /// stays single-threaded (the GPU-timeline owner).
    virtual void Submit(std::unique_ptr<ICommandBuffer> commandBuffer) = 0;

    /// Foundation WP4: Submit with submission-level queue synchronisation (SubmitDesc).
    /// The command buffer is consumed in every case; a result other than Success/Recorded means
    /// the work was NOT queued (the buffer is dropped). Validation on every backend: an unknown
    /// or destroyed semaphore -> InvalidHandle; a signal value not greater than the counter at
    /// submit time, or the same semaphore named twice across the two lists -> InvalidArgument; a
    /// non-empty desc on a device whose `timelineSemaphores` capability is not Native ->
    /// Unsupported. An empty desc is exactly Submit. Granularity is the backend's real queue
    /// granularity: Vulkan replays every submission of a frame into one native submission, so
    /// the waits of all submissions gate that frame's vkQueueSubmit and the signals of all of
    /// them are performed when that frame's GPU work completes - never earlier than asked, never
    /// claimed before the GPU reached it. A frame the backend skips (minimised window, acquire
    /// failure) drops its submissions AND their sync; a waiter on such a signal times out rather
    /// than being lied to. Named differently from Submit on purpose: an overload would be
    /// hidden by every existing Submit override (-Woverloaded-virtual).
    [[nodiscard]] virtual RenderOperationResult SubmitWithSync(std::unique_ptr<ICommandBuffer> commandBuffer,
                                                                    const SubmitDesc &desc) {
        if (desc.waits.empty() && desc.signals.empty()) {
            Submit(std::move(commandBuffer));
            return RenderOperationResult::Success;
        }
        return RenderOperationResult::Unsupported;
    }

    /// Presents the backend's default framebuffer (the window's back buffer), analogous to a
    /// real Vulkan/D3D12 swapchain's present call — a distinct, explicit step after command
    /// buffer submission, matching this RHI's own "modern explicit" design. Call once per frame,
    /// after Submit(). NullRenderDevice's implementation is a bookkeeping no-op (see
    /// GetPresentCallCount()); a windowless backend that never renders to a window has nothing
    /// meaningful to do here either.
    virtual void Present() = 0;

    /// True when the backend can safely accept resource/command calls. A device can become
    /// unusable after a native surface/context loss; callers must stop issuing work when this
    /// returns false. NullRenderDevice always returns true because it is intentionally inert.
    [[nodiscard]] virtual bool IsUsable() const noexcept = 0;

    /// A short human-readable backend name (e.g. `"Null"`), for logging/diagnostics.
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;

    /// True when the backend's default presentation surface performs hardware linear-to-sRGB
    /// encoding on writes (for example a Vulkan SRGB swapchain). When false (OpenGL default
    /// framebuffer without framebuffer sRGB or a UNORM headless swapchain), the final tone-mapping
    /// pass encodes linear HDR output with the IEC 61966-2-1 sRGB transfer function.
    [[nodiscard]] virtual bool IsPresentationSurfaceSrgb() const noexcept { return false; }

    /// True when clip-space depth [0, 1] is stored directly in [0, 1] in the depth buffer
    /// (Vulkan). When false (OpenGL default depth range), depth values are remapped from
    /// [-1, 1] into [0, 1] so [0, 1] clip Z lands in [0.5, 1.0].
    [[nodiscard]] virtual bool UsesZeroToOneDepthBuffer() const noexcept { return false; }
private:
    struct TextureRetirementState;
    std::shared_ptr<TextureRetirementState> m_textureRetirement;
};

} // namespace UVE::Render
