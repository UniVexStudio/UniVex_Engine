// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - OS window chrome. A borderless OS window gets PRISM's own title bar: chamfered title tab, hatched drag
// strip, minimize / maximize-restore / close buttons, plus a 1px frame. The OS still provides drag and edge
// resizing through the platform hit test (Platform::setWindowCaption), so the window behaves like a native one.
#pragma once
#include "Icons.h"
#include "Widget.h"
#include <string>

namespace prism {

constexpr float kOsTitleBarH = 32.0f;

struct WindowChromeOptions {
  std::string title = "PRISM";
  Icon icon = Icon::None;
  bool minimize = true, maximize = true;     // buttons to show; close is always there
};

// Wraps `content` as [title bar | content] inside a frame. `windowId` is the PRISM window the chrome belongs to
// (0 = main window): its buttons drive that window through the Platform, close ends it (window 0 quits the app).
Ref<Widget> wrapWithWindowChrome(Ref<Widget> content, int windowId, const WindowChromeOptions& opt);

} // namespace prism
