// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_text_component.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Scene {

[[nodiscard]] bool IsUITextComponentValidUVE(const UITextComponentUVE& component) noexcept {
    return component.text.size() <= kMaximumUITextBytesUVE &&
           component.text.find('\0') == std::string::npos &&
           std::isfinite(component.positionPixels.x) && std::isfinite(component.positionPixels.y) &&
           std::isfinite(component.fontSize) && component.fontSize >= kMinimumUIFontSizeUVE &&
           component.fontSize <= kMaximumUIFontSizeUVE && std::isfinite(component.color.x) &&
           std::isfinite(component.color.y) && std::isfinite(component.color.z) &&
           std::isfinite(component.alpha) && component.alpha >= 0.0F && component.alpha <= 1.0F &&
           std::isfinite(component.boxSize.x) && std::isfinite(component.boxSize.y) && component.boxSize.x >= 0.0F &&
           component.boxSize.y >= 0.0F && static_cast<std::uint8_t>(component.horizontalAlign) <= 2U &&
           static_cast<std::uint8_t>(component.verticalAlign) <= 2U && std::isfinite(component.lineHeight) &&
           component.lineHeight > 0.0F && std::isfinite(component.shadowAlpha) && component.shadowAlpha >= 0.0F &&
           component.shadowAlpha <= 1.0F && std::isfinite(component.outlineWidth) && component.outlineWidth >= 0.0F;
}

float ClampUIFontSizeUVE(const float fontSize) noexcept {
    if (!std::isfinite(fontSize)) {
        return kDefaultUIFontSizeUVE;
    }
    return std::clamp(fontSize, kMinimumUIFontSizeUVE, kMaximumUIFontSizeUVE);
}

UITextComponentUVE SanitizeUITextComponentUVE(const UITextComponentUVE& component) {
    UITextComponentUVE out = component;
    std::string cleaned;
    cleaned.reserve(std::min(out.text.size(), kMaximumUITextBytesUVE));
    for (const char ch : out.text) {
        if (ch == '\0') {
            continue;
        }
        cleaned.push_back(ch);
        if (cleaned.size() >= kMaximumUITextBytesUVE) {
            break;
        }
    }
    out.text = std::move(cleaned);
    if (!std::isfinite(out.positionPixels.x) || !std::isfinite(out.positionPixels.y)) {
        out.positionPixels = Math::Vector2UVE{0.0F, 0.0F};
    }
    out.fontSize = ClampUIFontSizeUVE(out.fontSize);
    if (!std::isfinite(out.color.x) || !std::isfinite(out.color.y) || !std::isfinite(out.color.z)) {
        out.color = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    } else {
        out.color.x = std::clamp(out.color.x, 0.0F, 1.0F);
        out.color.y = std::clamp(out.color.y, 0.0F, 1.0F);
        out.color.z = std::clamp(out.color.z, 0.0F, 1.0F);
    }
    out.alpha = std::isfinite(out.alpha) ? std::clamp(out.alpha, 0.0F, 1.0F) : 1.0F;
    if (!std::isfinite(out.boxSize.x) || !std::isfinite(out.boxSize.y)) {
        out.boxSize = Math::Vector2UVE{};
    }
    out.boxSize.x = std::max(0.0F, out.boxSize.x);
    out.boxSize.y = std::max(0.0F, out.boxSize.y);
    if (static_cast<std::uint8_t>(out.horizontalAlign) > 2U) {
        out.horizontalAlign = UITextHorizontalAlignUVE::Left;
    }
    if (static_cast<std::uint8_t>(out.verticalAlign) > 2U) {
        out.verticalAlign = UITextVerticalAlignUVE::Top;
    }
    out.lineHeight = std::isfinite(out.lineHeight) && out.lineHeight > 0.0F ? std::clamp(out.lineHeight, 0.5F, 4.0F) : 1.25F;
    out.shadowAlpha = std::isfinite(out.shadowAlpha) ? std::clamp(out.shadowAlpha, 0.0F, 1.0F) : 0.0F;
    out.outlineWidth = std::isfinite(out.outlineWidth) ? std::clamp(out.outlineWidth, 0.0F, 8.0F) : 0.0F;
    return out;
}

bool IsUITextVisibleForDrawUVE(const UITextComponentUVE& component) noexcept {
    return !component.text.empty() && std::isfinite(component.alpha) && component.alpha > 0.0F &&
           std::isfinite(component.fontSize) && component.fontSize >= kMinimumUIFontSizeUVE;
}

std::size_t CountUITextLinesUVE(const UITextComponentUVE& component) noexcept {
    if (component.text.empty()) {
        return 0U;
    }
    std::size_t lines = 1U;
    for (const char ch : component.text) {
        if (ch == '\n') {
            ++lines;
        }
    }
    return lines;
}

Math::Vector2UVE EstimateUITextBoundsSizePixelsUVE(const UITextComponentUVE& component,
                                                   const float glyphAdvanceFactor,
                                                   const float lineHeightFactor) noexcept {
    if (component.text.empty()) {
        return Math::Vector2UVE{0.0F, 0.0F};
    }
    const float safeFontSize = ClampUIFontSizeUVE(component.fontSize);
    const float safeAdvance =
        (std::isfinite(glyphAdvanceFactor) && glyphAdvanceFactor > 0.0F) ? glyphAdvanceFactor : 0.6F;
    const float safeLineHeight =
        (std::isfinite(lineHeightFactor) && lineHeightFactor > 0.0F) ? lineHeightFactor : 1.25F;
    std::size_t maxLineChars = 0U;
    std::size_t currentLineChars = 0U;
    std::size_t lines = 1U;
    for (const char ch : component.text) {
        if (ch == '\0' || ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            maxLineChars = std::max(maxLineChars, currentLineChars);
            currentLineChars = 0U;
            ++lines;
            continue;
        }
        ++currentLineChars;
    }
    maxLineChars = std::max(maxLineChars, currentLineChars);
    return Math::Vector2UVE{
        static_cast<float>(maxLineChars) * safeFontSize * safeAdvance,
        static_cast<float>(lines) * safeFontSize * safeLineHeight,
    };
}

} // namespace UVE::Scene
