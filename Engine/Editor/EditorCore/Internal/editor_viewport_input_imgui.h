// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_viewport_input.h"

namespace UVE::Editor {

/// Fills an EditorViewportInputUVE from the current ImGui window/IO. Call it inside the window that
/// shows the view, with the cursor at the view image's top-left (where the image will be drawn).
[[nodiscard]] EditorViewportInputUVE CaptureImGuiViewportInputUVE();

/// Applies the cursor the viewport renderer requested while it ran.
void ApplyImGuiViewportCursorUVE(const EditorViewportInputUVE& input);

} // namespace UVE::Editor
