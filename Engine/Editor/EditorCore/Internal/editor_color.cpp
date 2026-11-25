// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace UVE::Editor {

namespace {

[[nodiscard]] int HexDigitValue(const char digit) noexcept {
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

[[nodiscard]] int ToByte(const float channel) noexcept {
    // NaN compares false both ways and would survive a clamp, so it is mapped to 0 explicitly.
    const float clamped = std::isnan(channel) ? 0.0F : std::clamp(channel, 0.0F, 1.0F);
    return static_cast<int>(std::lround(clamped * 255.0F));
}

} // namespace

std::optional<EditorColor> ParseColorHex(std::string_view text) {
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
        digits[index] = HexDigitValue(text[index]);
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
    return EditorColor{static_cast<float>(bytes[0]) / 255.0F, static_cast<float>(bytes[1]) / 255.0F,
                          static_cast<float>(bytes[2]) / 255.0F, static_cast<float>(bytes[3]) / 255.0F};
}

std::string FormatColorHex(const EditorColor& color, const bool includeAlpha) {
    std::array<char, 16> text{};
    if (includeAlpha) {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X%02X", ToByte(color.r), ToByte(color.g),
                      ToByte(color.b), ToByte(color.a));
    } else {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X", ToByte(color.r), ToByte(color.g),
                      ToByte(color.b));
    }
    return std::string(text.data());
}

void PushRecentColor(std::vector<EditorColor>& recents, const EditorColor& color) {
    const std::string key = FormatColorHex(color, true);
    std::erase_if(recents, [&key](const EditorColor& recent) { return FormatColorHex(recent, true) == key; });
    recents.insert(recents.begin(), color);
    if (recents.size() > kMaxRecentColors) {
        recents.resize(kMaxRecentColors);
    }
}

bool AddSavedColor(std::vector<EditorColor>& saved, const EditorColor& color) {
    if (saved.size() >= kMaxSavedColors) {
        return false;
    }
    const std::string key = FormatColorHex(color, true);
    if (std::any_of(saved.begin(), saved.end(),
                    [&key](const EditorColor& entry) { return FormatColorHex(entry, true) == key; })) {
        return false;
    }
    saved.push_back(color);
    return true;
}

EditorHsv RgbToHsv(const EditorColor& color, const EditorHsv& previous) {
    const float maximum = std::max({color.r, color.g, color.b});
    const float minimum = std::min({color.r, color.g, color.b});
    const float chroma = maximum - minimum;
    EditorHsv hsv{previous.h, previous.s, maximum};
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

EditorColor HsvToRgb(const EditorHsv& hsv, const float alpha) {
    const float hue = (hsv.h - std::floor(hsv.h)) * 6.0F;
    const float saturation = std::clamp(hsv.s, 0.0F, 1.0F);
    const float value = std::clamp(hsv.v, 0.0F, 1.0F);
    const int sector = static_cast<int>(hue) % 6;
    const float fraction = hue - std::floor(hue);
    const float p = value * (1.0F - saturation);
    const float q = value * (1.0F - (saturation * fraction));
    const float t = value * (1.0F - (saturation * (1.0F - fraction)));
    switch (sector) {
        case 0: return EditorColor{value, t, p, alpha};
        case 1: return EditorColor{q, value, p, alpha};
        case 2: return EditorColor{p, value, t, alpha};
        case 3: return EditorColor{p, q, value, alpha};
        case 4: return EditorColor{t, p, value, alpha};
        default: return EditorColor{value, p, q, alpha};
    }
}

float LinearToSrgbChannel(const float linear) noexcept {
    if (!std::isfinite(linear) || linear <= 0.0F) {
        return 0.0F;
    }
    if (linear >= 1.0F) {
        return 1.0F;
    }
    return (linear <= 0.0031308F) ? (linear * 12.92F) : (1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F);
}

float SrgbToLinearChannel(const float srgb) noexcept {
    if (!std::isfinite(srgb) || srgb <= 0.0F) {
        return 0.0F;
    }
    if (srgb >= 1.0F) {
        return 1.0F;
    }
    return (srgb <= 0.04045F) ? (srgb / 12.92F) : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

EditorColor LinearToSrgbColor(const EditorColor& linearColor) noexcept {
    return EditorColor{LinearToSrgbChannel(linearColor.r), LinearToSrgbChannel(linearColor.g),
                          LinearToSrgbChannel(linearColor.b), std::clamp(linearColor.a, 0.0F, 1.0F)};
}

EditorColor SrgbToLinearColor(const EditorColor& srgbColor) noexcept {
    return EditorColor{SrgbToLinearChannel(srgbColor.r), SrgbToLinearChannel(srgbColor.g),
                          SrgbToLinearChannel(srgbColor.b), std::clamp(srgbColor.a, 0.0F, 1.0F)};
}

EditorColor ApplyHdrExposureToColor(const EditorColor& color, const float exposureStops,
                                          const bool clampToLdr) noexcept {
    const float scale = std::isfinite(exposureStops) ? std::exp2(exposureStops) : 1.0F;
    EditorColor out{
        std::max(0.0F, color.r * scale),
        std::max(0.0F, color.g * scale),
        std::max(0.0F, color.b * scale),
        std::clamp(color.a, 0.0F, 1.0F),
    };
    if (clampToLdr) {
        out.r = std::min(out.r, 1.0F);
        out.g = std::min(out.g, 1.0F);
        out.b = std::min(out.b, 1.0F);
    }
    return out;
}

void NudgeColorChannel(EditorColor& color, const std::size_t channelIndex, const float delta,
                          const bool clampToLdr) noexcept {
    if (!std::isfinite(delta)) {
        return;
    }
    float* channel = nullptr;
    switch (channelIndex) {
        case 0U: channel = &color.r; break;
        case 1U: channel = &color.g; break;
        case 2U: channel = &color.b; break;
        case 3U: channel = &color.a; break;
        default: return;
    }
    const float maxVal = (channelIndex == 3U || clampToLdr) ? 1.0F : 100.0F;
    *channel = std::clamp(*channel + delta, 0.0F, maxVal);
}

} // namespace UVE::Editor
