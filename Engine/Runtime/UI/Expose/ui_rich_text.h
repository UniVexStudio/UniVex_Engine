// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ui_text_component.h"
#include "vector3.h"

namespace UVE::UI {

/// One styled span emitted by `ParseRichTextSpans`.
struct UIRichTextSpan final {
    std::string text;
    Math::Vector3 color{1.0F, 1.0F, 1.0F};
    float fontSize = 16.0F;
    bool bold = false;
    bool italic = false;
};

/// Parses inline markup (`[color=#RRGGBB]`, `[b]`, `[i]`, `[size=N]`, and closing `[/...]`) into
/// styled spans. Unrecognized or malformed tags are preserved verbatim as literal text.
[[nodiscard]] std::vector<UIRichTextSpan> ParseRichTextSpans(
    std::string_view markup, const Math::Vector3& defaultColor, float defaultFontSize);

/// Decodes UTF-8 text into Unicode scalar codepoints (malformed bytes emit U+FFFD).
[[nodiscard]] std::vector<char32_t> DecodeUtf8Codepoints(std::string_view utf8);

/// Encodes Unicode scalar codepoints back to UTF-8.
[[nodiscard]] std::string EncodeUtf8Codepoints(const std::vector<char32_t>& codepoints);

/// True when `cp` belongs to a strong right-to-left script (Hebrew, Arabic, Syriac, Thaana).
[[nodiscard]] bool IsRtlCodepoint(char32_t cp) noexcept;

/// Reorders a UTF-8 line into visual display order according to `direction` (`Auto` inspects
/// the first strong directional codepoint; RTL lines reverse runs while keeping embedded LTR
/// numbers/words readable left-to-right).
[[nodiscard]] std::string ResolveVisualTextOrder(std::string_view utf8,
                                                    Scene::UITextDirection direction);

/// Prioritized Unicode font fallback chain mapping any codepoint to a registered font face index.
struct UIFontFallbackRange final {
    std::string fontName;
    char32_t firstCodepoint = 0U;
    char32_t lastCodepoint = 0x7FU;
};

class UIFontFallbackChain final {
public:
    UIFontFallbackChain();

    void AddFallbackFont(std::string fontName, char32_t firstCodepoint, char32_t lastCodepoint);
    /// Returns the index of the first matching fallback font entry, or 0 (primary) if none matched.
    [[nodiscard]] std::size_t ResolveFontIndexForCodepoint(char32_t cp) const noexcept;
    [[nodiscard]] const std::vector<UIFontFallbackRange>& Entries() const noexcept {
        return m_entries;
    }

private:
    std::vector<UIFontFallbackRange> m_entries;
};

} // namespace UVE::UI
