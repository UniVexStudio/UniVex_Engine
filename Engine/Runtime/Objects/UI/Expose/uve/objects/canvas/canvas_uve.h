// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Canvas scene object. The record lives in CanvasComponentUVE;
/// UIRuntimeUVE reads that component for visibility and sort order.
struct CanvasObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Canvas";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "Canvas" component label exactly, so both entry points feel identical.
    static constexpr std::string_view defaultName = "Canvas";

    /// Canvas authored defaults (visible, sort order 0); the entity's transform is attached by
    /// the creation shell.
    CanvasComponentUVE canvas{};

    [[nodiscard]] bool operator==(const CanvasObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsCanvasObjectDefinitionValidUVE(const CanvasObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyCanvasObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const CanvasObjectDefinitionUVE& value);

/// Node policy for a `Canvas` UI root object.
///
/// Centralises canvas visibility and sort-order comparisons consumed by `ResolveCanvasAncestryUVE`
/// and UI draw-order ranking.
class CanvasUVE final {
public:
    CanvasUVE() = delete;

    /// True when `canvas` is valid and its authored `visible` flag is true.
    [[nodiscard]] static bool IsCanvasVisibleUVE(const CanvasComponentUVE& canvas) noexcept;

    /// True when `entity` is alive, carries a visible `CanvasComponentUVE`, and is not hidden via
    /// `VisibilityComponentUVE::visibleInHierarchy`.
    [[nodiscard]] static bool IsEntityCanvasVisibleUVE(
        const IEntityManagerUVE& entityManager, EntityUVE entity) noexcept;

    /// Returns `canvas.sortOrder` when valid, or `0` otherwise.
    [[nodiscard]] static std::int32_t EffectiveSortOrderUVE(
        const CanvasComponentUVE& canvas) noexcept;

    /// True when `lhs` draws strictly before `rhs` by `sortOrder`.
    [[nodiscard]] static bool RendersBeforeUVE(
        const CanvasComponentUVE& lhs, const CanvasComponentUVE& rhs) noexcept;
};

} // namespace UVE::Scene
