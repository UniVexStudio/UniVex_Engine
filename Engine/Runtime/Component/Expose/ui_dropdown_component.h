// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui_text_component.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIDropdownSizePixels = 1.0F;
inline constexpr float kMaximumUIDropdownSizePixels = 8192.0F;
inline constexpr std::size_t kMaximumUIDropdownOptionsBytes = 2048U;
inline constexpr float kMinimumUIDropdownOptionHeight = 1.0F;
inline constexpr float kMaximumUIDropdownOptionHeight = 4096.0F;

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
struct UIDropdownComponent final {
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{160.0F, 28.0F}};
    std::string options;
    std::int32_t selectedIndex = 0;
    std::string placeholder{"Select..."};
    float fontSize = 16.0F;
    float optionHeight = 24.0F;
    float textPadding = 6.0F;
    Math::Vector3 boxColor{0.16F, 0.16F, 0.18F};
    Math::Vector3 boxHoverColor{0.28F, 0.28F, 0.33F};
    Math::Vector3 popupColor{0.10F, 0.10F, 0.12F};
    Math::Vector3 optionHoverColor{0.25F, 0.35F, 0.55F};
    Math::Vector3 selectedColor{0.20F, 0.28F, 0.45F};
    Math::Vector3 textColor{0.95F, 0.95F, 0.95F};
    /// Runtime state, never persisted: true while the popup is expanded.
    bool open = false;
    /// Runtime state, never persisted: true while the mouse cursor is over the closed box.
    bool isHovered = false;
    /// Runtime state, never persisted: the popup row under the cursor, or -1.
    std::int32_t hoveredIndex = -1;
    /// Runtime state, never persisted: true for exactly the tick a row press selected an option.
    bool wasSelectionChangedThisFrame = false;
};

[[nodiscard]] bool IsUIDropdownComponentValid(const UIDropdownComponent& component) noexcept;

/// Splits `component.options` on `'\n'`, skipping empty lines.
[[nodiscard]] std::vector<std::string> SplitUIDropdownOptions(
    const UIDropdownComponent& component);

/// Returns the selected option label (clamping a stale index to the last option), or `placeholder` when empty.
[[nodiscard]] std::string ResolveUIDropdownDisplayLabel(
    const UIDropdownComponent& component);

/// Selects `optionIndex` (clamped into `[0, count - 1]`), closes `open`, and sets `wasSelectionChangedThisFrame`.
bool SelectUIDropdownOption(UIDropdownComponent& component,
                               std::int32_t optionIndex) noexcept;

/// Resets runtime popup state (`open = false`, `isHovered = false`, `hoveredIndex = -1`, `wasSelectionChangedThisFrame = false`).
void ResetUIDropdownRuntimeState(UIDropdownComponent& component) noexcept;

} // namespace UVE::Scene
