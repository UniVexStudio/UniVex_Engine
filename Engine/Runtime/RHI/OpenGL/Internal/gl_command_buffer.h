// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gl_render_device_state.h"
#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "i_command_buffer.h"

namespace UVE::Render {

/// GlCommandBuffer (engine/render/src/, module-private — never named outside engine/render/src/)
/// is the real, immediate-mode command buffer GlRenderDevice hands out: unlike
/// NullCommandBuffer's "record into a vector, replay never happens" spy pattern, every method
/// here issues real GL calls directly, since OpenGL has no separate record/replay submission step
/// the way Vulkan/D3D12 do — by the time GlRenderDevice::Submit() runs, the work already
/// executed during recording.
/// Thread-safety: not thread-safe, matching ICommandBuffer's documented single-recorder
/// contract.
class GlCommandBuffer final : public ICommandBuffer {
public:
    explicit GlCommandBuffer(Detail::GlDeviceState& state);

    [[nodiscard]] RenderFeatureSupport GetMemoryBarrierSupport() const noexcept override;
    [[nodiscard]] RenderOperationResult MemoryBarrier(MemoryBarrierMask mask) override;
    // Foundation WP3: recorded query brackets. GL issues immediately, so these are the same
    // glBeginQuery/glEndQuery/glQueryCounter calls the device-level entry points make, minus
    // the device path's glFinish — a finish inside a recorded stream would serialise every draw.
    [[nodiscard]] RenderOperationResult BeginQuery(QueryHandle query) override;
    [[nodiscard]] RenderOperationResult EndQuery(QueryHandle query) override;
    [[nodiscard]] RenderOperationResult WriteTimestamp(QueryHandle query) override;
    void BeginRenderPass(const RenderPassDesc& renderPassDesc) override;
    void EndRenderPass() override;
    void BindPipeline(PipelineHandle pipeline) override;
    void BindVertexBuffer(BufferHandle buffer, std::uint32_t slot) override;
    void BindIndexBuffer(BufferHandle buffer) override;
    void BindTexture(TextureHandle texture, std::uint32_t slot) override;
    void BindSampler(SamplerHandle sampler, std::uint32_t slot) override;
    void BindUniformBuffer(BufferHandle buffer, std::uint32_t slot) override;
    void BindStorageBuffer(BufferHandle buffer, std::uint32_t slot) override;
    void SetUniformFloat(std::string_view name, float value) override;
    void SetUniformInt(std::string_view name, std::int32_t value) override;
    void SetUniformBool(std::string_view name, bool value) override;
    void SetUniformVector2(std::string_view name, const Math::Vector2& value) override;
    void SetUniformVector3(std::string_view name, const Math::Vector3& value) override;
    void SetUniformVector4(std::string_view name, const Math::Vector4& value) override;
    void SetUniformMatrix4x4(std::string_view name, const Math::Matrix4x4& value) override;
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                        std::uint32_t firstIndex) override;
    void DrawIndexedIndirect(BufferHandle buffer, std::uint64_t offsetBytes) override;
    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount) override;
    void DrawRange(std::uint32_t vertexCount, std::uint32_t instanceCount,
                   std::uint32_t firstVertex) override;
    void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                     std::uint32_t groupCountZ) override;
    [[nodiscard]] bool RetainsUniformStatePerPipeline() const noexcept override { return true; }
    [[nodiscard]] std::uint64_t GetUniformStateEpoch() const noexcept override {
        return m_state != nullptr ? m_state->uniformStateDomainId : GetInstanceId();
    }

private:
    using UniformRecord = Detail::GlDeviceState::PipelineRecord::UniformRecord;

    /// Resolves the currently bound pipeline handle against the live device map. The command
    /// buffer intentionally stores no pointer into a pipeline record because the device may
    /// destroy a resource while recording is still in progress.
    [[nodiscard]] const Detail::GlDeviceState::PipelineRecord* FindCurrentPipeline() const;
    /// M5a: true when the currently bound pipeline is a compute program — relaxes the
    /// inside-pass gate for storage binds and SetUniform* (the compute flow is outside-pass).
    [[nodiscard]] bool ActivePipelineIsCompute() const noexcept;

    /// Looks `name` up in the currently bound pipeline's reflected uniform map (cached at link
    /// time - see GlRenderDevice::CreatePipeline()'s ReflectPipelineUniforms() call), so
    /// every SetUniform*UVE() call is a plain hash-map lookup, never a per-draw
    /// glGetUniformLocation call. Returns nullptr (logging at WARNING, not ERROR - an optimized-
    /// out or simply absent uniform is a normal occurrence) if no pipeline is bound or `name`
    /// isn't one of its active uniforms.
    [[nodiscard]] const UniformRecord* FindUniform(std::string_view name) const;

    Detail::GlDeviceState* m_state;
    bool m_insideRenderPass = false;
    // Pass epoch of the open recorded occlusion bracket (0 = none open through this buffer);
    // brackets may not span pass markers (the rule Vulkan enforces, kept identical here).
    std::uint64_t m_passEpoch = 0U;
    std::uint64_t m_activeQueryPassEpoch = 0U;
    std::uint32_t m_activeQuery = 0U;
    GLuint m_tempFramebuffer = 0; // Nonzero while a real-texture-backed pass is active; the FBO
                                   // name comes from GlDeviceState's attachment-pair cache and
                                   // is released only when a dependent texture or the device is
                                   // destroyed.
    // Tier 2.5: GL_COLOR_ATTACHMENTi/GL_DEPTH_ATTACHMENT enums of the open FBO pass's DontCare
    // attachments (built at Begin, consumed at End; empty for default-framebuffer passes — the
    // window system owns that storage). A null invalidate entry only skips the hint.
    std::array<GLenum, 1U + kExtraColorAttachmentCount + 2U> m_passDiscardAttachments{};
    std::uint32_t m_passDiscardCount = 0U;
    GLuint m_currentProgram = 0;
    GLuint m_currentVao = 0;
    PipelineHandle m_currentPipeline = kInvalidPipelineHandle;
    BufferHandle m_boundVertexBuffer = kInvalidBufferHandle;
    BufferHandle m_boundIndexBuffer = kInvalidBufferHandle;
    std::uint32_t m_currentVertexStride = 0;
    std::unordered_map<std::uint32_t, TextureHandle> m_boundTextures;
    std::unordered_map<std::uint32_t, SamplerHandle> m_boundSamplers;
};

} // namespace UVE::Render
