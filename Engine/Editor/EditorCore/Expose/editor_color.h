// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

/// One colour as the editor's colour field edits it: four channels in 0..1. Colours without alpha
/// carry a = 1.
struct EditorColor final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

[[nodiscard]] constexpr bool operator==(const EditorColor& lhs, const EditorColor& rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

/// Reads a hex colour typed or pasted by an author: 3 digits (RGB, each doubled), 6 (RRGGBB) or 8
/// (RRGGBBAA), case-insensitive, with or without a leading '#', surrounding spaces ignored. A form
/// without alpha answers a = 1. Anything else - another length, a stray character - is no value,
/// never a partial colour.
[[nodiscard]] std::optional<EditorColor> ParseColorHex(std::string_view text);

/// "#RRGGBB", or "#RRGGBBAA" with `includeAlpha`: upper case, each channel clamped to 0..1 and
/// rounded to the nearest of 256 steps, so ParseColorHex reads back the same 8-bit colour.
[[nodiscard]] std::string FormatColorHex(const EditorColor& color, bool includeAlpha);

/// The picker's recent colours: newest first, no duplicates, at most kMaxRecentColors.
inline constexpr std::size_t kMaxRecentColors = 10U;

/// Puts `color` at the front of `recents`, removing an earlier copy of it (compared as 8-bit hex,
/// which is how recents are stored) and dropping the oldest past kMaxRecentColors.
void PushRecentColor(std::vector<EditorColor>& recents, const EditorColor& color);

/// The picker's saved colours: a palette the author builds by dropping colours on it. Kept apart
/// from the recents, so the automatic list never overwrites a curated one.
inline constexpr std::size_t kMaxSavedColors = 24U;

/// Appends `color` to `saved` unless the same 8-bit colour is already there or the palette is
/// full. Returns whether it was added.
bool AddSavedColor(std::vector<EditorColor>& saved, const EditorColor& color);

/// Hue, saturation and value, each 0..1, as the picker's disc and bars edit them.
struct EditorHsv final {
    float h = 0.0F;
    float s = 0.0F;
    float v = 0.0F;
};

/// RGB to HSV. A grey has no hue and black no saturation; for those, `previous` supplies the
/// missing component, so dragging the value bar to black and back does not snap the hue to red.
[[nodiscard]] EditorHsv RgbToHsv(const EditorColor& color, const EditorHsv& previous);
/// HSV to RGB; alpha is taken from `alpha`.
[[nodiscard]] EditorColor HsvToRgb(const EditorHsv& hsv, float alpha);

/// Picker geometry shapes (Part 3.2).
enum class ColorPickerShape : std::uint8_t {
    HueSaturationDisc = 0,
    HueBarRectangle = 1,
    ValueHueSaturationCircle = 2,
    PerceptualLightnessCircle = 3,
    PerceptualRectangle = 4,
    SlidersOnly = 5,
};

/// Picker numeric/colour-space display modes (Part 3.3).
enum class ColorPickerDisplayMode : std::uint8_t {
    RgbFloat = 0,
    RgbByte = 1,
    HsvDegreesPercent = 2,
    LinearRgb = 3,
    Srgb = 4,
    OkLch = 5,
};

/// IEC 61966-2-1 single-channel linear <-> sRGB transfer functions (Part 3.3 / 3.7).
[[nodiscard]] float LinearToSrgbChannel(float linear) noexcept;
[[nodiscard]] float SrgbToLinearChannel(float srgb) noexcept;
[[nodiscard]] EditorColor LinearToSrgbColor(const EditorColor& linearColor) noexcept;
[[nodiscard]] EditorColor SrgbToLinearColor(const EditorColor& srgbColor) noexcept;

/// Applies HDR exposure in stops (`2^exposureStops`) to RGB channels and optionally clamps to LDR [0..1] (Part 3.4).
[[nodiscard]] EditorColor ApplyHdrExposureToColor(const EditorColor& color, float exposureStops,
                                                        bool clampToLdr) noexcept;

/// Keyboard channel nudge helper for the selected RGBA channel (0..3) (Part 3.6).
void NudgeColorChannel(EditorColor& color, std::size_t channelIndex, float delta, bool clampToLdr) noexcept;

/// Colour picker choices that follow the author between pickers and sessions.
struct ColorPickerPreferences final {
    /// Whether the picker's per-channel section (RGBA, HSV, hex) is expanded.
    bool advancedOpen = true;
    ColorPickerShape defaultShape = ColorPickerShape::HueSaturationDisc;
    ColorPickerDisplayMode defaultMode = ColorPickerDisplayMode::RgbFloat;
    bool clampToLdr = true;
    int eyedropperSampleRadiusPixels = 1;
    std::vector<EditorColor> saved;
    std::vector<EditorColor> recents;
    std::vector<EditorColor> projectPalette;
};

} // namespace UVE::Editor
