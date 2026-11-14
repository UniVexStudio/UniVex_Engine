// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - blueprint authoring tools: auto-layout, collapse-to-subgraph, variables panel, debugger breakpoints.
#include "prism/BlueprintGraph.h"
#include "prism/BlueprintVM.h"
#include "prism/Application.h"
#include "prism/Docking.h"
#include "prism/TextInput.h"
#include <algorithm>
#include <cmath>

namespace prism {
using PT = BlueprintPinType;

// ------------------------------------------------------------------ auto layout
void BlueprintGraph::autoLayout(bool selectionOnly) {
  std::set<int> S;
  for (auto& n : m_nodes) if (!selectionOnly || n.selected) S.insert(n.id);
  if (S.size() < 2) { if (selectionOnly) Application::get().notify("Select at least two nodes to lay out", ToastLevel::Info, 1500); return; }
  float x0 = 1e9f, y0 = 1e9f;
  for (auto& n : m_nodes) if (S.count(n.id)) { x0 = std::min(x0, n.pos.x); y0 = std::min(y0, n.pos.y); }

  // longest-path layering (bounded passes, so loops in exec graphs cannot hang it)
  std::map<int, int> layer;
  for (int id : S) layer[id] = 0;
  for (size_t pass = 0; pass < S.size(); pass++) {
    bool ch = false;
    for (auto& w : m_wires)
      if (S.count(w.fromNodeId) && S.count(w.toNodeId) && w.fromNodeId != w.toNodeId && layer[w.toNodeId] < layer[w.fromNodeId] + 1 && layer[w.fromNodeId] < int(S.size())) {
        layer[w.toNodeId] = layer[w.fromNodeId] + 1; ch = true;
      }
    if (!ch) break;
  }
  int maxL = 0; for (auto& [_, l] : layer) maxL = std::max(maxL, l);
  std::vector<std::vector<int>> cols(size_t(maxL) + 1);
  std::vector<const BlueprintNode*> byY;
  for (auto& n : m_nodes) if (S.count(n.id)) byY.push_back(&n);
  std::stable_sort(byY.begin(), byY.end(), [](auto* a, auto* b) { return a->pos.y < b->pos.y; });
  for (auto* n : byY) cols[size_t(layer[n->id])].push_back(n->id);

  // barycentre ordering to reduce crossings (two down/up sweeps)
  std::map<int, float> rank;
  auto assignRank = [&](size_t c) { for (size_t i = 0; i < cols[c].size(); i++) rank[cols[c][i]] = float(i); };
  for (size_t c = 0; c < cols.size(); c++) assignRank(c);
  auto bary = [&](int id, bool fromLeft) {
    float sum = 0; int cnt = 0;
    for (auto& w : m_wires) {
      int other = fromLeft ? (w.toNodeId == id ? w.fromNodeId : -1) : (w.fromNodeId == id ? w.toNodeId : -1);
      if (other >= 0 && S.count(other)) { sum += rank[other]; cnt++; }
    }
    return cnt ? sum / float(cnt) : rank[id];
  };
  for (int sweep = 0; sweep < 2; sweep++) {
    for (size_t c = 1; c < cols.size(); c++) {
      std::map<int, float> b; for (int id : cols[c]) b[id] = bary(id, true);
      std::stable_sort(cols[c].begin(), cols[c].end(), [&](int a, int d) { return b[a] < b[d]; });
      assignRank(c);
    }
    for (int c = int(cols.size()) - 2; c >= 0; c--) {
      std::map<int, float> b; for (int id : cols[size_t(c)]) b[id] = bary(id, false);
      std::stable_sort(cols[size_t(c)].begin(), cols[size_t(c)].end(), [&](int a, int d) { return b[a] < b[d]; });
      assignRank(size_t(c));
    }
  }
  // place: columns left-to-right, nodes stacked top-down, each column vertically centred on the tallest
  const float gapX = 90, gapY = 28;
  std::vector<float> colH(cols.size(), 0), colW(cols.size(), 0);
  float tallest = 0;
  for (size_t c = 0; c < cols.size(); c++) {
    for (int id : cols[c]) { const BlueprintNode* n = findNode(id); colH[c] += n->size.y + gapY; colW[c] = std::max(colW[c], n->size.x); }
    colH[c] -= gapY; tallest = std::max(tallest, colH[c]);
  }
  float x = x0;
  for (size_t c = 0; c < cols.size(); c++) {
    float y = y0 + (tallest - colH[c]) * 0.5f;
    for (int id : cols[c]) {
      BlueprintNode* n = nodeById(id);
      n->pos = {std::round(x / 8) * 8, std::round(y / 8) * 8};
      y += n->size.y + gapY;
    }
    x += colW[c] + gapX;
  }
  touch();
  commit("Auto Layout");
  if (onChanged) onChanged();
  frameAll();
}

// ------------------------------------------------------------------ collapse / expand
bool BlueprintGraph::collapseSelected() {
  std::set<int> S;
  for (auto& n : m_nodes) if (n.selected) S.insert(n.id);
  if (S.size() < 2) { Application::get().notify("Select at least two nodes to collapse", ToastLevel::Info, 1500); return false; }
  Sub sub;
  BlueprintNode cn;
  cn.id = nextId();
  cn.title = "Collapsed Graph";
  cn.subtitle = std::to_string(S.size()) + " nodes";
  cn.category = BlueprintNodeCategory::Function;
  float minX = 1e9f, minY = 1e9f;
  for (auto& n : m_nodes) if (S.count(n.id)) { minX = std::min(minX, n.pos.x); minY = std::min(minY, n.pos.y); sub.nodes.push_back(n); sub.nodes.back().selected = false; }
  cn.pos = {minX, minY};
  std::vector<BlueprintWire> outer;                                  // wires that stay, retargeted to the collapsed node
  std::map<std::pair<int, int>, int> inPin, outPin;
  for (auto& w : m_wires) {
    bool a = S.count(w.fromNodeId) > 0, b = S.count(w.toNodeId) > 0;
    if (a && b) { sub.wires.push_back(w); continue; }
    if (!a && !b) { outer.push_back(w); continue; }
    BlueprintWire nw = w;
    if (b) {                                                         // outside -> inside: input pin
      auto key = std::make_pair(w.toNodeId, w.toPinIndex);
      auto it = inPin.find(key);
      if (it == inPin.end()) {
        const BlueprintNode* inner = findNode(w.toNodeId);
        BlueprintPin p = inner->inputs[size_t(w.toPinIndex)];
        p.kind = BlueprintPinKind::Input; p.hideLabel = false; p.required = false; p.defaultValue.clear();
        if (p.name.empty() || p.name == "Exec") p.name = inner->title;
        cn.inputs.push_back(p);
        sub.inMap.push_back(key);
        it = inPin.emplace(key, int(cn.inputs.size()) - 1).first;
      }
      nw.toNodeId = cn.id; nw.toPinIndex = it->second;
    } else {                                                         // inside -> outside: output pin (shared by all targets)
      auto key = std::make_pair(w.fromNodeId, w.fromPinIndex);
      auto it = outPin.find(key);
      if (it == outPin.end()) {
        const BlueprintNode* inner = findNode(w.fromNodeId);
        BlueprintPin p = inner->outputs[size_t(w.fromPinIndex)];
        p.kind = BlueprintPinKind::Output; p.hideLabel = false; p.defaultValue.clear();
        if (p.name.empty() || p.name == "Exec") p.name = inner->title;
        cn.outputs.push_back(p);
        sub.outMap.push_back(key);
        it = outPin.emplace(key, int(cn.outputs.size()) - 1).first;
      }
      nw.fromNodeId = cn.id; nw.fromPinIndex = it->second;
    }
    outer.push_back(nw);
  }
  m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&](const BlueprintNode& n) { return S.count(n.id) > 0; }), m_nodes.end());
  m_wires = std::move(outer);
  int id = cn.id;
  m_subs[id] = std::move(sub);
  addNode(cn);
  for (auto& n : m_nodes) n.selected = n.id == id;
  touch();
  commit("Collapse to Subgraph");
  if (onChanged) onChanged();
  return true;
}

// Replaces the collapsed node `nodeId` with the nodes / wires it stands for (shared by Expand and by program()).
void BlueprintGraph::expandSub(std::vector<BlueprintNode>& nodes, std::vector<BlueprintWire>& wires, std::map<int, Sub>& subs, int nodeId, bool select) {
  auto it = subs.find(nodeId);
  if (it == subs.end()) return;
  Sub sub = std::move(it->second);
  subs.erase(it);
  // inner ids may have been reused while collapsed: remap clashes to fresh ids
  int fresh = 1;
  for (auto& n : nodes) fresh = std::max(fresh, n.id + 1);
  for (auto& n : sub.nodes) fresh = std::max(fresh, n.id + 1);
  std::map<int, int> remap;
  for (auto& n : sub.nodes) {
    bool clash = std::any_of(nodes.begin(), nodes.end(), [&](const BlueprintNode& m) { return m.id == n.id && m.id != nodeId; });
    remap[n.id] = clash ? fresh++ : n.id;
  }
  auto rm = [&](int id) { auto f = remap.find(id); return f == remap.end() ? id : f->second; };
  for (auto& [from, to] : remap)                       // nested collapsed nodes keep their contents under the new id
    if (from != to) { auto nested = subs.find(from); if (nested != subs.end()) { Sub keep = std::move(nested->second); subs.erase(nested); subs[to] = std::move(keep); } }
  std::vector<BlueprintWire> keep;
  for (auto& w : wires) {
    BlueprintWire nw = w;
    if (w.toNodeId == nodeId) {
      if (w.toPinIndex < 0 || w.toPinIndex >= int(sub.inMap.size())) continue;
      nw.toNodeId = rm(sub.inMap[size_t(w.toPinIndex)].first); nw.toPinIndex = sub.inMap[size_t(w.toPinIndex)].second;
    } else if (w.fromNodeId == nodeId) {
      if (w.fromPinIndex < 0 || w.fromPinIndex >= int(sub.outMap.size())) continue;
      nw.fromNodeId = rm(sub.outMap[size_t(w.fromPinIndex)].first); nw.fromPinIndex = sub.outMap[size_t(w.fromPinIndex)].second;
    }
    keep.push_back(nw);
  }
  nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const BlueprintNode& n) { return n.id == nodeId; }), nodes.end());
  if (select) for (auto& n : nodes) n.selected = false;
  for (auto n : sub.nodes) { n.id = rm(n.id); n.selected = select; nodes.push_back(n); }
  for (auto w : sub.wires) { w.fromNodeId = rm(w.fromNodeId); w.toNodeId = rm(w.toNodeId); keep.push_back(w); }
  wires = std::move(keep);
}

bool BlueprintGraph::expandNode(int nodeId) {
  if (!m_subs.count(nodeId) || !findNode(nodeId)) return false;
  expandSub(m_nodes, m_wires, m_subs, nodeId, true);
  for (auto& n : m_nodes) { m_nextId = std::max(m_nextId, n.id + 1); layoutNode(n); }
  touch();
  commit("Expand Subgraph");
  if (onChanged) onChanged();
  return true;
}

BpProgram BlueprintGraph::program() const {
  std::vector<BlueprintNode> nodes = m_nodes;
  std::vector<BlueprintWire> wires = m_wires;
  std::map<int, Sub> subs = m_subs;
  for (int guard = 0; guard < 1024; guard++) {         // flatten collapsed nodes (and any nested inside them)
    int id = -1;
    for (auto& n : nodes) if (subs.count(n.id)) { id = n.id; break; }
    if (id < 0) break;
    expandSub(nodes, wires, subs, id, false);
  }
  return compileBlueprint(nodes, wires, m_vars);
}

// ------------------------------------------------------------------ variables
BlueprintVariable* BlueprintGraph::addVariable(const std::string& name, PT type, const std::string& value) {
  std::string n = name.empty() ? "NewVar" : name;
  auto taken = [&](const std::string& s) { return std::any_of(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == s; }); };
  std::string u = n; int k = 1;
  while (taken(u)) u = n + std::to_string(k++);
  std::string def = value;
  if (def.empty()) def = type == PT::Bool ? "false" : type == PT::Vector || type == PT::Color ? "0,0,0" : type == PT::Float || type == PT::Int ? "0" : "";
  m_vars.push_back({u, type, def});
  commit("Add Variable " + u);
  if (onChanged) onChanged();
  invalidate();
  return &m_vars.back();
}

bool BlueprintGraph::removeVariable(const std::string& name) {
  auto it = std::find_if(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == name; });
  if (it == m_vars.end()) return false;
  m_vars.erase(it);
  std::vector<int> dead;
  for (auto& n : m_nodes) if (n.category == BlueprintNodeCategory::Variable && (n.title == name || n.title == "Set " + name)) dead.push_back(n.id);
  for (int id : dead) removeNode(id);
  commit("Remove Variable " + name);
  if (onChanged) onChanged();
  return true;
}

bool BlueprintGraph::setVariableValue(const std::string& name, const std::string& value) {
  auto it = std::find_if(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == name; });
  if (it == m_vars.end()) return false;
  it->value = value;
  if (onChanged) onChanged();
  return true;
}

bool BlueprintGraph::renameVariable(const std::string& from, const std::string& to) {
  if (to.empty() || from == to) return false;
  auto it = std::find_if(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == from; });
  if (it == m_vars.end() || std::any_of(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == to; })) return false;
  it->name = to;
  for (auto& n : m_nodes) {
    if (n.category != BlueprintNodeCategory::Variable) continue;
    if (n.title == from) n.title = to;
    else if (n.title == "Set " + from) n.title = "Set " + to;
    else continue;
    for (auto& p : n.inputs) if (p.name == from) p.name = to;
    for (auto& p : n.outputs) if (p.name == from) p.name = to;
    layoutNode(n);
  }
  commit("Rename Variable");
  touch();
  return true;
}

int BlueprintGraph::spawnVariableNode(const std::string& name, bool setter, Vec2 world) {
  auto it = std::find_if(m_vars.begin(), m_vars.end(), [&](auto& v) { return v.name == name; });
  if (it == m_vars.end()) return -1;
  BlueprintNode n;
  n.category = BlueprintNodeCategory::Variable;
  n.pos = world;
  auto pin = [&](PT t, const std::string& nm, BlueprintPinKind k, const std::string& def = "", bool hide = false) {
    BlueprintPin p; p.type = t; p.name = nm; p.kind = k; p.defaultValue = def; p.hideLabel = hide; return p;
  };
  if (setter) {
    n.title = "Set " + name;
    n.inputs = {pin(PT::Exec, "Exec", BlueprintPinKind::Input, "", true), pin(it->type, name, BlueprintPinKind::Input, it->value)};
    n.outputs = {pin(PT::Exec, "Exec", BlueprintPinKind::Output, "", true), pin(it->type, name, BlueprintPinKind::Output)};
  } else {
    n.title = name;
    n.style = BlueprintNodeStyle::Compact;
    n.outputs = {pin(it->type, name, BlueprintPinKind::Output, "", true)};
  }
  addNode(n);
  int id = m_nodes.back().id;
  select(id);
  commit(setter ? "Add Set " + name : "Add Get " + name);
  if (onChanged) onChanged();
  return id;
}

namespace {
const PT kVarTypes[] = {PT::Float, PT::Int, PT::Bool, PT::Vector, PT::Color, PT::String, PT::Object};

class VariablesPanel : public Widget {
public:
  explicit VariablesPanel(std::weak_ptr<Widget> g) : m_graph(std::move(g)) {
    m_edit = make<TextInput>();
    m_edit->setVisible(false);
    addChild(m_edit);
    m_edit->onCommit = [this](const std::string&) { endEdit(true); };
    m_edit->onCancel = [this] { endEdit(false); };
    m_edit->onBlur = [this] { if (m_editing >= 0) endEdit(true); };
  }
  Vec2 measure(Vec2 a) override { return m_desired = constrain({a.x, 0}); }
  void arrange(Rect r) override { m_rect = r; if (m_editing >= 0) m_edit->arrange(editRect()); }
  bool focusable() const override { return false; }
  AccessRole accessRole() const override { return AccessRole::List; }
  std::string accessName() const override { return "Variables"; }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    BlueprintGraph* g = graph();
    c.fillRect(m_rect, t.panel);
    c.fillRect({m_rect.x, m_rect.y, m_rect.w, kHead}, t.header);
    c.textIn(*t.semibold, {m_rect.x + 10, m_rect.y, 120, kHead}, "Variables", t.textBright);
    Rect add = addRect();
    c.fillRoundRect(add, 3, m_hot == -2 ? t.controlHover : t.control);
    drawIcon(c, Icon::Plus, {add.x + 6, add.y + 5, 14, 14}, t.text, 1.8f);
    c.textIn(*t.ui, {add.x + 24, add.y, add.w - 28, add.h}, "Variable", t.text);
    if (!g || g->variables().empty()) {
      c.textIn(*t.ui, {m_rect.x + 12, m_rect.y + kHead + 10, m_rect.w - 24, 24}, "No variables yet - add one, then drag Get / Set into the graph.", t.textDim);
      return;
    }
    for (int i = 0; i < int(g->variables().size()); i++) {
      const auto& v = g->variables()[size_t(i)];
      Rect r = rowRect(i);
      if (m_hot == i) c.fillRect(r, Color(255, 255, 255, 10));
      c.hline(r.x + 8, r.right() - 8, r.bottom() - 1, t.borderSoft);
      c.fillCircle({r.x + 14, r.y + r.h / 2}, 4.5f, blueprintPinColor(v.type));
      c.textIn(*t.medium, {r.x + 26, r.y, r.w * 0.30f, r.h}, v.name, t.text);
      Rect tr = typeRect(r);
      c.fillRoundRect(tr, 3, t.control);
      c.textIn(*t.ui, {tr.x + 6, tr.y, tr.w - 12, tr.h}, blueprintPinTypeName(v.type), t.textDim, HAlign::Center);
      Rect vr = valueRect(r);
      c.textIn(*t.mono, {vr.x, vr.y, vr.w, vr.h}, v.value, t.text);
      static const char* lbl[] = {"Get", "Set"};
      for (int k = 0; k < 2; k++) {
        Rect b = btnRect(r, k);
        c.fillRoundRect(b, 3, m_hot == 100 + i * 4 + k ? t.accent : t.control);
        c.textIn(*t.ui, b, lbl[k], t.textBright, HAlign::Center);
      }
      Rect x = btnRect(r, 2);
      drawIcon(c, Icon::Trash, {x.x + 5, x.y + 5, 14, 14}, m_hot == 100 + i * 4 + 2 ? t.error : t.textDim, 1.6f);
    }
  }
  void paintChildren(Canvas& c) { Widget::paintChildren(c); }
  bool onMouseMove(const MouseEvent& e) override {
    int h = hit(e.pos);
    if (h != m_hot) { m_hot = h; invalidate(); }
    return false;
  }
  void onMouseLeave() override { m_hot = -1; invalidate(); }
  bool onMouseDown(const MouseEvent& e) override { return e.button == MouseButton::Left; }
  bool onMouseUp(const MouseEvent& e) override {
    BlueprintGraph* g = graph();
    if (!g) return true;
    int h = hit(e.pos);
    if (h == -2) { g->addVariable("NewVar", PT::Float); invalidate(); return true; }
    if (h < 100) {
      // a click on the name / type / value cell
      int i = rowIndex(e.pos);
      if (i < 0) return true;
      Rect r = rowRect(i);
      auto& v = g->variables()[size_t(i)];
      if (typeRect(r).contains(e.pos)) { cycleType(*g, i); return true; }
      if (valueRect(r).contains(e.pos)) {
        if (v.type == PT::Bool) { setValue(*g, i, v.value == "true" ? "false" : "true"); return true; }
        beginEdit(i, 1, valueRect(r)); return true;
      }
      if (e.pos.x < r.x + r.w * 0.42f) beginEdit(i, 0, {r.x + 24, r.y + 3, r.w * 0.30f, r.h - 6});
      return true;
    }
    int i = (h - 100) / 4, k = (h - 100) % 4;
    if (i >= int(g->variables().size())) return true;
    std::string name = g->variables()[size_t(i)].name;
    if (k == 2) { g->removeVariable(name); invalidate(); return true; }
    Vec2 at = g->screenToWorld(g->rect().center());
    g->spawnVariableNode(name, k == 1, at);
    return true;
  }
private:
  static constexpr float kHead = 32, kRow = 30;
  BlueprintGraph* graph() const { auto s = m_graph.lock(); return s ? static_cast<BlueprintGraph*>(s.get()) : nullptr; }
  Rect addRect() const { return {m_rect.right() - 98, m_rect.y + 4, 90, 24}; }
  Rect rowRect(int i) const { return {m_rect.x, m_rect.y + kHead + float(i) * kRow, m_rect.w, kRow}; }
  Rect typeRect(Rect r) const { return {r.x + r.w * 0.40f, r.y + 4, 62, r.h - 8}; }
  Rect valueRect(Rect r) const { return {typeRect(r).right() + 8, r.y, std::max(30.f, r.right() - 150 - typeRect(r).right() - 8), r.h}; }
  Rect btnRect(Rect r, int k) const { return {r.right() - 140 + float(k) * (k == 2 ? 40 : 44) + (k == 2 ? 4 : 0), r.y + 4, k == 2 ? 24.f : 40.f, r.h - 8}; }
  Rect editRect() const {
    BlueprintGraph* g = graph();
    if (!g || m_editing < 0 || m_editing >= int(g->variables().size())) return {};
    Rect r = rowRect(m_editing);
    return m_editField == 0 ? Rect{r.x + 24, r.y + 3, r.w * 0.30f, r.h - 6} : Rect{valueRect(r).x, r.y + 3, valueRect(r).w, r.h - 6};
  }
  int rowIndex(Vec2 p) const {
    BlueprintGraph* g = graph();
    if (!g || p.y < m_rect.y + kHead) return -1;
    int i = int((p.y - m_rect.y - kHead) / kRow);
    return i < int(g->variables().size()) ? i : -1;
  }
  int hit(Vec2 p) const {
    if (addRect().contains(p)) return -2;
    int i = rowIndex(p);
    if (i < 0) return -1;
    Rect r = rowRect(i);
    for (int k = 0; k < 3; k++) if (btnRect(r, k).contains(p)) return 100 + i * 4 + k;
    return i;
  }
  void beginEdit(int i, int field, Rect where) {
    BlueprintGraph* g = graph();
    m_editing = i; m_editField = field;
    m_edit->setText(field == 0 ? g->variables()[size_t(i)].name : g->variables()[size_t(i)].value);
    m_edit->setVisible(true);
    m_edit->arrange(where);
    Application::get().setFocus(m_edit.get());
    m_edit->selectAll();
    invalidate();
  }
  void endEdit(bool apply) {
    if (m_editing < 0) return;
    BlueprintGraph* g = graph();
    int i = m_editing; m_editing = -1;
    std::string s = m_edit->text();
    m_edit->setVisible(false);
    if (apply && g && i < int(g->variables().size())) {
      if (m_editField == 0) g->renameVariable(g->variables()[size_t(i)].name, s);
      else setValue(*g, i, s);
    }
    invalidate();
  }
  void setValue(BlueprintGraph& g, int i, const std::string& v) {
    auto& var = const_cast<BlueprintVariable&>(g.variables()[size_t(i)]);
    var.value = v;
    g.commit("Edit Variable");
    invalidate();
  }
  void cycleType(BlueprintGraph& g, int i) {
    auto& var = const_cast<BlueprintVariable&>(g.variables()[size_t(i)]);
    size_t k = 0;
    for (size_t j = 0; j < std::size(kVarTypes); j++) if (kVarTypes[j] == var.type) k = j;
    var.type = kVarTypes[(k + 1) % std::size(kVarTypes)];
    var.value = var.type == PT::Bool ? "false" : var.type == PT::Vector || var.type == PT::Color ? "0,0,0" : var.type == PT::Float || var.type == PT::Int ? "0" : "";
    g.commit("Change Variable Type");
    invalidate();
  }
  std::weak_ptr<Widget> m_graph;
  Ref<TextInput> m_edit;
  int m_editing = -1, m_editField = 0, m_hot = -1;
};
} // namespace

void BlueprintGraph::showVariables() {
  auto panel = make<VariablesPanel>(weak_from_this());
  FloatingWindowOptions o;
  o.title = "Variables";
  o.icon = Icon::Properties;
  o.size = {520, 320};
  o.minSize = {380, 160};
  showFloatingWindow(panel, o);
}

// ------------------------------------------------------------------ debugger
void BlueprintGraph::toggleBreakpoint(int id) {
  if (!findNode(id)) return;
  if (!m_breaks.erase(id)) m_breaks.insert(id);
  invalidate();
}

void BlueprintGraph::resume() {
  if (!m_paused) return;
  m_paused = false; m_resumeIdx = m_pauseIdx; m_stepIdx = -1;
  startFlowTimer();
}

void BlueprintGraph::step() {
  if (!m_paused) { if (!m_runActive) run(); m_stepIdx = 0; return; }
  m_paused = false; m_resumeIdx = m_pauseIdx; m_stepIdx = m_pauseIdx + 1;
  startFlowTimer();
}

void BlueprintGraph::stopRun() {
  m_runActive = false; m_paused = false; m_pauseIdx = m_resumeIdx = m_stepIdx = -1;
  invalidate();
}

void BlueprintGraph::paintDebugMarks(Canvas& c) {
  int paused = pausedNode();
  for (auto& n : m_nodes) {
    bool bp = m_breaks.count(n.id) > 0;
    if (!bp && n.id != paused) continue;
    Rect r = nodeScreenRect(n);
    if (n.id == paused) c.strokeRoundRect({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 8, Color(250, 214, 80), 2);
    if (bp) {
      float rad = std::max(4.f, 5.5f * m_zoom);
      c.fillCircle({r.x - rad * 0.6f, r.y + rad * 0.6f}, rad + 1.2f, Color(40, 12, 12));
      c.fillCircle({r.x - rad * 0.6f, r.y + rad * 0.6f}, rad, Color(228, 64, 64));
    }
  }
  if (m_paused) {
    const Theme& t = theme();
    Rect b{m_graphRect.x + m_graphRect.w / 2 - 150, m_graphRect.y + 44, 300, 26};
    c.fillRoundRect(b, 13, Color(60, 46, 12, 235));
    c.strokeRoundRect(b, 13, Color(250, 214, 80), 1);
    c.textIn(*t.medium, b, "Paused at breakpoint  -  F5 resume  F10 step", Color(250, 224, 130), HAlign::Center);
  }
}

} // namespace prism
