// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "aabb.h"
#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "i_command_buffer.h"
#include "i_render_device.h"
#include "i_shader_manager.h"
#include "shader_program.h"

namespace UVE::Render {

/// One debug-line vertex: a world-space position and an RGB color in linear space.
struct DebugLineVertex final {
    Math::Vector3 position;
    Math::Vector3 color;
};

/// Batch bound: 16K lines (768KB) per submit. Lines past the cap are dropped and counted
/// rather than reallocating mid-frame — a debug flood must not move the allocator.
inline constexpr std::size_t kMaximumDebugLineVertices = 32768U;

/// Segments per great circle of a debug sphere: 3 circles x 24 segments x 2 vertices.
inline constexpr std::size_t kDebugSphereSegments = 24U;

/// Point batch bound: 8K points (192KB) per submit, dropped and counted past the cap like lines.
inline constexpr std::size_t kMaximumDebugPointVertices = 8192U;

/// Default and clamp range of the batch-wide point size, in pixels.
inline constexpr float kDefaultDebugPointSize = 4.0F;
inline constexpr float kMinimumDebugPointSize = 1.0F;
inline constexpr float kMaximumDebugPointSize = 64.0F;

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
/// Lifetime: GPU resources (line program, vertex buffer) are created lazily on first Submit
/// and released by Shutdown, which must run while the device and shader manager are still
/// alive — the program's deleter destroys its pipeline on the device. Draw calls before any
/// submit are pure CPU batching; a submit with an empty batch is a no-op returning true.
///
/// Non-finite endpoints/colors and non-positive sphere radii are ignored with a warning rather
/// than poisoning the batch: a debug call must never be what breaks the frame it debugs.
///
/// Thread-safety: single-threaded, like every other per-frame renderer here — record from the
/// render thread only.
class DebugRenderer final {
public:
    DebugRenderer() = default;
    DebugRenderer(const DebugRenderer&) = delete;
    DebugRenderer& operator=(const DebugRenderer&) = delete;
    ~DebugRenderer();

    /// Appends one line. Ignored (counted, warned) past the batch cap or on non-finite input.
    void DrawLine3D(const Math::Vector3& from, const Math::Vector3& to,
                    const Math::Vector3& color);

    /// Appends the 12 edges of `bounds`: the bottom ring (min.y), the top ring (max.y), then
    /// the 4 verticals, in that fixed order.
    void DrawBox3D(const Math::Aabb& bounds, const Math::Vector3& color);

    /// Appends 3 great circles (XY, XZ, YZ planes) of `kDebugSphereSegments` segments each.
    /// Ignored with a warning when `radius` is not finite and positive.
    void DrawSphere3D(const Math::Vector3& center, float radius, const Math::Vector3& color);

    /// Appends a 2D rect outline at z = 0, ring order starting at min. The 2D face of the
    /// same one-call vocabulary — collider rects, sprite bounds, camera rects.
    void DrawRect2D(const Math::Vector2& min, const Math::Vector2& max,
                    const Math::Vector3& color);

    /// Appends one point, drawn as a screen-aligned square of PointSize() pixels. Ignored
    /// (counted, warned) past the point cap or on non-finite input.
    void DrawPoint3D(const Math::Vector3& position, const Math::Vector3& color);

    /// Batch-wide point size in pixels, clamped to [1, 64]; non-finite values are ignored with
    /// a warning. Applies to every point of the next submit.
    void SetPointSize(float pixels);
    [[nodiscard]] float PointSize() const noexcept;

    /// The current batch: pairs of vertices, one pair per line, in submission order.
    [[nodiscard]] std::span<const DebugLineVertex> Lines() const noexcept;

    /// The current point batch, one vertex per point, in submission order.
    [[nodiscard]] std::span<const DebugLineVertex> Points() const noexcept;

    /// Lines dropped from the current batch by the cap (DrawLine3D past 16K lines). Resets on
    /// Clear and after every successful submit.
    [[nodiscard]] std::size_t DroppedLineCount() const noexcept;

    /// Points dropped from the current batch by the point cap; same reset rule.
    [[nodiscard]] std::size_t DroppedPointCount() const noexcept;

    /// Discards the batch and the drop counter without touching the GPU.
    void Clear() noexcept;

    /// Uploads the batches and records bind + draw into `commands` (which must already be
    /// inside a render pass) — one Lines draw, then one Points draw, each only when its batch
    /// is non-empty — then clears both. Returns false — leaving the batches intact for a
    /// retry — only when GPU resources fail to create or an upload fails. A program whose
    /// async compile is still in flight defers instead of failing: the batches are kept and
    /// the call returns true. Empty batches return true without touching the GPU.
    [[nodiscard]] bool Submit(IRenderDevice& device, Shader::IShaderManager& shaders,
                                 ICommandBuffer& commands,
                                 const Math::Matrix4x4& viewProjection);

    /// Releases the vertex buffers and programs; safe to call twice and safe with no
    /// resources (a renderer that never submitted). Call before the device dies.
    void Shutdown(IRenderDevice& device);

private:
    /// One (program, streamed vertex buffer) pair per topology; created lazily on first use.
    struct BatchResources final {
        std::shared_ptr<Shader::ShaderProgram> program;
        BufferHandle vertexBuffer = kInvalidBufferHandle;
    };

    void PushLine(const Math::Vector3& from, const Math::Vector3& to,
                     const Math::Vector3& color);
    /// Creates the batch's resources on first use, uploads `vertices` and records bind + draw.
    /// Returns false on a resource/upload failure; sets `deferred` (and returns true) when the
    /// program is still compiling so the caller keeps the batch.
    [[nodiscard]] bool SubmitBatch(IRenderDevice& device, Shader::IShaderManager& shaders,
                                      ICommandBuffer& commands, const Math::Matrix4x4& viewProjection,
                                      BatchResources& resources, std::span<const DebugLineVertex> vertices,
                                      PrimitiveTopology topology, bool& deferred);

    std::vector<DebugLineVertex> m_lines;
    std::vector<DebugLineVertex> m_points;
    std::size_t m_droppedLines = 0U;
    std::size_t m_droppedPoints = 0U;
    float m_pointSizePixels = kDefaultDebugPointSize;
    BatchResources m_lineResources;
    BatchResources m_pointResources;
};

} // namespace UVE::Render
