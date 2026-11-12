// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/SearchMenu.h"

#include "prism/Application.h"
#include "prism/TextInput.h"

#include <algorithm>
#include <cctype>

namespace prism {
namespace {

const Color kPopupBg = Color::hex(0x252527);

std::string lowered(std::string s) {
  for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

class SearchMenuPopup : public Widget {
public:
  // One pickable entry: the item and the group it sits in ("" at the top level).
  struct Entry { MenuItem item; std::string group; };
  struct Row { int entry = -1; bool header = false, separator = false; std::string text; };

  SearchMenuPopup(std::vector<MenuItem> items, std::string placeholder) {
    collect(items, "");
    float labels = 0;
    for (const Entry& e : m_entries) labels = std::max(labels, std::ceil(theme().ui->width(e.item.label)));
    m_width = std::clamp(labels + 150.f, 260.f, 420.f);
    m_input = make<TextInput>("", std::move(placeholder));
    m_input->setLeadingIcon(Icon::Search);
    m_input->setClearable(true);
    m_input->onChanged = [this](const std::string& q) { m_query = q; rebuild(); };
    m_input->onCommit = [this](const std::string&) { accept(); };
    m_input->onCancel = [this] { Application::get().closePopup(this); };
    addChild(m_input);
    rebuild();
  }

  Vec2 measure(Vec2 avail) override {
    float listH = 0;
    for (const Row& r : m_rows) listH += rowHeight(r);
    const float maxH = std::max(160.f, std::min(480.f, avail.y > 0 ? avail.y - 16 : 480.f));
    m_desired = {m_width, std::clamp(kTop + std::max(listH, kRow) + 6, 80.f, maxH)};
    return m_desired;
  }
  void arrange(Rect r) override {
    m_rect = r;
    m_input->measure({r.w - 12, 28});
    m_input->arrange({r.x + 6, r.y + 6, r.w - 12, 28});
  }
  bool hitSelf() const override { return true; }

  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.shadow(m_rect, 5, 14, Color(0, 0, 0, 150));
    c.fillRoundRect(m_rect, 5, kPopupBg);
    c.strokeRoundRect(m_rect, 5, t.borderStrong, 1);
    m_input->paint(c);
    const Rect list = listRect();
    c.pushClip(list);
    float y = list.y - m_scroll;
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
        c.textIn(*t.semibold, {rr.x + 10, rr.y + 2, rr.w - 20, rr.h - 2}, row.text, t.textDim, HAlign::Left, true);
        continue;
      }
      const Entry& e = m_entries[size_t(row.entry)];
      const bool hot = int(i) == m_sel && e.item.enabled;
      if (hot) c.fillRoundRect(rr.deflated(Margin(2, 1)), 3, t.accent);
      const Color fg = !e.item.enabled ? t.textDisabled : (hot ? t.accentText : t.text);
      const Color dim = !e.item.enabled ? t.textDisabled : (hot ? t.accentText.alpha(0.8f) : t.textDim);
      const Rect ic{rr.x + 8, rr.y + (rr.h - 16) * 0.5f, 16, 16};
      if (e.item.image && e.item.imageSize > 0) c.blitBlend(e.item.image, e.item.imageSize, e.item.imageSize, e.item.imageSize, ic);
      else if (e.item.icon != Icon::None) drawIcon(c, e.item.icon, ic, dim);
      const bool searching = !m_query.empty();
      const float labelW = searching && !e.group.empty() ? rr.w * 0.62f : rr.w - 40;
      c.textIn(*t.ui, {rr.x + 32, rr.y, labelW - 32, rr.h}, e.item.label, fg, HAlign::Left, true);
      if (searching && !e.group.empty()) {
        c.textIn(*t.small, {rr.x + labelW, rr.y, rr.w - labelW - 10, rr.h}, e.group, dim, HAlign::Right, true);
      } else if (!e.item.shortcut.empty()) {
        c.textIn(*t.small, {rr.x, rr.y, rr.w - 10, rr.h}, e.item.shortcut, dim, HAlign::Right, true);
      }
    }
    c.popClip();
  }

  bool onMouseMove(const MouseEvent& e) override {
    const int i = rowAt(e.pos);
    if (i >= 0 && pickable(i) && i != m_sel) { m_sel = i; invalidate(); }
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override {
    const int i = rowAt(e.pos);
    if (i >= 0 && pickable(i)) { m_sel = i; accept(); }
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
      case Key::Escape: Application::get().closePopup(this); return true;
      default: return false;
    }
  }
  void focusInput() { Application::get().setFocus(m_input.get()); }

private:
  static constexpr float kRow = 24, kHeader = 24, kSep = 9, kTop = 40;

  void collect(const std::vector<MenuItem>& items, const std::string& group) {
    for (const MenuItem& it : items) {
      if (it.separator) {
        if (group.empty()) m_entries.push_back({it, group});
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
  static float rowHeight(const Row& r) { return r.separator ? kSep : r.header ? kHeader : kRow; }
  float contentHeight() const {
    float h = 0;
    for (const Row& r : m_rows) h += rowHeight(r);
    return h;
  }
  Rect listRect() const { return {m_rect.x + 4, m_rect.y + kTop, m_rect.w - 8, m_rect.h - kTop - 4}; }
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
    const float bottom = top + kRow, vh = listRect().h;
    if (top < m_scroll) m_scroll = top;
    if (bottom > m_scroll + vh) m_scroll = bottom - vh;
    invalidate();
  }
  void accept() {
    if (m_sel < 0 || m_sel >= int(m_rows.size()) || !pickable(m_sel)) return;
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
        if (e.item.separator) continue;
        const std::string name = lowered(e.item.label), hay = name + " " + lowered(e.group);
        int score = 0;
        bool ok = true;
        for (const std::string& tok : tokens) {
          const size_t pos = hay.find(tok);
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
  int m_sel = -1;
  float m_scroll = 0;
};

} // namespace

void showSearchMenu(std::vector<MenuItem> items, Rect anchor, PopupPlacement placement, std::string placeholder) {
  auto popup = make<SearchMenuPopup>(std::move(items), std::move(placeholder));
  PopupOptions o;
  o.placement = placement;
  o.anchor = anchor;
  Application::get().showPopup(popup, o);
  popup->focusInput();
}

} // namespace prism
