// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "ui_text_component.h"
#include "rect.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the UI Text scene object (Canvas family): the component set and
/// defaults a freshly created UI Text entity attaches — one empty text line at the origin in
/// raw window-pixel coordinates.
struct UITextObjectDefinition final {
    static constexpr std::string_view typeName = "UIText";
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "UI Text" component label exactly.
    static constexpr std::string_view defaultName = "UI Text";

    /// Text authored defaults (empty text, 16px, white, opaque); the entity's transform is
    /// attached by the creation shell.
    UITextComponent text{};

    [[nodiscard]] bool operator==(const UITextObjectDefinition&) const = default;
};

[[nodiscard]] bool IsUITextObjectDefinitionValid(const UITextObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUITextObjectDefinition(IEntityManager& entityManager, Entity entity,
                                    const UITextObjectDefinition& value);

/// Node policy for a `UIText` widget object.
///
/// Owns baseline cursor placement and bounding-box calculation shared by `UIRuntime` and
/// UI layout helpers.
class UIText final {
public:
    UIText() = delete;

    /// True when `text` is valid and carries a non-empty string.
    [[nodiscard]] static bool HasText(const UITextComponent& text) noexcept;

    /// Returns the baseline origin `{positionPixels.x, positionPixels.y + fontSize}` where glyph
    /// emission starts in window-pixel coordinates.
    [[nodiscard]] static Math::Vector2 BaselineCursorPixels(
        const UITextComponent& text) noexcept;

    /// Builds the screen-space bounding rectangle of `text` for a measured advance width in pixels.
    [[nodiscard]] static Math::Rect ResolveBoundingRect(
        const UITextComponent& text, float measuredWidthPixels) noexcept;

    /// One laid-out line: its text and the pen position of its baseline start.
    struct Line final {
        std::string text;
        Math::Vector2 baseline{};
        float width = 0.0F;
    };

    /// Lays `shown` out in `box` (or from the position when `box` is empty): lines split at
    /// newlines, and at spaces when `wrap` is on and a line is wider than the box, each placed by
    /// the horizontal and vertical alignment. `measure` gives a string's width at the text's size,
    /// so the runtime and the editor's designer place the same text the same way.
    [[nodiscard]] static std::vector<Line> LayoutLines(const UITextComponent& text, std::string_view shown,
                                                            const Math::Rect& box,
                                                            const std::function<float(std::string_view)>& measure);
};

} // namespace UVE::Scene
