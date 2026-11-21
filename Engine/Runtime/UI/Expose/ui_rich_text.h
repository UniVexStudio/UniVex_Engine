// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui_text_component.h"
#include "vector3.h"

namespace UVE::UI {

/// One styled span emitted by `ParseRichTextSpansUVE`.
struct UIRichTextSpanUVE final {
    std::string text;
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
    float fontSize = 16.0F;
    bool bold = false;
    bool italic = false;
};

/// Parses inline markup (`[color=#RRGGBB]`, `[b]`, `[i]`, `[size=N]`, and closing `[/...]`) into
/// styled spans. Unrecognized or malformed tags are preserved verbatim as literal text.
[[nodiscard]] std::vector<UIRichTextSpanUVE> ParseRichTextSpansUVE(
    std::string_view markup, const Math::Vector3UVE& defaultColor, float defaultFontSize);

/// Decodes UTF-8 text into Unicode scalar codepoints (malformed bytes emit U+FFFD).
[[nodiscard]] std::vector<char32_t> DecodeUtf8CodepointsUVE(std::string_view utf8);

/// Encodes Unicode scalar codepoints back to UTF-8.
[[nodiscard]] std::string EncodeUtf8CodepointsUVE(const std::vector<char32_t>& codepoints);

/// True when `cp` belongs to a strong right-to-left script (Hebrew, Arabic, Syriac, Thaana).
[[nodiscard]] bool IsRtlCodepointUVE(char32_t cp) noexcept;

/// Reorders a UTF-8 line into visual display order according to `direction` (`Auto` inspects
/// the first strong directional codepoint; RTL lines reverse runs while keeping embedded LTR
/// numbers/words readable left-to-right).
[[nodiscard]] std::string ResolveVisualTextOrderUVE(std::string_view utf8,
                                                    Scene::UITextDirectionUVE direction);

/// Prioritized Unicode font fallback chain mapping any codepoint to a registered font face index.
struct UIFontFallbackRangeUVE final {
    std::string fontName;
    char32_t firstCodepoint = 0U;
    char32_t lastCodepoint = 0x7FU;
};

class UIFontFallbackChainUVE final {
public:
    UIFontFallbackChainUVE();

    void AddFallbackFontUVE(std::string fontName, char32_t firstCodepoint, char32_t lastCodepoint);
    /// Returns the index of the first matching fallback font entry, or 0 (primary) if none matched.
    [[nodiscard]] std::size_t ResolveFontIndexForCodepointUVE(char32_t cp) const noexcept;
    [[nodiscard]] const std::vector<UIFontFallbackRangeUVE>& EntriesUVE() const noexcept {
        return m_entries;
    }

private:
    std::vector<UIFontFallbackRangeUVE> m_entries;
};

} // namespace UVE::UI
