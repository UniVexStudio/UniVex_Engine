// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ui_text_component.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUITextInputSizePixels = 1.0F;
inline constexpr float kMaximumUITextInputSizePixels = 8192.0F;
inline constexpr std::int32_t kMinimumUITextInputLength = 1;

/// A single-line text box. Pressing the box focuses it; pressing anywhere else blurs it, so
/// clicking between two inputs switches focus with no extra state. While focused, letter/digit/
/// space presses insert at the caret (shift capitalizes, shift+digits yield US-layout
/// `!@#$%^&*()`), Backspace deletes before it, Left/Right move it, Enter raises
/// `wasSubmittedThisFrame` for exactly that tick without blurring, and Escape blurs without
/// submitting. There is no Delete-forward key, no key repeat, no Tab navigation, and no
/// punctuation keys yet - the charset is exactly what KeyCode can express, and punctuation
/// beyond shift+digits waits on new key codes from a real backend.
///
/// `text` (capped by `maxLength` while typing) and `placeholder` are authored and persist; the
/// caret, focus, blink clock, and scroll offset are tick-recomputed runtime state that never
/// round-trips. The caret is a fixed 2px quad blinking on a one-second clock that restarts on
/// every edit, and the text scrolls horizontally to keep it visible - but with no clip rects in
/// the draw batch yet, text wider than the box overflows visibly until clipping lands. Draws on
/// the button layer (box) and text layer (glyphs, caret). Thread-safety: value type; trivially
/// safe to copy/move.
struct UITextInputComponent final {
    Math::Rect rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{160.0F, 28.0F}};
    std::string text;
    std::int32_t maxLength = 32;
    std::string placeholder;
    float fontSize = 16.0F;
    float textPadding = 6.0F;
    Math::Vector3 boxColor{0.16F, 0.16F, 0.18F};
    Math::Vector3 focusColor{0.22F, 0.24F, 0.30F};
    Math::Vector3 textColor{0.95F, 0.95F, 0.95F};
    Math::Vector3 caretColor{0.95F, 0.95F, 0.95F};
    Math::Vector3 placeholderColor{0.55F, 0.55F, 0.58F};
    /// Runtime state, never persisted: true while this box holds keyboard focus.
    bool focused = false;
    /// Runtime state, never persisted: insertion index into `text`, clamped to its size at use.
    std::int32_t caretIndex = 0;
    /// Runtime state, never persisted: true for exactly the tick Enter submitted the text.
    bool wasSubmittedThisFrame = false;
    /// Runtime state, never persisted: seconds since the caret blink clock restarted.
    float blinkTime = 0.0F;
    /// Runtime state, never persisted: horizontal pixels the text is scrolled to show the caret.
    float scrollOffset = 0.0F;
};

[[nodiscard]] bool IsUITextInputComponentValid(const UITextInputComponent& component) noexcept;

/// Clamps `caretIndex` into `[0, static_cast<int32_t>(text.size())]`.
[[nodiscard]] std::int32_t ClampUITextInputCaretIndex(
    const UITextInputComponent& component) noexcept;

/// True when `text` is empty and `placeholder` is non-empty.
[[nodiscard]] bool ShouldDisplayUITextInputPlaceholder(
    const UITextInputComponent& component) noexcept;

/// Inserts printable ASCII bytes (`0x20..0x7E`) from `typedAscii` at `caretIndex` up to `maxLength`.
bool InsertUITextInputCharacters(UITextInputComponent& component,
                                    const std::string& typedAscii);

/// Erases the character immediately before `caretIndex` (Backspace semantics).
bool ApplyUITextInputBackspace(UITextInputComponent& component) noexcept;

/// Resets runtime focus/caret/blink state.
void ResetUITextInputRuntimeState(UITextInputComponent& component) noexcept;

} // namespace UVE::Scene
