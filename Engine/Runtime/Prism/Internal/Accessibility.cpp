// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/AccessibilityBridge.h"
#include "prism/Application.h"
#include <functional>

namespace prism {

const char* accessRoleName(AccessRole r) {
  static const char* n[] = {"generic", "window", "group", "label", "button", "toggle-button", "checkbox", "slider", "combobox",
                            "textbox", "progressbar", "tab", "tablist", "menu", "menuitem", "table", "row", "columnheader",
                            "cell", "tree", "treeitem", "list", "listitem", "separator", "image", "canvas"};
  return n[int(r)];
}

static bool meaningful(const AccessNode& n) { return n.role != AccessRole::Generic || !n.name.empty() || n.focusable; }

static AccessNode build(Widget* w) {
  AccessNode n;
  n.widget = w; n.role = w->accessRole(); n.name = w->accessName(); n.description = w->accessDescription();
  n.value = w->accessValue(); n.rect = w->rect(); n.focusable = w->focusable(); n.focused = w->focused();
  n.enabled = w->enabled(); n.checked = w->accessChecked();
  for (auto& c : w->children()) {
    if (!c->visible()) continue;
    AccessNode k = build(c.get());
    if (meaningful(k)) n.children.push_back(std::move(k));
    else for (auto& g : k.children) n.children.push_back(std::move(g));   // hoist through purely structural widgets
  }
  return n;
}

AccessNode buildAccessTree(Widget* root) {
  if (!root) return {};
  AccessNode n = build(root);
  if (n.role == AccessRole::Generic) n.role = AccessRole::Window;
  return n;
}

static void dump(const AccessNode& n, int depth, std::string& out) {
  out += std::string(size_t(depth) * 2, ' ') + accessRoleName(n.role);
  if (!n.name.empty()) out += " \"" + n.name + "\"";
  if (!n.value.empty()) out += " =" + n.value;
  if (n.checked) out += " [checked]";
  if (!n.enabled) out += " [disabled]";
  if (n.focused) out += " [focused]";
  out += "\n";
  for (auto& c : n.children) dump(c, depth + 1, out);
}
std::string dumpAccessTree(const AccessNode& n) { std::string s; dump(n, 0, s); return s; }

AccessNode* findAccessNode(AccessNode& root, const std::string& name) {
  if (root.name == name) return &root;
  for (auto& c : root.children) if (auto* f = findAccessNode(c, name)) return f;
  return nullptr;
}

std::vector<Widget*> focusOrder(Widget* root) {
  std::vector<Widget*> out;
  std::function<void(Widget*)> walk = [&](Widget* w) {
    if (!w->visible() || !w->enabled()) return;
    if (w->focusable() && w->rect().w > 0 && w->rect().h > 0) out.push_back(w);
    for (auto& c : w->children()) walk(c.get());
  };
  if (root) walk(root);
  return out;
}

// ------------------------------------------------------------------ bridge view
bool AccessView::meaningful(const Widget* w) {
  return w->accessRole() != AccessRole::Generic || !w->accessName().empty() || w->focusable();
}

Widget* AccessView::parentOf(const Widget* w, const Widget* root) {
  for (Widget* p = w->parent(); p; p = p->parent()) if (p == root || meaningful(p)) return p;
  return nullptr;
}

static void collect(const Widget* w, std::vector<Widget*>& out) {
  for (auto& c : w->children()) {
    if (!c->visible()) continue;
    if (AccessView::meaningful(c.get())) out.push_back(c.get());
    else collect(c.get(), out);
  }
}
std::vector<Widget*> AccessView::children(const Widget* w) { std::vector<Widget*> v; collect(w, v); return v; }

int AccessView::indexInParent(const Widget* w, const Widget* root) {
  Widget* p = parentOf(w, root);
  if (!p) return -1;
  auto kids = children(p);
  for (size_t i = 0; i < kids.size(); i++) if (kids[i] == w) return int(i);
  return -1;
}

std::vector<std::string> AccessView::actions(const Widget* w) {
  std::vector<std::string> a;
  switch (w->accessRole()) {
    case AccessRole::Button: a.push_back("press"); break;
    case AccessRole::ToggleButton: case AccessRole::CheckBox: a.push_back("toggle"); break;
    default: break;
  }
  if (w->focusable()) a.push_back("focus");
  return a;
}

bool AccessView::doAction(Widget* w, int index) {
  auto a = actions(w);
  if (index < 0 || index >= int(a.size())) return false;
  if (a[size_t(index)] == "focus") { Application::get().setFocus(w); return true; }
  bool ok = w->accessActivate();
  Application::get().invalidate();
  return ok;
}

#if !defined(PRISM_HAS_ATK) && !defined(_WIN32)
std::shared_ptr<AccessibilityBridge> makePlatformAccessibilityBridge(Application&, const std::string&) { return nullptr; }
#endif

} // namespace prism
