// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/debug_renderer_uve.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi_shader/built_in_shaders_uve.h"
#include "uve/rhi_shader/shader_program_desc_uve.h"

namespace UVE::Render {
namespace {

static_assert(sizeof(DebugLineVertexUVE) == 24U, "position + color, tightly packed for one upload");
static_assert(offsetof(DebugLineVertexUVE, color) == 12U);

[[nodiscard]] bool IsValidLineUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                  const Math::Vector3UVE& color) noexcept {
    return Math::IsFiniteUVE(from) && Math::IsFiniteUVE(to) && Math::IsFiniteUVE(color);
}

} // namespace

DebugRendererUVE::~DebugRendererUVE() {
    // The program is RAII (its deleter destroys the pipeline on the device), but the vertex
    // buffer is a raw handle: a renderer that submitted and never shut down leaks it, which is
    // a programming error worth aborting a debug build over rather than hiding.
    UVE_ASSERT(m_vertexBuffer == kInvalidBufferHandleUVE);
}

void DebugRendererUVE::DrawLine3D(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                  const Math::Vector3UVE& color) {
    if (!IsValidLineUVE(from, to, color)) {
        UVE_WARNING("DebugRendererUVE: ignoring a line with a non-finite endpoint or color");
        return;
    }
    PushLineUVE(from, to, color);
}

void DebugRendererUVE::DrawBox3D(const Math::AabbUVE& bounds, const Math::Vector3UVE& color) {
    if (!Math::IsFiniteUVE(bounds.min) || !Math::IsFiniteUVE(bounds.max) ||
        !Math::IsFiniteUVE(color)) {
        UVE_WARNING("DebugRendererUVE: ignoring a box with non-finite bounds or color");
        return;
    }
    const float x0 = bounds.min.x;
    const float y0 = bounds.min.y;
    const float z0 = bounds.min.z;
    const float x1 = bounds.max.x;
    const float y1 = bounds.max.y;
    const float z1 = bounds.max.z;
    PushLineUVE(Math::Vector3UVE{x0, y0, z0}, Math::Vector3UVE{x1, y0, z0}, color);
    PushLineUVE(Math::Vector3UVE{x1, y0, z0}, Math::Vector3UVE{x1, y0, z1}, color);
    PushLineUVE(Math::Vector3UVE{x1, y0, z1}, Math::Vector3UVE{x0, y0, z1}, color);
    PushLineUVE(Math::Vector3UVE{x0, y0, z1}, Math::Vector3UVE{x0, y0, z0}, color);
    PushLineUVE(Math::Vector3UVE{x0, y1, z0}, Math::Vector3UVE{x1, y1, z0}, color);
    PushLineUVE(Math::Vector3UVE{x1, y1, z0}, Math::Vector3UVE{x1, y1, z1}, color);
    PushLineUVE(Math::Vector3UVE{x1, y1, z1}, Math::Vector3UVE{x0, y1, z1}, color);
    PushLineUVE(Math::Vector3UVE{x0, y1, z1}, Math::Vector3UVE{x0, y1, z0}, color);
    PushLineUVE(Math::Vector3UVE{x0, y0, z0}, Math::Vector3UVE{x0, y1, z0}, color);
    PushLineUVE(Math::Vector3UVE{x1, y0, z0}, Math::Vector3UVE{x1, y1, z0}, color);
    PushLineUVE(Math::Vector3UVE{x1, y0, z1}, Math::Vector3UVE{x1, y1, z1}, color);
    PushLineUVE(Math::Vector3UVE{x0, y0, z1}, Math::Vector3UVE{x0, y1, z1}, color);
}

void DebugRendererUVE::DrawSphere3D(const Math::Vector3UVE& center, const float radius,
                                    const Math::Vector3UVE& color) {
    if (!Math::IsFiniteUVE(center) || !std::isfinite(radius) || radius <= 0.0F ||
        !Math::IsFiniteUVE(color)) {
        UVE_WARNING("DebugRendererUVE: ignoring a sphere with a bad center, radius, or color");
        return;
    }
    std::array<float, kDebugSphereSegmentsUVE + 1U> cosTable{};
    std::array<float, kDebugSphereSegmentsUVE + 1U> sinTable{};
    for (std::size_t s = 0U; s <= kDebugSphereSegmentsUVE; ++s) {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(s) /
                            static_cast<float>(kDebugSphereSegmentsUVE);
        cosTable[s] = std::cos(angle);
        sinTable[s] = std::sin(angle);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegmentsUVE; ++s) {
        const Math::Vector3UVE xy0{radius * cosTable[s], radius * sinTable[s], 0.0F};
        const Math::Vector3UVE xy1{radius * cosTable[s + 1U], radius * sinTable[s + 1U], 0.0F};
        PushLineUVE(center + xy0, center + xy1, color);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegmentsUVE; ++s) {
        const Math::Vector3UVE xz0{radius * cosTable[s], 0.0F, radius * sinTable[s]};
        const Math::Vector3UVE xz1{radius * cosTable[s + 1U], 0.0F, radius * sinTable[s + 1U]};
        PushLineUVE(center + xz0, center + xz1, color);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegmentsUVE; ++s) {
        const Math::Vector3UVE yz0{0.0F, radius * cosTable[s], radius * sinTable[s]};
        const Math::Vector3UVE yz1{0.0F, radius * cosTable[s + 1U], radius * sinTable[s + 1U]};
        PushLineUVE(center + yz0, center + yz1, color);
    }
}

void DebugRendererUVE::DrawRect2D(const Math::Vector2UVE& min, const Math::Vector2UVE& max,
                                  const Math::Vector3UVE& color) {
    if (!Math::IsFiniteUVE(min) || !Math::IsFiniteUVE(max) || !Math::IsFiniteUVE(color)) {
        UVE_WARNING("DebugRendererUVE: ignoring a rect with non-finite corners or color");
        return;
    }
    PushLineUVE(Math::Vector3UVE{min.x, min.y, 0.0F}, Math::Vector3UVE{max.x, min.y, 0.0F}, color);
    PushLineUVE(Math::Vector3UVE{max.x, min.y, 0.0F}, Math::Vector3UVE{max.x, max.y, 0.0F}, color);
    PushLineUVE(Math::Vector3UVE{max.x, max.y, 0.0F}, Math::Vector3UVE{min.x, max.y, 0.0F}, color);
    PushLineUVE(Math::Vector3UVE{min.x, max.y, 0.0F}, Math::Vector3UVE{min.x, min.y, 0.0F}, color);
}

std::span<const DebugLineVertexUVE> DebugRendererUVE::LinesUVE() const noexcept {
    return m_lines;
}

std::size_t DebugRendererUVE::DroppedLineCountUVE() const noexcept {
    return m_droppedLines;
}

void DebugRendererUVE::ClearUVE() noexcept {
    m_lines.clear();
    m_droppedLines = 0U;
}

bool DebugRendererUVE::SubmitUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                 ICommandBufferUVE& commands,
                                 const Math::Matrix4x4UVE& viewProjection) {
    if (m_lines.empty()) {
        return true;
    }
    if (m_program == nullptr) {
        Shader::ShaderProgramDescUVE desc;
        desc.virtualFilePath = std::string(Shader::BuiltIn::kDebugLineVirtualPath);
        desc.embeddedFallbackSourceCode = std::string(Shader::BuiltIn::kDebugLineSource);
        desc.vertexLayout = {
            VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3,
                               offsetof(DebugLineVertexUVE, position)},
            VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float3,
                               offsetof(DebugLineVertexUVE, color)},
        };
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(DebugLineVertexUVE));
        desc.topology = PrimitiveTopologyUVE::Lines;
        desc.depthTestEnabled = true;
        // Paint, not geometry (the decal rule): debug lines hide behind the scene but never
        // occlude each other, so coplanar lines don't z-fight.
        desc.depthWriteEnabled = false;
        desc.debugNameUVE = "DebugLine";
        m_program = shaders.CreateProgramUVE(desc);
        if (m_program == nullptr) {
            return false;
        }
    }
    if (!m_program->IsValidUVE()) {
        // Async compile still in flight: keep the batch and let the next submit retry.
        return true;
    }
    if (m_vertexBuffer == kInvalidBufferHandleUVE) {
        m_vertexBuffer = device.CreateBufferUVE(
            BufferDescUVE{sizeof(DebugLineVertexUVE) * kMaximumDebugLineVerticesUVE,
                          BufferUsageUVE::Vertex});
        if (m_vertexBuffer == kInvalidBufferHandleUVE) {
            return false;
        }
    }
    if (!device.UpdateBufferUVE(m_vertexBuffer, std::as_bytes(std::span{m_lines}), 0U)) {
        return false;
    }
    m_program->SetMatrix4x4UVE("uViewProjection", viewProjection);
    // Binds the Lines pipeline and flushes the queued uniforms; then the batch draws as one.
    m_program->ApplyToUVE(commands);
    commands.BindVertexBufferUVE(m_vertexBuffer);
    commands.DrawUVE(static_cast<std::uint32_t>(m_lines.size()));
    ClearUVE();
    return true;
}

void DebugRendererUVE::ShutdownUVE(IRenderDeviceUVE& device) {
    if (m_vertexBuffer != kInvalidBufferHandleUVE) {
        device.DestroyBufferUVE(m_vertexBuffer);
        m_vertexBuffer = kInvalidBufferHandleUVE;
    }
    // Releasing the program destroys its pipeline on the device through the manager's deleter.
    m_program.reset();
    ClearUVE();
}

void DebugRendererUVE::PushLineUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                   const Math::Vector3UVE& color) {
    if (m_lines.size() + 2U > kMaximumDebugLineVerticesUVE) {
        ++m_droppedLines;
        return;
    }
    m_lines.push_back(DebugLineVertexUVE{from, color});
    m_lines.push_back(DebugLineVertexUVE{to, color});
}

} // namespace UVE::Render
