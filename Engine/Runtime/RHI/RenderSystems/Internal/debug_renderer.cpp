// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "debug_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "engine_assert.h"
#include "logging_macros.h"
#include "built_in_shaders.h"
#include "shader_program_desc.h"

namespace UVE::Render {
namespace {

static_assert(sizeof(DebugLineVertex) == 24U, "position + color, tightly packed for one upload");
static_assert(offsetof(DebugLineVertex, color) == 12U);

[[nodiscard]] bool IsValidLine(const Math::Vector3& from, const Math::Vector3& to,
                                  const Math::Vector3& color) noexcept {
    return Math::IsFinite(from) && Math::IsFinite(to) && Math::IsFinite(color);
}

} // namespace

DebugRenderer::~DebugRenderer() {
    // The program is RAII (its deleter destroys the pipeline on the device), but the vertex
    // buffer is a raw handle: a renderer that submitted and never shut down leaks it, which is
    // a programming error worth aborting a debug build over rather than hiding.
    UVE_ASSERT(m_lineResources.vertexBuffer == kInvalidBufferHandle);
    UVE_ASSERT(m_pointResources.vertexBuffer == kInvalidBufferHandle);
}

void DebugRenderer::DrawLine3D(const Math::Vector3& from, const Math::Vector3& to,
                                  const Math::Vector3& color) {
    if (!IsValidLine(from, to, color)) {
        UVE_WARNING("DebugRenderer: ignoring a line with a non-finite endpoint or color");
        return;
    }
    PushLine(from, to, color);
}

void DebugRenderer::DrawBox3D(const Math::Aabb& bounds, const Math::Vector3& color) {
    if (!Math::IsFinite(bounds.min) || !Math::IsFinite(bounds.max) ||
        !Math::IsFinite(color)) {
        UVE_WARNING("DebugRenderer: ignoring a box with non-finite bounds or color");
        return;
    }
    const float x0 = bounds.min.x;
    const float y0 = bounds.min.y;
    const float z0 = bounds.min.z;
    const float x1 = bounds.max.x;
    const float y1 = bounds.max.y;
    const float z1 = bounds.max.z;
    PushLine(Math::Vector3{x0, y0, z0}, Math::Vector3{x1, y0, z0}, color);
    PushLine(Math::Vector3{x1, y0, z0}, Math::Vector3{x1, y0, z1}, color);
    PushLine(Math::Vector3{x1, y0, z1}, Math::Vector3{x0, y0, z1}, color);
    PushLine(Math::Vector3{x0, y0, z1}, Math::Vector3{x0, y0, z0}, color);
    PushLine(Math::Vector3{x0, y1, z0}, Math::Vector3{x1, y1, z0}, color);
    PushLine(Math::Vector3{x1, y1, z0}, Math::Vector3{x1, y1, z1}, color);
    PushLine(Math::Vector3{x1, y1, z1}, Math::Vector3{x0, y1, z1}, color);
    PushLine(Math::Vector3{x0, y1, z1}, Math::Vector3{x0, y1, z0}, color);
    PushLine(Math::Vector3{x0, y0, z0}, Math::Vector3{x0, y1, z0}, color);
    PushLine(Math::Vector3{x1, y0, z0}, Math::Vector3{x1, y1, z0}, color);
    PushLine(Math::Vector3{x1, y0, z1}, Math::Vector3{x1, y1, z1}, color);
    PushLine(Math::Vector3{x0, y0, z1}, Math::Vector3{x0, y1, z1}, color);
}

void DebugRenderer::DrawSphere3D(const Math::Vector3& center, const float radius,
                                    const Math::Vector3& color) {
    if (!Math::IsFinite(center) || !std::isfinite(radius) || radius <= 0.0F ||
        !Math::IsFinite(color)) {
        UVE_WARNING("DebugRenderer: ignoring a sphere with a bad center, radius, or color");
        return;
    }
    std::array<float, kDebugSphereSegments + 1U> cosTable{};
    std::array<float, kDebugSphereSegments + 1U> sinTable{};
    for (std::size_t s = 0U; s <= kDebugSphereSegments; ++s) {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(s) /
                            static_cast<float>(kDebugSphereSegments);
        cosTable[s] = std::cos(angle);
        sinTable[s] = std::sin(angle);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegments; ++s) {
        const Math::Vector3 xy0{radius * cosTable[s], radius * sinTable[s], 0.0F};
        const Math::Vector3 xy1{radius * cosTable[s + 1U], radius * sinTable[s + 1U], 0.0F};
        PushLine(center + xy0, center + xy1, color);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegments; ++s) {
        const Math::Vector3 xz0{radius * cosTable[s], 0.0F, radius * sinTable[s]};
        const Math::Vector3 xz1{radius * cosTable[s + 1U], 0.0F, radius * sinTable[s + 1U]};
        PushLine(center + xz0, center + xz1, color);
    }
    for (std::size_t s = 0U; s < kDebugSphereSegments; ++s) {
        const Math::Vector3 yz0{0.0F, radius * cosTable[s], radius * sinTable[s]};
        const Math::Vector3 yz1{0.0F, radius * cosTable[s + 1U], radius * sinTable[s + 1U]};
        PushLine(center + yz0, center + yz1, color);
    }
}

void DebugRenderer::DrawRect2D(const Math::Vector2& min, const Math::Vector2& max,
                                  const Math::Vector3& color) {
    if (!Math::IsFinite(min) || !Math::IsFinite(max) || !Math::IsFinite(color)) {
        UVE_WARNING("DebugRenderer: ignoring a rect with non-finite corners or color");
        return;
    }
    PushLine(Math::Vector3{min.x, min.y, 0.0F}, Math::Vector3{max.x, min.y, 0.0F}, color);
    PushLine(Math::Vector3{max.x, min.y, 0.0F}, Math::Vector3{max.x, max.y, 0.0F}, color);
    PushLine(Math::Vector3{max.x, max.y, 0.0F}, Math::Vector3{min.x, max.y, 0.0F}, color);
    PushLine(Math::Vector3{min.x, max.y, 0.0F}, Math::Vector3{min.x, min.y, 0.0F}, color);
}

void DebugRenderer::DrawPoint3D(const Math::Vector3& position, const Math::Vector3& color) {
    if (!Math::IsFinite(position) || !Math::IsFinite(color)) {
        UVE_WARNING("DebugRenderer: ignoring a point with a non-finite position or color");
        return;
    }
    if (m_points.size() + 1U > kMaximumDebugPointVertices) {
        ++m_droppedPoints;
        return;
    }
    m_points.push_back(DebugLineVertex{position, color});
}

void DebugRenderer::SetPointSize(const float pixels) {
    if (!std::isfinite(pixels)) {
        UVE_WARNING("DebugRenderer: ignoring a non-finite point size");
        return;
    }
    m_pointSizePixels = std::clamp(pixels, kMinimumDebugPointSize, kMaximumDebugPointSize);
}

float DebugRenderer::PointSize() const noexcept {
    return m_pointSizePixels;
}

std::span<const DebugLineVertex> DebugRenderer::Lines() const noexcept {
    return m_lines;
}

std::span<const DebugLineVertex> DebugRenderer::Points() const noexcept {
    return m_points;
}

std::size_t DebugRenderer::DroppedLineCount() const noexcept {
    return m_droppedLines;
}

std::size_t DebugRenderer::DroppedPointCount() const noexcept {
    return m_droppedPoints;
}

void DebugRenderer::Clear() noexcept {
    m_lines.clear();
    m_points.clear();
    m_droppedLines = 0U;
    m_droppedPoints = 0U;
}

bool DebugRenderer::Submit(IRenderDevice& device, Shader::IShaderManager& shaders,
                                 ICommandBuffer& commands,
                                 const Math::Matrix4x4& viewProjection) {
    if (m_lines.empty() && m_points.empty()) {
        return true;
    }
    bool deferred = false;
    if (!m_lines.empty() &&
        !SubmitBatch(device, shaders, commands, viewProjection, m_lineResources, m_lines,
                        PrimitiveTopology::Lines, deferred)) {
        return false;
    }
    if (!m_points.empty() &&
        !SubmitBatch(device, shaders, commands, viewProjection, m_pointResources, m_points,
                        PrimitiveTopology::Points, deferred)) {
        return false;
    }
    if (deferred) {
        // At least one program's async compile is still in flight: keep both batches so the
        // next submit draws the frame whole rather than lines now and points later.
        return true;
    }
    Clear();
    return true;
}

bool DebugRenderer::SubmitBatch(IRenderDevice& device, Shader::IShaderManager& shaders,
                                      ICommandBuffer& commands, const Math::Matrix4x4& viewProjection,
                                      BatchResources& resources,
                                      const std::span<const DebugLineVertex> vertices,
                                      const PrimitiveTopology topology, bool& deferred) {
    const bool points = topology == PrimitiveTopology::Points;
    if (resources.program == nullptr) {
        Shader::ShaderProgramDesc desc;
        desc.virtualFilePath = std::string(points ? Shader::BuiltIn::kDebugPointVirtualPath
                                                  : Shader::BuiltIn::kDebugLineVirtualPath);
        desc.embeddedFallbackSourceCode = std::string(points ? Shader::BuiltIn::kDebugPointSource
                                                             : Shader::BuiltIn::kDebugLineSource);
        desc.vertexLayout = {
            VertexAttribute{"POSITION", VertexAttributeFormat::Float3,
                               offsetof(DebugLineVertex, position)},
            VertexAttribute{"COLOR", VertexAttributeFormat::Float3,
                               offsetof(DebugLineVertex, color)},
        };
        desc.vertexStride = static_cast<std::uint32_t>(sizeof(DebugLineVertex));
        desc.topology = topology;
        desc.depthTestEnabled = true;
        // Paint, not geometry (the decal rule): debug lines hide behind the scene but never
        // occlude each other, so coplanar lines don't z-fight.
        desc.depthWriteEnabled = false;
        desc.debugName = points ? "DebugPoint" : "DebugLine";
        resources.program = shaders.CreateProgram(desc);
        if (resources.program == nullptr) {
            return false;
        }
    }
    if (!resources.program->IsValid()) {
        // Async compile still in flight: keep the batch and let the next submit retry.
        deferred = true;
        return true;
    }
    if (resources.vertexBuffer == kInvalidBufferHandle) {
        // Rewritten from the CPU every submit: host-visible, streamed placement (on Vulkan a
        // memcpy into mapped memory instead of a staging submit per frame; GL_STREAM_DRAW on GL).
        const std::size_t capacity = points ? kMaximumDebugPointVertices : kMaximumDebugLineVertices;
        resources.vertexBuffer = device.CreateBuffer(
            BufferDesc{sizeof(DebugLineVertex) * capacity, BufferUsage::Vertex,
                          BufferMemory::Upload, BufferUpdateFrequency::Stream});
        if (resources.vertexBuffer == kInvalidBufferHandle) {
            return false;
        }
    }
    if (!device.UpdateBuffer(resources.vertexBuffer, std::as_bytes(vertices), 0U)) {
        return false;
    }
    resources.program->SetMatrix4x4("uViewProjection", viewProjection);
    if (points) {
        resources.program->SetFloat("uPointSize", m_pointSizePixels);
    }
    // Binds the pipeline and flushes the queued uniforms; then the batch draws as one.
    resources.program->ApplyTo(commands);
    commands.BindVertexBuffer(resources.vertexBuffer);
    commands.Draw(static_cast<std::uint32_t>(vertices.size()));
    return true;
}

void DebugRenderer::Shutdown(IRenderDevice& device) {
    for (BatchResources* const resources : {&m_lineResources, &m_pointResources}) {
        if (resources->vertexBuffer != kInvalidBufferHandle) {
            device.DestroyBuffer(resources->vertexBuffer);
            resources->vertexBuffer = kInvalidBufferHandle;
        }
        // Releasing the program destroys its pipeline on the device through the manager's deleter.
        resources->program.reset();
    }
    Clear();
}

void DebugRenderer::PushLine(const Math::Vector3& from, const Math::Vector3& to,
                                   const Math::Vector3& color) {
    if (m_lines.size() + 2U > kMaximumDebugLineVertices) {
        ++m_droppedLines;
        return;
    }
    m_lines.push_back(DebugLineVertex{from, color});
    m_lines.push_back(DebugLineVertex{to, color});
}

} // namespace UVE::Render
