// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_draw_list_uve.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace UVE::UI::Prism {

namespace {

constexpr Math::RectIntUVE kInfiniteClipRectUVE{
    Math::Vector2iUVE{0, 0},
    Math::Vector2iUVE{1000000, 1000000}
};

constexpr int kCornerTessellationStepsUVE = 8;

} // namespace

void PrismDrawListUVE::Reset() {
    m_vertices.clear();
    m_indices.clear();
    m_commands.clear();
    m_clipStack.clear();
    m_textureStack.clear();
    m_currentPipeline = PrismPipelineModeUVE::SolidOrTextured;
}

void PrismDrawListUVE::Reserve(std::size_t vertexCount, std::size_t indexCount, std::size_t commandCount) {
    m_vertices.reserve(vertexCount);
    m_indices.reserve(indexCount);
    m_commands.reserve(commandCount);
}

void PrismDrawListUVE::PushClipRect(Math::RectIntUVE rect) {
    if (m_clipStack.empty()) {
        m_clipStack.push_back(rect);
    } else {
        // Enforce hierarchical invariant: Child clip rect can never exceed parent clip rect
        m_clipStack.push_back(Math::IntersectionUVE(m_clipStack.back(), rect));
    }
}

void PrismDrawListUVE::PopClipRect() {
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
}

Math::RectIntUVE PrismDrawListUVE::GetCurrentClipRect() const noexcept {
    return m_clipStack.empty() ? kInfiniteClipRectUVE : m_clipStack.back();
}

void PrismDrawListUVE::PushTexture(std::uint64_t textureId) {
    m_textureStack.push_back(textureId);
}

void PrismDrawListUVE::PopTexture() {
    if (!m_textureStack.empty()) {
        m_textureStack.pop_back();
    }
}

void PrismDrawListUVE::SetPipelineMode(PrismPipelineModeUVE mode) {
    m_currentPipeline = mode;
}

void PrismDrawListUVE::EnsureCommandState() {
    const std::uint64_t activeTex = m_textureStack.empty() ? 0 : m_textureStack.back();
    const Math::RectIntUVE activeClip = GetCurrentClipRect();

    // Check if the current trailing command can be appended to
    if (!m_commands.empty()) {
        PrismDrawCmdUVE& tail = m_commands.back();
        if (tail.textureId == activeTex &&
            tail.clipRect == activeClip &&
            tail.pipeline == m_currentPipeline) {
            return;
        }
    }

    // New batch / state boundary
    PrismDrawCmdUVE newCmd{};
    newCmd.indexOffset = static_cast<std::uint32_t>(m_indices.size());
    newCmd.indexCount = 0;
    newCmd.clipRect = activeClip;
    newCmd.textureId = activeTex;
    newCmd.pipeline = m_currentPipeline;
    m_commands.push_back(newCmd);
}

void PrismDrawListUVE::AppendQuad(const PrismVertexUVE& v0, const PrismVertexUVE& v1,
                                   const PrismVertexUVE& v2, const PrismVertexUVE& v3) {
    EnsureCommandState();

    const auto baseIndex = static_cast<std::uint32_t>(m_vertices.size());
    m_vertices.push_back(v0);
    m_vertices.push_back(v1);
    m_vertices.push_back(v2);
    m_vertices.push_back(v3);

    // CCW standard: (0, 1, 2) and (0, 2, 3)
    m_indices.push_back(baseIndex + 0);
    m_indices.push_back(baseIndex + 1);
    m_indices.push_back(baseIndex + 2);

    m_indices.push_back(baseIndex + 0);
    m_indices.push_back(baseIndex + 2);
    m_indices.push_back(baseIndex + 3);

    m_commands.back().indexCount += 6;
}

void PrismDrawListUVE::AddRectFilled(const Math::RectUVE& rect, PrismColorUVE color, PrismCornerRadiiUVE radii) {
    if (rect.size.x <= 0.0F || rect.size.y <= 0.0F) {
        return;
    }

    const float x0 = rect.position.x;
    const float y0 = rect.position.y;
    const float x1 = x0 + rect.size.x;
    const float y1 = y0 + rect.size.y;

    if (radii.IsZero()) {
        // Fast path: Exact 4-vertex quad
        PrismVertexUVE v0{{x0, y0}, {0.0F, 0.0F}, color};
        PrismVertexUVE v1{{x1, y0}, {1.0F, 0.0F}, color};
        PrismVertexUVE v2{{x1, y1}, {1.0F, 1.0F}, color};
        PrismVertexUVE v3{{x0, y1}, {0.0F, 1.0F}, color};
        AppendQuad(v0, v1, v2, v3);
        return;
    }

    // Rounded rectangle tessellation:
    // Clamp radii so opposing corners cannot overlap
    const float maxRadius = 0.5F * std::min(rect.size.x, rect.size.y);
    const float rTL = std::clamp(radii.topLeft, 0.0F, maxRadius);
    const float rTR = std::clamp(radii.topRight, 0.0F, maxRadius);
    const float rBR = std::clamp(radii.bottomRight, 0.0F, maxRadius);
    const float rBL = std::clamp(radii.bottomLeft, 0.0F, maxRadius);

    EnsureCommandState();
    const auto centerIndex = static_cast<std::uint32_t>(m_vertices.size());
    const Math::Vector2UVE centerPos{0.5F * (x0 + x1), 0.5F * (y0 + y1)};
    m_vertices.push_back(PrismVertexUVE{centerPos, {0.5F, 0.5F}, color});

    const auto firstFanVertex = static_cast<std::uint32_t>(m_vertices.size());

    auto addCornerArc = [&](Math::Vector2UVE c, float r, float startAngle, float endAngle) {
        if (r <= 0.0F) {
            m_vertices.push_back(PrismVertexUVE{c, {0.0F, 0.0F}, color});
            return;
        }
        for (int i = 0; i <= kCornerTessellationStepsUVE; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kCornerTessellationStepsUVE);
            const float angle = startAngle + t * (endAngle - startAngle);
            const Math::Vector2UVE pos{c.x + r * std::cos(angle), c.y + r * std::sin(angle)};
            m_vertices.push_back(PrismVertexUVE{pos, {0.0F, 0.0F}, color});
        }
    };

    // 4 corners starting Top-Right clockwise
    addCornerArc(Math::Vector2UVE{x1 - rTR, y0 + rTR}, rTR, -std::numbers::pi_v<float> * 0.5F, 0.0F);
    addCornerArc(Math::Vector2UVE{x1 - rBR, y1 - rBR}, rBR, 0.0F, std::numbers::pi_v<float> * 0.5F);
    addCornerArc(Math::Vector2UVE{x0 + rBL, y1 - rBL}, rBL, std::numbers::pi_v<float> * 0.5F, std::numbers::pi_v<float>);
    addCornerArc(Math::Vector2UVE{x0 + rTL, y0 + rTL}, rTL, std::numbers::pi_v<float>, std::numbers::pi_v<float> * 1.5F);

    const auto perimeterCount = static_cast<std::uint32_t>(m_vertices.size() - firstFanVertex);
    for (std::uint32_t i = 0; i < perimeterCount; ++i) {
        const std::uint32_t next = (i + 1) % perimeterCount;
        m_indices.push_back(centerIndex);
        m_indices.push_back(firstFanVertex + i);
        m_indices.push_back(firstFanVertex + next);
        m_commands.back().indexCount += 3;
    }
}

void PrismDrawListUVE::AddRectStroke(const Math::RectUVE& rect, PrismColorUVE color, float strokeWidth, PrismCornerRadiiUVE radii) {
    if (rect.size.x <= 0.0F || rect.size.y <= 0.0F || strokeWidth <= 0.0F) {
        return;
    }

    const float half = strokeWidth * 0.5F;
    const float x0 = rect.position.x;
    const float y0 = rect.position.y;
    const float x1 = x0 + rect.size.x;
    const float y1 = y0 + rect.size.y;

    if (radii.IsZero()) {
        // Top edge
        AddRectFilled(Math::RectUVE{{x0, y0}, {rect.size.x, strokeWidth}}, color);
        // Bottom edge
        AddRectFilled(Math::RectUVE{{x0, y1 - strokeWidth}, {rect.size.x, strokeWidth}}, color);
        // Left edge (excluding corners to avoid overdraw)
        AddRectFilled(Math::RectUVE{{x0, y0 + strokeWidth}, {strokeWidth, std::max(0.0F, rect.size.y - 2.0F * strokeWidth)}}, color);
        // Right edge
        AddRectFilled(Math::RectUVE{{x1 - strokeWidth, y0 + strokeWidth}, {strokeWidth, std::max(0.0F, rect.size.y - 2.0F * strokeWidth)}}, color);
        return;
    }

    // Outer & inner path generation for rounded stroke
    const float maxRadius = 0.5F * std::min(rect.size.x, rect.size.y);
    const float rTL = std::clamp(radii.topLeft, 0.0F, maxRadius);
    const float rTR = std::clamp(radii.topRight, 0.0F, maxRadius);
    const float rBR = std::clamp(radii.bottomRight, 0.0F, maxRadius);
    const float rBL = std::clamp(radii.bottomLeft, 0.0F, maxRadius);

    std::vector<Math::Vector2UVE> outerPoints;
    std::vector<Math::Vector2UVE> innerPoints;

    auto addArc = [&](Math::Vector2UVE c, float r, float startAngle, float endAngle) {
        const float rOut = r + half;
        const float rIn = std::max(0.0F, r - half);
        for (int i = 0; i <= kCornerTessellationStepsUVE; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kCornerTessellationStepsUVE);
            const float a = startAngle + t * (endAngle - startAngle);
            const float cosA = std::cos(a);
            const float sinA = std::sin(a);
            outerPoints.push_back(Math::Vector2UVE{c.x + rOut * cosA, c.y + rOut * sinA});
            innerPoints.push_back(Math::Vector2UVE{c.x + rIn * cosA, c.y + rIn * sinA});
        }
    };

    addArc(Math::Vector2UVE{x1 - rTR, y0 + rTR}, rTR, -std::numbers::pi_v<float> * 0.5F, 0.0F);
    addArc(Math::Vector2UVE{x1 - rBR, y1 - rBR}, rBR, 0.0F, std::numbers::pi_v<float> * 0.5F);
    addArc(Math::Vector2UVE{x0 + rBL, y1 - rBL}, rBL, std::numbers::pi_v<float> * 0.5F, std::numbers::pi_v<float>);
    addArc(Math::Vector2UVE{x0 + rTL, y0 + rTL}, rTL, std::numbers::pi_v<float>, std::numbers::pi_v<float> * 1.5F);

    EnsureCommandState();
    const auto baseVertex = static_cast<std::uint32_t>(m_vertices.size());
    const auto pointCount = static_cast<std::uint32_t>(outerPoints.size());

    for (std::uint32_t i = 0; i < pointCount; ++i) {
        m_vertices.push_back(PrismVertexUVE{outerPoints[i], {0.0F, 0.0F}, color});
        m_vertices.push_back(PrismVertexUVE{innerPoints[i], {1.0F, 1.0F}, color});
    }

    for (std::uint32_t i = 0; i < pointCount; ++i) {
        const std::uint32_t next = (i + 1) % pointCount;
        const std::uint32_t out0 = baseVertex + i * 2;
        const std::uint32_t in0  = baseVertex + i * 2 + 1;
        const std::uint32_t out1 = baseVertex + next * 2;
        const std::uint32_t in1  = baseVertex + next * 2 + 1;

        m_indices.push_back(out0);
        m_indices.push_back(out1);
        m_indices.push_back(in0);

        m_indices.push_back(out1);
        m_indices.push_back(in1);
        m_indices.push_back(in0);

        m_commands.back().indexCount += 6;
    }
}

void PrismDrawListUVE::AddTexturedQuad(const Math::RectUVE& rect, const Math::RectUVE& uvRect,
                                       PrismColorUVE tint, std::uint64_t textureId) {
    if (rect.size.x <= 0.0F || rect.size.y <= 0.0F) {
        return;
    }

    PushTexture(textureId);

    const float x0 = rect.position.x;
    const float y0 = rect.position.y;
    const float x1 = x0 + rect.size.x;
    const float y1 = y0 + rect.size.y;

    const float u0 = uvRect.position.x;
    const float v0 = uvRect.position.y;
    const float u1 = u0 + uvRect.size.x;
    const float v1 = v0 + uvRect.size.y;

    PrismVertexUVE vTopLeft{{x0, y0}, {u0, v0}, tint};
    PrismVertexUVE vTopRight{{x1, y0}, {u1, v0}, tint};
    PrismVertexUVE vBottomRight{{x1, y1}, {u1, v1}, tint};
    PrismVertexUVE vBottomLeft{{x0, y1}, {u0, v1}, tint};

    AppendQuad(vTopLeft, vTopRight, vBottomRight, vBottomLeft);

    PopTexture();
}

void PrismDrawListUVE::AddDropShadow(const Math::RectUVE& rect, float blurRadius, float spread,
                                      PrismColorUVE shadowColor, Math::Vector2UVE offset,
                                      PrismCornerRadiiUVE radii) {
    if (shadowColor.a <= 0.0F || (blurRadius <= 0.0F && spread <= 0.0F)) {
        return;
    }

    const float expand = blurRadius + spread;
    const Math::RectUVE shadowRect{
        Math::Vector2UVE{rect.position.x + offset.x - expand, rect.position.y + offset.y - expand},
        Math::Vector2UVE{rect.size.x + 2.0F * expand, rect.size.y + 2.0F * expand}
    };

    const PrismPipelineModeUVE savedMode = m_currentPipeline;
    SetPipelineMode(PrismPipelineModeUVE::DropShadow);

    // Multi-layer gaussian approximation
    PrismColorUVE c = shadowColor;
    c.a *= 0.5F;
    AddRectFilled(shadowRect, c, PrismCornerRadiiUVE{radii.topLeft + expand, radii.topRight + expand, radii.bottomRight + expand, radii.bottomLeft + expand});

    SetPipelineMode(savedMode);
}

void PrismDrawListUVE::AddLine(Math::Vector2UVE p0, Math::Vector2UVE p1, PrismColorUVE color, float thickness) {
    const Math::Vector2UVE d = p1 - p0;
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len <= 0.0001F || thickness <= 0.0F) {
        return;
    }

    const float half = thickness * 0.5F;
    const Math::Vector2UVE normal{-d.y / len * half, d.x / len * half};

    PrismVertexUVE v0{p0 + normal, {0.0F, 0.0F}, color};
    PrismVertexUVE v1{p1 + normal, {1.0F, 0.0F}, color};
    PrismVertexUVE v2{p1 - normal, {1.0F, 1.0F}, color};
    PrismVertexUVE v3{p0 - normal, {0.0F, 1.0F}, color};

    AppendQuad(v0, v1, v2, v3);
}

} // namespace UVE::UI::Prism
