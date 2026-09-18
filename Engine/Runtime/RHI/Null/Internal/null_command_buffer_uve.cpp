// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_command_buffer_uve.h"

#include <string>

#include "uve/logging/assert_uve.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool RequireInsideRenderPassUVE(bool insideRenderPass, std::string_view operation) noexcept {
    UVE_ASSERT(insideRenderPass);
    if (!insideRenderPass) {
        UVE_ERROR("NullCommandBufferUVE: {} must be called inside a render pass", operation);
        return false;
    }
    return true;
}

[[nodiscard]] bool RequireOutsideRenderPassUVE(bool insideRenderPass) noexcept {
    UVE_ASSERT(!insideRenderPass);
    if (insideRenderPass) {
        UVE_ERROR("NullCommandBufferUVE: BeginRenderPassUVE does not support nested render passes");
        return false;
    }
    return true;
}

} // namespace

void NullCommandBufferUVE::BeginRenderPassUVE(const RenderPassDescUVE& renderPassDesc) {
    if (!RequireOutsideRenderPassUVE(m_insideRenderPass)) {
        return;
    }
    if (!IsLoadOpValidUVE(renderPassDesc.colorLoadOp) || !IsLoadOpValidUVE(renderPassDesc.depthLoadOp)) {
        UVE_ERROR("NullCommandBufferUVE: BeginRenderPassUVE received an unknown load operation");
        return;
    }
    m_insideRenderPass = true;
    m_commands.emplace_back(BeginRenderPassCommandUVE{renderPassDesc});
}

void NullCommandBufferUVE::EndRenderPassUVE() {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "EndRenderPassUVE")) {
        return;
    }
    m_insideRenderPass = false;
    m_commands.emplace_back(EndRenderPassCommandUVE{});
}

void NullCommandBufferUVE::BindPipelineUVE(PipelineHandleUVE pipeline) {
    // M5a: no inside-pass gate anymore — compute pipelines MUST bind outside render-pass markers
    // (Vulkan forbids binding a COMPUTE pipeline inside a pass instance), and Null only records.
    // The structural gates live on the consuming commands: DrawUVE (inside) and DispatchUVE
    // (outside). Kept identical to GlCommandBufferUVE so one portable stream records everywhere.
    m_commands.emplace_back(BindPipelineCommandUVE{pipeline});
}

void NullCommandBufferUVE::BindVertexBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindVertexBufferUVE")) {
        return;
    }
    m_commands.emplace_back(BindVertexBufferCommandUVE{buffer, slot});
}

void NullCommandBufferUVE::BindIndexBufferUVE(BufferHandleUVE buffer) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindIndexBufferUVE")) {
        return;
    }
    m_commands.emplace_back(BindIndexBufferCommandUVE{buffer});
}

void NullCommandBufferUVE::BindTextureUVE(TextureHandleUVE texture, std::uint32_t slot) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindTextureUVE")) {
        return;
    }
    m_commands.emplace_back(BindTextureCommandUVE{texture, slot});
}

void NullCommandBufferUVE::BindUniformBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindUniformBufferUVE")) {
        return;
    }
    m_commands.emplace_back(BindUniformBufferCommandUVE{buffer, slot});
}

void NullCommandBufferUVE::BindStorageBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    // M5a: storage binds and the SetUniform* family below no longer gate on pass state. The
    // compute flow (bind compute pipeline, bind its SSBOs/uniforms, dispatch) lives entirely
    // OUTSIDE pass markers, and NullCommandBufferUVE holds no device back-reference to tell a
    // compute handle from a graphics one — so Null records ungated exactly like the Vulkan
    // record side does, and the executing backends keep the real kind-aware rules.
    m_commands.emplace_back(BindStorageBufferCommandUVE{buffer, slot});
}

void NullCommandBufferUVE::SetUniformFloatUVE(std::string_view name, float value) {
    // M5a: ungated — see the comment at BindStorageBufferUVE.
    m_commands.emplace_back(SetUniformFloatCommandUVE{std::string(name), value});
}

void NullCommandBufferUVE::SetUniformIntUVE(std::string_view name, std::int32_t value) {
    // M5a: ungated — see the comment at BindStorageBufferUVE.
    m_commands.emplace_back(SetUniformIntCommandUVE{std::string(name), value});
}

void NullCommandBufferUVE::SetUniformBoolUVE(std::string_view name, bool value) {
    // M5a: ungated — see the comment at BindStorageBufferUVE.
    m_commands.emplace_back(SetUniformBoolCommandUVE{std::string(name), value});
}

void NullCommandBufferUVE::SetUniformVector3UVE(std::string_view name, const Math::Vector3UVE& value) {
    // M5a: ungated — see the comment at BindStorageBufferUVE.
    m_commands.emplace_back(SetUniformVector3CommandUVE{std::string(name), value});
}

void NullCommandBufferUVE::SetUniformMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE& value) {
    // M5a: ungated — see the comment at BindStorageBufferUVE.
    m_commands.emplace_back(SetUniformMatrix4x4CommandUVE{std::string(name), value});
}

void NullCommandBufferUVE::DrawIndexedUVE(std::uint32_t indexCount, std::uint32_t instanceCount) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "DrawIndexedUVE")) {
        return;
    }
    m_commands.emplace_back(DrawIndexedCommandUVE{indexCount, instanceCount});
}

void NullCommandBufferUVE::DrawUVE(std::uint32_t vertexCount, std::uint32_t instanceCount) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "DrawUVE")) {
        return;
    }
    m_commands.emplace_back(DrawCommandUVE{vertexCount, instanceCount});
}

void NullCommandBufferUVE::DispatchUVE(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                       std::uint32_t groupCountZ) {
    // M5a: dispatch belongs OUTSIDE render-pass markers — the mirror image of DrawUVE's
    // inside-pass gate (Vulkan compute is illegal inside a render-pass instance).
    UVE_ASSERT(!m_insideRenderPass);
    if (m_insideRenderPass) {
        UVE_ERROR("NullCommandBufferUVE: DispatchUVE must be called outside a render pass");
        return;
    }
    m_commands.emplace_back(DispatchCommandUVE{groupCountX, groupCountY, groupCountZ});
}

const std::vector<RecordedCommandUVE>& NullCommandBufferUVE::GetRecordedCommandsUVE() const noexcept {
    return m_commands;
}

} // namespace UVE::Render
