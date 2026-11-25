// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "canvas_component.h"
#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Canvas scene object. The record lives in CanvasComponent;
/// UIRuntime reads that component for visibility and sort order.
struct CanvasObjectDefinition final {
    static constexpr std::string_view typeName = "Canvas";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "Canvas" component label exactly, so both entry points feel identical.
    static constexpr std::string_view defaultName = "Canvas";

    /// Canvas authored defaults (visible, sort order 0); the entity's transform is attached by
    /// the creation shell.
    CanvasComponent canvas{};

    [[nodiscard]] bool operator==(const CanvasObjectDefinition&) const = default;
};

[[nodiscard]] bool IsCanvasObjectDefinitionValid(const CanvasObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyCanvasObjectDefinition(IEntityManager& entityManager, Entity entity,
                                    const CanvasObjectDefinition& value);

/// Node policy for a `Canvas` UI root object.
///
/// Centralises canvas visibility and sort-order comparisons consumed by `ResolveCanvasAncestry`
/// and UI draw-order ranking.
class Canvas final {
public:
    Canvas() = delete;

    /// True when `canvas` is valid and its authored `visible` flag is true.
    [[nodiscard]] static bool IsCanvasVisible(const CanvasComponent& canvas) noexcept;

    /// True when `entity` is alive, carries a visible `CanvasComponent`, and is not hidden via
    /// `VisibilityComponent::visibleInHierarchy`.
    [[nodiscard]] static bool IsEntityCanvasVisible(
        const IEntityManager& entityManager, Entity entity) noexcept;

    /// Returns `canvas.sortOrder` when valid, or `0` otherwise.
    [[nodiscard]] static std::int32_t EffectiveSortOrder(
        const CanvasComponent& canvas) noexcept;

    /// True when `lhs` draws strictly before `rhs` by `sortOrder`.
    [[nodiscard]] static bool RendersBefore(
        const CanvasComponent& lhs, const CanvasComponent& rhs) noexcept;
};

} // namespace UVE::Scene
