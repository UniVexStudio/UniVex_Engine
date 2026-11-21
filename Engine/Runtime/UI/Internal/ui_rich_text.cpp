// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_rich_text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace UVE::UI {
namespace {

[[nodiscard]] int HexValUVE(const char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

[[nodiscard]] bool TryParseHexColorUVE(const std::string_view hex, Math::Vector3UVE& outColor) noexcept {
    if (hex.size() != 7U || hex[0] != '#') {
        return false;
    }
    int d[6];
    for (int i = 0; i < 6; ++i) {
        d[i] = HexValUVE(hex[static_cast<std::size_t>(i + 1)]);
        if (d[i] < 0) return false;
    }
    outColor = Math::Vector3UVE{
        static_cast<float>((d[0] << 4) | d[1]) / 255.0F,
        static_cast<float>((d[2] << 4) | d[3]) / 255.0F,
        static_cast<float>((d[4] << 4) | d[5]) / 255.0F,
    };
    return true;
}

[[nodiscard]] bool IsStrongLtrUVE(const char32_t cp) noexcept {
    return (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z') ||
           (cp >= 0x00C0U && cp <= 0x02AFU) || (cp >= 0x0370U && cp <= 0x052FU);
}

[[nodiscard]] bool IsAsciiDigitOrLtrRunUVE(const char32_t cp) noexcept {
    return IsStrongLtrUVE(cp) || (cp >= U'0' && cp <= U'9');
}

} // namespace

std::vector<UIRichTextSpanUVE> ParseRichTextSpansUVE(const std::string_view markup,
                                                     const Math::Vector3UVE& defaultColor,
                                                     const float defaultFontSize) {
    std::vector<UIRichTextSpanUVE> spans;
    std::vector<Math::Vector3UVE> colorStack{defaultColor};
    std::vector<float> sizeStack{defaultFontSize};
    int boldDepth = 0;
    int italicDepth = 0;

    std::string current;
    const auto flush = [&]() {
        if (!current.empty()) {
            spans.push_back(UIRichTextSpanUVE{
                std::move(current),
                colorStack.back(),
                sizeStack.back(),
                boldDepth > 0,
                italicDepth > 0,
            });
            current.clear();
        }
    };

    std::size_t i = 0U;
    while (i < markup.size()) {
        if (markup[i] == '[') {
            const std::size_t close = markup.find(']', i + 1U);
            if (close != std::string_view::npos) {
                const std::string_view tag = markup.substr(i + 1U, close - i - 1U);
                bool recognized = false;
                if (tag == "b") {
                    flush();
                    ++boldDepth;
                    recognized = true;
                } else if (tag == "/b") {
                    flush();
                    if (boldDepth > 0) --boldDepth;
                    recognized = true;
                } else if (tag == "i") {
                    flush();
                    ++italicDepth;
                    recognized = true;
                } else if (tag == "/i") {
                    flush();
                    if (italicDepth > 0) --italicDepth;
                    recognized = true;
                } else if (tag == "/color") {
                    flush();
                    if (colorStack.size() > 1U) colorStack.pop_back();
                    recognized = true;
                } else if (tag == "/size") {
                    flush();
                    if (sizeStack.size() > 1U) sizeStack.pop_back();
                    recognized = true;
                } else if (tag.rfind("color=", 0U) == 0U) {
                    Math::Vector3UVE parsed{};
                    if (TryParseHexColorUVE(tag.substr(6U), parsed)) {
                        flush();
                        colorStack.push_back(parsed);
                        recognized = true;
                    }
                } else if (tag.rfind("size=", 0U) == 0U) {
                    const std::string numStr(tag.substr(5U));
                    char* end = nullptr;
                    const float sz = std::strtof(numStr.c_str(), &end);
                    if (end != numStr.c_str() && sz >= Scene::kMinimumUIFontSizeUVE &&
                        sz <= Scene::kMaximumUIFontSizeUVE) {
                        flush();
                        sizeStack.push_back(sz);
                        recognized = true;
                    }
                }
                if (recognized) {
                    i = close + 1U;
                    continue;
                }
            }
        }
        current.push_back(markup[i]);
        ++i;
    }
    flush();
    return spans;
}

std::vector<char32_t> DecodeUtf8CodepointsUVE(const std::string_view utf8) {
    std::vector<char32_t> out;
    out.reserve(utf8.size());
    std::size_t i = 0U;
    while (i < utf8.size()) {
        const auto c0 = static_cast<unsigned char>(utf8[i]);
        if (c0 < 0x80U) {
            out.push_back(static_cast<char32_t>(c0));
            ++i;
        } else if ((c0 & 0xE0U) == 0xC0U && i + 1U < utf8.size()) {
            const auto c1 = static_cast<unsigned char>(utf8[i + 1U]);
            out.push_back((static_cast<char32_t>(c0 & 0x1FU) << 6U) | static_cast<char32_t>(c1 & 0x3FU));
            i += 2U;
        } else if ((c0 & 0xF0U) == 0xE0U && i + 2U < utf8.size()) {
            const auto c1 = static_cast<unsigned char>(utf8[i + 1U]);
            const auto c2 = static_cast<unsigned char>(utf8[i + 2U]);
            out.push_back((static_cast<char32_t>(c0 & 0x0FU) << 12U) |
                          (static_cast<char32_t>(c1 & 0x3FU) << 6U) | static_cast<char32_t>(c2 & 0x3FU));
            i += 3U;
        } else if ((c0 & 0xF8U) == 0xF0U && i + 3U < utf8.size()) {
            const auto c1 = static_cast<unsigned char>(utf8[i + 1U]);
            const auto c2 = static_cast<unsigned char>(utf8[i + 2U]);
            const auto c3 = static_cast<unsigned char>(utf8[i + 3U]);
            out.push_back((static_cast<char32_t>(c0 & 0x07U) << 18U) |
                          (static_cast<char32_t>(c1 & 0x3FU) << 12U) |
                          (static_cast<char32_t>(c2 & 0x3FU) << 6U) | static_cast<char32_t>(c3 & 0x3FU));
            i += 4U;
        } else {
            out.push_back(0xFFFDU);
            ++i;
        }
    }
    return out;
}

std::string EncodeUtf8CodepointsUVE(const std::vector<char32_t>& codepoints) {
    std::string out;
    out.reserve(codepoints.size());
    for (const char32_t cp : codepoints) {
        if (cp < 0x80U) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800U) {
            out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else if (cp < 0x10000U) {
            out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        }
    }
    return out;
}

bool IsRtlCodepointUVE(const char32_t cp) noexcept {
    return (cp >= 0x0590U && cp <= 0x05FFU) || // Hebrew
           (cp >= 0x0600U && cp <= 0x06FFU) || // Arabic
           (cp >= 0x0750U && cp <= 0x077FU) || // Arabic Supplement
           (cp >= 0x08A0U && cp <= 0x08FFU) || // Arabic Extended-A
           (cp >= 0xFB50U && cp <= 0xFDFFU) || // Arabic Presentation Forms-A
           (cp >= 0xFE70U && cp <= 0xFEFFU);   // Arabic Presentation Forms-B
}

std::string ResolveVisualTextOrderUVE(const std::string_view utf8,
                                      const Scene::UITextDirectionUVE direction) {
    if (utf8.empty()) {
        return {};
    }
    std::vector<char32_t> cps = DecodeUtf8CodepointsUVE(utf8);
    bool useRtl = (direction == Scene::UITextDirectionUVE::RightToLeft);
    if (direction == Scene::UITextDirectionUVE::Auto) {
        for (const char32_t cp : cps) {
            if (IsRtlCodepointUVE(cp)) {
                useRtl = true;
                break;
            }
            if (IsStrongLtrUVE(cp)) {
                useRtl = false;
                break;
            }
        }
    }
    if (!useRtl) {
        return std::string(utf8);
    }

    // Reverse full line visually, then restore embedded LTR/numeric runs to left-to-right order.
    std::reverse(cps.begin(), cps.end());
    std::size_t i = 0U;
    while (i < cps.size()) {
        if (IsAsciiDigitOrLtrRunUVE(cps[i])) {
            std::size_t j = i + 1U;
            while (j < cps.size() && IsAsciiDigitOrLtrRunUVE(cps[j])) {
                ++j;
            }
            std::reverse(cps.begin() + static_cast<std::ptrdiff_t>(i),
                         cps.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        } else {
            ++i;
        }
    }
    return EncodeUtf8CodepointsUVE(cps);
}

UIFontFallbackChainUVE::UIFontFallbackChainUVE() {
    m_entries = {
        {"EmbeddedLatin", 0x0000U, 0x024FU},
        {"FallbackGreekCyrillic", 0x0370U, 0x052FU},
        {"FallbackHebrewArabic", 0x0590U, 0x08FFU},
        {"FallbackCjk", 0x3000U, 0x9FFFU},
        {"FallbackHangul", 0xAC00U, 0xD7AFU},
    };
}

void UIFontFallbackChainUVE::AddFallbackFontUVE(std::string fontName,
                                                const char32_t firstCodepoint,
                                                const char32_t lastCodepoint) {
    m_entries.push_back(UIFontFallbackRangeUVE{std::move(fontName), firstCodepoint, lastCodepoint});
}

std::size_t UIFontFallbackChainUVE::ResolveFontIndexForCodepointUVE(const char32_t cp) const noexcept {
    for (std::size_t i = 0U; i < m_entries.size(); ++i) {
        if (cp >= m_entries[i].firstCodepoint && cp <= m_entries[i].lastCodepoint) {
            return i;
        }
    }
    return 0U;
}

} // namespace UVE::UI
