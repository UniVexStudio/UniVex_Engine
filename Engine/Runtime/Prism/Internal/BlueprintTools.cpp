// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - blueprint editor tools: Save / Load graph files and Find (search nodes, pins and values).
#include "BlueprintInternal.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Docking.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace prism {

namespace fs = std::filesystem;

namespace {

std::string fileStem(BlueprintGraphPreset p) {
  std::string n = blueprintPresetName(p);
  for (char& c : n) if (c == ' ') c = '_';
  return n;
}
std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

struct Hit { int node; std::string label, detail; Color dot; Icon icon = Icon::Blueprint; };

class FindTitle : public Widget {
public:
  explicit FindTitle(bool all) : m_all(all) {}
  Vec2 measure(Vec2 a) override { return m_desired = constrain(Vec2(a.x, 28)); }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    drawIcon(c, Icon::Search, {m_rect.x + 8, m_rect.y + 6, 16, 16}, t.accentHover);
    c.textIn(*t.semibold, {m_rect.x + 30, m_rect.y, 200, m_rect.h}, m_all ? "Find in All Graphs" : "Find in Graph", t.textBright);
    c.textIn(*t.ui, {m_rect.right() - 130, m_rect.y, 122, m_rect.h}, m_all ? "Ctrl+Shift+F" : "Ctrl+F", t.textDim, HAlign::Right);
    c.hline(m_rect.x, m_rect.right(), m_rect.bottom() - 1, t.border);
  }
private:
  bool m_all;
};

// Scrollless result list: rows of "node title  -  matched detail".
class HitList : public Widget {
public:
  std::vector<Hit> hits;
  std::string emptyText = "Type to search...";
  std::function<void(int)> onPick;
  Vec2 measure(Vec2 a) override { return m_desired = constrain(Vec2(a.x, 40)); }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.fillRect(m_rect, t.panel);
    if (hits.empty()) { c.textIn(*t.ui, m_rect, emptyText, t.textDim, HAlign::Center); return; }
    for (size_t i = 0; i < hits.size() && float(i + 1) * kRow <= m_rect.h + kRow; i++) {
      Rect r{m_rect.x, m_rect.y + float(i) * kRow, m_rect.w, kRow};
      if (int(i) == m_hot) c.fillRect(r, t.controlHover);
      Rect chip{r.x + 8, r.y + 3, kRow - 6, kRow - 6};
      c.fillRoundRect(chip, 4, hits[i].dot.alpha(0.9f));
      drawIcon(c, hits[i].icon, chip.inflated(-2), Color(255, 255, 255), 1.2f);
      c.textIn(*t.medium, {r.x + 38, r.y, r.w * 0.5f, kRow}, hits[i].label, t.text);
      c.textIn(*t.ui, {r.x + 38 + r.w * 0.5f, r.y, r.w * 0.5f - 46, kRow}, hits[i].detail, t.textDim);
      if (i + 1 < hits.size()) c.hline(r.x + 8, r.right() - 8, r.bottom() - 1, t.borderSoft);
    }
  }
  bool onMouseMove(const MouseEvent& e) override { int h = int((e.pos.y - m_rect.y) / kRow); if (h < 0 || h >= int(hits.size())) h = -1; if (h != m_hot) { m_hot = h; invalidate(); } return true; }
  bool onMouseDown(const MouseEvent& e) override { int h = int((e.pos.y - m_rect.y) / kRow); if (e.button == MouseButton::Left && h >= 0 && h < int(hits.size()) && onPick) onPick(hits[size_t(h)].node); return true; }
  void onMouseLeave() override { m_hot = -1; invalidate(); }
  static constexpr float kRow = 28;
private:
  int m_hot = -1;
};

} // namespace

std::vector<std::string> BlueprintGraph::savedGraphs() const {
  std::vector<std::string> out;
  std::error_code ec;
  if (!fs::is_directory(saveDir, ec)) return out;
  for (auto& e : fs::directory_iterator(saveDir, ec))
    if (e.is_regular_file() && e.path().extension() == ".prismbp") out.push_back(e.path().filename().string());
  std::sort(out.begin(), out.end());
  return out;
}

bool BlueprintGraph::saveToFile() {
  std::error_code ec;
  fs::create_directories(saveDir, ec);
  fs::path p = fs::path(saveDir) / (fileStem(m_preset) + ".prismbp");
  std::ofstream f(p, std::ios::binary);
  if (!f) { Application::get().notify("Cannot write " + p.string(), ToastLevel::Error); return false; }
  f << serialize();
  Application::get().notify("Saved " + p.string(), ToastLevel::Success, 1800);
  return true;
}

bool BlueprintGraph::loadFromFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { Application::get().notify("Cannot open " + path, ToastLevel::Error); return false; }
  std::stringstream ss; ss << f.rdbuf();
  if (!deserialize(ss.str())) { Application::get().notify(path + " is not a valid PRISM graph", ToastLevel::Error); return false; }
  std::string stem = fs::path(path).stem().string();
  for (int i = 0; i < kBlueprintPresetCount; i++) {
    auto pr = BlueprintGraphPreset(i);
    if (fileStem(pr) == stem) m_preset = pr;
  }
  m_compiled = false; m_report = {}; m_history.clear(); m_lastSnapshot = serialize();
  m_needFrame = true;
  invalidate();
  Application::get().notify("Loaded " + path, ToastLevel::Info, 1500);
  return true;
}

void BlueprintGraph::openLoadMenu(Rect anchor) {
  std::vector<MenuItem> items;
  auto files = savedGraphs();
  for (auto& f : files) {
    std::string path = (fs::path(saveDir) / f).string();
    items.push_back(MenuItem::Item(f, [this, path] { loadFromFile(path); }, "", blueprintPresetIcon(m_preset)));
  }
  if (files.empty()) items.push_back(MenuItem::Item("No saved graphs yet - press Save", [] {}).disabled());
  showMenu(std::move(items), anchor, PopupPlacement::Below);
}

void BlueprintGraph::openFind(Rect anchor, bool allGraphs) {
  const Theme& t = theme();
  auto root = make<Border>();
  root->setBackground(t.panel);
  root->setBorder(t.borderStrong, Margin(1));
  root->setRadius(5);
  root->setFixedWidth(420);
  root->setFixedHeight(allGraphs ? 360 : 350);
  auto col = vbox();
  auto head = make<Border>();                       // title strip: icon, scope, shortcut
  head->setBackground(t.header);
  head->setFixedHeight(28);
  head->setChild(make<FindTitle>(allGraphs));
  col->add(head);
  auto in = make<TextInput>("", allGraphs ? "Search every saved graph..." : "Find nodes, pins, values in this graph...");
  in->setLeadingIcon(Icon::Search);
  in->setClearable(true);
  auto list = make<HitList>();
  std::weak_ptr<Widget> wroot = root;
  std::weak_ptr<HitList> wl = list;
  auto rebuild = [this, wl, allGraphs](const std::string& q) {
    auto l = wl.lock();
    if (!l) return;
    l->hits.clear();
    std::string needle = lower(q);
    l->emptyText = needle.empty() ? "Type to search..." : "No matching nodes";
    if (!needle.empty() && allGraphs) {
      auto files = savedGraphs();
      for (size_t i = 0; i < files.size() && l->hits.size() < 11; i++) {
        std::ifstream f((fs::path(saveDir) / files[i]).string(), std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::string txt = lower(ss.str());
        size_t cnt = 0, pos = 0;
        while ((pos = txt.find(needle, pos)) != std::string::npos) { cnt++; pos += needle.size(); }
        if (cnt) l->hits.push_back({-1 - int(i), files[i], std::to_string(cnt) + (cnt == 1 ? " match" : " matches"), Color(84, 130, 200), Icon::File});
      }
    } else if (!needle.empty()) {
      for (auto& n : m_nodes) {
        if (n.style == BlueprintNodeStyle::Reroute) continue;
        std::string detail;
        bool ok = lower(n.title).find(needle) != std::string::npos;
        if (!ok && lower(n.subtitle).find(needle) != std::string::npos) { ok = true; detail = n.subtitle; }
        if (!ok) for (auto& p : n.inputs) {
          if (lower(p.name).find(needle) != std::string::npos || lower(p.defaultValue).find(needle) != std::string::npos) { ok = true; detail = "pin: " + p.name + (p.defaultValue.empty() ? "" : " = " + p.defaultValue); break; }
        }
        if (!ok) for (auto& p : n.outputs) if (lower(p.name).find(needle) != std::string::npos) { ok = true; detail = "pin: " + p.name; break; }
        if (!ok) continue;
        if (detail.empty()) detail = n.subtitle.empty() ? blueprintPresetName(m_preset) : n.subtitle;
        Icon ic = n.category == BlueprintNodeCategory::Event ? Icon::EventGraph : n.category == BlueprintNodeCategory::Animation ? Icon::AnimGraph :
                  n.category == BlueprintNodeCategory::State ? Icon::StateGraph : n.category == BlueprintNodeCategory::Behavior ? Icon::BehaviorGraph :
                  n.category == BlueprintNodeCategory::Effect ? Icon::VfxGraph : n.category == BlueprintNodeCategory::Audio ? Icon::AudioGraph :
                  n.category == BlueprintNodeCategory::Material || n.category == BlueprintNodeCategory::Texture ? Icon::Material : Icon::Blueprint;
        l->hits.push_back({n.id, n.title.empty() ? "(node)" : n.title, detail, bp::chromeFor(n.category).left, ic});
        if (l->hits.size() >= 11) break;
      }
    }
    l->invalidate();
  };
  list->onPick = [this, wroot](int id) {
    if (id < 0) {                                   // a saved graph file (find in all graphs)
      auto files = savedGraphs();
      size_t i = size_t(-1 - id);
      if (i < files.size()) loadFromFile((fs::path(saveDir) / files[i]).string());
      if (auto r = wroot.lock()) Application::get().closePopup(r.get());
      return;
    }
    clearSelection();
    if (auto* n = nodeById(id)) n->selected = true;
    frameSelection();
    invalidate();
    if (auto r = wroot.lock()) Application::get().closePopup(r.get());
  };
  Widget* rawRoot = root.get();
  in->onChanged = rebuild;
  in->onCommit = [list, rawRoot](const std::string&) { if (!list->hits.empty() && list->onPick) list->onPick(list->hits.front().node); else Application::get().closePopup(rawRoot); };
  in->onCancel = [rawRoot] { Application::get().closePopup(rawRoot); };
  col->add(in);
  col->add(list, {.fill = 1});
  root->setChild(col);
  PopupOptions o;
  o.placement = PopupPlacement::Below;
  o.anchor = anchor;
  o.shadow = true;
  Application& app = Application::get();
  app.showPopup(root, o);
  app.setFocus(in.get());
}


void BlueprintGraph::showDiff(Rect anchor) {
  fs::path p = fs::path(saveDir) / (fileStem(m_preset) + ".prismbp");
  std::ifstream f(p, std::ios::binary);
  if (!f) { Application::get().notify("Nothing to compare: press Save first (" + p.string() + ")", ToastLevel::Info, 1800); return; }
  std::stringstream ss; ss << f.rdbuf();
  std::vector<int> ids;
  auto lines = diffAgainst(ss.str(), &ids);
  std::vector<MenuItem> items;
  items.push_back(MenuItem::Item("Compared with " + p.filename().string(), [] {}).disabled());
  items.push_back(MenuItem::Sep());
  if (lines.empty()) items.push_back(MenuItem::Item("No differences", [] {}, "", Icon::Check));
  for (size_t i = 0; i < lines.size() && i < 14; i++) {
    int id = ids[i];
    items.push_back(MenuItem::Item(lines[i], [this, id] { if (id >= 0 && nodeById(id)) { clearSelection(); nodeById(id)->selected = true; frameSelection(); } }));
  }
  if (lines.size() > 14) items.push_back(MenuItem::Item("... and " + std::to_string(lines.size() - 14) + " more", [] {}).disabled());
  showMenu(std::move(items), anchor, PopupPlacement::Below);
}

namespace {
// Two-column read-only info list used by the Class Settings / Class Defaults windows.
class InfoList : public Widget {
public:
  struct Row { std::string k, v; bool head = false; };
  std::vector<Row> rows;
  Vec2 measure(Vec2 a) override { return m_desired = constrain(Vec2(a.x, float(rows.size()) * 26.f + 8)); }
  void paint(Canvas& c) override {
    const Theme& t = theme();
    c.fillRect(m_rect, t.panel);
    float y = m_rect.y + 4;
    for (auto& r : rows) {
      Rect rr{m_rect.x, y, m_rect.w, 26};
      if (r.head) { c.fillRect(rr, t.header); c.textIn(*t.semibold, {rr.x + 10, rr.y, rr.w - 20, rr.h}, r.k, t.textBright); }
      else {
        c.textIn(*t.ui, {rr.x + 14, rr.y, rr.w * 0.45f, rr.h}, r.k, t.textDim);
        c.textIn(*t.medium, {rr.x + rr.w * 0.45f + 14, rr.y, rr.w * 0.55f - 24, rr.h}, r.v, t.text);
        c.hline(rr.x + 10, rr.right() - 10, rr.bottom() - 1, t.borderSoft);
      }
      y += 26;
    }
  }
};
}

void BlueprintGraph::showClassSettings() {
  auto list = make<InfoList>();
  fs::path p = fs::path(saveDir) / (fileStem(m_preset) + ".prismbp");
  std::error_code ec;
  list->rows = {{"General", "", true}, {"Graph Type", blueprintPresetName(m_preset)}, {"Category", blueprintPresetGroup(m_preset)},
                {"Parent Class", usesExecFlow() ? "PrismActor" : "PrismAsset"},
                {"Statistics", "", true}, {"Nodes", std::to_string(m_nodes.size())}, {"Wires", std::to_string(m_wires.size())}, {"Comments", std::to_string(m_comments.size())},
                {"Compile", m_compiled ? (m_report.errors ? std::to_string(m_report.errors) + " error(s)" : m_report.warnings ? std::to_string(m_report.warnings) + " warning(s)" : "Up to date") : "Needs compile"},
                {"Asset", "", true}, {"File", p.string()}, {"Saved", fs::exists(p, ec) ? "Yes" : "Not yet"},
                {"Options", "", true}, {"Save On Compile", m_saveOnCompile ? "On" : "Off"}};
  FloatingWindowOptions o;
  o.title = std::string("Class Settings - ") + blueprintPresetName(m_preset);
  o.icon = Icon::Settings;
  o.size = {400, 440};
  o.minSize = {300, 200};
  showFloatingWindow(list, o);
}

void BlueprintGraph::showClassDefaults() {
  auto list = make<InfoList>();
  list->rows.push_back({"Variables", "", true});
  int n = 0;
  for (auto& nd : m_nodes) {
    if (nd.category != BlueprintNodeCategory::Variable) continue;
    std::string type = nd.outputs.empty() ? (nd.inputs.size() > 1 ? blueprintPinTypeName(nd.inputs[1].type) : "") : blueprintPinTypeName(nd.outputs.back().type);
    std::string val = nd.inputs.size() > 1 ? nd.inputs[1].defaultValue : "";
    list->rows.push_back({nd.title.empty() ? "(variable)" : nd.title, type + (val.empty() ? "" : "  =  " + val)});
    n++;
  }
  if (!n) list->rows.push_back({"No variables in this graph", ""});
  list->rows.push_back({"Defaults", "", true});
  list->rows.push_back({"Graph Type", blueprintPresetName(m_preset)});
  list->rows.push_back({"Hide Unrelated", m_hideUnrelated ? "On" : "Off"});
  FloatingWindowOptions o;
  o.title = std::string("Class Defaults - ") + blueprintPresetName(m_preset);
  o.icon = Icon::Properties;
  o.size = {400, 360};
  o.minSize = {300, 180};
  showFloatingWindow(list, o);
}

} // namespace prism
