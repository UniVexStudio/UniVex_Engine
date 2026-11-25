// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumUITextBytes = 4096U;
inline constexpr float kMinimumUIFontSize = 1.0F;
inline constexpr float kMaximumUIFontSize = 512.0F;
inline constexpr float kDefaultUIFontSize = 16.0F;

/// Where text sits across its box.
enum class UITextHorizontalAlign : std::uint8_t { Left = 0, Center = 1, Right = 2 };
/// Where the lines sit down their box.
enum class UITextVerticalAlign : std::uint8_t { Top = 0, Middle = 1, Bottom = 2 };

/// Text layout direction for BiDi / right-to-left localization.
enum class UITextDirection : std::uint8_t { Auto = 0, LeftToRight = 1, RightToLeft = 2 };

/// A single line of screen-space UI text, anchored at `positionPixels` (raw window pixel
/// coordinates, top-left origin - the same convention IInputSystem::GetMousePosition()
/// already uses, so no unprojection is ever needed anywhere in this system). Rendered by
/// UIRuntime/Renderer3D's "UIOverlay" pass against a baked bitmap-font glyph atlas.
/// Thread-safety: value type; trivially safe to copy/move.
struct UITextComponent final {
    std::string text;
    Math::Vector2 positionPixels{};
    float fontSize = kDefaultUIFontSize;
    Math::Vector3 color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    /// The box the text is laid out in, from `positionPixels`. Zero keeps the old behaviour: the
    /// first line starts at the position, left and top.
    Math::Vector2 boxSize{};
    /// The box is the parent widget's rect (a button's label stays centred on its button).
    bool fillParent = false;
    UITextHorizontalAlign horizontalAlign = UITextHorizontalAlign::Left;
    UITextVerticalAlign verticalAlign = UITextVerticalAlign::Top;
    /// Long lines break at spaces to stay inside the box's width.
    bool wrap = false;
    /// Enables inline rich-text markup (`[color=#RRGGBB]`, `[b]`, `[i]`, `[size=N]`).
    bool richText = false;
    /// Text reading direction (`Auto` detects Arabic/Hebrew RTL scripts automatically).
    UITextDirection textDirection = UITextDirection::Auto;
    /// Distance between lines, in font sizes.
    float lineHeight = 1.25F;
    Math::Vector3 shadowColor{0.0F, 0.0F, 0.0F};
    /// 0 draws no shadow.
    float shadowAlpha = 0.0F;
    Math::Vector2 shadowOffset{2.0F, 2.0F};
    Math::Vector3 outlineColor{0.0F, 0.0F, 0.0F};
    /// Pixels; 0 draws no outline.
    float outlineWidth = 0.0F;
    [[nodiscard]] bool operator==(const UITextComponent&) const = default;
};

[[nodiscard]] bool IsUITextComponentValid(const UITextComponent& component) noexcept;

/// Clamps `fontSize` into `[kMinimumUIFontSize, kMaximumUIFontSize]`.
[[nodiscard]] float ClampUIFontSize(float fontSize) noexcept;

/// Sanitizes `component` so `text`, `positionPixels`, `fontSize`, `color`, and `alpha` are valid.
[[nodiscard]] UITextComponent SanitizeUITextComponent(const UITextComponent& component);

/// True when `component` has non-empty text, positive alpha, and a valid font size.
[[nodiscard]] bool IsUITextVisibleForDraw(const UITextComponent& component) noexcept;

/// Counts the number of newline-delimited lines in `component.text` (`0` when empty).
[[nodiscard]] std::size_t CountUITextLines(const UITextComponent& component) noexcept;

/// Estimates the pixel bounding size of `component.text` using `fontSize` and glyph metrics.
[[nodiscard]] Math::Vector2 EstimateUITextBoundsSizePixels(
    const UITextComponent& component, float glyphAdvanceFactor = 0.6F,
    float lineHeightFactor = 1.25F) noexcept;

} // namespace UVE::Scene
