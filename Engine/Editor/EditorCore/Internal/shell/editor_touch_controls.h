// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

namespace UVE::Editor {

/// A toolbar button for touch screens that opens the floating Controls panel: Undo, Redo, the
/// Ctrl/Shift/Alt latches, Delete, Duplicate, Save, Copy and Paste - the keys a phone has no keyboard
/// for. Null where there is no touch screen (desktop); on Android the button hides itself while a
/// mouse or keyboard is attached.
[[nodiscard]] prism::Ref<prism::Widget> MakeTouchControlsButtonUVE();

} // namespace UVE::Editor
