// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/line_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/2d/path_2d_uve.h"

namespace UVE::Scene {

bool IsLine2DComponentValidUVE(const Line2DComponentUVE& value) noexcept {
    if (value.points.size() > kMaximumPath2DPointsUVE || !std::isfinite(value.widthPixels) ||
        value.widthPixels <= 0.0F || !std::isfinite(value.defaultColor.r) ||
        !std::isfinite(value.defaultColor.g) || !std::isfinite(value.defaultColor.b) ||
        !std::isfinite(value.opacity) || value.opacity < 0.0F || value.opacity > 1.0F) {
        return false;
    }
    for (const Math::Vector2UVE& pt : value.points) {
        if (!Math::IsFiniteUVE(pt)) {
            return false;
        }
    }
    return true;
}

Line2DComponentUVE SanitizeLine2DComponentUVE(const Line2DComponentUVE& value) {
    Line2DComponentUVE out = value;
    if (out.points.size() > kMaximumPath2DPointsUVE) {
        out.points.resize(kMaximumPath2DPointsUVE);
    }
    for (Math::Vector2UVE& pt : out.points) {
        if (!Math::IsFiniteUVE(pt)) {
            pt = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    if (!std::isfinite(out.widthPixels) || out.widthPixels <= 0.0F) {
        out.widthPixels = 4.0F;
    }
    if (!std::isfinite(out.defaultColor.r) || !std::isfinite(out.defaultColor.g) ||
        !std::isfinite(out.defaultColor.b)) {
        out.defaultColor = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.opacity)) {
        out.opacity = 1.0F;
    } else {
        out.opacity = std::clamp(out.opacity, 0.0F, 1.0F);
    }
    return out;
}

bool IsLine2DObjectDefinitionValidUVE(const Line2DObjectDefinitionUVE& value) noexcept {
    return IsLine2DComponentValidUVE(value.line);
}

void ApplyLine2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Line2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Line2DComponentUVE>(entity,
                                                      SanitizeLine2DComponentUVE(value.line));
}

std::size_t Line2DUVE::ResolveSegmentCountUVE(const Line2DComponentUVE& line) noexcept {
    if (line.points.size() < 2U) {
        return 0U;
    }
    return (line.closed && line.points.size() >= 3U) ? line.points.size()
                                                     : (line.points.size() - 1U);
}

Math::RectUVE Line2DUVE::ComputeLocalBoundingRectUVE(const Line2DComponentUVE& line) noexcept {
    if (!IsLine2DComponentValidUVE(line) || line.points.empty()) {
        return Math::RectUVE{};
    }
    float minX = line.points.front().x;
    float maxX = line.points.front().x;
    float minY = line.points.front().y;
    float maxY = line.points.front().y;
    for (const Math::Vector2UVE& pt : line.points) {
        minX = std::min(minX, pt.x);
        maxX = std::max(maxX, pt.x);
        minY = std::min(minY, pt.y);
        maxY = std::max(maxY, pt.y);
    }
    const float halfW = 0.5F * line.widthPixels;
    return Math::RectUVE{Math::Vector2UVE{minX - halfW, minY - halfW},
                         Math::Vector2UVE{(maxX - minX) + line.widthPixels,
                                          (maxY - minY) + line.widthPixels}};
}

bool IsPolygon2DComponentValidUVE(const Polygon2DComponentUVE& value) noexcept {
    if (value.vertices.size() > kMaximumPath2DPointsUVE || !std::isfinite(value.color.r) ||
        !std::isfinite(value.color.g) || !std::isfinite(value.color.b) ||
        !std::isfinite(value.opacity) || value.opacity < 0.0F || value.opacity > 1.0F) {
        return false;
    }
    for (const Math::Vector2UVE& v : value.vertices) {
        if (!Math::IsFiniteUVE(v)) {
            return false;
        }
    }
    return true;
}

Polygon2DComponentUVE SanitizePolygon2DComponentUVE(const Polygon2DComponentUVE& value) {
    Polygon2DComponentUVE out = value;
    if (out.vertices.size() > kMaximumPath2DPointsUVE) {
        out.vertices.resize(kMaximumPath2DPointsUVE);
    }
    for (Math::Vector2UVE& v : out.vertices) {
        if (!Math::IsFiniteUVE(v)) {
            v = Math::Vector2UVE{0.0F, 0.0F};
        }
    }
    if (!std::isfinite(out.color.r) || !std::isfinite(out.color.g) ||
        !std::isfinite(out.color.b)) {
        out.color = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.opacity)) {
        out.opacity = 1.0F;
    } else {
        out.opacity = std::clamp(out.opacity, 0.0F, 1.0F);
    }
    return out;
}

bool IsPolygon2DObjectDefinitionValidUVE(const Polygon2DObjectDefinitionUVE& value) noexcept {
    return IsPolygon2DComponentValidUVE(value.polygon);
}

void ApplyPolygon2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const Polygon2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Polygon2DComponentUVE>(
        entity, SanitizePolygon2DComponentUVE(value.polygon));
}

float Polygon2DUVE::ComputeSignedAreaUVE(const Polygon2DComponentUVE& polygon) noexcept {
    if (!IsPolygon2DComponentValidUVE(polygon) || polygon.vertices.size() < 3U) {
        return 0.0F;
    }
    double doubleArea = 0.0;
    const std::size_t n = polygon.vertices.size();
    for (std::size_t i = 0U; i < n; ++i) {
        const Math::Vector2UVE& a = polygon.vertices[i];
        const Math::Vector2UVE& b = polygon.vertices[(i + 1U) % n];
        doubleArea += static_cast<double>(a.x) * static_cast<double>(b.y) -
                      static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    return static_cast<float>(0.5 * doubleArea);
}

bool Polygon2DUVE::ContainsLocalPointUVE(const Polygon2DComponentUVE& polygon,
                                         const Math::Vector2UVE& localPoint) noexcept {
    if (!IsPolygon2DComponentValidUVE(polygon) || polygon.vertices.size() < 3U ||
        !Math::IsFiniteUVE(localPoint)) {
        return false;
    }
    bool inside = false;
    const std::size_t n = polygon.vertices.size();
    for (std::size_t i = 0U, j = n - 1U; i < n; j = i++) {
        const Math::Vector2UVE& vi = polygon.vertices[i];
        const Math::Vector2UVE& vj = polygon.vertices[j];
        const bool intersects = ((vi.y > localPoint.y) != (vj.y > localPoint.y)) &&
                                (localPoint.x < (vj.x - vi.x) * (localPoint.y - vi.y) /
                                                        (vj.y - vi.y) +
                                                    vi.x);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

bool IsCanvasModulateComponentValidUVE(const CanvasModulateComponentUVE& value) noexcept {
    return std::isfinite(value.color.r) && std::isfinite(value.color.g) &&
           std::isfinite(value.color.b) && value.color.r >= 0.0F && value.color.g >= 0.0F &&
           value.color.b >= 0.0F && std::isfinite(value.opacity) && value.opacity >= 0.0F &&
           value.opacity <= 1.0F;
}

CanvasModulateComponentUVE SanitizeCanvasModulateComponentUVE(
    const CanvasModulateComponentUVE& value) noexcept {
    CanvasModulateComponentUVE out = value;
    if (!std::isfinite(out.color.r) || !std::isfinite(out.color.g) ||
        !std::isfinite(out.color.b) || out.color.r < 0.0F || out.color.g < 0.0F ||
        out.color.b < 0.0F) {
        out.color = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.opacity)) {
        out.opacity = 1.0F;
    } else {
        out.opacity = std::clamp(out.opacity, 0.0F, 1.0F);
    }
    return out;
}

bool IsCanvasModulateObjectDefinitionValidUVE(
    const CanvasModulateObjectDefinitionUVE& value) noexcept {
    return IsCanvasModulateComponentValidUVE(value.modulate);
}

void ApplyCanvasModulateObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const CanvasModulateObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<CanvasModulateComponentUVE>(
        entity, SanitizeCanvasModulateComponentUVE(value.modulate));
}

CanvasModulateComponentUVE ResolveActiveCanvasModulateUVE(IEntityManagerUVE& entityManager) {
    CanvasModulateComponentUVE combined{};
    entityManager.ForEachUVE<CanvasModulateComponentUVE>(
        [&](const EntityUVE entity, const CanvasModulateComponentUVE& mod) {
            if (!mod.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsCanvasModulateComponentValidUVE(mod)) {
                return;
            }
            combined.color.r *= mod.color.r;
            combined.color.g *= mod.color.g;
            combined.color.b *= mod.color.b;
            combined.opacity *= mod.opacity;
        });
    return combined;
}

} // namespace UVE::Scene
