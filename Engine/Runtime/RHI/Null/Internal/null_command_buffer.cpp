// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_command_buffer.h"

#include <string>

#include "engine_assert.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool RequireInsideRenderPass(bool insideRenderPass, std::string_view operation) noexcept {
    UVE_ASSERT(insideRenderPass);
    if (!insideRenderPass) {
        UVE_ERROR("NullCommandBuffer: {} must be called inside a render pass", operation);
        return false;
    }
    return true;
}

[[nodiscard]] bool RequireOutsideRenderPass(bool insideRenderPass) noexcept {
    UVE_ASSERT(!insideRenderPass);
    if (insideRenderPass) {
        UVE_ERROR("NullCommandBuffer: BeginRenderPass does not support nested render passes");
        return false;
    }
    return true;
}

} // namespace

void NullCommandBuffer::BeginRenderPass(const RenderPassDesc& renderPassDesc) {
    if (!RequireOutsideRenderPass(m_insideRenderPass)) {
        return;
    }
    if (!IsLoadOpValid(renderPassDesc.colorLoadOp) || !IsLoadOpValid(renderPassDesc.depthLoadOp)) {
        UVE_ERROR("NullCommandBuffer: BeginRenderPass received an unknown load operation");
        return;
    }
    // Tier 2.5: store ops validate like load ops (Null records opaquely, but a garbage enum is
    // still a malformed pass on every backend).
    if (!IsStoreOpValid(renderPassDesc.colorStoreOp) || !IsStoreOpValid(renderPassDesc.depthStoreOp)) {
        UVE_ERROR("NullCommandBuffer: BeginRenderPass received an unknown store operation");
        return;
    }
    // Tier 2.4: the extras prefix rule is structural (no handle lookups — Null never resolves
    // handles), so it enforces here: contiguous prefix, valid per-slot ops.
    {
        bool gapSeen = false;
        for (const ColorAttachment& slot : renderPassDesc.extraColorAttachments) {
            if (slot.target == kInvalidTextureHandle) {
                gapSeen = true;
                continue;
            }
            if (gapSeen) {
                UVE_ERROR("NullCommandBuffer: extra color attachments must be a contiguous "
                          "prefix (a gap is malformed)");
                return;
            }
            if (!IsLoadOpValid(slot.loadOp) || !IsStoreOpValid(slot.storeOp)) {
                UVE_ERROR("NullCommandBuffer: BeginRenderPass received an unknown extra "
                          "attachment load or store operation");
                return;
            }
        }
    }
    m_insideRenderPass = true;
    ++m_passEpoch;
    m_commands.emplace_back(BeginRenderPassCommand{renderPassDesc});
}

void NullCommandBuffer::EndRenderPass() {
    if (!RequireInsideRenderPass(m_insideRenderPass, "EndRenderPass")) {
        return;
    }
    if (m_activeQuery != 0U) {
        // A bracket may not span the pass boundary: close it here (loudly) so the recorded
        // sequence stays valid, and let the caller's late EndQuery report InvalidState.
        UVE_ERROR("NullCommandBuffer: EndRenderPass closed an occlusion query that was still open");
        m_commands.emplace_back(EndQueryCommand{QueryHandle{m_activeQuery}});
        m_activeQuery = 0U;
    }
    m_insideRenderPass = false;
    ++m_passEpoch;
    m_commands.emplace_back(EndRenderPassCommand{});
}

void NullCommandBuffer::BindPipeline(PipelineHandle pipeline) {
    // M5a: no inside-pass gate anymore — compute pipelines MUST bind outside render-pass markers
    // (Vulkan forbids binding a COMPUTE pipeline inside a pass instance), and Null only records.
    // The structural gates live on the consuming commands: Draw (inside) and Dispatch
    // (outside). Kept identical to GlCommandBuffer so one portable stream records everywhere.
    m_commands.emplace_back(BindPipelineCommand{pipeline});
}

void NullCommandBuffer::BindVertexBuffer(BufferHandle buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindVertexBuffer")) {
        return;
    }
    m_commands.emplace_back(BindVertexBufferCommand{buffer, slot});
}

void NullCommandBuffer::BindIndexBuffer(BufferHandle buffer) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindIndexBuffer")) {
        return;
    }
    m_commands.emplace_back(BindIndexBufferCommand{buffer});
}

void NullCommandBuffer::BindTexture(TextureHandle texture, std::uint32_t slot) {
    // M5b: texture binds no longer gate on pass state. Since M5b, BindTexture feeds
    // STORAGE_IMAGE descriptors as well (unified texture slot space), and the compute flow
    // lives entirely OUTSIDE pass markers. NullCommandBuffer holds no device back-reference
    // to tell a compute handle from a graphics one — so Null records ungated exactly like
    // the Vulkan record side does (and like BindStorageBuffer does), and the executing
    // backends keep the real kind-aware rules.
    m_commands.emplace_back(BindTextureCommand{texture, slot});
}

void NullCommandBuffer::BindSampler(SamplerHandle sampler, std::uint32_t slot) {
    // Ungated exactly like BindTexture above: sampler binds pair with texture binds, and
    // the executing backends keep the real kind-aware rules.
    m_commands.emplace_back(BindSamplerCommand{sampler, slot});
}

void NullCommandBuffer::BindUniformBuffer(BufferHandle buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindUniformBuffer")) {
        return;
    }
    m_commands.emplace_back(BindUniformBufferCommand{buffer, slot});
}

void NullCommandBuffer::BindStorageBuffer(BufferHandle buffer, std::uint32_t slot) {
    // M5a: storage binds and the SetUniform* family below no longer gate on pass state. The
    // compute flow (bind compute pipeline, bind its SSBOs/uniforms, dispatch) lives entirely
    // OUTSIDE pass markers, and NullCommandBuffer holds no device back-reference to tell a
    // compute handle from a graphics one — so Null records ungated exactly like the Vulkan
    // record side does, and the executing backends keep the real kind-aware rules.
    m_commands.emplace_back(BindStorageBufferCommand{buffer, slot});
}

void NullCommandBuffer::SetUniformFloat(std::string_view name, float value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformFloatCommand{std::string(name), value});
}

void NullCommandBuffer::SetUniformInt(std::string_view name, std::int32_t value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformIntCommand{std::string(name), value});
}

void NullCommandBuffer::SetUniformBool(std::string_view name, bool value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformBoolCommand{std::string(name), value});
}

void NullCommandBuffer::SetUniformVector2(std::string_view name, const Math::Vector2& value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformVector2Command{std::string(name), value});
}

void NullCommandBuffer::SetUniformVector3(std::string_view name, const Math::Vector3& value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformVector3Command{std::string(name), value});
}

void NullCommandBuffer::SetUniformVector4(std::string_view name, const Math::Vector4& value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformVector4Command{std::string(name), value});
}

void NullCommandBuffer::SetUniformMatrix4x4(std::string_view name, const Math::Matrix4x4& value) {
    // M5a: ungated — see the comment at BindStorageBuffer.
    m_commands.emplace_back(SetUniformMatrix4x4Command{std::string(name), value});
}

void NullCommandBuffer::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                          std::uint32_t firstIndex) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "DrawIndexed")) {
        return;
    }
    m_commands.emplace_back(DrawIndexedCommand{indexCount, instanceCount, firstIndex});
}

void NullCommandBuffer::DrawIndexedIndirect(const BufferHandle buffer,
                                                  const std::uint64_t offsetBytes) {
    // Same inside-pass gate as DrawIndexed: the indirection changes where the PARAMETERS come
    // from, not when a draw is legal. Handle validity is the device's to judge at replay - this
    // backend records faithfully and executes nothing.
    if (!RequireInsideRenderPass(m_insideRenderPass, "DrawIndexedIndirect")) {
        return;
    }
    m_commands.emplace_back(DrawIndexedIndirectCommandRecord{buffer, offsetBytes});
}

void NullCommandBuffer::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount) {
    DrawRange(vertexCount, instanceCount, 0U);
}

void NullCommandBuffer::DrawRange(std::uint32_t vertexCount, std::uint32_t instanceCount,
                                  std::uint32_t firstVertex) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "Draw")) {
        return;
    }
    m_commands.emplace_back(DrawCommand{vertexCount, instanceCount, firstVertex});
}

void NullCommandBuffer::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                       std::uint32_t groupCountZ) {
    // M5a: dispatch belongs OUTSIDE render-pass markers — the mirror image of Draw's
    // inside-pass gate (Vulkan compute is illegal inside a render-pass instance).
    UVE_ASSERT(!m_insideRenderPass);
    if (m_insideRenderPass) {
        UVE_ERROR("NullCommandBuffer: Dispatch must be called outside a render pass");
        return;
    }
    m_commands.emplace_back(DispatchCommand{groupCountX, groupCountY, groupCountZ});
}

const std::vector<RecordedCommand>& NullCommandBuffer::GetRecordedCommands() const noexcept {
    return m_commands;
}

} // namespace UVE::Render

namespace UVE::Render {
RenderOperationResult NullCommandBuffer::MemoryBarrier(MemoryBarrierMask mask) {
    if (!IsMemoryBarrierMaskValid(mask))
        return RenderOperationResult::InvalidArgument;
    if (m_insideRenderPass)
        return RenderOperationResult::InvalidState;
    m_commands.emplace_back(MemoryBarrierCommand{mask});
    return RenderOperationResult::Recorded;
}

std::optional<RenderQueryKind> NullCommandBuffer::LookupQueryKind(const QueryHandle query) const {
    if (query.value == 0U)
        return std::nullopt;
    if (!m_queryKindLookup)
        return std::nullopt;
    return m_queryKindLookup(query);
}

RenderOperationResult NullCommandBuffer::BeginQuery(const QueryHandle query) {
    if (query.value == 0U)
        return RenderOperationResult::InvalidHandle;
    if (m_queryKindLookup) {
        const auto kind = LookupQueryKind(query);
        if (!kind.has_value())
            return RenderOperationResult::InvalidHandle;
        if (*kind != RenderQueryKind::AnySamplesPassed)
            return RenderOperationResult::InvalidArgument;
    }
    if (m_activeQuery != 0U)
        return RenderOperationResult::InvalidState;
    m_activeQuery = query.value;
    m_activeQueryPassEpoch = m_passEpoch;
    m_commands.emplace_back(BeginQueryCommand{query});
    return RenderOperationResult::Recorded;
}

RenderOperationResult NullCommandBuffer::EndQuery(const QueryHandle query) {
    if (query.value == 0U)
        return RenderOperationResult::InvalidHandle;
    if (m_activeQuery != query.value)
        return RenderOperationResult::InvalidState;
    if (m_activeQueryPassEpoch != m_passEpoch)
        return RenderOperationResult::InvalidState;
    m_activeQuery = 0U;
    m_commands.emplace_back(EndQueryCommand{query});
    return RenderOperationResult::Recorded;
}

RenderOperationResult NullCommandBuffer::WriteTimestamp(const QueryHandle query) {
    if (query.value == 0U)
        return RenderOperationResult::InvalidHandle;
    if (m_queryKindLookup) {
        const auto kind = LookupQueryKind(query);
        if (!kind.has_value())
            return RenderOperationResult::InvalidHandle;
        if (*kind != RenderQueryKind::Timestamp)
            return RenderOperationResult::InvalidArgument;
    }
    m_commands.emplace_back(WriteTimestampCommand{query});
    return RenderOperationResult::Recorded;
}
} // namespace UVE::Render
