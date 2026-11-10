// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - menus: MenuItem model, popup menus (with submenus), context menus and the MenuBar.
#pragma once
#include "Application.h"
#include <map>

namespace prism {

struct MenuItem {
  std::string label, shortcut;
  Icon icon = Icon::None;
  bool separator = false, checkable = false, checked = false, enabled = true;
  std::vector<MenuItem> sub;
  std::function<void()> action;

  static MenuItem Sep() { MenuItem m; m.separator = true; return m; }
  static MenuItem Item(std::string label, std::function<void()> action, std::string shortcut = "", Icon icon = Icon::None) {
    MenuItem m; m.label = std::move(label); m.action = std::move(action); m.shortcut = std::move(shortcut); m.icon = icon; return m;
  }
  static MenuItem Check(std::string label, bool checked, std::function<void()> action, std::string shortcut = "") {
    MenuItem m; m.label = std::move(label); m.checkable = true; m.checked = checked; m.action = std::move(action); m.shortcut = std::move(shortcut); return m;
  }
  static MenuItem Sub(std::string label, std::vector<MenuItem> items, Icon icon = Icon::None) {
    MenuItem m; m.label = std::move(label); m.sub = std::move(items); m.icon = icon; return m;
  }
  MenuItem& disabled() { enabled = false; return *this; }
};

using MenuFactory = std::function<std::vector<MenuItem>()>;

// Extension points: plugins add menu entries and toolbar buttons by path, without touching the host's menu code.
//   MenuExtensions::global().add("Tools/Profiler/Open Capture...", action);       // creates the "Profiler" submenu
//   MenuExtensions::global().add("MyPlugin/Run", action);                         // creates a whole new top-level menu
// MenuBar merges the entries into the menu with the same first path element; items with an `order` sort ascending.
class MenuExtensions {
public:
  struct Entry { std::string path; MenuItem item; int order = 0; int owner = 0; };
  static MenuExtensions& global();
  int add(std::string path, std::function<void()> action, std::string shortcut = "", Icon icon = Icon::None, int order = 0, int owner = 0);
  void addSeparator(std::string path, int order = 0, int owner = 0);
  void remove(int owner);                       // unload a plugin: drops everything it added
  void clear() { m_entries.clear(); }
  size_t size() const { return m_entries.size(); }
  std::vector<std::string> topLevelMenus() const;                          // first path elements (sorted, unique)
  void merge(const std::string& menu, std::vector<MenuItem>& items) const; // appends entries below `menu`
private:
  std::vector<Entry> m_entries;
};

struct ToolbarEntry { std::string id, tooltip; Icon icon = Icon::None; std::function<void()> action; int order = 0; int owner = 0; };
class ToolbarExtensions {
public:
  static ToolbarExtensions& global();
  void add(const std::string& toolbar, ToolbarEntry e) { m_map[toolbar].push_back(std::move(e)); }
  void remove(int owner);
  std::vector<ToolbarEntry> entries(const std::string& toolbar) const;     // sorted by order
private:
  std::map<std::string, std::vector<ToolbarEntry>> m_map;
};

// Show a popup menu. `anchor`/`placement` as in PopupOptions.
void showMenu(std::vector<MenuItem> items, Rect anchor, PopupPlacement placement, std::function<void()> onClose = {});
void showContextMenu(std::vector<MenuItem> items, Vec2 pos);

class MenuBar : public Widget {
public:
  MenuBar();
  void addMenu(std::string title, MenuFactory factory);
  void syncExtensions();                    // add top-level menus that plugins registered (call after plugins load)
  void setRightContent(Ref<Widget> w);
  void openMenu(int index);
  int menuCount() const { return int(m_menus.size()); }
  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  void onMouseLeave() override { m_hot = -1; invalidate(); }
private:
  struct M { std::string title; MenuFactory factory; Rect rect; };
  int indexAt(Vec2 p) const;
  std::vector<M> m_menus;
  Ref<Widget> m_right;
  int m_open = -1, m_hot = -1, m_last = -1;
};

} // namespace prism
