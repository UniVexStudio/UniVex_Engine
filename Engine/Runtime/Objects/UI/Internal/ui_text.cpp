// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_text.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsUITextObjectDefinitionValidUVE(const UITextObjectDefinitionUVE& value) noexcept {
    return IsUITextComponentValidUVE(value.text);
}

void ApplyUITextObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const UITextObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UITextComponentUVE>(entity, value.text);
}

bool UITextUVE::HasTextUVE(const UITextComponentUVE& text) noexcept {
    return IsUITextComponentValidUVE(text) && !text.text.empty();
}

Math::Vector2UVE UITextUVE::BaselineCursorPixelsUVE(const UITextComponentUVE& text) noexcept {
    return Math::Vector2UVE{text.positionPixels.x, text.positionPixels.y + text.fontSize};
}

Math::RectUVE UITextUVE::ResolveBoundingRectUVE(
    const UITextComponentUVE& text, const float measuredWidthPixels) noexcept {
    const float safeWidth =
        std::isfinite(measuredWidthPixels) ? std::max(0.0F, measuredWidthPixels) : 0.0F;
    const float safeHeight = std::isfinite(text.fontSize) ? ClampUIFontSizeUVE(text.fontSize) : 0.0F;
    return Math::RectUVE{text.positionPixels, Math::Vector2UVE{safeWidth, safeHeight}};
}

namespace {
/// The height of a capital, in font sizes, for the UI font.
constexpr float kCapHeightUVE = 0.72F;
} // namespace

std::vector<UITextUVE::LineUVE> UITextUVE::LayoutLinesUVE(const UITextComponentUVE& text, const std::string_view shown,
                                                           const Math::RectUVE& box,
                                                           const std::function<float(std::string_view)>& measure) {
    const float size = ClampUIFontSizeUVE(text.fontSize);
    const float step = size * (std::isfinite(text.lineHeight) && text.lineHeight > 0.0F ? text.lineHeight : 1.25F);
    const bool boxed = box.size.x > 0.0F && box.size.y > 0.0F;
    const float limit = boxed && text.wrap ? box.size.x : 0.0F;
    std::vector<LineUVE> lines;
    std::size_t start = 0U;
    while (start <= shown.size()) {
        const std::size_t end = shown.find('\n', start);
        const std::string_view paragraph = shown.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (limit > 0.0F && measure(paragraph) > limit) {
            // Words onto the line while they fit; a word wider than the box gets a line of its own.
            std::string current;
            std::size_t at = 0U;
            while (at < paragraph.size()) {
                std::size_t space = paragraph.find(' ', at);
                if (space == std::string_view::npos) {
                    space = paragraph.size();
                }
                const std::string_view word = paragraph.substr(at, space - at);
                const std::string candidate = current.empty() ? std::string(word) : current + " " + std::string(word);
                if (!current.empty() && measure(candidate) > limit) {
                    lines.push_back(LineUVE{current, {}, 0.0F});
                    current = std::string(word);
                } else {
                    current = candidate;
                }
                at = space + 1U;
            }
            lines.push_back(LineUVE{current, {}, 0.0F});
        } else {
            lines.push_back(LineUVE{std::string(paragraph), {}, 0.0F});
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    // In a box the capitals are what is centred (the height of an "H"); without one the first
    // baseline sits a font size below the position, as it always has.
    const float firstLine = boxed ? size * kCapHeightUVE : size;
    const float blockHeight = firstLine + step * static_cast<float>(lines.size() > 0U ? lines.size() - 1U : 0U);
    const Math::Vector2UVE origin = boxed ? box.position : text.positionPixels;
    float top = origin.y;
    if (boxed) {
        const float slack = box.size.y - blockHeight;
        top += text.verticalAlign == UITextVerticalAlignUVE::Middle ? slack * 0.5F
             : text.verticalAlign == UITextVerticalAlignUVE::Bottom ? slack
                                                                     : 0.0F;
    }
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        LineUVE& line = lines[index];
        line.width = measure(line.text);
        float x = origin.x;
        if (boxed) {
            const float slack = box.size.x - line.width;
            x += text.horizontalAlign == UITextHorizontalAlignUVE::Center ? slack * 0.5F
               : text.horizontalAlign == UITextHorizontalAlignUVE::Right  ? slack
                                                                           : 0.0F;
        }
        line.baseline = Math::Vector2UVE{x, top + firstLine + step * static_cast<float>(index)};
    }
    return lines;
}

} // namespace UVE::Scene
