// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Menu.h"
#include <sstream>
#include <algorithm>
#include "prism/Application.h"

namespace prism {
namespace {

constexpr float kDesktopRowH = 22.f, kCompactRowH = 20.f, kSepH = 7.f, kPadY = 3.f;

class MenuPopup : public Widget {
public:
  MenuPopup(std::vector<MenuItem> items) : m_items(std::move(items)) {}

  Vec2 measure(Vec2 avail) override {
    const Theme& t = theme();
    const bool compact = isCompact();
    float lw = 0, sw = 0;
    for (auto& it : m_items) {
      if (it.separator) continue;
      lw = std::max(lw, std::ceil(t.ui->width(it.label)));
      sw = std::max(sw, std::ceil(t.ui->width(it.shortcut)));
    }
    const float iconPad = compact ? 26.f : 30.f;
    const float trailPad = compact ? 24.f : 30.f;
    float w = iconPad + lw + trailPad + 12.f + (sw > 0 ? 22.f + sw : 0.f);
    w = std::max(w, compact ? 156.f : 184.f);
    const Vec2 ws = Application::exists() ? Application::get().windowSize() : avail;
    if (ws.x > 0.f) w = std::min(w, std::max(140.f, ws.x - 16.f));
    const float contentH = totalContentHeight();
    const float maxH = ws.y > 0.f ? std::max(80.f, ws.y - 16.f) : contentH;
    return m_desired = {w, std::min(contentH, maxH)};
  }

  void paint(Canvas& c) override {
    const Theme& t = theme();
    const bool compact = isCompact();
    c.fillRoundRect(m_rect, 5, t.popup);
    c.strokeRoundRect(m_rect, 5, t.borderStrong);
    const Rect clip = m_rect.deflated(Margin(1.f, kPadY));
    const float totalH = totalContentHeight();
    const bool scrollable = totalH > m_rect.h + 0.5f;
    const float maxScroll = std::max(0.f, totalH - m_rect.h);
    m_scroll = std::clamp(m_scroll, 0.f, maxScroll);
    c.pushClip(clip);
    const float iconPad = compact ? 26.f : 30.f;
    const float iconSz = compact ? 14.f : 16.f;
    for (int i = 0; i < int(m_items.size()); i++) {
      const MenuItem& it = m_items[size_t(i)];
      Rect r = rowRect(i);
      if (r.bottom() < clip.y || r.y > clip.bottom()) continue;
      if (it.separator) {
        c.fillRect({r.x + 6, std::floor(r.y + r.h * 0.5f), r.w - 12, 1}, t.borderSoft);
        continue;
      }
      if (it.header) {
        c.textIn(*t.small, {r.x + 8, r.y, r.w - 16, r.h}, it.label, t.textDim, HAlign::Left, true);
        continue;
      }
      bool hot = i == m_hover && it.enabled;
      if (hot) c.fillRoundRect(r, 3, t.rowSelected);
      Color fg = !it.enabled ? t.textDisabled : (hot ? t.textBright : t.text);
      Color dim = !it.enabled ? t.textDisabled : (hot ? t.text : t.textDim);
      Rect ic{r.x + 6, r.y + (r.h - iconSz) * 0.5f, iconSz, iconSz};
      if (it.checkable) {
        if (it.checked) drawIcon(c, Icon::Check, ic, t.highlight, 1.6f);
        else if (it.image && it.imageSize > 0) c.blitBlend(it.image, it.imageSize, it.imageSize, it.imageSize, ic);
        else if (it.icon != Icon::None) drawIcon(c, it.icon, ic, dim);
      } else if (it.image && it.imageSize > 0) {
        c.blitBlend(it.image, it.imageSize, it.imageSize, it.imageSize, ic);
      } else if (it.icon != Icon::None) {
        drawIcon(c, it.icon, ic, dim);
      }
      const float rightReserve = !it.sub.empty() ? 22.f : (!it.shortcut.empty() ? 14.f : 8.f);
      c.textIn(*t.ui, {r.x + iconPad, r.y, std::max(24.f, r.w - iconPad - rightReserve), r.h}, it.label, fg, HAlign::Left, true);
      if (!it.sub.empty()) drawIcon(c, Icon::ChevronRight, {r.right() - 18, ic.y, iconSz, iconSz}, dim, 1.4f);
      else if (!it.shortcut.empty()) c.textIn(*t.small, {r.x, r.y, r.w - 10, r.h}, it.shortcut, dim, HAlign::Right, false);
    }
    c.popClip();
    if (scrollable && maxScroll > 0.f) {
      const float trackH = std::max(16.f, m_rect.h - 8.f);
      const float thumbH = std::clamp(trackH * (m_rect.h / totalH), 18.f, trackH);
      const float thumbY = m_rect.y + 4.f + (trackH - thumbH) * (m_scroll / maxScroll);
      c.fillRoundRect({m_rect.right() - 5.f, thumbY, 3.f, thumbH}, 1.5f, t.textDim);
    }
  }

  bool onMouseMove(const MouseEvent& e) override {
    if (m_pressed && totalContentHeight() > m_rect.h + 0.5f) {
      const float dy = e.pos.y - m_pressY;
      if (!m_didDragScroll && std::abs(dy) > 6.f) {
        m_didDragScroll = true;
        closeSub();
      }
      if (m_didDragScroll) {
        const float maxScroll = std::max(0.f, totalContentHeight() - m_rect.h);
        m_scroll = std::clamp(m_pressScroll - dy, 0.f, maxScroll);
        invalidate();
        return true;
      }
    }
    int i = rowAt(e.pos);
    if (i >= 0 && !m_items[size_t(i)].separator && i != m_hover) hoverRow(i);
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override {
    if (e.button == MouseButton::Left) {
      m_pressed = true;
      m_didDragScroll = false;
      m_pressY = e.pos.y;
      m_pressScroll = m_scroll;
    }
    return true;
  }
  bool onMouseUp(const MouseEvent& e) override {
    if (e.button != MouseButton::Left) return true;
    const bool dragged = m_didDragScroll;
    m_pressed = false;
    m_didDragScroll = false;
    if (dragged) return true;
    int i = rowAt(e.pos);
    if (i >= 0) activate(i);
    return true;
  }
  bool onMouseWheel(const MouseEvent& e) override {
    const float maxScroll = std::max(0.f, totalContentHeight() - m_rect.h);
    if (maxScroll <= 0.f) return false;
    m_scroll = std::clamp(m_scroll - e.wheel.y * 36.f, 0.f, maxScroll);
    invalidate();
    return true;
  }
  void onMouseLeave() override {
    if (!m_sub) { m_hover = -1; invalidate(); }
  }

  bool onKeyDown(const KeyEvent& e) override {
    int n = int(m_items.size());
    auto step = [&](int dir) {
      for (int k = 1; k <= n; k++) {
        int j = ((m_hover < 0 ? (dir > 0 ? -1 : 0) : m_hover) + dir * k + n * 2) % n;
        if (!m_items[size_t(j)].separator && m_items[size_t(j)].enabled) { hoverRow(j); ensureVisible(j); return; }
      }
    };
    switch (e.key) {
    case Key::Down: step(1); return true;
    case Key::Up: step(-1); return true;
    case Key::Enter: case Key::Right: if (m_hover >= 0) activate(m_hover); return true;
    case Key::Left: if (m_parentMenu) { Application::get().closePopup(this); return true; } return false;
    default: return false;
    }
  }

  void setParentMenu(bool p) { m_parentMenu = p; }

private:
  static bool isCompact() {
    return Application::exists() && Application::get().platform().singleSurface();
  }
  static float itemHeight(const MenuItem& it) {
    return it.separator ? kSepH : (isCompact() ? kCompactRowH : kDesktopRowH);
  }
  float totalContentHeight() const {
    float h = kPadY * 2.f;
    for (const auto& it : m_items) h += itemHeight(it);
    return h;
  }
  Rect rowRect(int i) const {
    float y = m_rect.y + kPadY - m_scroll;
    for (int k = 0; k < i; k++) y += itemHeight(m_items[size_t(k)]);
    return {m_rect.x + 4, y, m_rect.w - 8, itemHeight(m_items[size_t(i)])};
  }
  void ensureVisible(int i) {
    float top = kPadY;
    for (int k = 0; k < i; k++) top += itemHeight(m_items[size_t(k)]);
    const float bot = top + itemHeight(m_items[size_t(i)]);
    if (top < m_scroll) m_scroll = top;
    if (bot > m_scroll + m_rect.h) m_scroll = bot - m_rect.h;
    m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, totalContentHeight() - m_rect.h));
  }
  int rowAt(Vec2 p) const {
    if (!m_rect.deflated(Margin(1.f, kPadY)).contains(p)) return -1;
    for (int i = 0; i < int(m_items.size()); i++) if (rowRect(i).contains(p)) return i;
    return -1;
  }
  void closeSub() {
    if (m_sub) { auto s = m_sub; m_sub.reset(); Application::get().closePopup(s.get()); }
    m_subRow = -1;
  }
  void hoverRow(int i) {
    m_hover = i;
    invalidate();
    const MenuItem& it = m_items[size_t(i)];
    if (m_subRow == i) return;
    closeSub();
    if (!it.sub.empty() && it.enabled) openSub(i);
  }
  void openSub(int i) {
    auto sub = make<MenuPopup>(m_items[size_t(i)].sub);
    sub->setParentMenu(true);
    m_sub = sub;
    m_subRow = i;
    PopupOptions o;
    o.placement = PopupPlacement::Right;
    o.anchor = rowRect(i);
    o.onClose = [this] { m_sub.reset(); m_subRow = -1; };
    Application::get().showPopup(sub, o);
    sub->m_hover = -1;
  }
  void activate(int i) {
    const MenuItem& it = m_items[size_t(i)];
    if (it.separator || !it.enabled) return;
    if (!it.sub.empty()) { hoverRow(i); if (m_sub) static_cast<MenuPopup*>(m_sub.get())->stepFirst(); return; }
    auto act = it.action;
    Application::get().closeAllPopups();
    if (act) act();
  }
  void stepFirst() {
    for (int j = 0; j < int(m_items.size()); j++)
      if (!m_items[size_t(j)].separator && m_items[size_t(j)].enabled) { m_hover = j; break; }
    invalidate();
  }

  std::vector<MenuItem> m_items;
  int m_hover = -1, m_subRow = -1;
  Ref<Widget> m_sub;
  bool m_parentMenu = false;
  float m_scroll = 0.f;
  bool m_pressed = false;
  bool m_didDragScroll = false;
  float m_pressY = 0.f;
  float m_pressScroll = 0.f;
};

} // namespace

Ref<Widget> showSubmenu(std::vector<MenuItem> items, Rect row, std::function<void()> onClose) {
  auto sub = make<MenuPopup>(std::move(items));
  sub->setParentMenu(true);
  PopupOptions o;
  o.placement = PopupPlacement::Right;
  o.anchor = row;
  o.onClose = std::move(onClose);
  Application::get().showPopup(sub, o);
  return sub;
}

void showMenu(std::vector<MenuItem> items, Rect anchor, PopupPlacement placement, std::function<void()> onClose) {
  PopupOptions o;
  o.placement = placement;
  o.anchor = anchor;
  o.onClose = std::move(onClose);
  Application::get().showPopup(make<MenuPopup>(std::move(items)), o);
}

void showContextMenu(std::vector<MenuItem> items, Vec2 pos) {
  showMenu(std::move(items), {pos.x, pos.y, 0, 0}, PopupPlacement::AtPoint);
}

// ------------------------------------------------------------------ MenuBar
MenuBar::MenuBar() {}

void MenuBar::addMenu(std::string title, MenuFactory f) {
  m_menus.push_back({std::move(title), std::move(f), {}});
  relayout();
}

void MenuBar::setRightContent(Ref<Widget> w) {
  if (m_right) removeChild(m_right.get());
  m_right = w;
  addChild(std::move(w));
}

Vec2 MenuBar::measure(Vec2 avail) {
  if (m_right) m_right->measure(avail);
  float w = 0;
  if (!m_showLogo) {
    const Font& f = *theme().ui;
    w = 8;
    for (const auto& m : m_menus) w += std::ceil(f.width(m.title)) + m_itemPad + 1;
  }
  return m_desired = constrain({w, theme().menuBarH});
}

void MenuBar::arrange(Rect r) {
  m_rect = r;
  const Font& f = *theme().ui;
  float x = m_showLogo ? r.x + 10 + 18 + 10 : r.x + 4;
  for (auto& m : m_menus) {
    float w = std::ceil(f.width(m.title)) + m_itemPad;
    m.rect = {x, r.y + 3, w, r.h - 6};
    x += w + 1;
  }
  if (m_right) {
    Vec2 d = m_right->desired();
    m_right->arrange({r.right() - d.x - 12, r.y, d.x, r.h});
  }
}

int MenuBar::indexAt(Vec2 p) const {
  for (int i = 0; i < int(m_menus.size()); i++) if (m_menus[size_t(i)].rect.contains(p)) return i;
  return -1;
}

void MenuBar::openMenu(int i) {
  if (i < 0 || i >= int(m_menus.size())) return;
  Application& app = Application::get();
  if (m_open >= 0) app.closeAllPopups();
  m_open = i;
  m_last = i;
  const Rect& r = m_menus[size_t(i)].rect;
  auto items = m_menus[size_t(i)].factory();
  MenuExtensions::global().merge(m_menus[size_t(i)].title, items);
  showMenu(std::move(items), {r.x, m_rect.bottom() - 1, r.w, 0}, PopupPlacement::Below, [this] { m_open = -1; invalidate(); });
  invalidate();
}

bool MenuBar::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  int i = indexAt(e.pos);
  if (i < 0) return false;
  if (Application::get().popupJustDismissed() && i == m_last) return true;
  openMenu(i);
  return true;
}

bool MenuBar::onMouseMove(const MouseEvent& e) {
  int i = indexAt(e.pos);
  if (i != m_hot) { m_hot = i; invalidate(); }
  if (m_open >= 0 && i >= 0 && i != m_open) openMenu(i);
  return false;
}

void MenuBar::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRect(m_rect, t.header);
  c.fillRect({m_rect.x, m_rect.bottom() - 1, m_rect.w, 1}, t.border);
  if (m_showLogo) drawLogo(c, {m_rect.x + 10, m_rect.y + (m_rect.h - 17) * 0.5f, 18, 17});
  for (int i = 0; i < int(m_menus.size()); i++) {
    const M& m = m_menus[size_t(i)];
    bool on = i == m_open, hot = i == m_hot;
    if (on) c.fillRoundRect(m.rect, 3, t.controlHover);
    else if (hot) c.fillRoundRect(m.rect, 3, t.control);
    c.textIn(*t.ui, m.rect, m.title, on || hot ? t.textBright : t.text, HAlign::Center, false);
  }
  paintChildren(c);
}


// ------------------------------------------------------------------ extension points
MenuExtensions& MenuExtensions::global() { static MenuExtensions e; return e; }

int MenuExtensions::add(std::string path, std::function<void()> action, std::string shortcut, Icon icon, int order, int owner) {
  Entry e;
  e.path = std::move(path);
  std::string label = e.path.substr(e.path.rfind('/') == std::string::npos ? 0 : e.path.rfind('/') + 1);
  e.item = MenuItem::Item(std::move(label), std::move(action), std::move(shortcut), icon);
  e.order = order; e.owner = owner;
  m_entries.push_back(std::move(e));
  return int(m_entries.size());
}
void MenuExtensions::addSeparator(std::string path, int order, int owner) {
  Entry e; e.path = std::move(path); e.item = MenuItem::Sep(); e.order = order; e.owner = owner;
  m_entries.push_back(std::move(e));
}
void MenuExtensions::remove(int owner) {
  m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.owner == owner; }), m_entries.end());
}
std::vector<std::string> MenuExtensions::topLevelMenus() const {
  std::vector<std::string> v;
  for (auto& e : m_entries) {
    std::string top = e.path.substr(0, e.path.find('/'));
    if (std::find(v.begin(), v.end(), top) == v.end()) v.push_back(top);
  }
  return v;
}

namespace {
// Finds (or creates) the submenu `name` inside `items`.
std::vector<MenuItem>& subOf(std::vector<MenuItem>& items, const std::string& name) {
  for (auto& m : items) if (!m.separator && m.label == name && !m.sub.empty()) return m.sub;
  for (auto& m : items) if (!m.separator && m.label == name && !m.action) return m.sub;
  items.push_back(MenuItem::Sub(name, {}));
  return items.back().sub;
}
}

void MenuExtensions::merge(const std::string& menu, std::vector<MenuItem>& items) const {
  std::vector<const Entry*> mine;
  for (auto& e : m_entries) if (e.path.substr(0, e.path.find('/')) == menu) mine.push_back(&e);
  std::stable_sort(mine.begin(), mine.end(), [](const Entry* a, const Entry* b) { return a->order < b->order; });
  bool first = true;
  for (const Entry* e : mine) {
    // path = "Menu/Sub/.../Label": walk the submenus between the first and the last element
    std::vector<std::string> parts;
    std::stringstream ss(e->path);
    std::string part;
    while (std::getline(ss, part, '/')) parts.push_back(part);
    std::vector<MenuItem>* level = &items;
    if (first && !items.empty() && !items.back().separator && parts.size() >= 2) { items.push_back(MenuItem::Sep()); }
    first = false;
    for (size_t i = 1; i + 1 < parts.size(); i++) level = &subOf(*level, parts[i]);
    level->push_back(e->item);
  }
}

void MenuBar::syncExtensions() {
  for (auto& top : MenuExtensions::global().topLevelMenus()) {
    bool have = false;
    for (auto& m : m_menus) if (m.title == top) have = true;
    if (!have) addMenu(top, [] { return std::vector<MenuItem>{}; });
  }
}

ToolbarExtensions& ToolbarExtensions::global() { static ToolbarExtensions e; return e; }
void ToolbarExtensions::remove(int owner) {
  for (auto& kv : m_map) kv.second.erase(std::remove_if(kv.second.begin(), kv.second.end(), [&](const ToolbarEntry& e) { return e.owner == owner; }), kv.second.end());
}
std::vector<ToolbarEntry> ToolbarExtensions::entries(const std::string& toolbar) const {
  auto it = m_map.find(toolbar);
  std::vector<ToolbarEntry> v = it == m_map.end() ? std::vector<ToolbarEntry>{} : it->second;
  std::stable_sort(v.begin(), v.end(), [](const ToolbarEntry& a, const ToolbarEntry& b) { return a.order < b.order; });
  return v;
}

} // namespace prism
