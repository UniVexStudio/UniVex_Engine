// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string_view>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/pipeline_handle_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {

/// ICommandBufferUVE is a **retained** command buffer (per the approved architecture decision):
/// a caller records a sequence of render-pass/bind/draw calls into it, then hands the finished
/// object to IRenderDeviceUVE::SubmitUVE() as a batch — mirroring Vulkan/D3D12/Metal's explicit
/// command-list model (the spec's actual named target backends) rather than issuing draw calls
/// immediately as the scene is walked. Obtained via IRenderDeviceUVE::CreateCommandBufferUVE(),
/// never constructed directly.
/// Thread-safety: a single command-buffer instance is recorded by ONE thread and handed to
/// SubmitUVE() exactly once; recording the same instance from multiple threads concurrently, or
/// reusing it after submission, is undefined. Parallel recording ACROSS threads, however, is
/// real as of M4: N threads may each create, record, and submit their OWN instances
/// concurrently — the Vulkan backend's command buffers carry no device state at all and its
/// submission FIFO is mutex-guarded (SubmitUVE is callable from any thread; PresentUVE drains
/// the FIFO in submission order, main-thread), and the Null backend keeps the same discipline
/// for its spy list. That makes the "one recording stream per worker, merged at submission"
/// pattern available today. The GL backend executes every command during recording (an
/// inherent GL-context property), so GL recording stays bound to the context thread.
class ICommandBufferUVE {
public:
    virtual ~ICommandBufferUVE() = default;

    /// Begins a render pass targeting `renderPassDesc`'s attachments. Must not be called while
    /// already inside a render pass (no nested passes).
    virtual void BeginRenderPassUVE(const RenderPassDescUVE& renderPassDesc) = 0;

    /// Ends the current render pass. Must be called exactly once per BeginRenderPassUVE().
    virtual void EndRenderPassUVE() = 0;

    /// Binds `pipeline` as the active pipeline state for subsequent draw or dispatch commands.
    /// Since M5a this is allowed both inside and outside render-pass markers: graphics pipelines
    /// are consumed by DrawUVE() (inside a pass), while compute pipelines must be bound OUTSIDE
    /// pass markers because DispatchUVE() is outside-pass and Vulkan forbids binding a COMPUTE
    /// pipeline inside a render-pass instance.
    virtual void BindPipelineUVE(PipelineHandleUVE pipeline) = 0;

    /// Binds `buffer` as the vertex buffer at `slot`. Must be called inside a render pass.
    virtual void BindVertexBufferUVE(BufferHandleUVE buffer, std::uint32_t slot = 0) = 0;

    /// Binds `buffer` as the index buffer for subsequent DrawIndexedUVE() calls. Must be called
    /// inside a render pass.
    virtual void BindIndexBufferUVE(BufferHandleUVE buffer) = 0;

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
    virtual void BindTextureUVE(TextureHandleUVE texture, std::uint32_t slot) = 0;

    /// Binds `buffer` as a uniform buffer at `slot` for the active pipeline's shaders. Must be
    /// called inside a render pass.
    virtual void BindUniformBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) = 0;

    /// Binds `buffer` as a shader-storage buffer (SSBO) at `slot` for the active pipeline's
    /// shaders (Vulkan M2f). `slot` mirrors GL's shader-storage binding points: the pipeline's
    /// i-th reflected storage-buffer binding (sorted ascending) is fed from slot i — the same
    /// global-slot contract BindTextureUVE uses for samplers. `buffer` must have been created
    /// with BufferUsageUVE::Storage; backends reject (loudly, no crash) any other usage, and a
    /// storage binding left unbound resolves to a deterministic all-zero buffer on Vulkan
    /// (GL's unbound-SSBO reads are undefined; the engine never relies on them). Storage-image
    /// bindings are deliberately NOT covered here — images land with the compute milestone.
    /// Must be called inside a render pass.
    virtual void BindStorageBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) = 0;

    /// Sets a scalar/vector/matrix uniform on the currently bound pipeline by name (Increment 21
    /// — a lighter-weight alternative to BindUniformBufferUVE()'s UBO path, for the common case
    /// of a handful of loose uniforms). Must be called inside a render pass, after
    /// BindPipelineUVE(). A `name` the bound pipeline doesn't declare (e.g. optimized out by the
    /// shader compiler) is a safe no-op, logged at a low severity — not every uniform a caller
    /// might set is guaranteed to still be active after linking.
    virtual void SetUniformFloatUVE(std::string_view name, float value) = 0;
    virtual void SetUniformIntUVE(std::string_view name, std::int32_t value) = 0;
    virtual void SetUniformBoolUVE(std::string_view name, bool value) = 0;
    virtual void SetUniformVector3UVE(std::string_view name, const Math::Vector3UVE& value) = 0;
    virtual void SetUniformMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE& value) = 0;

    /// Draws using the currently bound index buffer. Must be called inside a render pass, after
    /// a pipeline and the buffers it needs are bound.
    virtual void DrawIndexedUVE(std::uint32_t indexCount, std::uint32_t instanceCount = 1) = 0;

    /// Draws without an index buffer. Must be called inside a render pass, after a pipeline and
    /// the buffers it needs are bound.
    virtual void DrawUVE(std::uint32_t vertexCount, std::uint32_t instanceCount = 1) = 0;

    /// Dispatches compute work: `groupCountX * groupCountY * groupCountZ` workgroups of the
    /// currently bound COMPUTE pipeline (created via CreateComputePipelineUVE and bound with
    /// BindPipelineUVE; zero on any axis dispatches nothing and is a recorded no-op). M5a
    /// contract — dispatch belongs OUTSIDE render-pass markers (Vulkan compute is illegal
    /// inside a render-pass instance), after the pipeline and any SSBOs it writes are bound:
    /// the Vulkan dynamic arm closes a still-open pass instance before dispatching (the next
    /// Begin marker re-opens it with Load semantics), classic pre-1.3 devices cannot dispatch
    /// at all (their single native pass spans the frame) and warn-once + skip, GL executes
    /// glDispatchCompute immediately followed by a conservative memory barrier (GL has no
    /// native pass object), and Null records the command for spy tests. Writes a dispatch
    /// makes to bound storage buffers are coherent for later draws in the same submission —
    /// each backend inserts the barriers that guarantee it.
    virtual void DispatchUVE(std::uint32_t groupCountX, std::uint32_t groupCountY,
                             std::uint32_t groupCountZ) = 0;
};

} // namespace UVE::Render
