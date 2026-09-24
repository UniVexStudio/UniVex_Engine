// univex/render/SelectionOutline.h
// -----------------------------------------------------------------------
// What the selection outline draws and how it looks, with no GL in sight,
// so the host can build it and the tests can check it without a context.
//
// The outline is drawn from the selected meshes' own triangles, in world
// space, each vertex carrying how strongly it is selected: the active node
// at full strength, the rest of a multi-selection dimmer. SelectionOutline-
// Renderer turns that into a band of colour just outside the silhouette.
// -----------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>

namespace univex::render {

inline constexpr float kSelectionOutlineActiveWeight = 1.0f;
inline constexpr float kSelectionOutlineOtherWeight = 0.55f;

inline constexpr float kSelectionOutlineMinThicknessPixels = 1.0f;
inline constexpr float kSelectionOutlineMaxThicknessPixels = 6.0f;

// One corner of a selected triangle; three in a row make a triangle.
struct SelectionOutlineVertex {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float weight = kSelectionOutlineActiveWeight;
};

struct SelectionOutlineSettings {
    bool visible = true;
    float r = 1.0f;
    float g = 0.62f;
    float b = 0.16f;
    // Width of the band in pixels.
    float thicknessPixels = 2.0f;
};

// Settings as the renderer will use them: colour channels clamped to 0..1 and the thickness to
// the supported range, with anything non-finite replaced by the default.
[[nodiscard]] inline SelectionOutlineSettings SanitizeSelectionOutlineSettings(SelectionOutlineSettings settings) {
    const SelectionOutlineSettings defaults{};
    const auto channel = [](const float value, const float fallback) {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
    };
    settings.r = channel(settings.r, defaults.r);
    settings.g = channel(settings.g, defaults.g);
    settings.b = channel(settings.b, defaults.b);
    settings.thicknessPixels = std::isfinite(settings.thicknessPixels)
                                   ? std::clamp(settings.thicknessPixels, kSelectionOutlineMinThicknessPixels,
                                                kSelectionOutlineMaxThicknessPixels)
                                   : defaults.thicknessPixels;
    return settings;
}

} // namespace univex::render
