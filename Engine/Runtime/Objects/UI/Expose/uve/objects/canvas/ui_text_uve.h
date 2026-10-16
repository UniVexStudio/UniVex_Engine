// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/math/rect_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Text scene object (Canvas family): the component set and
/// defaults a freshly created UI Text entity attaches — one empty text line at the origin in
/// raw window-pixel coordinates.
struct UITextObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "UIText";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "UI Text" component label exactly.
    static constexpr std::string_view defaultName = "UI Text";

    /// Text authored defaults (empty text, 16px, white, opaque); the entity's transform is
    /// attached by the creation shell.
    UITextComponentUVE text{};

    [[nodiscard]] bool operator==(const UITextObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsUITextObjectDefinitionValidUVE(const UITextObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUITextObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const UITextObjectDefinitionUVE& value);

/// Node policy for a `UIText` widget object.
///
/// Owns baseline cursor placement and bounding-box calculation shared by `UIRuntimeUVE` and
/// UI layout helpers.
class UITextUVE final {
public:
    UITextUVE() = delete;

    /// True when `text` is valid and carries a non-empty string.
    [[nodiscard]] static bool HasTextUVE(const UITextComponentUVE& text) noexcept;

    /// Returns the baseline origin `{positionPixels.x, positionPixels.y + fontSize}` where glyph
    /// emission starts in window-pixel coordinates.
    [[nodiscard]] static Math::Vector2UVE BaselineCursorPixelsUVE(
        const UITextComponentUVE& text) noexcept;

    /// Builds the screen-space bounding rectangle of `text` for a measured advance width in pixels.
    [[nodiscard]] static Math::RectUVE ResolveBoundingRectUVE(
        const UITextComponentUVE& text, float measuredWidthPixels) noexcept;
};

} // namespace UVE::Scene
