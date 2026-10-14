// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/device_operations_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/pipeline_handle_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi/sampler_handle_uve.h"
#include "uve/rhi/shader_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"
#include "uve/rhi/uniform_reflection_uve.h"

namespace UVE::Render {

/// IRenderDeviceUVE is the engine's backend-agnostic RHI (render hardware interface): the
/// "modern explicit" style (per the approved architecture decision), mirroring Vulkan/D3D12/
/// Metal directly — pipeline state objects, explicit render passes, and resources created/
/// destroyed by handle rather than through implicit global state. The only implementation this
/// sandbox can build and test is NullRenderDeviceUVE (engine/render); a real Vulkan/Metal/D3D12
/// backend needs SDK headers, a GPU, and windowing this environment doesn't have (see
/// docs/CODING_STANDARDS.md) and is future work once it does. Every future backend implements
/// exactly this interface, so nothing above the RHI (CameraSystemUVE, MeshRendererUVE,
/// Renderer3DUVE — later increments) needs to change when one arrives.
/// Thread-safety: implementation-defined; NullRenderDeviceUVE documents its own contract. Callers
/// should assume a render device is only safe to use from the main engine/render thread unless a
/// concrete implementation states otherwise.
class IRenderDeviceUVE {
public:
    IRenderDeviceUVE();
    virtual ~IRenderDeviceUVE();
    IRenderDeviceUVE(const IRenderDeviceUVE&) = delete;
    IRenderDeviceUVE& operator=(const IRenderDeviceUVE&) = delete;

    /// Takes exclusive retirement responsibility for newly allocated, distinct live textures.
    /// Attach the returned lease to every command buffer that uses them. The input vector stays
    /// owned by the caller on rejection/exception. Releasing a lease on any thread only enqueues
    /// retirement; native destruction occurs on the owner thread at a safe backend boundary.
    /// No device pointer survives device destruction. Requires retainedTextures capability.
    [[nodiscard]] std::shared_ptr<void> AdoptTransientTexturesUVE(std::vector<TextureHandleUVE>&& textures);
    /// Owner-thread drain; only backend-completed/discarded leases arrive here. Present/WaitIdle
    /// call it automatically. Clients may also drain discarded, unsubmitted command-buffer leases.
    std::size_t CollectRetiredTexturesUVE();

    /// Optional capabilities are explicit. The default is unsupported for third-party adapters;
    /// it never fabricates success or changes output. Model recording is distinct from GPU work.
    [[nodiscard]] virtual RenderDeviceCapabilitiesUVE GetCapabilitiesUVE() const noexcept { return {}; }
    [[nodiscard]] virtual RenderOperationResultUVE GetTextureDescUVE(TextureHandleUVE,
                                                                     TextureDescUVE &) const {
        return RenderOperationResultUVE::Unsupported;
    }
    /// The descriptor a live buffer was created with — size, usage and the memory/update hints
    /// exactly as requested (not the placement the backend chose). Output untouched on failure.
    [[nodiscard]] virtual RenderOperationResultUVE GetBufferDescUVE(BufferHandleUVE,
                                                                    BufferDescUVE &) const {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE CopyBufferUVE(const BufferCopyDescUVE &) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE BlitTextureUVE(const TextureBlitDescUVE &) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Rebuilds already allocated lower levels from level zero. Does not change texture shape.
    [[nodiscard]] virtual RenderOperationResultUVE GenerateMipmapsUVE(TextureHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Owner-thread fence after ALREADY SUBMITTED GPU work. A retained backend rejects a fence
    /// while its CPU submission FIFO is nonempty; Present must drain that FIFO first. Fences
    /// are one-shot, device-owned and non-recycled. Destroy may return NotReady until completion.
    [[nodiscard]] virtual RenderOperationResultUVE InsertFenceUVE(FenceHandleUVE &) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE WaitFenceUVE(FenceHandleUVE, std::uint64_t) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE DestroyFenceUVE(FenceHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Does not pretend that deferred CPU command lists have executed. Retained backends return
    /// InvalidState until they have been presented; owner-thread resource lifetime boundary.
    [[nodiscard]] virtual RenderOperationResultUVE WaitIdleUVE() {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Creates a timeline semaphore object. Its counter starts at `desc.initialValue`; the
    /// backend refuses (Unsupported) when it cannot provide a real counter. Output is untouched
    /// on failure.
    [[nodiscard]] virtual RenderOperationResultUVE CreateSemaphoreUVE(const SemaphoreDescUVE &,
                                                                     SemaphoreHandleUVE &) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Reads the current counter value. Owner-thread, non-blocking, never invents a value:
    /// failures leave the output untouched.
    [[nodiscard]] virtual RenderOperationResultUVE GetSemaphoreCounterUVE(SemaphoreHandleUVE,
                                                                         std::uint64_t &) const {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Advances the counter. A value not strictly greater than the current one is an
    /// `InvalidArgument` (a timeline counter never moves backwards). A CPU-side signal is real
    /// device state, not a promise that any GPU work has finished.
    [[nodiscard]] virtual RenderOperationResultUVE SignalSemaphoreUVE(SemaphoreHandleUVE,
                                                                     std::uint64_t) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Blocks until the counter reaches `value` (immediately, if it already has) or the timeout
    /// expires, returning `TimedOut` on expiry. Never claims unrelated GPU work completed. May be
    /// called from a thread other than the one that signals, exactly like a real timeline wait.
    [[nodiscard]] virtual RenderOperationResultUVE WaitSemaphoreUVE(SemaphoreHandleUVE, std::uint64_t,
                                                                   std::uint64_t) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Destroys the object. Callers must guarantee no submission still references it, exactly as
    /// for buffers and textures.
    [[nodiscard]] virtual RenderOperationResultUVE DestroySemaphoreUVE(SemaphoreHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE CreateQueryUVE(RenderQueryKindUVE, QueryHandleUVE &) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE BeginQueryUVE(QueryHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE EndQueryUVE(QueryHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE WriteTimestampUVE(QueryHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Nonblocking by default; NotReady/Unsupported leave output untouched. Null never returns
    /// invented zero timings or visibility results. wait=true is an explicitly blocking cold path.
    [[nodiscard]] virtual RenderOperationResultUVE GetQueryResultUVE(QueryHandleUVE, RenderQueryResultUVE &,
                                                                     bool = false) {
        return RenderOperationResultUVE::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResultUVE DestroyQueryUVE(QueryHandleUVE) {
        return RenderOperationResultUVE::Unsupported;
    }
    /// Describes the existing device-owned default surface/swapchain; no second window owner.
    [[nodiscard]] virtual RenderOperationResultUVE GetSurfaceInfoUVE(RenderSurfaceInfoUVE &) const {
        return RenderOperationResultUVE::Unsupported;
    }

    /// Creates a GPU buffer per `desc`, optionally uploading `initialData` (must be no larger
    /// than `desc.sizeBytes`). Never returns kInvalidBufferHandleUVE on success.
    [[nodiscard]] virtual BufferHandleUVE CreateBufferUVE(const BufferDescUVE& desc,
                                                           std::span<const std::byte> initialData = {}) = 0;

    /// Destroys `buffer`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyBufferUVE(BufferHandleUVE buffer) = 0;

    /// Overwrites `buffer`'s contents at `offsetBytes` with `data`. Returns false (logging the
    /// reason) if `buffer` is unknown or the write would exceed the buffer's size.
    [[nodiscard]] virtual bool UpdateBufferUVE(BufferHandleUVE buffer, std::span<const std::byte> data,
                                                std::uint64_t offsetBytes = 0) = 0;

    /// Copies `buffer`'s current contents at `offsetBytes` into `outData`, filling it exactly.
    /// This is the read direction of UpdateBufferUVE and the only way engine code can observe
    /// what the GPU wrote — the capability GPU compute needs to be verifiable rather than
    /// merely dispatched (a compute result nobody can read back cannot be asserted on).
    ///
    /// Cold path by construction: the call synchronizes with the device (each backend drains
    /// the work that could still be writing the buffer) before copying, so it belongs in
    /// tooling, tests, and deliberate CPU-readback steps — never in a per-frame hot loop.
    ///
    /// Returns false (logging the reason) if `buffer` is unknown, the range would exceed the
    /// buffer's size, or the backend cannot read that buffer's memory. Every usage and every
    /// `BufferMemoryUVE` placement reads back on the shipped backends (device-local memory goes
    /// through a staging copy); `BufferMemoryUVE::Readback` is the hint for buffers the CPU reads
    /// routinely. This is a cold path — it drains the queue so what the GPU last wrote is what
    /// the host sees. An empty `outData` is a successful no-op.
    [[nodiscard]] virtual bool ReadbackBufferUVE(BufferHandleUVE buffer, std::span<std::byte> outData,
                                                  std::uint64_t offsetBytes = 0) = 0;

    /// Creates a GPU texture per `desc`, optionally uploading `initialData`. A non-empty upload
    /// is tightly packed in mip-level order (level 0 first, then levels 1..N) and must contain
    /// exactly the bytes required by every declared level. Array/cubemap uploads pack every
    /// level's layers 0..N-1 contiguously (cube faces in `CubemapFaceUVE` order) before the
    /// next level starts. Empty data is legal only for a one-level render target. The backend
    /// must reject invalid descriptors or partial uploads through `ValidateTextureUploadUVE`
    /// before allocating a resource; valid creation never returns kInvalidTextureHandleUVE.
    [[nodiscard]] virtual TextureHandleUVE CreateTextureUVE(const TextureDescUVE& desc,
                                                             std::span<const std::byte> initialData = {}) = 0;

    /// Reports whether this device can sample a texture using `format`. RenderSystems uses this
    /// capability to choose a native Basis transcode target; compressed formats unsupported by
    /// the active GPU fall back to RGBA8. Null deliberately reports no compressed support.
    [[nodiscard]] virtual bool SupportsTextureFormatUVE(
        TextureFormatUVE format, TextureColorSpaceUVE colorSpace = TextureColorSpaceUVE::Linear) const noexcept = 0;

    /// Destroys `texture`.  A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyTextureUVE(TextureHandleUVE texture) = 0;

    /// Creates a sampler object per `desc` (Tier 2.2). The backend rejects malformed descs
    /// (see IsSamplerDescValidUVE) with kInvalidSamplerHandleUVE; above-limit anisotropy is
    /// clamped to the device maximum (warn-once), never a creation failure. Valid creation
    /// never returns kInvalidSamplerHandleUVE.
    [[nodiscard]] virtual SamplerHandleUVE CreateSamplerUVE(const SamplerDescUVE& desc) = 0;

    /// Destroys `sampler`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroySamplerUVE(SamplerHandleUVE sampler) = 0;

    /// Creates a shader per `desc`. Never returns kInvalidShaderHandleUVE on success. If
    /// `outInfoLog` is non-null, the backend's raw compile info log is written to it regardless
    /// of success/failure (Increment 21 — Shader::ShaderManagerUVE parses this into structured,
    /// line-numbered diagnostics; a successful compile can still produce non-fatal warning text).
    /// NullRenderDeviceUVE never populates `outInfoLog` (leaves it untouched) — it never really
    /// compiles anything, so it has no log to give.
    [[nodiscard]] virtual ShaderHandleUVE CreateShaderUVE(const ShaderDescUVE& desc,
                                                           std::string* outInfoLog = nullptr) = 0;

    /// Destroys `shader`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyShaderUVE(ShaderHandleUVE shader) = 0;

    /// Creates a pipeline state object per `desc`. Returns kInvalidPipelineHandleUVE (logging the
    /// reason) if `desc.vertexShader`/`desc.fragmentShader` don't reference live shaders. Same
    /// `outInfoLog` contract as CreateShaderUVE(), but for the link step's info log.
    [[nodiscard]] virtual PipelineHandleUVE CreatePipelineUVE(const PipelineDescUVE& desc,
                                                               std::string* outInfoLog = nullptr) = 0;

    /// Creates a COMPUTE pipeline (M5a) per `desc` — one compute-stage shader, no fixed-function
    /// state. Returns kInvalidPipelineHandleUVE (logging the reason into `outInfoLog`, same
    /// contract as CreatePipelineUVE) when the shader handle is invalid, is not a compute-stage
    /// shader, or the backend cannot build the pipeline. The returned handle shares the graphics
    /// pipeline handle domain: BindPipelineUVE binds it, and ICommandBufferUVE::DispatchUVE
    /// executes it. Since M5b, STORAGE_IMAGE bindings are accepted by both compute and graphics
    /// reflection — they share the one texture-slot space fed by BindTextureUVE (see its doc).
    /// Backends without compute (GL contexts older than 4.3, the fixed ES 3.0
    /// Android baseline) fail creation loudly rather than returning a handle that could never
    /// dispatch.
    [[nodiscard]] virtual PipelineHandleUVE CreateComputePipelineUVE(const ComputePipelineDescUVE& desc,
                                                                      std::string* outInfoLog = nullptr) = 0;

    /// Destroys `pipeline`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyPipelineUVE(PipelineHandleUVE pipeline) = 0;

    /// Returns every uniform `pipeline`'s shaders declare, reflected once at link time (Increment
    /// 21). Empty for an unknown handle. NullRenderDeviceUVE always returns empty — it never
    /// really links anything, so it has nothing to reflect.
    [[nodiscard]] virtual std::vector<UniformReflectionUVE> GetPipelineUniformsUVE(
        PipelineHandleUVE pipeline) const = 0;

    /// Retrieves `pipeline`'s compiled GL program binary (Increment 21's on-disk shader cache),
    /// writing it to `outBinary` and the backend-specific binary format to `outFormat`. Returns
    /// false (leaving both out-params untouched) if `pipeline` is unknown or the backend/driver
    /// can't produce a binary — always false for NullRenderDeviceUVE, since it never compiles
    /// anything real.
    [[nodiscard]] virtual bool GetPipelineBinaryUVE(PipelineHandleUVE pipeline, std::vector<std::byte>& outBinary,
                                                     std::uint32_t& outFormat) const = 0;

    /// Creates a pipeline directly from a previously retrieved GetPipelineBinaryUVE() blob and
    /// its `format`, skipping shader compilation/linking entirely — the fast path Shader::
    /// ShaderManagerUVE's on-disk cache uses on a cache hit. Returns kInvalidPipelineHandleUVE
    /// (logging the reason) if the backend/driver rejects `binary` — e.g. a stale cache entry
    /// from before a driver update — which callers must treat as an ordinary cache miss, never a
    /// hard failure. NullRenderDeviceUVE always succeeds, bookkeeping `desc`'s fixed-function
    /// state exactly like CreatePipelineUVE() does (with invalid shader handles, since none was
    /// ever compiled).
    [[nodiscard]] virtual PipelineHandleUVE CreatePipelineFromBinaryUVE(std::span<const std::byte> binary,
                                                                         std::uint32_t format,
                                                                         const PipelineBinaryDescUVE& desc) = 0;

    /// Creates a new, empty ICommandBufferUVE ready for recording.
    /// M4 threading contract: each ICommandBufferUVE is fully independent — the Vulkan and
    /// Null backends record into per-object retained lists that touch no device state — so
    /// distinct threads may create and record into their OWN command buffers concurrently
    /// (one object is never shared between threads). The GL backend executes every command
    /// during recording (an inherent GL-context property), so GL recording must happen on
    /// the context's thread.
    [[nodiscard]] virtual std::unique_ptr<ICommandBufferUVE> CreateCommandBufferUVE() = 0;

    /// Submits a finished command buffer for execution. Consumes `commandBuffer` — it must not be
    /// used again after this call.
    /// M4 threading contract: safe to call from any thread on every backend — the Vulkan
    /// backend enqueues the recorded commands into a mutex-guarded submission FIFO that the
    /// next PresentUVE() drains in submission order, the Null backend stores its spy list
    /// under the same discipline, and GL simply releases the object. PresentUVE() itself
    /// stays single-threaded (the GPU-timeline owner).
    virtual void SubmitUVE(std::unique_ptr<ICommandBufferUVE> commandBuffer) = 0;

    /// Presents the backend's default framebuffer (the window's back buffer), analogous to a
    /// real Vulkan/D3D12 swapchain's present call — a distinct, explicit step after command
    /// buffer submission, matching this RHI's own "modern explicit" design. Call once per frame,
    /// after SubmitUVE(). NullRenderDeviceUVE's implementation is a bookkeeping no-op (see
    /// GetPresentCallCountUVE()); a windowless backend that never renders to a window has nothing
    /// meaningful to do here either.
    virtual void PresentUVE() = 0;

    /// True when the backend can safely accept resource/command calls. A device can become
    /// unusable after a native surface/context loss; callers must stop issuing work when this
    /// returns false. NullRenderDeviceUVE always returns true because it is intentionally inert.
    [[nodiscard]] virtual bool IsUsableUVE() const noexcept = 0;

    /// A short human-readable backend name (e.g. `"Null"`), for logging/diagnostics.
    [[nodiscard]] virtual std::string_view GetBackendNameUVE() const noexcept = 0;
private:
    struct TextureRetirementStateUVE;
    std::shared_ptr<TextureRetirementStateUVE> m_textureRetirement;
};

} // namespace UVE::Render
