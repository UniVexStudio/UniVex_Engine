// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "uve/component/ui_text_component_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIDropdownSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUIDropdownSizePixelsUVE = 8192.0F;
inline constexpr std::size_t kMaximumUIDropdownOptionsBytesUVE = 2048U;
inline constexpr float kMinimumUIDropdownOptionHeightUVE = 1.0F;
inline constexpr float kMaximumUIDropdownOptionHeightUVE = 4096.0F;

/// A closed box showing the selected option that expands into a popup list. `options` holds every
/// label as one newline-separated blob ("Easy\nNormal\nHard") - there is no string-list metadata
/// codec, so one String property carries the whole list and empty lines are skipped, never
/// options. Pressing the box toggles `open`; pressing a popup row selects it, closes, and raises
/// `wasSelectionChangedThisFrame` for exactly that tick; pressing anywhere else closes without
/// selecting. `selectedIndex` is authored and persists (a stale index past the last option reads
/// as the last); `open`, `isHovered`, `hoveredIndex`, and `wasSelectionChangedThisFrame` are
/// tick-recomputed runtime state that never round-trips.
///
/// The popup is as wide as the box and one `optionHeight` row per option: it opens below the box,
/// flips above when below would leave the viewport and above fits, and otherwise clamps below
/// into the viewport. With no scrolling in the engine yet, long lists simply run long - authors
/// keep them short. Layout and anchors move the box only; the popup positions itself every frame,
/// like a tooltip. The box draws on the button layer, the popup and every label on the text
/// layer. Thread-safety: value type; trivially safe to copy/move.
struct UIDropdownComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{160.0F, 28.0F}};
    std::string options;
    std::int32_t selectedIndex = 0;
    std::string placeholder{"Select..."};
    float fontSize = 16.0F;
    float optionHeight = 24.0F;
    float textPadding = 6.0F;
    Math::Vector3UVE boxColor{0.16F, 0.16F, 0.18F};
    Math::Vector3UVE boxHoverColor{0.28F, 0.28F, 0.33F};
    Math::Vector3UVE popupColor{0.10F, 0.10F, 0.12F};
    Math::Vector3UVE optionHoverColor{0.25F, 0.35F, 0.55F};
    Math::Vector3UVE selectedColor{0.20F, 0.28F, 0.45F};
    Math::Vector3UVE textColor{0.95F, 0.95F, 0.95F};
    /// Runtime state, never persisted: true while the popup is expanded.
    bool open = false;
    /// Runtime state, never persisted: true while the mouse cursor is over the closed box.
    bool isHovered = false;
    /// Runtime state, never persisted: the popup row under the cursor, or -1.
    std::int32_t hoveredIndex = -1;
    /// Runtime state, never persisted: true for exactly the tick a row press selected an option.
    bool wasSelectionChangedThisFrame = false;
};

[[nodiscard]] bool IsUIDropdownComponentValidUVE(const UIDropdownComponentUVE& component) noexcept;

} // namespace UVE::Scene
