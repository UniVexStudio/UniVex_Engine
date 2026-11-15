// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumUITextBytesUVE = 512U;
inline constexpr float kMinimumUIFontSizeUVE = 1.0F;
inline constexpr float kMaximumUIFontSizeUVE = 512.0F;
inline constexpr float kDefaultUIFontSizeUVE = 16.0F;

/// Where text sits across its box.
enum class UITextHorizontalAlignUVE : std::uint8_t { Left = 0, Center = 1, Right = 2 };
/// Where the lines sit down their box.
enum class UITextVerticalAlignUVE : std::uint8_t { Top = 0, Middle = 1, Bottom = 2 };

/// A single line of screen-space UI text, anchored at `positionPixels` (raw window pixel
/// coordinates, top-left origin - the same convention IInputSystemUVE::GetMousePositionUVE()
/// already uses, so no unprojection is ever needed anywhere in this system). Rendered by
/// UIRuntimeUVE/Renderer3DUVE's "UIOverlay" pass against a baked bitmap-font glyph atlas; only the
/// embedded font's own covered characters (Latin-1 subset) are guaranteed to render.
/// Thread-safety: value type; trivially safe to copy/move.
struct UITextComponentUVE final {
    std::string text;
    Math::Vector2UVE positionPixels{};
    float fontSize = kDefaultUIFontSizeUVE;
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    /// The box the text is laid out in, from `positionPixels`. Zero keeps the old behaviour: the
    /// first line starts at the position, left and top.
    Math::Vector2UVE boxSize{};
    /// The box is the parent widget's rect (a button's label stays centred on its button).
    bool fillParent = false;
    UITextHorizontalAlignUVE horizontalAlign = UITextHorizontalAlignUVE::Left;
    UITextVerticalAlignUVE verticalAlign = UITextVerticalAlignUVE::Top;
    /// Long lines break at spaces to stay inside the box's width.
    bool wrap = false;
    /// Distance between lines, in font sizes.
    float lineHeight = 1.25F;
    Math::Vector3UVE shadowColor{0.0F, 0.0F, 0.0F};
    /// 0 draws no shadow.
    float shadowAlpha = 0.0F;
    Math::Vector2UVE shadowOffset{2.0F, 2.0F};
    Math::Vector3UVE outlineColor{0.0F, 0.0F, 0.0F};
    /// Pixels; 0 draws no outline.
    float outlineWidth = 0.0F;
    [[nodiscard]] bool operator==(const UITextComponentUVE&) const = default;
};

[[nodiscard]] bool IsUITextComponentValidUVE(const UITextComponentUVE& component) noexcept;

/// Clamps `fontSize` into `[kMinimumUIFontSizeUVE, kMaximumUIFontSizeUVE]`.
[[nodiscard]] float ClampUIFontSizeUVE(float fontSize) noexcept;

/// Sanitizes `component` so `text`, `positionPixels`, `fontSize`, `color`, and `alpha` are valid.
[[nodiscard]] UITextComponentUVE SanitizeUITextComponentUVE(const UITextComponentUVE& component);

/// True when `component` has non-empty text, positive alpha, and a valid font size.
[[nodiscard]] bool IsUITextVisibleForDrawUVE(const UITextComponentUVE& component) noexcept;

/// Counts the number of newline-delimited lines in `component.text` (`0` when empty).
[[nodiscard]] std::size_t CountUITextLinesUVE(const UITextComponentUVE& component) noexcept;

/// Estimates the pixel bounding size of `component.text` using `fontSize` and glyph metrics.
[[nodiscard]] Math::Vector2UVE EstimateUITextBoundsSizePixelsUVE(
    const UITextComponentUVE& component, float glyphAdvanceFactor = 0.6F,
    float lineHeightFactor = 1.25F) noexcept;

} // namespace UVE::Scene
