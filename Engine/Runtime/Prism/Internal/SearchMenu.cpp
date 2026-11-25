// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/SearchMenu.h"

#include "prism/Application.h"
#include "prism/TextInput.h"

#include <algorithm>
#include <cctype>

namespace prism {
namespace {


std::string lowered(std::string s) {
  for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

class SearchMenuPopup : public Widget {
public:
  // One pickable entry: the item and the group it sits in ("" at the top level).
  // `flyout`: a top-level submenu shown as one row that opens beside the menu (not searched itself;
  // its items are, under its name).
  struct Entry { MenuItem item; std::string group; bool flyout = false; };
  struct Row { int entry = -1; bool header = false, separator = false; std::string text; };

  SearchMenuPopup(std::vector<MenuItem> items, std::string placeholder, bool flyouts) : m_flyouts(flyouts) {
    collect(items, "");
    const bool compact = isCompact();
    float labels = 0;
    for (const Entry& e : m_entries) labels = std::max(labels, std::ceil(theme().ui->width(e.item.label)));
    const float maxW = Application::exists() ? std::max(180.f, Application::get().windowSize().x - 16.f) : 380.f;
    m_width = std::clamp(labels + (compact ? 96.f : 136.f), compact ? 200.f : 250.f, std::min(compact ? 320.f : 400.f, maxW));
    m_input = make<TextInput>("", std::move(placeholder));
    m_input->setLeadingIcon(Icon::Search);
    m_input->setClearable(true);
    m_input->onChanged = [this](const std::string& q) { m_query = q; closeSub(); rebuild(); };
    m_input->onCommit = [this](const std::string&) { accept(); };
    m_input->onCancel = [this] { Application::get().closePopup(this); };
    addChild(m_input);
    rebuild();
  }

  Vec2 measure(Vec2 avail) override {
    const bool compact = isCompact();
    float listH = 0;
    for (const Row& r : m_rows) listH += rowHeight(r);
    const float cap = compact ? 300.f : 440.f;
    const float maxH = std::max(110.f, std::min(cap, avail.y > 0 ? avail.y - 16.f : cap));
    const float h = std::clamp(topHeight() + std::max(listH, rowPx()) + 6.f, 76.f, maxH);
    if (m_height <= 0) m_height = h;
    const float w = avail.x > 0 ? std::min(m_width, std::max(160.f, avail.x - 16.f)) : m_width;
    m_desired = {w, std::min(maxH, std::max(m_height, h))};
    return m_desired;
  }
  void arrange(Rect r) override {
    m_rect = r;
    const float inputH = isCompact() ? 22.f : 26.f;
    m_input->measure({r.w - 12.f, inputH});
    m_input->arrange({r.x + 6.f, r.y + 5.f, r.w - 12.f, inputH});
  }
  bool hitSelf() const override { return true; }

  void paint(Canvas& c) override {
    const Theme& t = theme();
    const bool compact = isCompact();
    c.shadow(m_rect, 5, 14, Color(0, 0, 0, 150));
    c.fillRoundRect(m_rect, 5, t.popup);
    c.strokeRoundRect(m_rect, 5, t.borderStrong, 1);
    m_input->paint(c);
    const Rect list = listRect();
    const float totalH = contentHeight();
    const float maxScroll = std::max(0.f, totalH - list.h);
    m_scroll = std::clamp(m_scroll, 0.f, maxScroll);
    c.pushClip(list);
    float y = list.y - m_scroll;
    const float iconSz = compact ? 14.f : 16.f;
    for (size_t i = 0; i < m_rows.size(); i++) {
      const Row& row = m_rows[i];
      const Rect rr{list.x, y, list.w, rowHeight(row)};
      y += rr.h;
      if (rr.bottom() < list.y || rr.y > list.bottom()) continue;
      if (row.separator) {
        c.fillRect({rr.x + 6, std::floor(rr.y + rr.h * 0.5f), rr.w - 12, 1}, t.borderSoft);
        continue;
      }
      if (row.header) {
        const bool section = row.entry >= 0;
        if (section) {
          c.textIn(*t.small, {rr.x + 8, rr.y + 1, rr.w - 16, rr.h - 1}, row.text, t.textDisabled, HAlign::Left, true);
          const float tw = t.small->width(row.text);
          c.fillRect({rr.x + 14 + tw, std::floor(rr.y + rr.h * 0.5f + 1), rr.w - 22 - tw, 1}, t.borderSoft);
        } else {
          c.textIn(*t.semibold, {rr.x + 8, rr.y + 1, rr.w - 16, rr.h - 1}, row.text, t.textDim, HAlign::Left, true);
        }
        continue;
      }
      const Entry& e = m_entries[size_t(row.entry)];
      const bool hot = int(i) == m_sel && e.item.enabled;
      if (hot) c.fillRoundRect(rr.deflated(Margin(2, 1)), 3, t.rowSelected);
      const Color fg = !e.item.enabled ? t.textDisabled : (hot ? t.textBright : t.text);
      const Color dim = !e.item.enabled ? t.textDisabled : (hot ? t.text : t.textDim);
      const Rect ic{rr.x + 6, rr.y + (rr.h - iconSz) * 0.5f, iconSz, iconSz};
      if (e.item.image && e.item.imageSize > 0) c.blitBlend(e.item.image, e.item.imageSize, e.item.imageSize, e.item.imageSize, ic);
      else if (e.item.icon != Icon::None) drawIcon(c, e.item.icon, ic, dim);
      const bool searching = !m_query.empty();
      const float textX = compact ? 26.f : 30.f;
      const float labelW = searching && !e.group.empty() ? rr.w * 0.62f : rr.w - 34.f;
      c.textIn(*t.ui, {rr.x + textX, rr.y, std::max(24.f, labelW - textX), rr.h}, e.item.label, fg, HAlign::Left, true);
      if (e.flyout) {
        drawIcon(c, Icon::ChevronRight, {rr.right() - 20, ic.y, iconSz, iconSz}, dim, 1.4f);
      } else if (searching && !e.group.empty()) {
        c.textIn(*t.small, {rr.x + labelW, rr.y, rr.w - labelW - 8, rr.h}, e.group, dim, HAlign::Right, true);
      } else if (!e.item.shortcut.empty()) {
        c.textIn(*t.small, {rr.x, rr.y, rr.w - 8, rr.h}, e.item.shortcut, dim, HAlign::Right, true);
      }
    }
    c.popClip();
    if (maxScroll > 0.f) {
      const float trackH = std::max(16.f, list.h - 4.f);
      const float thumbH = std::clamp(trackH * (list.h / totalH), 18.f, trackH);
      const float thumbY = list.y + 2.f + (trackH - thumbH) * (m_scroll / maxScroll);
      c.fillRoundRect({m_rect.right() - 5.f, thumbY, 3.f, thumbH}, 1.5f, t.textDim);
    }
  }

  bool onMouseMove(const MouseEvent& e) override {
    if (m_listPressed && contentHeight() > listRect().h + 0.5f) {
      const float dy = e.pos.y - m_pressY;
      if (!m_didDragScroll && std::abs(dy) > 6.f) {
        m_didDragScroll = true;
        closeSub();
      }
      if (m_didDragScroll) {
        m_scroll = std::clamp(m_pressScroll - dy, 0.f, std::max(0.f, contentHeight() - listRect().h));
        invalidate();
        return true;
      }
    }
    const int i = rowAt(e.pos);
    if (i >= 0 && pickable(i) && i != m_sel) { m_sel = i; invalidate(); }
    if (i >= 0 && pickable(i) && isFlyout(i)) openSub(i);
    else if (i != m_subRow && m_rect.contains(e.pos)) closeSub();
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override {
    if (e.button == MouseButton::Left) {
      if (listRect().contains(e.pos)) {
        m_listPressed = true;
        m_didDragScroll = false;
        m_pressY = e.pos.y;
        m_pressScroll = m_scroll;
        const int i = rowAt(e.pos);
        if (i >= 0 && pickable(i)) { m_sel = i; invalidate(); }
      }
    }
    return true;
  }
  bool onMouseUp(const MouseEvent& e) override {
    if (e.button != MouseButton::Left) return true;
    const bool wasPressed = m_listPressed;
    const bool dragged = m_didDragScroll;
    m_listPressed = false;
    m_didDragScroll = false;
    if (wasPressed && !dragged) {
      const int i = rowAt(e.pos);
      if (i >= 0 && pickable(i)) { m_sel = i; accept(); }
    }
    return true;
  }
  bool onMouseWheel(const MouseEvent& e) override {
    m_scroll = std::clamp(m_scroll - e.wheel.y * 40.f, 0.f, std::max(0.f, contentHeight() - listRect().h));
    invalidate();
    return true;
  }
  bool onKeyDown(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Down: step(1); return true;
      case Key::Up: step(-1); return true;
      case Key::PageDown: for (int k = 0; k < 8; k++) step(1); return true;
      case Key::PageUp: for (int k = 0; k < 8; k++) step(-1); return true;
      case Key::Enter: accept(); return true;
      case Key::Right: if (m_sel >= 0 && isFlyout(m_sel)) { openSub(m_sel); return true; } return false;
      case Key::Escape: Application::get().closePopup(this); return true;
      default: return false;
    }
  }
  void focusInput() { Application::get().setFocus(m_input.get()); }

private:
  static bool isCompact() {
    return Application::exists() && Application::get().platform().singleSurface();
  }
  static float rowPx() { return isCompact() ? 20.f : 22.f; }
  static float headerPx() { return isCompact() ? 19.f : 22.f; }
  static float topHeight() { return isCompact() ? 31.f : 36.f; }
  static constexpr float kSep = 7.f;

  void collect(const std::vector<MenuItem>& items, const std::string& group) {
    for (const MenuItem& it : items) {
      if (it.separator || it.header) {
        if (group.empty()) m_entries.push_back({it, group});
        continue;
      }
      if (!it.sub.empty() && m_flyouts && group.empty()) {
        m_entries.push_back({it, group, true});
        collect(it.sub, it.label);
        continue;
      }
      if (!it.sub.empty()) {
        // A group at the top level; deeper submenus join the group they are in.
        collect(it.sub, group.empty() ? it.label : group);
        continue;
      }
      m_entries.push_back({it, group});
    }
  }
  static float rowHeight(const Row& r) { return r.separator ? kSep : r.header ? headerPx() : rowPx(); }
  float contentHeight() const {
    float h = 0;
    for (const Row& r : m_rows) h += rowHeight(r);
    return h;
  }
  Rect listRect() const { return {m_rect.x + 4, m_rect.y + topHeight(), m_rect.w - 8, std::max(0.f, m_rect.h - topHeight() - 4)}; }
  int rowAt(Vec2 p) const {
    const Rect l = listRect();
    if (!l.contains(p)) return -1;
    float y = l.y - m_scroll;
    for (size_t i = 0; i < m_rows.size(); i++) {
      const float h = rowHeight(m_rows[i]);
      if (p.y >= y && p.y < y + h) return int(i);
      y += h;
    }
    return -1;
  }
  bool isFlyout(int i) const {
    const Row& r = m_rows[size_t(i)];
    return !r.header && !r.separator && r.entry >= 0 && m_entries[size_t(r.entry)].flyout;
  }
  Rect rowRectOf(int i) const {
    const Rect l = listRect();
    float y = l.y - m_scroll;
    for (int k = 0; k < i; k++) y += rowHeight(m_rows[size_t(k)]);
    return {l.x, y, l.w, rowHeight(m_rows[size_t(i)])};
  }
  void openSub(int i) {
    if (m_subRow == i && m_sub) return;
    closeSub();
    m_subRow = i;
    std::weak_ptr<Widget> self = weak_from_this();
    m_sub = showSubmenu(m_entries[size_t(m_rows[size_t(i)].entry)].item.sub, rowRectOf(i), [self] {
      if (auto locked = self.lock()) {
        auto* p = static_cast<SearchMenuPopup*>(locked.get());
        p->m_sub.reset();
        p->m_subRow = -1;
      }
    });
  }
  void closeSub() {
    if (m_sub) { auto sub = m_sub; m_sub.reset(); Application::get().closePopup(sub.get()); }
    m_subRow = -1;
  }
  bool pickable(int i) const {
    const Row& r = m_rows[size_t(i)];
    return !r.header && !r.separator && r.entry >= 0 && m_entries[size_t(r.entry)].item.enabled;
  }
  void step(int d) {
    if (m_rows.empty()) return;
    int i = m_sel;
    for (size_t guard = 0; guard < m_rows.size(); guard++) {
      i += d;
      if (i < 0 || i >= int(m_rows.size())) return;
      if (pickable(i)) break;
    }
    if (i < 0 || i >= int(m_rows.size()) || !pickable(i)) return;
    m_sel = i;
    float top = 0;
    for (int k = 0; k < m_sel; k++) top += rowHeight(m_rows[size_t(k)]);
    const float bottom = top + rowPx(), vh = listRect().h;
    if (top < m_scroll) m_scroll = top;
    if (bottom > m_scroll + vh) m_scroll = bottom - vh;
    invalidate();
  }
  void accept() {
    if (m_sel < 0 || m_sel >= int(m_rows.size()) || !pickable(m_sel)) return;
    if (isFlyout(m_sel)) { openSub(m_sel); return; }
    closeSub();
    const std::function<void()> action = m_entries[size_t(m_rows[size_t(m_sel)].entry)].item.action;
    Application::get().closePopup(this);
    if (action) action();
  }
  void rebuild() {
    m_rows.clear();
    std::vector<std::string> tokens;
    {
      std::string cur;
      for (char ch : lowered(m_query)) {
        if (ch == ' ') { if (!cur.empty()) tokens.push_back(cur); cur.clear(); }
        else cur += ch;
      }
      if (!cur.empty()) tokens.push_back(cur);
    }
    if (tokens.empty()) {
      std::string group = "\x01";
      bool lastWasSep = true;
      for (size_t i = 0; i < m_entries.size(); i++) {
        const Entry& e = m_entries[i];
        if (e.item.separator) {
          if (!lastWasSep) m_rows.push_back({-1, false, true, ""});
          lastWasSep = true;
          continue;
        }
        if (e.item.header) {
          m_rows.push_back({int(i), true, false, e.item.label});
          lastWasSep = true;
          continue;
        }
        if (m_flyouts && !e.group.empty()) continue;   // inside a category: shown in its flyout
        if (e.group != group) {
          group = e.group;
          if (!group.empty()) {
            if (!lastWasSep && !m_rows.empty()) m_rows.push_back({-1, false, true, ""});
            m_rows.push_back({-1, true, false, group});
          }
        }
        m_rows.push_back({int(i), false, false, e.item.label});
        lastWasSep = false;
      }
      while (!m_rows.empty() && m_rows.back().separator) m_rows.pop_back();
    } else {
      std::vector<std::pair<int, int>> scored;
      for (size_t i = 0; i < m_entries.size(); i++) {
        const Entry& e = m_entries[i];
        if (e.item.separator || e.item.header || e.flyout) continue;
        const std::string name = lowered(e.item.label), hay = name + " " + lowered(e.group);
        int score = 0;
        bool ok = true;
        std::string squeezed = hay;   // "boxmesh" finds "Box Mesh"
        squeezed.erase(std::remove(squeezed.begin(), squeezed.end(), ' '), squeezed.end());
        for (const std::string& tok : tokens) {
          size_t pos = hay.find(tok);
          if (pos == std::string::npos && squeezed.find(tok) != std::string::npos) pos = name.size();
          if (pos == std::string::npos) { ok = false; break; }
          score += name.rfind(tok, 0) == 0 ? 0 : pos < name.size() ? 2 : 6;
        }
        if (ok) scored.push_back({score, int(i)});
      }
      std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first < b.first; });
      for (auto& [score, i] : scored) m_rows.push_back({i, false, false, m_entries[size_t(i)].item.label});
    }
    m_sel = -1;
    for (size_t k = 0; k < m_rows.size(); k++) if (pickable(int(k))) { m_sel = int(k); break; }
    m_scroll = 0;
    relayout();
    invalidate();
  }

  std::vector<Entry> m_entries;
  std::vector<Row> m_rows;
  Ref<TextInput> m_input;
  std::string m_query;
  float m_width = 300;
  float m_height = 0;
  bool m_flyouts = false;
  Ref<Widget> m_sub;
  int m_subRow = -1;
  int m_sel = -1;
  float m_scroll = 0;
  bool m_listPressed = false;
  bool m_didDragScroll = false;
  float m_pressY = 0;
  float m_pressScroll = 0;
};

} // namespace

void showSearchMenu(std::vector<MenuItem> items, Rect anchor, PopupPlacement placement, std::string placeholder, bool flyouts) {
  auto popup = make<SearchMenuPopup>(std::move(items), std::move(placeholder), flyouts);
  PopupOptions o;
  o.placement = placement;
  o.anchor = anchor;
  Application::get().showPopup(popup, o);
  // On touch/mobile hosts, avoid auto-focusing the search box so the soft keyboard does not pop up
  // over the menu items before the user explicitly taps the search field.
  if (!Application::get().platform().singleSurface()) {
    popup->focusInput();
  }
}

} // namespace prism
