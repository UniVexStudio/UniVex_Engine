// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_color_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace UVE::Editor {

namespace {

[[nodiscard]] int HexDigitValueUVE(const char digit) noexcept {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] int ToByteUVE(const float channel) noexcept {
    // NaN compares false both ways and would survive a clamp, so it is mapped to 0 explicitly.
    const float clamped = std::isnan(channel) ? 0.0F : std::clamp(channel, 0.0F, 1.0F);
    return static_cast<int>(std::lround(clamped * 255.0F));
}

} // namespace

std::optional<EditorColorUVE> ParseColorHexUVE(std::string_view text) {
    const auto isSpace = [](const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 3U && text.size() != 6U && text.size() != 8U) {
        return std::nullopt;
    }

    std::array<int, 8> digits{};
    for (std::size_t index = 0; index < text.size(); ++index) {
        digits[index] = HexDigitValueUVE(text[index]);
        if (digits[index] < 0) {
            return std::nullopt;
        }
    }

    std::array<int, 4> bytes{0, 0, 0, 255};
    if (text.size() == 3U) {
        // #RGB is shorthand for #RRGGBB: each digit is doubled, so F becomes FF, not F0.
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            bytes[channel] = digits[channel] * 17;
        }
    } else {
        for (std::size_t channel = 0; channel < text.size() / 2U; ++channel) {
            bytes[channel] = (digits[channel * 2U] * 16) + digits[(channel * 2U) + 1U];
        }
    }
    return EditorColorUVE{static_cast<float>(bytes[0]) / 255.0F, static_cast<float>(bytes[1]) / 255.0F,
                          static_cast<float>(bytes[2]) / 255.0F, static_cast<float>(bytes[3]) / 255.0F};
}

std::string FormatColorHexUVE(const EditorColorUVE& color, const bool includeAlpha) {
    std::array<char, 16> text{};
    if (includeAlpha) {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X%02X", ToByteUVE(color.r), ToByteUVE(color.g),
                      ToByteUVE(color.b), ToByteUVE(color.a));
    } else {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X", ToByteUVE(color.r), ToByteUVE(color.g),
                      ToByteUVE(color.b));
    }
    return std::string(text.data());
}

void PushRecentColorUVE(std::vector<EditorColorUVE>& recents, const EditorColorUVE& color) {
    const std::string key = FormatColorHexUVE(color, true);
    std::erase_if(recents, [&key](const EditorColorUVE& recent) { return FormatColorHexUVE(recent, true) == key; });
    recents.insert(recents.begin(), color);
    if (recents.size() > kMaxRecentColorsUVE) {
        recents.resize(kMaxRecentColorsUVE);
    }
}

bool AddSavedColorUVE(std::vector<EditorColorUVE>& saved, const EditorColorUVE& color) {
    if (saved.size() >= kMaxSavedColorsUVE) {
        return false;
    }
    const std::string key = FormatColorHexUVE(color, true);
    if (std::any_of(saved.begin(), saved.end(),
                    [&key](const EditorColorUVE& entry) { return FormatColorHexUVE(entry, true) == key; })) {
        return false;
    }
    saved.push_back(color);
    return true;
}

EditorHsvUVE RgbToHsvUVE(const EditorColorUVE& color, const EditorHsvUVE& previous) {
    const float maximum = std::max({color.r, color.g, color.b});
    const float minimum = std::min({color.r, color.g, color.b});
    const float chroma = maximum - minimum;
    EditorHsvUVE hsv{previous.h, previous.s, maximum};
    if (maximum <= 0.0F) {
        return hsv; // black: hue and saturation are whatever they were
    }
    hsv.s = chroma / maximum;
    if (chroma <= 0.0F) {
        return hsv; // a grey: saturation is zero and the hue is kept
    }
    float hue = 0.0F;
    if (maximum == color.r) {
        hue = (color.g - color.b) / chroma;
    } else if (maximum == color.g) {
        hue = 2.0F + ((color.b - color.r) / chroma);
    } else {
        hue = 4.0F + ((color.r - color.g) / chroma);
    }
    hue /= 6.0F;
    if (hue < 0.0F) {
        hue += 1.0F;
    }
    hsv.h = hue;
    return hsv;
}

EditorColorUVE HsvToRgbUVE(const EditorHsvUVE& hsv, const float alpha) {
    const float hue = (hsv.h - std::floor(hsv.h)) * 6.0F;
    const float saturation = std::clamp(hsv.s, 0.0F, 1.0F);
    const float value = std::clamp(hsv.v, 0.0F, 1.0F);
    const int sector = static_cast<int>(hue) % 6;
    const float fraction = hue - std::floor(hue);
    const float p = value * (1.0F - saturation);
    const float q = value * (1.0F - (saturation * fraction));
    const float t = value * (1.0F - (saturation * (1.0F - fraction)));
    switch (sector) {
        case 0: return EditorColorUVE{value, t, p, alpha};
        case 1: return EditorColorUVE{q, value, p, alpha};
        case 2: return EditorColorUVE{p, value, t, alpha};
        case 3: return EditorColorUVE{p, q, value, alpha};
        case 4: return EditorColorUVE{t, p, value, alpha};
        default: return EditorColorUVE{value, p, q, alpha};
    }
}

} // namespace UVE::Editor
