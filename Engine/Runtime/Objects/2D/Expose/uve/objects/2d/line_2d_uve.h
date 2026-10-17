// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Segmented 2D polyline (`points`, `widthPixels`, `defaultColor`, `opacity`, `closed`).
struct Line2DComponentUVE final {
    std::vector<Math::Vector2UVE> points{};
    float widthPixels = 4.0F;
    Math::ColorUVE defaultColor{1.0F, 1.0F, 1.0F};
    float opacity = 1.0F;
    bool closed = false;

    [[nodiscard]] bool operator==(const Line2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsLine2DComponentValidUVE(const Line2DComponentUVE& value) noexcept;

[[nodiscard]] Line2DComponentUVE SanitizeLine2DComponentUVE(const Line2DComponentUVE& value);

struct Line2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Line2D";
    static constexpr std::string_view defaultName = "Line2D";
    Line2DComponentUVE line{};

    [[nodiscard]] bool operator==(const Line2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsLine2DObjectDefinitionValidUVE(const Line2DObjectDefinitionUVE& value) noexcept;

void ApplyLine2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Line2DObjectDefinitionUVE& value);

class Line2DUVE final {
public:
    Line2DUVE() = delete;

    /// Returns the number of segments (`points.size() - 1`, or `points.size()` when `closed` and
    /// `points.size() >= 3`).
    [[nodiscard]] static std::size_t ResolveSegmentCountUVE(const Line2DComponentUVE& line) noexcept;

    /// Computes the local-space bounding rectangle expanded by `widthPixels * 0.5F`.
    [[nodiscard]] static Math::RectUVE ComputeLocalBoundingRectUVE(
        const Line2DComponentUVE& line) noexcept;
};

/// Filled 2D polygon (`vertices`, `color`, `opacity`).
struct Polygon2DComponentUVE final {
    std::vector<Math::Vector2UVE> vertices{};
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float opacity = 1.0F;

    [[nodiscard]] bool operator==(const Polygon2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPolygon2DComponentValidUVE(const Polygon2DComponentUVE& value) noexcept;

[[nodiscard]] Polygon2DComponentUVE SanitizePolygon2DComponentUVE(
    const Polygon2DComponentUVE& value);

struct Polygon2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Polygon2D";
    static constexpr std::string_view defaultName = "Polygon2D";
    Polygon2DComponentUVE polygon{};

    [[nodiscard]] bool operator==(const Polygon2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsPolygon2DObjectDefinitionValidUVE(
    const Polygon2DObjectDefinitionUVE& value) noexcept;

void ApplyPolygon2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Polygon2DObjectDefinitionUVE& value);

class Polygon2DUVE final {
public:
    Polygon2DUVE() = delete;

    /// Computes the signed shoelace area of `polygon.vertices` (positive when counter-clockwise).
    [[nodiscard]] static float ComputeSignedAreaUVE(const Polygon2DComponentUVE& polygon) noexcept;

    /// Point-in-polygon even-odd ray-casting test for `localPoint`.
    [[nodiscard]] static bool ContainsLocalPointUVE(const Polygon2DComponentUVE& polygon,
                                                    const Math::Vector2UVE& localPoint) noexcept;
};

/// Global color and alpha multiplier (`color`, `opacity`, `enabled`) for a 2D scene/canvas.
struct CanvasModulateComponentUVE final {
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float opacity = 1.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const CanvasModulateComponentUVE&) const = default;
};

[[nodiscard]] bool IsCanvasModulateComponentValidUVE(
    const CanvasModulateComponentUVE& value) noexcept;

[[nodiscard]] CanvasModulateComponentUVE SanitizeCanvasModulateComponentUVE(
    const CanvasModulateComponentUVE& value) noexcept;

struct CanvasModulateObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "CanvasModulate";
    static constexpr std::string_view defaultName = "CanvasModulate";
    CanvasModulateComponentUVE modulate{};

    [[nodiscard]] bool operator==(const CanvasModulateObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsCanvasModulateObjectDefinitionValidUVE(
    const CanvasModulateObjectDefinitionUVE& value) noexcept;

void ApplyCanvasModulateObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const CanvasModulateObjectDefinitionUVE& value);

/// Combines all enabled `CanvasModulateComponentUVE` entities by component-wise multiplication.
[[nodiscard]] CanvasModulateComponentUVE ResolveActiveCanvasModulateUVE(
    IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
