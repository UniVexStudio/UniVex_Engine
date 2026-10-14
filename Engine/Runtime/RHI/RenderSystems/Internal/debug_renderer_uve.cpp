// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/debug_renderer_uve.h"

#include <algorithm>
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
    UVE_ASSERT(m_lineResources.vertexBuffer == kInvalidBufferHandleUVE);
    UVE_ASSERT(m_pointResources.vertexBuffer == kInvalidBufferHandleUVE);
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

void DebugRendererUVE::DrawPoint3D(const Math::Vector3UVE& position, const Math::Vector3UVE& color) {
    if (!Math::IsFiniteUVE(position) || !Math::IsFiniteUVE(color)) {
        UVE_WARNING("DebugRendererUVE: ignoring a point with a non-finite position or color");
        return;
    }
    if (m_points.size() + 1U > kMaximumDebugPointVerticesUVE) {
        ++m_droppedPoints;
        return;
    }
    m_points.push_back(DebugLineVertexUVE{position, color});
}

void DebugRendererUVE::SetPointSizeUVE(const float pixels) {
    if (!std::isfinite(pixels)) {
        UVE_WARNING("DebugRendererUVE: ignoring a non-finite point size");
        return;
    }
    m_pointSizePixels = std::clamp(pixels, kMinimumDebugPointSizeUVE, kMaximumDebugPointSizeUVE);
}

float DebugRendererUVE::PointSizeUVE() const noexcept {
    return m_pointSizePixels;
}

std::span<const DebugLineVertexUVE> DebugRendererUVE::LinesUVE() const noexcept {
    return m_lines;
}

std::span<const DebugLineVertexUVE> DebugRendererUVE::PointsUVE() const noexcept {
    return m_points;
}

std::size_t DebugRendererUVE::DroppedLineCountUVE() const noexcept {
    return m_droppedLines;
}

std::size_t DebugRendererUVE::DroppedPointCountUVE() const noexcept {
    return m_droppedPoints;
}

void DebugRendererUVE::ClearUVE() noexcept {
    m_lines.clear();
    m_points.clear();
    m_droppedLines = 0U;
    m_droppedPoints = 0U;
}

bool DebugRendererUVE::SubmitUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                 ICommandBufferUVE& commands,
                                 const Math::Matrix4x4UVE& viewProjection) {
    if (m_lines.empty() && m_points.empty()) {
        return true;
    }
    bool deferred = false;
    if (!m_lines.empty() &&
        !SubmitBatchUVE(device, shaders, commands, viewProjection, m_lineResources, m_lines,
                        PrimitiveTopologyUVE::Lines, deferred)) {
        return false;
    }
    if (!m_points.empty() &&
        !SubmitBatchUVE(device, shaders, commands, viewProjection, m_pointResources, m_points,
                        PrimitiveTopologyUVE::Points, deferred)) {
        return false;
    }
    if (deferred) {
        // At least one program's async compile is still in flight: keep both batches so the
        // next submit draws the frame whole rather than lines now and points later.
        return true;
    }
    ClearUVE();
    return true;
}

bool DebugRendererUVE::SubmitBatchUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                      ICommandBufferUVE& commands, const Math::Matrix4x4UVE& viewProjection,
                                      BatchResourcesUVE& resources,
                                      const std::span<const DebugLineVertexUVE> vertices,
                                      const PrimitiveTopologyUVE topology, bool& deferred) {
    const bool points = topology == PrimitiveTopologyUVE::Points;
    if (resources.program == nullptr) {
        Shader::ShaderProgramDescUVE desc;
        desc.virtualFilePath = std::string(points ? Shader::BuiltIn::kDebugPointVirtualPath
                                                  : Shader::BuiltIn::kDebugLineVirtualPath);
        desc.embeddedFallbackSourceCode = std::string(points ? Shader::BuiltIn::kDebugPointSource
                                                             : Shader::BuiltIn::kDebugLineSource);
        desc.vertexLayout = {
            VertexAttributeUVE{"POSITION", VertexAttributeFormatUVE::Float3,
                               offsetof(DebugLineVertexUVE, position)},
            VertexAttributeUVE{"COLOR", VertexAttributeFormatUVE::Float3,
                               offsetof(DebugLineVertexUVE, color)},
        };
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(DebugLineVertexUVE));
        desc.topology = topology;
        desc.depthTestEnabled = true;
        // Paint, not geometry (the decal rule): debug lines hide behind the scene but never
        // occlude each other, so coplanar lines don't z-fight.
        desc.depthWriteEnabled = false;
        desc.debugNameUVE = points ? "DebugPoint" : "DebugLine";
        resources.program = shaders.CreateProgramUVE(desc);
        if (resources.program == nullptr) {
            return false;
        }
    }
    if (!resources.program->IsValidUVE()) {
        // Async compile still in flight: keep the batch and let the next submit retry.
        deferred = true;
        return true;
    }
    if (resources.vertexBuffer == kInvalidBufferHandleUVE) {
        // Rewritten from the CPU every submit: host-visible, streamed placement (on Vulkan a
        // memcpy into mapped memory instead of a staging submit per frame; GL_STREAM_DRAW on GL).
        const std::size_t capacity = points ? kMaximumDebugPointVerticesUVE : kMaximumDebugLineVerticesUVE;
        resources.vertexBuffer = device.CreateBufferUVE(
            BufferDescUVE{sizeof(DebugLineVertexUVE) * capacity, BufferUsageUVE::Vertex,
                          BufferMemoryUVE::Upload, BufferUpdateFrequencyUVE::Stream});
        if (resources.vertexBuffer == kInvalidBufferHandleUVE) {
            return false;
        }
    }
    if (!device.UpdateBufferUVE(resources.vertexBuffer, std::as_bytes(vertices), 0U)) {
        return false;
    }
    resources.program->SetMatrix4x4UVE("uViewProjection", viewProjection);
    if (points) {
        resources.program->SetFloatUVE("uPointSize", m_pointSizePixels);
    }
    // Binds the pipeline and flushes the queued uniforms; then the batch draws as one.
    resources.program->ApplyToUVE(commands);
    commands.BindVertexBufferUVE(resources.vertexBuffer);
    commands.DrawUVE(static_cast<std::uint32_t>(vertices.size()));
    return true;
}

void DebugRendererUVE::ShutdownUVE(IRenderDeviceUVE& device) {
    for (BatchResourcesUVE* const resources : {&m_lineResources, &m_pointResources}) {
        if (resources->vertexBuffer != kInvalidBufferHandleUVE) {
            device.DestroyBufferUVE(resources->vertexBuffer);
            resources->vertexBuffer = kInvalidBufferHandleUVE;
        }
        // Releasing the program destroys its pipeline on the device through the manager's deleter.
        resources->program.reset();
    }
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
