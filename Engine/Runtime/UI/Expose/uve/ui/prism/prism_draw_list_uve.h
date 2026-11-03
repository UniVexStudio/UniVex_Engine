// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>
#include "uve/math/color_uve.h"
#include "uve/math/rect_int_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/ui/prism/prism_geometry_uve.h"

namespace UVE::UI::Prism {

/// High-performance, immediate-geometry stream generator for vector UI.
/// Generates batched index/vertex buffers with hierarchical scissor clipping.
/// Guarantees zero heap allocation when initialized with an arena or capacity reserve.
class PrismDrawListUVE final {
public:
    PrismDrawListUVE() = default;

    /// Clears vertex, index, and command buffers for the next frame.
    void Reset();
    void Clear() noexcept { Reset(); }

    /// Reserve memory ahead of time to avoid mid-frame reallocations.
    void Reserve(std::size_t vertexCount, std::size_t indexCount, std::size_t commandCount);

    // --- Scissor / Clip Rect Stack (Hierarchical Invariant) ---
    /// Pushes a new clip rect, mathematically intersected with the current parent clip rect.
    void PushClipRect(Math::RectIntUVE rect);
    /// Pops the active clip rect and restores the previous one.
    void PopClipRect();
    /// Returns the active intersection clip rect.
    [[nodiscard]] Math::RectIntUVE GetCurrentClipRect() const noexcept;

    // --- Texture & Pipeline State ---
    void PushTexture(std::uint64_t textureId);
    void PopTexture();
    void SetPipelineMode(PrismPipelineModeUVE mode);

    // --- Geometry Generators ---
    /// Solid flat rectangle (with optional independent corner radii).
    void AddRectFilled(const Math::RectUVE& rect, PrismColorUVE color, PrismCornerRadiiUVE radii = {});

    /// Outlined stroked rectangle with thickness and inner/outer alignment.
    void AddRectStroke(const Math::RectUVE& rect, PrismColorUVE color, float strokeWidth = 1.0F, PrismCornerRadiiUVE radii = {});

    /// Textured quad (e.g. icons, images, font atlas glyphs).
    void AddTexturedQuad(const Math::RectUVE& rect, const Math::RectUVE& uvRect, PrismColorUVE tint, std::uint64_t textureId);

    /// Drop shadow around a rectangle (uses analytical quad expansion).
    void AddDropShadow(const Math::RectUVE& rect, float blurRadius, float spread, PrismColorUVE shadowColor, Math::Vector2UVE offset = {}, PrismCornerRadiiUVE radii = {});

    /// 2D Line segment with thickness.
    void AddLine(Math::Vector2UVE p0, Math::Vector2UVE p1, PrismColorUVE color, float thickness = 1.0F);

    /// Renders text with font rasterization on supported backends.
    void AddText(Math::Vector2UVE pos, std::string text, PrismColorUVE color, float fontSize = 13.0F);

    // --- Output Accessors ---
    [[nodiscard]] const std::vector<PrismVertexUVE>& GetVertices() const noexcept { return m_vertices; }
    [[nodiscard]] const std::vector<std::uint32_t>&   GetIndices() const noexcept { return m_indices; }
    [[nodiscard]] const std::vector<PrismDrawCmdUVE>& GetCommands() const noexcept { return m_commands; }

    [[nodiscard]] std::size_t GetTotalVertexCount() const noexcept { return m_vertices.size(); }
    [[nodiscard]] std::size_t GetTotalIndexCount() const noexcept { return m_indices.size(); }
    [[nodiscard]] std::size_t GetCommandCount() const noexcept { return m_commands.size(); }
    [[nodiscard]] const std::vector<PrismTextCommandUVE>& GetTextCommands() const noexcept { return m_textCommands; }

private:
    void EnsureCommandState();
    void AppendQuad(const PrismVertexUVE& v0, const PrismVertexUVE& v1, const PrismVertexUVE& v2, const PrismVertexUVE& v3);

    std::vector<PrismVertexUVE>  m_vertices;
    std::vector<std::uint32_t>    m_indices;
    std::vector<PrismDrawCmdUVE>  m_commands;
    std::vector<PrismTextCommandUVE> m_textCommands;

    std::vector<Math::RectIntUVE> m_clipStack;
    std::vector<std::uint64_t>    m_textureStack;
    PrismPipelineModeUVE          m_currentPipeline = PrismPipelineModeUVE::SolidOrTextured;
};

} // namespace UVE::UI::Prism
