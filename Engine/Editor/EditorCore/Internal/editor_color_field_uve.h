// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/editor/editor_color_uve.h"

namespace UVE::Editor {

/// What a colour field did this frame.
enum class ColorFieldEventUVE {
    None,
    /// The picker is open and the colour moved. `color` holds the new value; the caller may show
    /// it live, but the edit is not finished.
    Edited,
    /// The edit is finished and kept (OK, Enter, a click outside the picker, or a colour dropped
    /// on the swatch). `color` holds the final value.
    Committed,
    /// The picker was closed with Cancel or Escape. `color` holds the value it opened with.
    Cancelled,
};

/// The editor's one colour field: a swatch showing the colour and its hex code, which opens the
/// colour picker:
///   * a shelf of saved colours along the top - drop any colour on it to keep it, click one to use
///     it, drag one to the bin to remove it;
///   * a hue/saturation disc beside separate saturation and value bars;
///   * the colour the picker opened with above the current one (click the old one to go back),
///     and the recent colours under them;
///   * an Advanced section (open or closed as the author left it) with R, G, B, A and H, S, V
///     sliders, each over a strip showing where that channel leads, and a hex field;
///   * OK and Cancel. Enter and Escape do the same.
///
/// While the picker is open the field edits its own working copy, so a caller that applies only
/// on Committed still sees the colour move under the pointer. Every colour row in the editor goes
/// through here, so they all look and behave alike.
[[nodiscard]] ColorFieldEventUVE DrawColorFieldUVE(const char* id, const char* title, EditorColorUVE& color,
                                                   bool hasAlpha, ColorPickerPreferencesUVE& preferences);

} // namespace UVE::Editor
