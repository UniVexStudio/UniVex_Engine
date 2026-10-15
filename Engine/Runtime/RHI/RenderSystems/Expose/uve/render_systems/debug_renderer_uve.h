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

/// Point batch bound: 8K points (192KB) per submit, dropped and counted past the cap like lines.
inline constexpr std::size_t kMaximumDebugPointVerticesUVE = 8192U;

/// Default and clamp range of the batch-wide point size, in pixels.
inline constexpr float kDefaultDebugPointSizeUVE = 4.0F;
inline constexpr float kMinimumDebugPointSizeUVE = 1.0F;
inline constexpr float kMaximumDebugPointSizeUVE = 64.0F;

/// Batches debug lines and shapes for one frame and submits them as a single 1px line-list
/// draw through the `debug_line` built-in (world-space positions, per-vertex colors, one
/// uViewProjection). Depth-tested against the scene but never depth-writing, so debug paint
/// hides behind geometry without z-fighting itself.
///
/// Lines are the whole vocabulary: a box is 12 lines from one call (the Tier 2.8 done-when),
/// a 2D rect is 4 lines at z = 0, a sphere 3 axis circles, and anything else (frusta, contacts,
/// paths) composes from DrawLine3D. Points are the one other vocabulary: DrawPoint3D batches
/// into a second, `Points`-topology draw through the `debug_point` built-in, sized batch-wide
/// in pixels. There is no line width and no x-ray overlay pass — follow-up increments, not
/// missing features. Polylines (sphere circles) stay expanded into the line list on purpose:
/// one strip per circle would cost a draw each, the list keeps the whole frame in one.
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

    /// Appends one point, drawn as a screen-aligned square of PointSizeUVE() pixels. Ignored
    /// (counted, warned) past the point cap or on non-finite input.
    void DrawPoint3D(const Math::Vector3UVE& position, const Math::Vector3UVE& color);

    /// Batch-wide point size in pixels, clamped to [1, 64]; non-finite values are ignored with
    /// a warning. Applies to every point of the next submit.
    void SetPointSizeUVE(float pixels);
    [[nodiscard]] float PointSizeUVE() const noexcept;

    /// The current batch: pairs of vertices, one pair per line, in submission order.
    [[nodiscard]] std::span<const DebugLineVertexUVE> LinesUVE() const noexcept;

    /// The current point batch, one vertex per point, in submission order.
    [[nodiscard]] std::span<const DebugLineVertexUVE> PointsUVE() const noexcept;

    /// Lines dropped from the current batch by the cap (DrawLine3D past 16K lines). Resets on
    /// ClearUVE and after every successful submit.
    [[nodiscard]] std::size_t DroppedLineCountUVE() const noexcept;

    /// Points dropped from the current batch by the point cap; same reset rule.
    [[nodiscard]] std::size_t DroppedPointCountUVE() const noexcept;

    /// Discards the batch and the drop counter without touching the GPU.
    void ClearUVE() noexcept;

    /// Uploads the batches and records bind + draw into `commands` (which must already be
    /// inside a render pass) — one Lines draw, then one Points draw, each only when its batch
    /// is non-empty — then clears both. Returns false — leaving the batches intact for a
    /// retry — only when GPU resources fail to create or an upload fails. A program whose
    /// async compile is still in flight defers instead of failing: the batches are kept and
    /// the call returns true. Empty batches return true without touching the GPU.
    [[nodiscard]] bool SubmitUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                 ICommandBufferUVE& commands,
                                 const Math::Matrix4x4UVE& viewProjection);

    /// Releases the vertex buffers and programs; safe to call twice and safe with no
    /// resources (a renderer that never submitted). Call before the device dies.
    void ShutdownUVE(IRenderDeviceUVE& device);

private:
    /// One (program, streamed vertex buffer) pair per topology; created lazily on first use.
    struct BatchResourcesUVE final {
        std::shared_ptr<Shader::ShaderProgramUVE> program;
        BufferHandleUVE vertexBuffer = kInvalidBufferHandleUVE;
    };

    void PushLineUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                     const Math::Vector3UVE& color);
    /// Creates the batch's resources on first use, uploads `vertices` and records bind + draw.
    /// Returns false on a resource/upload failure; sets `deferred` (and returns true) when the
    /// program is still compiling so the caller keeps the batch.
    [[nodiscard]] bool SubmitBatchUVE(IRenderDeviceUVE& device, Shader::IShaderManagerUVE& shaders,
                                      ICommandBufferUVE& commands, const Math::Matrix4x4UVE& viewProjection,
                                      BatchResourcesUVE& resources, std::span<const DebugLineVertexUVE> vertices,
                                      PrimitiveTopologyUVE topology, bool& deferred);

    std::vector<DebugLineVertexUVE> m_lines;
    std::vector<DebugLineVertexUVE> m_points;
    std::size_t m_droppedLines = 0U;
    std::size_t m_droppedPoints = 0U;
    float m_pointSizePixels = kDefaultDebugPointSizeUVE;
    BatchResourcesUVE m_lineResources;
    BatchResourcesUVE m_pointResources;
};

} // namespace UVE::Render
