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

inline constexpr float kMinimumUITextInputSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUITextInputSizePixelsUVE = 8192.0F;
inline constexpr std::int32_t kMinimumUITextInputLengthUVE = 1;

/// A single-line text box. Pressing the box focuses it; pressing anywhere else blurs it, so
/// clicking between two inputs switches focus with no extra state. While focused, letter/digit/
/// space presses insert at the caret (shift capitalizes, shift+digits yield US-layout
/// `!@#$%^&*()`), Backspace deletes before it, Left/Right move it, Enter raises
/// `wasSubmittedThisFrame` for exactly that tick without blurring, and Escape blurs without
/// submitting. There is no Delete-forward key, no key repeat, no Tab navigation, and no
/// punctuation keys yet - the charset is exactly what KeyCodeUVE can express, and punctuation
/// beyond shift+digits waits on new key codes from a real backend.
///
/// `text` (capped by `maxLength` while typing) and `placeholder` are authored and persist; the
/// caret, focus, blink clock, and scroll offset are tick-recomputed runtime state that never
/// round-trips. The caret is a fixed 2px quad blinking on a one-second clock that restarts on
/// every edit, and the text scrolls horizontally to keep it visible - but with no clip rects in
/// the draw batch yet, text wider than the box overflows visibly until clipping lands. Draws on
/// the button layer (box) and text layer (glyphs, caret). Thread-safety: value type; trivially
/// safe to copy/move.
struct UITextInputComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{160.0F, 28.0F}};
    std::string text;
    std::int32_t maxLength = 32;
    std::string placeholder;
    float fontSize = 16.0F;
    float textPadding = 6.0F;
    Math::Vector3UVE boxColor{0.16F, 0.16F, 0.18F};
    Math::Vector3UVE focusColor{0.22F, 0.24F, 0.30F};
    Math::Vector3UVE textColor{0.95F, 0.95F, 0.95F};
    Math::Vector3UVE caretColor{0.95F, 0.95F, 0.95F};
    Math::Vector3UVE placeholderColor{0.55F, 0.55F, 0.58F};
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

[[nodiscard]] bool IsUITextInputComponentValidUVE(const UITextInputComponentUVE& component) noexcept;

} // namespace UVE::Scene
