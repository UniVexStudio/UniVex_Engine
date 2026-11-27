// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "buffer_handle.h"
#include "device_operations.h"
#include "pipeline_handle.h"
#include "render_resource_descs.h"
#include "sampler_handle.h"
#include "texture_handle.h"

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace UVE::Render {

/// ICommandBuffer is a **retained** command buffer (per the approved architecture decision):
/// a caller records a sequence of render-pass/bind/draw calls into it, then hands the finished
/// object to IRenderDevice::Submit() as a batch — mirroring Vulkan/D3D12/Metal's explicit
/// command-list model (the spec's actual named target backends) rather than issuing draw calls
/// immediately as the scene is walked. Obtained via IRenderDevice::CreateCommandBuffer(),
/// never constructed directly.
/// Thread-safety: a single command-buffer instance is recorded by ONE thread and handed to
/// Submit() exactly once; recording the same instance from multiple threads concurrently, or
/// reusing it after submission, is undefined. Parallel recording ACROSS threads, however, is
/// real as of M4: N threads may each create, record, and submit their OWN instances
/// concurrently — the Vulkan backend's command buffers carry no device state at all and its
/// submission FIFO is mutex-guarded (Submit is callable from any thread; Present drains
/// the FIFO in submission order, main-thread), and the Null backend keeps the same discipline
/// for its spy list. That makes the "one recording stream per worker, merged at submission"
/// pattern available today. The GL backend executes every command during recording (an
/// inherent GL-context property), so GL recording stays bound to the context thread.
class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;
    /// Retained backends transfer these leases alongside commands and release them only after
    /// completion/discard. Immediate/model backends release after submit/discard. Lease deletion
    /// never performs native resource calls on a worker thread.
    void RetainResources(std::shared_ptr<void> resources) {
        if (resources) m_retainedResources.push_back(std::move(resources));
    }
    [[nodiscard]] std::vector<std::shared_ptr<void>> TakeRetainedResources() noexcept {
        auto result = std::move(m_retainedResources);
        m_retainedResources.clear();
        return result;
    }
    [[nodiscard]] virtual RenderFeatureSupport GetMemoryBarrierSupport() const noexcept {
        return RenderFeatureSupport::Unsupported;
    }
    /// Visibility dependency outside render passes; no queue-family or image-layout ownership
    /// transfer is implied. Unsupported adapters return explicitly without recording anything.
    [[nodiscard]] virtual RenderOperationResult MemoryBarrier(MemoryBarrierMask) {
        return RenderOperationResult::Unsupported;
    }

    /// Recorded query brackets (Foundation WP3). Unlike the device-level
    /// IRenderDevice::BeginQuery/EndQuery pair, these bracket the draws recorded
    /// between them in THIS command buffer, which is the condition a Vulkan occlusion query
    /// needs (vkCmdBeginQuery/vkCmdEndQuery around the caller's own draws in one command
    /// buffer). Rules shared by every backend: the handle must come from CreateQuery
    /// (InvalidHandle), Begin/End take an AnySamplesPassed query and WriteTimestamp a Timestamp
    /// query (InvalidArgument), brackets do not nest and must begin and end between the same
    /// pass markers (InvalidState). Results are read with IRenderDevice::GetQueryResult
    /// after the buffer was submitted and the frame presented; NotReady until then. Backends
    /// without the feature return Unsupported without recording anything.
    [[nodiscard]] virtual RenderOperationResult BeginQuery(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult EndQuery(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }
    [[nodiscard]] virtual RenderOperationResult WriteTimestamp(QueryHandle) {
        return RenderOperationResult::Unsupported;
    }

    /// Begins a render pass targeting `renderPassDesc`'s attachments. Must not be called while
    /// already inside a render pass (no nested passes).
    virtual void BeginRenderPass(const RenderPassDesc& renderPassDesc) = 0;

    /// Ends the current render pass. Must be called exactly once per BeginRenderPass().
    virtual void EndRenderPass() = 0;

    /// Binds `pipeline` as the active pipeline state for subsequent draw or dispatch commands.
    /// Since M5a this is allowed both inside and outside render-pass markers: graphics pipelines
    /// are consumed by Draw() (inside a pass), while compute pipelines must be bound OUTSIDE
    /// pass markers because Dispatch() is outside-pass and Vulkan forbids binding a COMPUTE
    /// pipeline inside a render-pass instance.
    virtual void BindPipeline(PipelineHandle pipeline) = 0;

    /// Binds `buffer` as the vertex buffer at `slot`. Must be called inside a render pass.
    virtual void BindVertexBuffer(BufferHandle buffer, std::uint32_t slot = 0) = 0;

    /// Binds `buffer` as the index buffer for subsequent DrawIndexed() calls. Must be called
    /// inside a render pass.
    virtual void BindIndexBuffer(BufferHandle buffer) = 0;

    /// Binds `texture` at `slot` for the active pipeline's shaders. Must be called inside a
    /// render pass for graphics pipelines; since M5a, binds recorded while a COMPUTE pipeline
    /// is bound are honored outside pass markers too (the compute flow lives there).
    /// Since M5b one slot space feeds the whole texture family: a pipeline's i-th reflected
    /// texture-family binding (combined samplers, separate sampled images, and STORAGE_IMAGE
    /// slots, sorted ascending by binding number) reads global slot i. Storage-image slots
    /// imageLoad/imageStore the bound texture (Vulkan: it is transitioned to and permanently
    /// rests in VK_IMAGE_LAYOUT_GENERAL, which stays samplable; GL: the texture is also bound
    /// to the image unit of the same index). Depth textures are never storage-bound — such
    /// slots deterministically fall back to a device-owned sink (warn-once). Unbound or
    /// destroyed-after-bind storage-image slots write into the same sink; sampled slots keep
    /// the M2c 1x1-white fallback.
    virtual void BindTexture(TextureHandle texture, std::uint32_t slot) = 0;

    /// Binds `sampler` at `slot`, pairing with the texture bound at the same slot (Tier 2.2).
    /// Slots share one space with BindTexture: the pipeline's i-th texture-family binding
    /// samples texture[i] through sampler[i]. A slot with no bound sampler keeps the device
    /// default (linear/trilinear, clamp-to-edge — the pre-2.2 behavior); a sampler bound with
    /// no texture applies to the slot's fallback texture, deterministically. Storage-image
    /// slots ignore samplers (imageLoad/imageStore don't sample). Same pass-gating as
    /// BindTexture (compute-aware since M5a).
    virtual void BindSampler(SamplerHandle sampler, std::uint32_t slot) = 0;

    /// Binds `buffer` as a uniform buffer at `slot` for the active pipeline's shaders. Must be
    /// called inside a render pass.
    virtual void BindUniformBuffer(BufferHandle buffer, std::uint32_t slot) = 0;

    /// Binds `buffer` as a shader-storage buffer (SSBO) at `slot` for the active pipeline's
    /// shaders (Vulkan M2f). `slot` mirrors GL's shader-storage binding points: the pipeline's
    /// i-th reflected storage-buffer binding (sorted ascending) is fed from slot i — the same
    /// global-slot contract BindTexture uses for samplers. `buffer` must have been created
    /// with BufferUsage::Storage; backends reject (loudly, no crash) any other usage, and a
    /// storage binding left unbound resolves to a deterministic all-zero buffer on Vulkan
    /// (GL's unbound-SSBO reads are undefined; the engine never relies on them). Storage-image
    /// bindings are deliberately NOT covered here — images land with the compute milestone.
    /// Must be called inside a render pass.
    virtual void BindStorageBuffer(BufferHandle buffer, std::uint32_t slot) = 0;

    /// Sets a scalar/vector/matrix uniform on the currently bound pipeline by name (Increment 21
    /// — a lighter-weight alternative to BindUniformBuffer()'s UBO path, for the common case
    /// of a handful of loose uniforms). Must be called inside a render pass, after
    /// BindPipeline(). A `name` the bound pipeline doesn't declare (e.g. optimized out by the
    /// shader compiler) is a safe no-op, logged at a low severity — not every uniform a caller
    /// might set is guaranteed to still be active after linking.
    virtual void SetUniformFloat(std::string_view name, float value) = 0;
    virtual void SetUniformInt(std::string_view name, std::int32_t value) = 0;
    virtual void SetUniformBool(std::string_view name, bool value) = 0;
    virtual void SetUniformVector2(std::string_view name, const Math::Vector2& value) = 0;
    virtual void SetUniformVector3(std::string_view name, const Math::Vector3& value) = 0;
    virtual void SetUniformVector4(std::string_view name, const Math::Vector4& value) = 0;
    virtual void SetUniformMatrix4x4(std::string_view name, const Math::Matrix4x4& value) = 0;

    /// Draws using the currently bound index buffer. Must be called inside a render pass, after
    /// a pipeline and the buffers it needs are bound.
    ///
    /// `firstIndex` is an index into the bound buffer - in INDICES, not bytes - where this draw's
    /// range begins, the same parameter `glDrawElements`/`vkCmdDrawIndexed` take. It is what lets
    /// one mesh asset hold several surfaces (index ranges with their own materials) and draw each
    /// as an ordinary indexed draw over the same buffer, instead of the mesh having to be split
    /// into one GPU buffer per material. A `firstIndex + indexCount` that runs past the bound
    /// buffer is a loud no-op in every backend, exactly like an oversized `indexCount`.
    virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1,
                                std::uint32_t firstIndex = 0) = 0;

    /// Draws without an index buffer. Must be called inside a render pass, after a pipeline and
    /// the buffers it needs are bound.
    virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1) = 0;

    /// Non-indexed draw starting at `firstVertex` inside the bound vertex buffer.
    virtual void DrawRange(std::uint32_t vertexCount, std::uint32_t instanceCount,
                           std::uint32_t firstVertex) {
        static_cast<void>(firstVertex);
        Draw(vertexCount, instanceCount);
    }

    /// Draws using the currently bound index buffer, taking the draw's PARAMETERS from GPU memory
    /// instead of from these arguments: `buffer` must be an IndirectStorage buffer holding a
    /// DrawIndexedIndirectCommand at `offsetBytes`, and the GPU reads indexCount,
    /// instanceCount, firstIndex, vertexOffset and firstInstance out of it at execution time.
    ///
    /// That indirection is the entire feature. A compute dispatch can WRITE those parameters -
    /// the same buffer binds as an SSBO - so GPU culling can decide what to draw without the
    /// answer ever travelling back to the CPU, which is the round trip CS5's culling still pays.
    ///
    /// Must be called inside a render pass, after a pipeline and the buffers it needs are bound,
    /// exactly like DrawIndexed(). An invalid handle, a buffer of the wrong usage, or an offset
    /// that does not leave a whole command inside the buffer is a loud no-op rather than a draw
    /// with garbage parameters. Backends without indirect-draw support (the fixed ES 3.0 baseline,
    /// GL below 4.0) warn once and skip, the same degradation the compute paths use.
    virtual void DrawIndexedIndirect(BufferHandle buffer, std::uint64_t offsetBytes = 0) = 0;

    /// Dispatches compute work: `groupCountX * groupCountY * groupCountZ` workgroups of the
    /// currently bound COMPUTE pipeline (created via CreateComputePipeline and bound with
    /// BindPipeline; zero on any axis dispatches nothing and is a recorded no-op). M5a
    /// contract — dispatch belongs OUTSIDE render-pass markers (Vulkan compute is illegal
    /// inside a render-pass instance), after the pipeline and any SSBOs it writes are bound:
    /// the Vulkan dynamic arm closes a still-open pass instance before dispatching (the next
    /// Begin marker re-opens it with Load semantics), classic pre-1.3 devices cannot dispatch
    /// at all (their single native pass spans the frame) and warn-once + skip, GL executes
    /// glDispatchCompute immediately followed by a conservative memory barrier (GL has no
    /// native pass object), and Null records the command for spy tests. Writes a dispatch
    /// makes to bound storage buffers are coherent for later draws in the same submission —
    /// each backend inserts the barriers that guarantee it.
    virtual void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                             std::uint32_t groupCountZ) = 0;

    /// True when the backend retains uniform values inside each pipeline's native program object
    /// across draws on the same command buffer (immediate OpenGL / GLES), letting ShaderProgram
    /// flush only uniforms that changed since the last ApplyTo() instead of re-uploading all ~200.
    [[nodiscard]] virtual bool RetainsUniformStatePerPipeline() const noexcept { return false; }

    /// Domain identity for retained per-pipeline uniform state. Backends whose native program
    /// objects persist uniform values across per-frame command buffers (OpenGL / GLES) return a
    /// stable per-device ID so ShaderProgram avoids re-uploading ~200 unchanged uniforms every frame.
    [[nodiscard]] virtual std::uint64_t GetUniformStateEpoch() const noexcept { return m_instanceId; }

    /// Monotonic identity unique to this command buffer object (prevents heap-address aliasing
    /// across frames when command buffers are allocated and destroyed each frame).
    [[nodiscard]] std::uint64_t GetInstanceId() const noexcept { return m_instanceId; }

private:
    [[nodiscard]] static std::uint64_t AllocateInstanceId() noexcept {
        static std::atomic<std::uint64_t> nextId{1U};
        return nextId.fetch_add(1U, std::memory_order_relaxed);
    }

    std::uint64_t m_instanceId = AllocateInstanceId();
    std::vector<std::shared_ptr<void>> m_retainedResources;
};

} // namespace UVE::Render
