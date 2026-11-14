// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - accessibility tree. A pruned, semantic mirror of the widget tree: only widgets that have a role,
// a name or are focusable appear. Platform bridges (UIA / NSAccessibility / AT-SPI) read this structure;
// `dumpAccessTree` renders it as text for tests and screen-reader debugging.
#pragma once
#include "Widget.h"
#include <string>
#include <vector>

namespace prism {

struct AccessNode {
  AccessRole role = AccessRole::Generic;
  std::string name, description, value;
  Rect rect;
  bool focusable = false, focused = false, enabled = true, checked = false;
  Widget* widget = nullptr;
  std::vector<AccessNode> children;
};

const char* accessRoleName(AccessRole r);
AccessNode buildAccessTree(Widget* root);                 // hidden widgets are skipped
std::string dumpAccessTree(const AccessNode& n);
AccessNode* findAccessNode(AccessNode& root, const std::string& name);
// Keyboard-only navigation: the focusable widgets under `root` in reading order (visible + enabled).
std::vector<Widget*> focusOrder(Widget* root);

} // namespace prism
