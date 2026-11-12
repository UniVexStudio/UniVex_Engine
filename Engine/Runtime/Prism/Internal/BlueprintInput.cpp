// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - BlueprintGraph interaction: mouse / keyboard, wiring, search palette, inline value editing.
#include "BlueprintInternal.h"
#include "prism/Application.h"
#include "prism/ColorPicker.h"
#include "prism/Menu.h"
#include "prism/Icons.h"
#include "prism/TextInput.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace prism {

using PT = BlueprintPinType;
using PK = BlueprintPinKind;
using namespace bp;

// ============================================================================ search palette
namespace {

std::string lower(std::string s) { for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch))); return s; }

class Palette : public Widget {
public:
  struct Row { bool header = false; std::string text, category; int spec = -1; };

  Palette(BlueprintPinFilter f, std::function<void(const BlueprintNodeSpec&)> pick, const std::vector<BlueprintNodeSpec>& lib)
      : m_filter(f), m_pick(std::move(pick)), m_lib(lib) {
    m_input = make<TextInput>("", "Search nodes...");
    m_input->setLeadingIcon(Icon::Search);
    m_input->setClearable(true);
    m_input->onChanged = [this](const std::string& q) { m_query = q; rebuild(); };
    m_input->onCommit = [this](const std::string&) { accept(); };
    addChild(m_input);
    rebuild();
  }

  Vec2 measure(Vec2) override { return m_desired = {350, 392}; }
  void arrange(Rect r) override {
    m_rect = r;
    m_input->measure({r.w - 16, 30});
    m_input->arrange({r.x + 8, r.y + 30, r.w - 16, 28});
  }
  bool hitSelf() const override { return true; }

  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.shadow(m_rect, 6, 14, Color(0, 0, 0, 160));
    c.fillRoundRect(m_rect, 6, t.panel);
    c.strokeRoundRect(m_rect, 6, t.borderStrong, 1);
    std::string head = "All Actions";
    if (m_filter.active) head = std::string("Actions for ") + blueprintPinTypeName(m_filter.type) + (m_filter.fromKind == PK::Output ? " output" : " input");
    c.textIn(*t.medium, {m_rect.x + 12, m_rect.y + 4, m_rect.w - 24, 24}, head, t.textBright, HAlign::Left, true);
    m_input->paint(c);
    Rect list = listRect();
    c.pushClip(list);
    float y = list.y - m_scroll;
    for (size_t i = 0; i < m_rows.size(); i++) {
      const Row& r = m_rows[i];
      Rect rr{list.x, y, list.w, kRow};
      y += kRow;
      if (rr.bottom() < list.y || rr.y > list.bottom()) continue;
      if (r.header) {
        c.textIn(*t.semibold, {rr.x + 12, rr.y, rr.w - 20, rr.h}, r.text, t.textDim, HAlign::Left, true);
        continue;
      }
      bool sel = int(i) == m_sel;
      if (sel) c.fillRoundRect(rr.deflated(Margin(4, 1)), 4, t.accent.alpha(0.85f));
      const BlueprintNodeSpec& sp = m_lib[size_t(r.spec)];
      BlueprintNode probe = sp.make();
      Color cc = chromeFor(probe.category).left;
      c.fillRoundRect({rr.x + 12, rr.y + 8, 4, rr.h - 16}, 2, cc);
      c.textIn(*t.ui, {rr.x + 24, rr.y, rr.w * 0.62f, rr.h}, r.text, sel ? Color(255, 255, 255) : t.text, HAlign::Left, true);
      c.textIn(*t.small, {rr.x + rr.w * 0.62f + 8, rr.y, rr.w * 0.38f - 20, rr.h}, r.category, sel ? Color(235, 240, 250) : t.textDim, HAlign::Right, true);
    }
    if (m_rows.empty()) c.textIn(*t.ui, list, "No matching nodes", t.textDisabled, HAlign::Center);
    c.popClip();
  }

  bool onMouseMove(const MouseEvent& e) override {
    int i = rowAt(e.pos);
    if (i >= 0 && !m_rows[size_t(i)].header && i != m_sel) { m_sel = i; invalidate(); }
    return true;
  }
  bool onMouseDown(const MouseEvent& e) override {
    int i = rowAt(e.pos);
    if (i >= 0 && !m_rows[size_t(i)].header) { m_sel = i; accept(); }
    return true;
  }
  bool onMouseWheel(const MouseEvent& e) override {
    m_scroll = std::clamp(m_scroll - e.wheel.y * 40.f, 0.f, std::max(0.f, float(m_rows.size()) * kRow - listRect().h));
    invalidate();
    return true;
  }
  bool onKeyDown(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Down: step(1); return true;
      case Key::Up: step(-1); return true;
      case Key::PageDown: step(8); return true;
      case Key::PageUp: step(-8); return true;
      case Key::Enter: accept(); return true;
      default: return false;
    }
  }
  void focusInput() { Application::get().setFocus(m_input.get()); }

private:
  static constexpr float kRow = 26;
  Rect listRect() const { return {m_rect.x + 4, m_rect.y + 64, m_rect.w - 8, m_rect.h - 70}; }
  int rowAt(Vec2 p) const {
    Rect l = listRect();
    if (!l.contains(p)) return -1;
    int i = int((p.y - l.y + m_scroll) / kRow);
    return i >= 0 && i < int(m_rows.size()) ? i : -1;
  }
  void step(int d) {
    if (m_rows.empty()) return;
    int i = m_sel;
    do { i += d > 0 ? 1 : -1; } while (i >= 0 && i < int(m_rows.size()) && m_rows[size_t(i)].header);
    if (i < 0 || i >= int(m_rows.size())) {
      // wrap/clamp: find the first/last selectable
      i = m_sel;
      for (int k = d > 0 ? int(m_rows.size()) - 1 : 0; k >= 0 && k < int(m_rows.size()); k += d > 0 ? -1 : 1) if (!m_rows[size_t(k)].header) { i = k; break; }
      if (d > 0 && std::abs(d) == 1) i = firstSelectable();
      else if (d < 0 && std::abs(d) == 1) i = lastSelectable();
    }
    m_sel = i;
    float top = float(m_sel) * kRow, bottom = top + kRow, vh = listRect().h;
    if (top < m_scroll) m_scroll = top;
    if (bottom > m_scroll + vh) m_scroll = bottom - vh;
    invalidate();
  }
  int firstSelectable() const { for (size_t k = 0; k < m_rows.size(); k++) if (!m_rows[k].header) return int(k); return -1; }
  int lastSelectable() const { for (int k = int(m_rows.size()) - 1; k >= 0; k--) if (!m_rows[size_t(k)].header) return k; return -1; }
  void accept() {
    if (m_sel < 0 || m_sel >= int(m_rows.size()) || m_rows[size_t(m_sel)].header) return;
    int spec = m_rows[size_t(m_sel)].spec;
    auto pick = m_pick;
    Application::get().closePopup(this);
    const BlueprintNodeSpec chosen = m_lib[size_t(spec)];
    if (pick) pick(chosen);
  }
  bool allowed(const BlueprintNodeSpec& s) const {
    if (!m_filter.active) return true;
    BlueprintNode n = s.make();
    const auto& pins = m_filter.fromKind == PK::Output ? n.inputs : n.outputs;
    for (auto& p : pins)
      if (m_filter.fromKind == PK::Output ? blueprintTypesCompatible(m_filter.type, p.type) : blueprintTypesCompatible(p.type, m_filter.type)) return true;
    return false;
  }
  void rebuild() {
    m_rows.clear();
    auto& lib = m_lib;
    std::vector<std::string> toks;
    {
      std::string cur;
      for (char ch : lower(m_query)) { if (ch == ' ') { if (!cur.empty()) toks.push_back(cur); cur.clear(); } else cur += ch; }
      if (!cur.empty()) toks.push_back(cur);
    }
    if (toks.empty()) {
      std::vector<std::string> cats;
      for (size_t i = 0; i < lib.size(); i++) {
        if (!allowed(lib[i])) continue;
        std::string top = lib[i].category.substr(0, lib[i].category.find('|'));
        if (std::find(cats.begin(), cats.end(), top) == cats.end()) cats.push_back(top);
      }
      for (auto& cat : cats) {
        m_rows.push_back({true, cat, "", -1});
        for (size_t i = 0; i < lib.size(); i++)
          if (lib[i].category.substr(0, lib[i].category.find('|')) == cat && allowed(lib[i])) {
            std::string sub = lib[i].category.find('|') == std::string::npos ? "" : lib[i].category.substr(lib[i].category.find('|') + 1);
            m_rows.push_back({false, lib[i].name, sub, int(i)});
          }
      }
    } else {
      std::vector<std::pair<int, int>> scored;
      for (size_t i = 0; i < lib.size(); i++) {
        if (!allowed(lib[i])) continue;
        std::string name = lower(lib[i].name), hay = name + " " + lower(lib[i].category) + " " + lower(lib[i].keywords);
        int score = 0;
        bool ok = true;
        std::string squeezed = hay;   // "oneshot" finds "One Shot"
        squeezed.erase(std::remove(squeezed.begin(), squeezed.end(), ' '), squeezed.end());
        for (auto& t : toks) {
          size_t pos = hay.find(t);
          if (pos == std::string::npos && squeezed.find(t) != std::string::npos) pos = name.size();
          if (pos == std::string::npos) { ok = false; break; }
          score += name.rfind(t, 0) == 0 ? 0 : pos < name.size() ? 2 : 6;
        }
        if (ok) scored.push_back({score, int(i)});
      }
      std::stable_sort(scored.begin(), scored.end(), [&](auto& a, auto& b) { return a.first < b.first; });
      for (auto& [sc, i] : scored) m_rows.push_back({false, lib[size_t(i)].name, lib[size_t(i)].category.substr(0, lib[size_t(i)].category.find('|')), i});
    }
    m_sel = firstSelectable();
    m_scroll = 0;
    invalidate();
  }

  BlueprintPinFilter m_filter;
  std::function<void(const BlueprintNodeSpec&)> m_pick;
  const std::vector<BlueprintNodeSpec>& m_lib;
  Ref<TextInput> m_input;
  std::string m_query;
  std::vector<Row> m_rows;
  int m_sel = -1;
  float m_scroll = 0;
};

// One-line popup editor for pin values and comment titles.
void textPopup(Widget* refocus, Rect anchor, const std::string& initial, float minW, std::function<bool(const std::string&)> accept) {
  auto in = make<TextInput>(initial);
  in->setFrameless(false);
  Application& app = Application::get();
  PopupOptions o;
  o.placement = PopupPlacement::AtPoint;
  o.anchor = {anchor.x, anchor.y, 0, 0};
  o.shadow = true;
  if (refocus) o.onClose = [refocus] { Application::get().setFocus(refocus); };   // keep keyboard shortcuts working afterwards
  Widget* raw = in.get();
  in->onCommit = [raw, accept](const std::string& s) {
    if (accept(s)) Application::get().closePopup(raw);
  };
  in->onCancel = [raw] { Application::get().closePopup(raw); };
  app.showPopup(in, o);
  (void)minW;
  in->selectAll();
  app.setFocus(in.get());
}

bool validValue(PT t, const std::string& v) {
  auto num = [](const std::string& s) { try { size_t k; std::stof(s, &k); return k == s.size(); } catch (...) { return false; } };
  switch (t) {
    case PT::Float: return num(v);
    case PT::Int: { try { size_t k; std::stoi(v, &k); return k == v.size(); } catch (...) { return false; } }
    case PT::Bool: return v == "true" || v == "false";
    case PT::Vector:
    case PT::Color: {
      float a, b, c; char extra;
      return std::sscanf(v.c_str(), "%f,%f,%f%c", &a, &b, &c, &extra) == 3;
    }
    default: return true;
  }
}

} // namespace

void BlueprintGraph::openPalette(Vec2 screenPos, const BlueprintPinFilter& filter, int fromNode, int fromPin) {
  Vec2 world = screenToWorld(screenPos);
  auto pal = make<Palette>(filter, [this, world, filter, fromNode, fromPin](const BlueprintNodeSpec& spec) { finishSpawn(spec, world, filter, fromNode, fromPin); }, nodeLibrary());
  PopupOptions o;
  o.placement = PopupPlacement::AtPoint;
  o.anchor = {screenPos.x, screenPos.y, 0, 0};
  o.onClose = [this] { Application::get().setFocus(this); };
  Application::get().showPopup(pal, o);
  pal->focusInput();
}

void BlueprintGraph::finishSpawn(const BlueprintNodeSpec& spec, Vec2 world, const BlueprintPinFilter& f, int fromNode, int fromPin) {
  if (m_snap) world = {std::round(world.x / 8) * 8, std::round(world.y / 8) * 8};
  // What the dragged pin was wired to, so the new node can be inserted into that wire.
  std::optional<BlueprintWire> was;
  // A wire picked up off an input and dropped in empty space: the new node goes back into it.
  if (m_pickedWire.has_value() && f.fromKind == PK::Output && m_pickedWire->fromNodeId == fromNode &&
      m_pickedWire->fromPinIndex == fromPin) {
    was = m_pickedWire;
  }
  m_pickedWire.reset();
  if (!was.has_value() && f.active && m_features.singleLinkOutputs) {
    for (const BlueprintWire& w : m_wires) {
      const bool same = f.fromKind == PK::Output ? (w.fromNodeId == fromNode && w.fromPinIndex == fromPin)
                                                 : (w.toNodeId == fromNode && w.toPinIndex == fromPin);
      if (same) { was = w; break; }
    }
  }
  int id = onSpawn ? onSpawn(spec, world) : spawnNode(spec, world);
  if (id < 0) return;
  BlueprintNode* n = nodeById(id);
  if (n && f.active && findNode(fromNode)) {
    // wire to the first compatible pin, preferring an exact type match
    auto tryPins = [&](bool exact) {
      const auto& pins = f.fromKind == PK::Output ? n->inputs : n->outputs;
      for (int i = 0; i < int(pins.size()); i++) {
        bool ok = f.fromKind == PK::Output ? blueprintTypesCompatible(f.type, pins[size_t(i)].type) : blueprintTypesCompatible(pins[size_t(i)].type, f.type);
        if (!ok || (exact && pins[size_t(i)].type != f.type)) continue;
        bool done = f.fromKind == PK::Output ? connect(fromNode, fromPin, id, i) : connect(id, i, fromNode, fromPin);
        if (done) {
          // line the new pin up with where the wire was dropped
          Vec2 dropAt = world;
          Vec2 local = pinLocal(*n, i, f.fromKind == PK::Output);
          n->pos = {dropAt.x - local.x + (f.fromKind == PK::Output ? 4.f : -4.f), dropAt.y - local.y};
          if (m_snap) n->pos = {std::round(n->pos.x / 8) * 8, std::round(n->pos.y / 8) * 8};
          return true;
        }
      }
      return false;
    };
    if (!tryPins(true)) tryPins(false);
    // Insert: the other end of the old wire goes to the new node's free side.
    if (was.has_value() && (n = nodeById(id)) != nullptr) {
      const bool fromOutput = f.fromKind == PK::Output;
      const auto& pins = fromOutput ? n->outputs : n->inputs;
      for (int i = 0; i < int(pins.size()); i++) {
        const bool used = std::any_of(m_wires.begin(), m_wires.end(), [&](const BlueprintWire& w) {
          return fromOutput ? (w.fromNodeId == id && w.fromPinIndex == i) : (w.toNodeId == id && w.toPinIndex == i);
        });
        if (used) continue;
        const bool done = fromOutput ? connect(id, i, was->toNodeId, was->toPinIndex) : connect(was->fromNodeId, was->fromPinIndex, id, i);
        if (done) break;
      }
    }
  }
  select(id);
  commit("Add " + spec.name);
}

// ============================================================================ hit testing
BlueprintGraph::PinHit BlueprintGraph::pinAt(Vec2 p) const {
  float rad = std::max(9.f, 12.f * m_zoom);
  PinHit best;
  float bestD = rad;
  for (const auto& n : m_nodes) {
    if (!nodeScreenRect(n).inflated(rad).contains(p)) continue;
    for (int pass = 0; pass < 2; pass++) {
      bool in = pass == 0;
      int cnt = int(in ? n.inputs.size() : n.outputs.size());
      for (int i = 0; i < cnt; i++) {
        float d = (pinScreen(n, i, in) - p).length();
        if (d < bestD) { bestD = d; best = {n.id, i, in}; }
      }
    }
  }
  return best;
}

const BlueprintNode* BlueprintGraph::nodeAt(Vec2 p) const {
  for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it)
    if (it->selected && nodeScreenRect(*it).contains(p)) return &*it;
  for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it)
    if (nodeScreenRect(*it).contains(p)) return &*it;
  return nullptr;
}

int BlueprintGraph::wireAt(Vec2 p) const {
  if (m_wires.size() > 600) return -1;
  int best = -1;
  float bestD = std::max(5.f, 6.f * m_zoom);
  for (size_t wi = 0; wi < m_wires.size(); wi++) {
    const auto& w = m_wires[wi];
    const BlueprintNode* a = findNode(w.fromNodeId);
    const BlueprintNode* b = findNode(w.toNodeId);
    if (!a || !b) continue;
    Vec2 pa = pinScreen(*a, w.fromPinIndex, false), pb = pinScreen(*b, w.toPinIndex, true);
    Rect bb{std::min(pa.x, pb.x) - 60, std::min(pa.y, pb.y) - 60, std::fabs(pa.x - pb.x) + 120, std::fabs(pa.y - pb.y) + 120};
    if (!bb.contains(p)) continue;
    auto pts = wirePoints(pa, pb, 32);
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      Vec2 d = pts[i + 1] - pts[i], q = p - pts[i];
      float len2 = d.x * d.x + d.y * d.y, t = len2 > 0 ? std::clamp((q.x * d.x + q.y * d.y) / len2, 0.f, 1.f) : 0.f;
      float dist = (q - d * t).length();
      if (dist < bestD) { bestD = dist; best = int(wi); }
    }
  }
  return best;
}

BlueprintComment* BlueprintGraph::commentTitleAt(Vec2 p) {
  for (auto it = m_comments.rbegin(); it != m_comments.rend(); ++it) {
    Vec2 o = worldToScreen(it->bounds.pos());
    if (Rect{o.x, o.y, it->bounds.w * m_zoom, 26 * m_zoom}.contains(p)) return &*it;
  }
  return nullptr;
}

BlueprintComment* BlueprintGraph::commentResizeAt(Vec2 p) {
  for (auto it = m_comments.rbegin(); it != m_comments.rend(); ++it) {
    Vec2 o = worldToScreen(it->bounds.pos());
    Rect r{o.x, o.y, it->bounds.w * m_zoom, it->bounds.h * m_zoom};
    if (Rect{r.right() - 18, r.bottom() - 18, 18, 18}.contains(p)) return &*it;
  }
  return nullptr;
}

BlueprintGraph::PinHit BlueprintGraph::defaultBoxAt(Vec2 p) const {
  for (const auto& n : m_nodes) {
    if (!nodeScreenRect(n).contains(p)) continue;
    for (int i = 0; i < int(n.inputs.size()); i++) {
      const BlueprintPin& pin = n.inputs[size_t(i)];
      if (pin.type == PT::Exec || pin.defaultValue.empty() || isPinConnected(n.id, i, true)) continue;
      Rect lb = defaultBoxLocal(n, i);
      Vec2 o = worldToScreen(n.pos);
      if (Rect{o.x + lb.x * m_zoom, o.y + lb.y * m_zoom, lb.w * m_zoom, lb.h * m_zoom}.inflated(2).contains(p)) return {n.id, i, true};
    }
  }
  return {};
}

// ============================================================================ editing helpers
void BlueprintGraph::editDefault(const PinHit& h) {
  BlueprintNode* n = nodeById(h.node);
  if (!n) return;
  BlueprintPin& p = n->inputs[size_t(h.pin)];
  PT t = p.type;
  if (t == PT::Bool) {
    p.defaultValue = p.defaultValue == "true" ? "false" : "true";
    commit("Toggle " + p.name);
    invalidate();
    return;
  }
  if (t == PT::Color) {                      // colour pins open the floating colour picker
    float r = 1, g = 1, b = 1;
    std::sscanf(p.defaultValue.c_str(), "%f,%f,%f", &r, &g, &b);
    if (r > 1 || g > 1 || b > 1) { r /= 255; g /= 255; b /= 255; }
    int cid = n->id, cpi = h.pin;
    std::string cname = p.name;
    auto write = [this, cid, cpi](Color c, bool final, const std::string& nm) {
      if (BlueprintNode* nn = nodeById(cid)) {
        char buf[64]; std::snprintf(buf, sizeof buf, "%.3f,%.3f,%.3f", c.r / 255.f, c.g / 255.f, c.b / 255.f);
        nn->inputs[size_t(cpi)].defaultValue = buf;
        if (final) commit("Edit " + nm);
        touch();
      }
    };
    std::string before = p.defaultValue;
    showColorPickerWindow(Color(uint8_t(std::clamp(r, 0.f, 1.f) * 255 + .5f), uint8_t(std::clamp(g, 0.f, 1.f) * 255 + .5f), uint8_t(std::clamp(b, 0.f, 1.f) * 255 + .5f)),
      [write, cname](Color c) { write(c, true, cname); },
      [this, cid, cpi, before] { if (BlueprintNode* nn = nodeById(cid)) { nn->inputs[size_t(cpi)].defaultValue = before; touch(); } },
      [write, cname](Color c) { write(c, false, cname); });
    return;
  }
  Rect lb = defaultBoxLocal(*n, h.pin);
  Vec2 o = worldToScreen(n->pos);
  int id = n->id, pi = h.pin;
  std::string name = p.name;
  textPopup(this, {o.x + lb.x * m_zoom, o.y + lb.y * m_zoom, 0, 0}, p.defaultValue, 120, [this, id, pi, t, name](const std::string& v) {
    if (!validValue(t, v)) { Application::get().notify("'" + v + "' is not a valid " + blueprintPinTypeName(t) + " value", ToastLevel::Warning, 1800); return false; }
    if (BlueprintNode* nn = nodeById(id)) {
      nn->inputs[size_t(pi)].defaultValue = v;
      layoutNode(*nn);
      commit("Edit " + name);
      touch();
    }
    return true;
  });
}

void BlueprintGraph::editComment(BlueprintComment& c) {
  Vec2 o = worldToScreen(c.bounds.pos());
  int id = c.id;
  textPopup(this, {o.x + 8, o.y + 2, 0, 0}, c.title, 200, [this, id](const std::string& v) {
    for (auto& cm : m_comments) if (cm.id == id) { cm.title = v; commit("Rename comment"); invalidate(); }
    return true;
  });
}

void BlueprintGraph::addCommentAround(const std::vector<int>& ids, Vec2 fallback) {
  BlueprintComment c;
  c.id = nextId();
  m_nextId = c.id + 1;
  c.title = "Comment";
  static const Color tints[] = {Color(40, 100, 160, 52), Color(60, 140, 80, 52), Color(160, 100, 40, 52), Color(130, 60, 160, 52)};
  c.color = tints[size_t(c.id) % 4];
  if (ids.empty()) c.bounds = {fallback.x, fallback.y, 260, 160};
  else {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int id : ids) if (auto* n = findNode(id)) { x0 = std::min(x0, n->pos.x); y0 = std::min(y0, n->pos.y); x1 = std::max(x1, n->pos.x + n->size.x); y1 = std::max(y1, n->pos.y + n->size.y); }
    c.bounds = {x0 - 24, y0 - 46, x1 - x0 + 48, y1 - y0 + 70};
  }
  m_comments.push_back(c);
  commit("Add comment");
  touch();
  editComment(m_comments.back());
}

void BlueprintGraph::insertReroute(int wi, Vec2 world) {
  if (wi < 0 || wi >= int(m_wires.size())) return;
  const BlueprintNodeSpec* spec = nullptr;
  for (auto& s : blueprintNodeLibrary()) if (s.name == "Reroute Node") spec = &s;
  if (!spec) return;
  BlueprintWire w = m_wires[size_t(wi)];
  m_wires.erase(m_wires.begin() + wi);
  int id = spawnNode(*spec, {world.x - 17, world.y - 11});
  if (m_snap) if (auto* n = nodeById(id)) n->pos = {std::round(n->pos.x / 8) * 8, std::round(n->pos.y / 8) * 8};
  connect(w.fromNodeId, w.fromPinIndex, id, 0);
  connect(id, 0, w.toNodeId, w.toPinIndex);
  commit("Add reroute");
}

// ============================================================================ wiring
void BlueprintGraph::beginWire(const PinHit& h) {
  m_wireFrom = h;
  m_wireTarget = {};
  m_wireValid = false;
  m_pickedWire.reset();
  const bool input = h.input;
  // Grabbing a connected data input picks the wire up: the wire's source becomes the dragged end.
  if (input) {
    PT t = resolvedType(h.node, h.pin, true);
    if (t != PT::Exec) {
      for (size_t i = 0; i < m_wires.size(); i++) {
        if (m_wires[i].toNodeId == h.node && m_wires[i].toPinIndex == h.pin) {
          m_wireFrom = {m_wires[i].fromNodeId, m_wires[i].fromPinIndex, false};
          if (m_features.singleLinkOutputs) m_pickedWire = m_wires[i];
          m_wires.erase(m_wires.begin() + long(i));
          touch();
          break;
        }
      }
    }
  }
  m_drag = Drag::Wire;
}

void BlueprintGraph::finishWire(Vec2 pos) {
  PinHit from = m_wireFrom, target = m_wireTarget;
  bool valid = m_wireValid;
  m_drag = Drag::None;
  m_wireTarget = {};
  m_wireFrom = {};
  if (target && valid) {
    m_pickedWire.reset();
    bool ok = from.input ? connect(target.node, target.pin, from.node, from.pin) : connect(from.node, from.pin, target.node, target.pin);
    commit(ok ? "Connect pins" : "Disconnect");
    return;
  }
  if (!target) {
    if (!m_dragMoved) { commit("Disconnect"); return; }
    BlueprintPinFilter f;
    f.active = true;
    f.type = resolvedType(from.node, from.pin, from.input);
    if (f.type == PT::Wildcard) f.active = false;
    f.fromKind = from.input ? PK::Input : PK::Output;
    commit("Disconnect");            // the pick-up above may have removed a wire
    openPalette(pos, f, from.node, from.pin);
    return;
  }
  commit("Disconnect");
}

// ============================================================================ mouse
bool BlueprintGraph::toolbarClick(Vec2 p) {
  for (auto& b : m_btns) {
    if (!b.r.contains(p)) continue;
    const std::string& id = b.id;
    if (id == "preset:menu" || id == "preset:caret") {
      std::vector<MenuItem> items;
      const BlueprintGraphPreset order[] = {BlueprintGraphPreset::WidgetBlueprint, BlueprintGraphPreset::EventBlueprint, BlueprintGraphPreset::GameplayScript,
          BlueprintGraphPreset::AnimationGraph, BlueprintGraphPreset::StateMachine, BlueprintGraphPreset::BehaviorTree,
          BlueprintGraphPreset::ParticleGraph, BlueprintGraphPreset::AudioGraph, BlueprintGraphPreset::MaterialShader};
      const char* lastGroup = "";
      for (auto pr : order) {
        if (std::string(lastGroup) != blueprintPresetGroup(pr)) {
          if (*lastGroup) items.push_back(MenuItem::Sep());
          lastGroup = blueprintPresetGroup(pr);
        }
        {
          MenuItem mi = MenuItem::Item(blueprintPresetName(pr), [this, pr] { setPreset(pr); }, m_preset == pr ? "current" : "", blueprintPresetIcon(pr));
          items.push_back(std::move(mi));
        }
      }
      showMenu(std::move(items), b.r, PopupPlacement::Below);
      return true;
    }
    else if (id == "compile:opts") {
      showMenu({MenuItem::Check("Save on Compile", m_saveOnCompile, [this] { m_saveOnCompile = !m_saveOnCompile; }),
                MenuItem::Item("Jump to Error Node", [this] { jumpToError(); }, "", Icon::Error),
                MenuItem::Sep(),
                MenuItem::Item("Compile Now", [this] { compile(); }, "", Icon::Compile)}, b.r, PopupPlacement::Below);
      return true;
    }
    else if (id == "nav:back") navBack();
    else if (id == "nav:fwd") navForward();
    else if (id == "hide") m_hideUnrelated = !m_hideUnrelated;
    else if (id == "diff") { showDiff(b.r); return true; }
    else if (id == "classsettings") showClassSettings();
    else if (id == "classdefaults") showClassDefaults();
    else if (id == "layout") autoLayout(selectedCount() >= 2);
    else if (id == "variables") showVariables();
    else if (id == "collapse") {
      auto ids = selectedIds();
      if (ids.size() == 1 && isCollapsed(ids[0])) expandNode(ids[0]); else collapseSelected();
    }
    else if (id == "save") saveToFile();
    else if (id == "load") { openLoadMenu(b.r); return true; }
    else if (id == "find") { openFind(b.r); return true; }
    else if (id == "browse") { if (onBrowse) onBrowse(); else Application::get().notify("Browse: no content browser connected", ToastLevel::Info, 1500); }
    else if (id == "undo") undo();
    else if (id == "redo") redo();
    else if (id == "run") run();
    else if (id == "compile") {
      auto r = compile();
      if (r.errors) Application::get().notify(std::to_string(r.errors) + " error(s): " + r.messages.front(), ToastLevel::Error);
      else if (r.warnings) Application::get().notify(std::to_string(r.warnings) + " warning(s): " + r.messages.front(), ToastLevel::Warning);
      else Application::get().notify("Compiled successfully - 0 errors, 0 warnings", ToastLevel::Success);
    } else if (id == "snap") m_snap = !m_snap;
    else if (id == "minimap") m_showMinimap = !m_showMinimap;
    else if (id == "frame") frameAll();
    invalidate();
    return true;
  }
  return false;
}

void BlueprintGraph::zoomAt(Vec2 p, float factor) {
  Vec2 w = screenToWorld(p);
  m_zoom = std::clamp(m_zoom * factor, 0.3f, 2.0f);
  m_pan = {p.x - m_graphRect.x - w.x * m_zoom, p.y - m_graphRect.y - w.y * m_zoom};
  invalidate();
}

bool BlueprintGraph::onMouseDown(const MouseEvent& e) {
  Application::get().setFocus(this);
  m_dragStartScreen = m_dragCurrent = e.pos;
  m_dragMoved = false;
  m_rightDown = false;
  if (e.pos.y < m_graphRect.y) return e.button == MouseButton::Left ? toolbarClick(e.pos) || true : true;

  if (m_features.minimap && m_showMinimap && m_minimapRect.w > 0 && m_minimapRect.contains(e.pos) && e.button == MouseButton::Left) {
    m_drag = Drag::Minimap;
    return onMouseMove(e), true;
  }
  if (e.button == MouseButton::Middle || e.button == MouseButton::Right) {
    m_drag = Drag::Pan;
    m_panStart = m_pan;
    m_rightDown = e.button == MouseButton::Right;
    return true;
  }
  if (e.button != MouseButton::Left) return false;

  const bool additive = (e.mods & (ModCtrl | ModShift)) != 0;
  if (PinHit h = pinAt(e.pos)) {
    if (e.mods & ModAlt) { if (disconnectPin(h.node, h.pin, h.input)) commit("Break links"); return true; }
    beginWire(h);
    return true;
  }
  if (PinHit d = defaultBoxAt(e.pos)) { editDefault(d); return true; }

  if (const BlueprintNode* hit = nodeAt(e.pos)) {
    int id = hit->id;
    if (e.clicks == 2 && onNodeDoubleClicked) { select(id); onNodeDoubleClicked(id); return true; }
    BlueprintNode* n = nodeById(id);
    if (additive) n->selected = !n->selected;
    else if (!n->selected) { clearSelection(); n->selected = true; }
    else m_pendingSelect = id;                 // already part of a selection: collapse on release if it was a plain click
    m_dragOrigin.clear();
    for (auto& nn : m_nodes) if (nn.selected) m_dragOrigin.push_back({nn.id, nn.pos});
    m_drag = m_dragOrigin.empty() ? Drag::None : Drag::Nodes;
    invalidate();
    return true;
  }
  if (BlueprintComment* rc = commentResizeAt(e.pos)) {
    m_drag = Drag::CommentResize;
    m_dragCommentId = rc->id;
    m_commentSizeStart = {rc->bounds.w, rc->bounds.h};
    return true;
  }
  if (BlueprintComment* cm = commentTitleAt(e.pos)) {
    if (e.clicks == 2) { editComment(*cm); return true; }
    if (!additive) clearSelection();
    cm->selected = true;
    m_drag = Drag::Comment;
    m_dragCommentId = cm->id;
    m_dragOrigin.clear();
    m_commentOrigin.clear();
    for (auto& c : m_comments) if (c.selected) m_commentOrigin.push_back({c.id, c.bounds.pos()});
    for (auto& n : m_nodes) {            // nodes fully inside the comment travel with it
      if (cm->bounds.contains(n.pos) && cm->bounds.contains(n.pos + n.size)) m_dragOrigin.push_back({n.id, n.pos});
    }
    invalidate();
    return true;
  }
  int wi = wireAt(e.pos);
  if (wi >= 0) {
    if (e.mods & ModAlt) { m_wires.erase(m_wires.begin() + wi); touch(); commit("Delete wire"); return true; }
    if (e.clicks == 2) { insertReroute(wi, screenToWorld(e.pos)); return true; }
  }
  // empty space: marquee
  m_dragOrigin.clear();
  if (!additive) clearSelection();
  for (auto& n : m_nodes) if (n.selected) m_dragOrigin.push_back({n.id, n.pos});   // keep for additive marquee
  m_drag = Drag::Marquee;
  return true;
}

bool BlueprintGraph::onMouseMove(const MouseEvent& e) {
  m_dragCurrent = e.pos;
  if (m_drag == Drag::None) {
    // hover feedback
    int btn = -1;
    if (e.pos.y < m_graphRect.y) for (size_t i = 0; i < m_btns.size(); i++) if (m_btns[i].r.contains(e.pos)) btn = int(i);
    PinHit hp = e.pos.y >= m_graphRect.y ? pinAt(e.pos) : PinHit{};
    const BlueprintNode* nh = e.pos.y >= m_graphRect.y ? nodeAt(e.pos) : nullptr;
    int wh = (!hp && !nh && e.pos.y >= m_graphRect.y) ? wireAt(e.pos) : -1;
    bool changed = btn != m_toolbarHover || hp.node != m_hoverPin.node || hp.pin != m_hoverPin.pin || hp.input != m_hoverPin.input ||
                   (nh ? nh->id : -1) != m_hoverNode || wh != m_hoverWire;
    m_toolbarHover = btn; m_hoverPin = hp; m_hoverNode = nh ? nh->id : -1; m_hoverWire = wh;
    if (changed) invalidate();
    return false;
  }
  if ((e.pos - m_dragStartScreen).length() > 3) m_dragMoved = true;
  switch (m_drag) {
    case Drag::Pan:
      m_pan = m_panStart + (e.pos - m_dragStartScreen);
      invalidate();
      break;
    case Drag::Nodes: {
      if (!m_dragMoved) break;
      Vec2 d = (e.pos - m_dragStartScreen) / m_zoom;
      bool snap = m_snap && !(e.mods & ModAlt);
      for (auto& [id, org] : m_dragOrigin) {
        BlueprintNode* n = nodeById(id);
        if (!n) continue;
        Vec2 p = org + d;
        if (snap) p = {std::round(p.x / 8) * 8, std::round(p.y / 8) * 8};
        n->pos = p;
      }
      invalidate();
      break;
    }
    case Drag::Comment: {
      Vec2 d = (e.pos - m_dragStartScreen) / m_zoom;
      bool snap = m_snap && !(e.mods & ModAlt);
      auto sn = [&](Vec2 p) { return snap ? Vec2(std::round(p.x / 8) * 8, std::round(p.y / 8) * 8) : p; };
      for (auto& [id, org] : m_commentOrigin) for (auto& c : m_comments) if (c.id == id) { Vec2 p = sn(org + d); c.bounds.x = p.x; c.bounds.y = p.y; }
      for (auto& [id, org] : m_dragOrigin) if (BlueprintNode* n = nodeById(id)) n->pos = sn(org + d);
      invalidate();
      break;
    }
    case Drag::CommentResize:
      for (auto& c : m_comments)
        if (c.id == m_dragCommentId) {
          Vec2 d = (e.pos - m_dragStartScreen) / m_zoom;
          c.bounds.w = std::max(90.f, m_commentSizeStart.x + d.x);
          c.bounds.h = std::max(60.f, m_commentSizeStart.y + d.y);
          if (m_snap) { c.bounds.w = std::round(c.bounds.w / 8) * 8; c.bounds.h = std::round(c.bounds.h / 8) * 8; }
        }
      invalidate();
      break;
    case Drag::Marquee: {
      Rect m{std::min(m_dragStartScreen.x, e.pos.x), std::min(m_dragStartScreen.y, e.pos.y), std::fabs(e.pos.x - m_dragStartScreen.x), std::fabs(e.pos.y - m_dragStartScreen.y)};
      for (auto& n : m_nodes) {
        bool keep = std::any_of(m_dragOrigin.begin(), m_dragOrigin.end(), [&](auto& o) { return o.first == n.id; });
        n.selected = keep || nodeScreenRect(n).intersects(m);
      }
      invalidate();
      break;
    }
    case Drag::Wire: {
      PinHit t = pinAt(e.pos);
      if (!t) {            // dropping on a node body connects to its first compatible pin
        if (const BlueprintNode* nh = nodeAt(e.pos)) {
          PT ft = resolvedType(m_wireFrom.node, m_wireFrom.pin, m_wireFrom.input);
          const auto& pins = m_wireFrom.input ? nh->outputs : nh->inputs;
          for (int i = 0; i < int(pins.size()) && !t; i++) {
            bool ok = m_wireFrom.input ? canConnect(nh->id, i, m_wireFrom.node, m_wireFrom.pin) : canConnect(m_wireFrom.node, m_wireFrom.pin, nh->id, i);
            if (ok && (pins[size_t(i)].type == ft || ft == PT::Wildcard)) t = {nh->id, i, !m_wireFrom.input};
          }
          for (int i = 0; i < int(pins.size()) && !t; i++) {
            bool ok = m_wireFrom.input ? canConnect(nh->id, i, m_wireFrom.node, m_wireFrom.pin) : canConnect(m_wireFrom.node, m_wireFrom.pin, nh->id, i);
            if (ok) t = {nh->id, i, !m_wireFrom.input};
          }
        }
      }
      m_wireTarget = t;
      m_wireValid = false;
      m_wireWhy.clear();
      if (t) {
        if (t.input == m_wireFrom.input) m_wireWhy = t.input ? "Cannot connect an input to an input" : "Cannot connect an output to an output";
        else m_wireValid = m_wireFrom.input ? canConnect(t.node, t.pin, m_wireFrom.node, m_wireFrom.pin, &m_wireWhy)
                                            : canConnect(m_wireFrom.node, m_wireFrom.pin, t.node, t.pin, &m_wireWhy);
      }
      invalidate();
      break;
    }
    case Drag::Minimap: {
      Rect in = m_minimapRect.deflated(6);
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
      for (auto& n : m_nodes) { x0 = std::min(x0, n.pos.x); y0 = std::min(y0, n.pos.y); x1 = std::max(x1, n.pos.x + n.size.x); y1 = std::max(y1, n.pos.y + n.size.y); }
      Vec2 vtl = screenToWorld(m_graphRect.pos()), vbr = screenToWorld({m_graphRect.right(), m_graphRect.bottom()});
      x0 = std::min(x0, vtl.x); y0 = std::min(y0, vtl.y); x1 = std::max(x1, vbr.x); y1 = std::max(y1, vbr.y);
      float k = std::min(in.w / std::max(1.f, x1 - x0), in.h / std::max(1.f, y1 - y0));
      Vec2 off{in.x + (in.w - (x1 - x0) * k) * 0.5f - x0 * k, in.y + (in.h - (y1 - y0) * k) * 0.5f - y0 * k};
      Vec2 world{(e.pos.x - off.x) / k, (e.pos.y - off.y) / k};
      m_pan = {m_graphRect.w * 0.5f - world.x * m_zoom, m_graphRect.h * 0.5f - world.y * m_zoom};
      invalidate();
      break;
    }
    default: break;
  }
  return true;
}

bool BlueprintGraph::onMouseUpInner(const MouseEvent& e) {
  Drag d = m_drag;
  if (d == Drag::Wire) { m_dragMoved = m_dragMoved || (e.pos - m_dragStartScreen).length() > 3; finishWire(e.pos); invalidate(); return true; }
  m_drag = Drag::None;
  switch (d) {
    case Drag::Pan:
      if (m_rightDown && !m_dragMoved) openPalette(e.pos, {});
      break;
    case Drag::Nodes:
      if (m_dragMoved) commit("Move nodes");
      else if (m_pendingSelect >= 0) select(m_pendingSelect);
      break;
    case Drag::Comment:
    case Drag::CommentResize:
      if (m_dragMoved) commit(d == Drag::Comment ? "Move comment" : "Resize comment");
      break;
    default: break;
  }
  m_pendingSelect = -1;
  invalidate();
  return true;
}

bool BlueprintGraph::onMouseUp(const MouseEvent& e) {
  const bool handled = onMouseUpInner(e);
  notifySelection();
  return handled;
}

void BlueprintGraph::onMouseLeave() {
  m_hoverPin = {}; m_hoverNode = -1; m_hoverWire = -1; m_toolbarHover = -1;
  invalidate();
}

bool BlueprintGraph::onMouseWheel(const MouseEvent& e) {
  if (e.pos.y < m_graphRect.y) return false;
  zoomAt(e.pos, std::pow(1.12f, e.wheel.y));
  return true;
}

const std::string& BlueprintGraph::tooltipAt(Vec2 p) const {
  m_tipCache.clear();
  if (p.y < m_graphRect.y) {
    for (auto& b : m_btns) if (b.r.contains(p)) m_tipCache = b.tip;
    return m_tipCache;
  }
  if (m_drag != Drag::None) return m_tipCache;
  if (PinHit h = pinAt(p)) {
    const BlueprintNode* n = findNode(h.node);
    const BlueprintPin& pin = (h.input ? n->inputs : n->outputs)[size_t(h.pin)];
    PT t = resolvedType(h.node, h.pin, h.input);
    m_tipCache = (pin.name.empty() || pin.hideLabel ? std::string(h.input ? "Input" : "Output") : pin.name) + "\n" + blueprintPinTypeName(t) + (h.input ? " (input)" : " (output)");
    if (!pin.defaultValue.empty() && !isPinConnected(h.node, h.pin, h.input)) m_tipCache += "\nDefault: " + pin.defaultValue;
    return m_tipCache;
  }
  if (const BlueprintNode* n = nodeAt(p)) {
    if (m_compiled) { auto it = m_report.nodeIssues.find(n->id); if (it != m_report.nodeIssues.end()) { m_tipCache = it->second; return m_tipCache; } }
    m_tipCache = n->title.empty() ? "Reroute" : n->title;
    if (!n->tooltip.empty()) m_tipCache += "\n" + n->tooltip;
  }
  return m_tipCache;
}

Cursor BlueprintGraph::cursor() const {
  if (m_drag == Drag::Pan || m_drag == Drag::Minimap || m_drag == Drag::CommentResize) return Cursor::SizeAll;
  if (m_drag == Drag::Wire || m_hoverPin) return Cursor::Hand;
  if (m_drag == Drag::Nodes) return Cursor::SizeAll;
  return Cursor::Arrow;
}

// ============================================================================ keyboard
bool BlueprintGraph::onKeyDown(const KeyEvent& e) {
  const bool ctrl = (e.mods & ModCtrl) != 0, shift = (e.mods & ModShift) != 0;
  if (!m_features.shortcuts) {
    // An embedded graph: the host owns copy / paste / undo / save; only selection and view keys here.
    const bool own = e.key == Key::Escape || e.key == Key::Delete || e.key == Key::Backspace || (e.key == Key::A && ctrl) ||
                     (e.key == Key::F && !ctrl) || e.key == Key::Home;
    if (!own) return false;
  }
  Vec2 mouse = Application::get().mousePos();
  Vec2 pasteAt = m_graphRect.contains(mouse) ? screenToWorld(mouse) : screenToWorld(m_graphRect.center());
  switch (e.key) {
    case Key::Escape:
      if (m_drag == Drag::Wire) { m_drag = Drag::None; m_wireTarget = {}; commit("Disconnect"); invalidate(); return true; }
      if (selectedCount()) { clearSelection(); return true; }
      return false;
    case Key::Delete:
    case Key::Backspace: if (selectedCount() || std::any_of(m_comments.begin(), m_comments.end(), [](auto& c) { return c.selected; })) { removeSelected(); return true; } return false;
    case Key::A: if (ctrl) { selectAll(); return true; } return false;
    case Key::F9: for (int id : selectedIds()) toggleBreakpoint(id); return true;
    case Key::F5: if (m_paused) { resume(); return true; } return false;
    case Key::F10: step(); return true;
    case Key::C:
      if (ctrl) { copySelected(); return true; }
      if (!e.repeat) { addCommentAround(selectedIds(), pasteAt); return true; }
      return false;
    case Key::X: if (ctrl) { copySelected(); removeSelected(); return true; } return false;
    case Key::V: if (ctrl) { paste(pasteAt); return true; } return false;
    case Key::D: if (ctrl) { duplicateSelected(); return true; } return false;
    case Key::Z: if (ctrl) { return shift ? redo() || true : undo() || true; } return false;
    case Key::Y: if (ctrl) { redo(); return true; } return false;
    case Key::F: if (!ctrl) { frameSelection(); return true; } openFind({m_graphRect.x + 10, m_graphRect.y + 4, 200, 24}, shift); return true;
    case Key::S: if (ctrl) { saveToFile(); return true; } return false;
    case Key::Home: frameAll(); return true;
    case Key::Enter: if (ctrl) { run(); return true; } return false;
    default: return false;
  }
}

} // namespace prism
