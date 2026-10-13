// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "uve/math/aabb_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi_shader/i_shader_manager_uve.h"
#include "uve/rhi_shader/shader_program_uve.h"

namespace UVE::Render {

/// One debug-line vertex: a world-space position and an RGB color in linear space.
struct DebugLineVertexUVE final {
    Math::Vector3UVE position;
    Math::Vector3UVE color;
};

/// Batch bound: 16K lines (768KB) per submit. Lines past the cap are dropped and counted
/// rather than reallocating mid-frame — a debug flood must not move the allocator.
inline constexpr std::size_t kMaximumDebugLineVerticesUVE = 32768U;

/// Segments per great circle of a debug sphere: 3 circles x 24 segments x 2 vertices.
inline constexpr std::size_t kDebugSphereSegmentsUVE = 24U;

/// Batches debug lines and shapes for one frame and submits them as a single 1px line-list
/// draw through the `debug_line` built-in (world-space positions, per-vertex colors, one
/// uViewProjection). Depth-tested against the scene but never depth-writing, so debug paint
/// hides behind geometry without z-fighting itself.
///
/// Lines are the whole vocabulary: a box is 12 lines from one call (the Tier 2.8 done-when),
/// a 2D rect is 4 lines at z = 0, a sphere 3 axis circles, and anything else (frusta, contacts,
/// paths) composes from DrawLine3D. There is no strip/fan mode, no line width, and no x-ray
/// overlay pass — those are follow-up increments, not missing features.
///
/// Lifetime: GPU resources (line program, vertex buffer) are created lazily on first SubmitUVE
/// and released by ShutdownUVE, which must run while the device and shader manager are still
/// alive — the program's deleter destroys its pipeline on the device. Draw calls before any
/// submit are pure CPU batching; a submit with an empty batch is a no-op returning true.
///
/// Non-finite endpoints/colors and non-positive sphere radii are ignored with a warning rather
/// than poisoning the batch: a debug call must never be what breaks the frame it debugs.
///
/// Thread-safety: single-threaded, like every other per-frame renderer here — record from the
/// render thread only.
class DebugRendererUVE final {
public:
    DebugRendererUVE() = default;
    DebugRendererUVE(const DebugRendererUVE&) = delete;
    DebugRendererUVE& operator=(const DebugRendererUVE&) = delete;
    ~DebugRendererUVE();

    /// Appends one line. Ignored (counted, warned) past the batch cap or on non-finite input.
    void DrawLine3D(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                    const Math::Vector3UVE& color);

    /// Appends the 12 edges of `bounds`: the bottom ring (min.y), the top ring (max.y), then
    /// the 4 verticals, in that fixed order.
    void DrawBox3D(const Math::AabbUVE& bounds, const Math::Vector3UVE& color);

    /// Appends 3 great circles (XY, XZ, YZ planes) of `kDebugSphereSegmentsUVE` segments each.
    /// Ignored with a warning when `radius` is not finite and positive.
    void DrawSphere3D(const Math::Vector3UVE& center, float radius, const Math::Vector3UVE& color);

    /// Appends a 2D rect outline at z = 0, ring order starting at min. The 2D face of the
    /// same one-call vocabulary — collider rects, sprite bounds, camera rects.
    void DrawRect2D(const Math::Vector2UVE& min, const Math::Vector2UVE& max,
                    const Math::Vector3UVE& color);

    /// The current batch: pairs of vertices, one pair per line, in submission order.
    [[nodiscard]] std::span<const DebugLineVertexUVE> LinesUVE() const noexcept;

    /// Lines dropped from the current batch by the cap (DrawLine3D past 16K lines). Resets on
    /// ClearUVE and after every successful submit.
    [[nodiscard]] std::size_t DroppedLineCountUVE() const noexcept;

    /// Discards the batch and the drop counter without touching the GPU.
    void ClearUVE() noexcept;

    /// Uploads the batch and records bind + draw into `commands` (which must already be inside
    /// a render pass), then clears the batch. Returns false — leaving the batch intact for a
    /// retry — only when GPU resources fail to create or the upload fails. A program whose
    /// async compile is still in flight defers instead of failing: the batch is kept and the
    /// call returns true. Empty batches return true without touching the GPU.
    [[nodiscard]] bool SubmitUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                 ICommandBufferUVE& commands,
                                 const Math::Matrix4x4UVE& viewProjection);

    /// Releases the vertex buffer and line program; safe to call twice and safe with no
    /// resources (a renderer that never submitted). Call before the device dies.
    void ShutdownUVE(IRenderDeviceUVE& device);

private:
    void PushLineUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                     const Math::Vector3UVE& color);

    std::vector<DebugLineVertexUVE> m_lines;
    std::size_t m_droppedLines = 0U;
    std::shared_ptr<Shader::ShaderProgramUVE> m_program;
    BufferHandleUVE m_vertexBuffer = kInvalidBufferHandleUVE;
};

} // namespace UVE::Render
