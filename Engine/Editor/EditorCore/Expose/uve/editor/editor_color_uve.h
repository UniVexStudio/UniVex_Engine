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
struct EditorColorUVE final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

[[nodiscard]] constexpr bool operator==(const EditorColorUVE& lhs, const EditorColorUVE& rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

/// Reads a hex colour typed or pasted by an author: 3 digits (RGB, each doubled), 6 (RRGGBB) or 8
/// (RRGGBBAA), case-insensitive, with or without a leading '#', surrounding spaces ignored. A form
/// without alpha answers a = 1. Anything else - another length, a stray character - is no value,
/// never a partial colour.
[[nodiscard]] std::optional<EditorColorUVE> ParseColorHexUVE(std::string_view text);

/// "#RRGGBB", or "#RRGGBBAA" with `includeAlpha`: upper case, each channel clamped to 0..1 and
/// rounded to the nearest of 256 steps, so ParseColorHexUVE reads back the same 8-bit colour.
[[nodiscard]] std::string FormatColorHexUVE(const EditorColorUVE& color, bool includeAlpha);

/// The picker's recent colours: newest first, no duplicates, at most kMaxRecentColorsUVE.
inline constexpr std::size_t kMaxRecentColorsUVE = 10U;

/// Puts `color` at the front of `recents`, removing an earlier copy of it (compared as 8-bit hex,
/// which is how recents are stored) and dropping the oldest past kMaxRecentColorsUVE.
void PushRecentColorUVE(std::vector<EditorColorUVE>& recents, const EditorColorUVE& color);

/// The picker's saved colours: a palette the author builds by dropping colours on it. Kept apart
/// from the recents, so the automatic list never overwrites a curated one.
inline constexpr std::size_t kMaxSavedColorsUVE = 24U;

/// Appends `color` to `saved` unless the same 8-bit colour is already there or the palette is
/// full. Returns whether it was added.
bool AddSavedColorUVE(std::vector<EditorColorUVE>& saved, const EditorColorUVE& color);

/// Hue, saturation and value, each 0..1, as the picker's disc and bars edit them.
struct EditorHsvUVE final {
    float h = 0.0F;
    float s = 0.0F;
    float v = 0.0F;
};

/// RGB to HSV. A grey has no hue and black no saturation; for those, `previous` supplies the
/// missing component, so dragging the value bar to black and back does not snap the hue to red.
[[nodiscard]] EditorHsvUVE RgbToHsvUVE(const EditorColorUVE& color, const EditorHsvUVE& previous);
/// HSV to RGB; alpha is taken from `alpha`.
[[nodiscard]] EditorColorUVE HsvToRgbUVE(const EditorHsvUVE& hsv, float alpha);

/// Picker geometry shapes (Part 3.2).
enum class ColorPickerShapeUVE : std::uint8_t {
    HueSaturationDisc = 0,
    HueBarRectangle = 1,
    ValueHueSaturationCircle = 2,
    PerceptualLightnessCircle = 3,
    PerceptualRectangle = 4,
    SlidersOnly = 5,
};

/// Picker numeric/colour-space display modes (Part 3.3).
enum class ColorPickerDisplayModeUVE : std::uint8_t {
    RgbFloat = 0,
    RgbByte = 1,
    HsvDegreesPercent = 2,
    LinearRgb = 3,
    Srgb = 4,
    OkLch = 5,
};

/// IEC 61966-2-1 single-channel linear <-> sRGB transfer functions (Part 3.3 / 3.7).
[[nodiscard]] float LinearToSrgbChannelUVE(float linear) noexcept;
[[nodiscard]] float SrgbToLinearChannelUVE(float srgb) noexcept;
[[nodiscard]] EditorColorUVE LinearToSrgbColorUVE(const EditorColorUVE& linearColor) noexcept;
[[nodiscard]] EditorColorUVE SrgbToLinearColorUVE(const EditorColorUVE& srgbColor) noexcept;

/// Applies HDR exposure in stops (`2^exposureStops`) to RGB channels and optionally clamps to LDR [0..1] (Part 3.4).
[[nodiscard]] EditorColorUVE ApplyHdrExposureToColorUVE(const EditorColorUVE& color, float exposureStops,
                                                        bool clampToLdr) noexcept;

/// Keyboard channel nudge helper for the selected RGBA channel (0..3) (Part 3.6).
void NudgeColorChannelUVE(EditorColorUVE& color, std::size_t channelIndex, float delta, bool clampToLdr) noexcept;

/// Colour picker choices that follow the author between pickers and sessions.
struct ColorPickerPreferencesUVE final {
    /// Whether the picker's per-channel section (RGBA, HSV, hex) is expanded.
    bool advancedOpen = true;
    ColorPickerShapeUVE defaultShape = ColorPickerShapeUVE::HueSaturationDisc;
    ColorPickerDisplayModeUVE defaultMode = ColorPickerDisplayModeUVE::RgbFloat;
    bool clampToLdr = true;
    int eyedropperSampleRadiusPixels = 1;
    std::vector<EditorColorUVE> saved;
    std::vector<EditorColorUVE> recents;
    std::vector<EditorColorUVE> projectPalette;
};

} // namespace UVE::Editor
