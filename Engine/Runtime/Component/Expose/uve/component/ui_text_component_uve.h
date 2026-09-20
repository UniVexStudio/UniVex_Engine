// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <string>

#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumUITextBytesUVE = 512U;
inline constexpr float kMinimumUIFontSizeUVE = 1.0F;
inline constexpr float kMaximumUIFontSizeUVE = 512.0F;

/// A single line of screen-space UI text, anchored at `positionPixels` (raw window pixel
/// coordinates, top-left origin - the same convention IInputSystemUVE::GetMousePositionUVE()
/// already uses, so no unprojection is ever needed anywhere in this system). Rendered by
/// UIRuntimeUVE/Renderer3DUVE's "UIOverlay" pass against a baked bitmap-font glyph atlas; only the
/// embedded font's own covered characters (Latin-1 subset) are guaranteed to render.
/// Thread-safety: value type; trivially safe to copy/move.
struct UITextComponentUVE final {
    std::string text;
    Math::Vector2UVE positionPixels{};
    float fontSize = 16.0F;
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
};

[[nodiscard]] bool IsUITextComponentValidUVE(const UITextComponentUVE& component) noexcept;

} // namespace UVE::Scene
