// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Profiler implementation.
#include "prism/Profiler.h"
#include "prism/Theme.h"
#include "prism/Canvas.h"
#include "prism/Icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace prism {

namespace {
constexpr float kBarH = 30, kCardsH = 50, kGraphH = 92, kTrackLabelW = 78, kRow = 17;
std::string fmt(const char* f, double a, double b = 0) { char s[64]; snprintf(s, sizeof s, f, a, b); return s; }
}

Profiler::Profiler() { m_frames.reserve(kHistory); }

void Profiler::pushFrame(ProfileFrame f) {
  if (m_paused) return;
  if (m_frames.size() >= kHistory) {
    m_frames.erase(m_frames.begin());
    if (m_selected > 0) m_selected--;
    else if (m_selected == 0) m_selected = -1;
  }
  m_frames.push_back(std::move(f));
  invalidate();
}
void Profiler::clear() { m_frames.clear(); m_selected = -1; invalidate(); }
void Profiler::setPaused(bool p) { m_paused = p; invalidate(); }
void Profiler::selectFrame(int i) {
  m_selected = (i < 0 || i >= int(m_frames.size())) ? -1 : i;
  invalidate();
}
const ProfileFrame* Profiler::shownFrame() const {
  if (m_frames.empty()) return nullptr;
  return m_selected >= 0 ? &m_frames[m_selected] : &m_frames.back();
}

Vec2 Profiler::measure(Vec2 avail) { return m_desired = constrain(Vec2(avail.x, avail.y)); }

Profiler::Layout Profiler::layout() const {
  Layout l;
  bool compact = m_rect.h < 430;              // short docked panel: slimmer cards/graph, no table
  l.bar = {m_rect.x, m_rect.y, m_rect.w, kBarH};
  l.cards = {m_rect.x, l.bar.bottom(), m_rect.w, compact ? 40.0f : kCardsH};
  l.graph = {m_rect.x, l.cards.bottom(), m_rect.w, compact ? 56.0f : kGraphH};
  float rest = std::max(0.0f, m_rect.bottom() - l.graph.bottom());
  float tbl = compact ? 0.0f : std::min(rest * 0.38f, 7 * kRow + 22);
  l.timeline = {m_rect.x, l.graph.bottom(), m_rect.w, rest - tbl};
  l.table = {m_rect.x, l.timeline.bottom(), m_rect.w, tbl};
  return l;
}
Rect Profiler::recordRect() const { return {m_rect.x + 8, m_rect.y + 4, 100, 22}; }
Rect Profiler::clearRect() const { return {m_rect.x + 114, m_rect.y + 4, 56, 22}; }
Rect Profiler::t60Rect() const { return {m_rect.right() - 128, m_rect.y + 4, 56, 22}; }
Rect Profiler::t30Rect() const { return {m_rect.right() - 66, m_rect.y + 4, 56, 22}; }

Color Profiler::scopeColor(const std::string& n) {
  uint32_t h = 2166136261u;
  for (unsigned char ch : n) h = (h ^ ch) * 16777619u;
  static const Color pal[] = {Color(70, 132, 224), Color(52, 168, 150), Color(198, 120, 58), Color(150, 98, 206),
                              Color(196, 80, 110), Color(98, 158, 70), Color(60, 150, 196), Color(190, 160, 56)};
  return pal[h % 8];
}

int Profiler::frameAt(Vec2 p) const {
  Layout l = layout();
  Rect g = l.graph.inflated(-1.0f);
  if (m_frames.empty() || !g.contains(p)) return -1;
  float bw = (g.w - 12) / float(kHistory);
  int idx = int((p.x - (g.right() - 6 - bw * float(m_frames.size()))) / bw);
  return std::clamp(idx, 0, int(m_frames.size()) - 1);
}

void Profiler::paint(Canvas& c) {
  const Theme& t = theme();
  Layout l = layout();
  c.fillRect(m_rect, t.panel);

  // ---- toolbar
  c.fillRect(l.bar, t.header);
  c.hline(l.bar.x, l.bar.right(), l.bar.bottom() - 1, t.border);
  auto chip = [&](Rect r, const char* label, bool on, bool hot, Color tint) {
    Vec2 q[4] = {Vec2(r.x + 5, r.y), Vec2(r.right(), r.y), Vec2(r.right() - 5, r.bottom()), Vec2(r.x, r.bottom())};
    c.fillPolygon(std::span<const Vec2>(q, 4), on ? tint.alpha(0.28f) : (hot ? t.controlHover : t.control), false);
    if (on) c.fillRect({r.x + 3, r.bottom() - 2, r.w - 8, 2}, tint);
    c.textIn(*t.semibold, r, label, on ? t.textBright : t.text, HAlign::Center);
  };
  Rect rr = recordRect();
  chip(Rect{rr.x, rr.y, rr.w, rr.h}, "", true, m_hot == 1, m_paused ? t.warning : Color(210, 70, 70));
  if (!m_paused) c.fillCircle(Vec2(rr.x + 14, rr.y + 11), 3.2f, Color(240, 90, 90));
  c.textIn(*t.semibold, Rect{rr.x + 22, rr.y, rr.w - 24, rr.h}, m_paused ? "Paused" : "Recording", t.textBright, HAlign::Center);
  chip(clearRect(), "Clear", false, m_hot == 2, t.accent);
  c.textIn(*t.ui, Rect{m_rect.right() - 196, m_rect.y, 64, kBarH}, "Budget", t.textDim, HAlign::Right);
  chip(t60Rect(), "60 fps", std::fabs(m_targetMs - 16.667f) < 0.1f, m_hot == 3, t.accent);
  chip(t30Rect(), "30 fps", std::fabs(m_targetMs - 33.333f) < 0.1f, m_hot == 4, t.accent);

  const ProfileFrame* f = shownFrame();
  if (!f) {
    c.textIn(*t.ui, Rect{m_rect.x, l.cards.y, m_rect.w, m_rect.h - kBarH}, "No frames captured yet", t.textDim, HAlign::Center);
    return;
  }

  // ---- stat cards
  float cpu = f->cpuMs;
  float avg = 0; int n = std::min<int>(30, int(m_frames.size()));
  for (int i = 0; i < n; i++) avg += m_frames[m_frames.size() - 1 - i].cpuMs;
  avg /= float(std::max(1, n));
  struct Card { const char* k; std::string v; Color col; };
  Color okc = cpu <= m_targetMs ? t.success : (cpu <= m_targetMs * 1.5f ? t.warning : t.error);
  Card cards[5] = {
    {"FPS (avg 30)", fmt("%.0f", 1000.0 / std::max(0.01f, avg)), t.textBright},
    {"CPU frame", fmt("%.2f ms", cpu), okc},
    {"GPU frame", fmt("%.2f ms", f->gpuMs), f->gpuMs <= m_targetMs ? t.success : t.warning},
    {"Draw calls", fmt("%.0f", f->drawCalls), t.textBright},
    {"Triangles", f->triangles >= 1000000 ? fmt("%.2f M", f->triangles / 1e6) : fmt("%.1f K", f->triangles / 1e3), t.textBright}};
  float cw = (l.cards.w - 16) / 5.0f;
  for (int i = 0; i < 5; i++) {
    Rect r{l.cards.x + 8 + cw * float(i), l.cards.y + 4, cw - 6, l.cards.h - 6};
    c.fillRoundRect(r, 4, t.field);
    c.fillRect({r.x, r.y + 5, 3, r.h - 10}, cards[i].col);
    bool tall = r.h > 40;
    c.textIn(*t.ui, Rect{r.x + 12, r.y + (tall ? 3 : 0), tall ? r.w - 14 : r.w * 0.5f, tall ? 14 : r.h}, cards[i].k, t.textDim);
    if (tall) c.textIn(*t.semibold, Rect{r.x + 12, r.y + 17, r.w - 14, r.h - 19}, cards[i].v, cards[i].col);
    else c.textIn(*t.semibold, Rect{r.x + r.w * 0.5f, r.y, r.w * 0.5f - 8, r.h}, cards[i].v, cards[i].col, HAlign::Right);
  }

  // ---- frame history graph
  Rect g = l.graph.inflated(-1.0f);
  c.fillRect(g, t.field);
  float top = std::max(m_targetMs * 2.0f, 8.0f);
  for (auto& fr : m_frames) top = std::max(top, fr.cpuMs * 1.1f);
  Rect plot{g.x + 6, g.y + 6, g.w - 12, g.h - 12};
  auto yOf = [&](float ms) { return plot.bottom() - plot.h * std::min(1.0f, ms / top); };
  float bw = plot.w / float(kHistory);
  float x0 = plot.right() - bw * float(m_frames.size());
  for (size_t i = 0; i < m_frames.size(); i++) {
    const ProfileFrame& fr = m_frames[i];
    Color bc = fr.cpuMs <= m_targetMs ? t.success : (fr.cpuMs <= m_targetMs * 1.5f ? t.warning : t.error);
    float y = yOf(fr.cpuMs);
    bool sel = int(i) == (m_selected >= 0 ? m_selected : int(m_frames.size()) - 1);
    c.fillRect({x0 + bw * float(i), y, std::max(1.0f, bw - 0.6f), plot.bottom() - y}, bc.alpha(sel ? 1.0f : 0.62f));
    float gy = yOf(fr.gpuMs);
    c.fillRect({x0 + bw * float(i), gy, std::max(1.0f, bw - 0.6f), 1.5f}, t.info);
    if (sel) c.strokeRect({x0 + bw * float(i) - 1, plot.y, std::max(3.0f, bw + 1.4f), plot.h}, t.textBright);
  }
  float ty = yOf(m_targetMs);
  for (float x = plot.x; x < plot.right(); x += 8) c.hline(x, std::min(plot.right(), x + 4), ty, t.accentHover);
  c.textIn(*t.ui, Rect{plot.x + 4, ty - 14, 120, 12}, fmt("budget %.1f ms", m_targetMs), t.accentHover);
  c.textIn(*t.ui, Rect{plot.right() - 150, plot.y, 146, 12}, "CPU bars    GPU ticks", t.textDim, HAlign::Right);
  c.hline(g.x, g.right(), g.bottom(), t.border);

  paintTimeline(c, l.timeline, *f);
  if (l.table.h > 40) paintTable(c, l.table, *f);
}

void Profiler::paintTimeline(Canvas& c, Rect r, const ProfileFrame& f) {
  const Theme& t = theme();
  c.fillRect(r, t.window);
  c.pushClip(r);
  float total = std::max(f.cpuMs, f.gpuMs);
  for (auto& s : f.scopes) total = std::max(total, s.startMs + s.durMs);
  total = std::max(total, 1.0f);
  Rect area{r.x + kTrackLabelW, r.y + 18, r.w - kTrackLabelW - 6, r.h - 20};
  float span = total / m_zoom;
  float t0 = std::clamp(m_pan, 0.0f, total - span);
  auto xOf = [&](float ms) { return area.x + (ms - t0) / span * area.w; };

  // ruler
  c.fillRect({r.x, r.y, r.w, 17}, t.header);
  float step = 1.0f;
  while (span / step > 12) step *= (step < 2 ? 2.0f : (step < 5 ? 2.5f : 2.0f));
  for (float ms = std::ceil(t0 / step) * step; ms <= t0 + span; ms += step) {
    float x = xOf(ms);
    c.vline(x, r.y + 12, r.bottom(), t.borderSoft);
    c.textIn(*t.ui, Rect{x + 3, r.y, 50, 14}, fmt("%.1f", ms), t.textDim);
  }
  // budget line
  if (m_targetMs >= t0 && m_targetMs <= t0 + span) {
    float x = xOf(m_targetMs);
    for (float y = r.y + 17; y < r.bottom(); y += 6) c.vline(x, y, std::min(r.bottom(), y + 3), t.accentHover);
  }
  static const char* names[3] = {"CPU Main", "Render", "GPU"};
  int maxDepth[3] = {0, 0, 0};
  for (auto& s : f.scopes) maxDepth[std::clamp(s.track, 0, 2)] = std::max(maxDepth[std::clamp(s.track, 0, 2)], s.depth);
  float y = area.y;
  m_hoverScope.clear();
  for (int tr = 0; tr < 3; tr++) {
    float h = float(maxDepth[tr] + 1) * kRow + 6;
    Rect lane{r.x, y, r.w, h};
    if (tr % 2 == 0) c.fillRect(lane, Color(255, 255, 255, 5));
    c.fillRect({r.x, y, 3, h}, tr == 2 ? t.info : t.accent);
    c.textIn(*t.semibold, Rect{r.x + 9, y, kTrackLabelW - 12, h}, names[tr], t.textDim);
    for (auto& s : f.scopes) {
      if (std::clamp(s.track, 0, 2) != tr) continue;
      float x1 = xOf(s.startMs), x2 = xOf(s.startMs + s.durMs);
      if (x2 < area.x || x1 > area.right()) continue;
      Rect b{std::max(x1, area.x), y + 3 + float(s.depth) * kRow, std::max(1.5f, std::min(x2, area.right()) - std::max(x1, area.x)), kRow - 2};
      bool hot = b.contains(m_mouse);
      if (hot) m_hoverScope = s.name;
      Color col = scopeColor(s.name);
      c.gradientV(b, hot ? col.lighten(0.25f) : col, hot ? col : col.darken(0.22f));
      c.strokeRect(b, col.darken(0.45f));
      if (b.w > 34) {
        c.pushClip(b);
        c.textIn(*t.ui, Rect{b.x + 4, b.y, b.w - 6, b.h}, s.name + "  " + fmt("%.2f", s.durMs), Color(255, 255, 255, 235));
        c.popClip();
      }
    }
    y += h;
  }
  c.popClip();
  c.hline(r.x, r.right(), r.bottom() - 1, t.border);
}

void Profiler::paintTable(Canvas& c, Rect r, const ProfileFrame& f) {
  const Theme& t = theme();
  c.fillRect(r, t.panel);
  c.pushClip(r);
  struct Row { std::string name; float total = 0, self = 0; int calls = 0; };
  std::map<std::string, Row> m;
  for (auto& s : f.scopes) {
    Row& row = m[s.name]; row.name = s.name; row.total += s.durMs; row.calls++;
    float childSum = 0;
    for (auto& k : f.scopes) if (k.track == s.track && k.depth == s.depth + 1 && k.startMs >= s.startMs && k.startMs + k.durMs <= s.startMs + s.durMs + 1e-4f) childSum += k.durMs;
    row.self += std::max(0.0f, s.durMs - childSum);
  }
  std::vector<Row> rows; for (auto& kv : m) rows.push_back(kv.second);
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.self > b.self; });
  float total = std::max(0.01f, f.cpuMs + f.gpuMs);
  c.fillRect({r.x, r.y, r.w, 20}, t.header);
  float cx[5] = {r.x + 10, r.x + r.w * 0.46f, r.x + r.w * 0.58f, r.x + r.w * 0.70f, r.x + r.w * 0.80f};
  const char* hd[5] = {"Scope (hot first)", "Self ms", "Total ms", "Calls", "Share"};
  for (int i = 0; i < 5; i++) c.textIn(*t.semibold, Rect{cx[i], r.y, 120, 20}, hd[i], t.textDim);
  float y = r.y + 21;
  for (size_t i = 0; i < rows.size() && y + kRow <= r.bottom(); i++, y += kRow) {
    const Row& w = rows[i];
    if (i % 2) c.fillRect({r.x, y, r.w, kRow}, Color(255, 255, 255, 6));
    c.fillRect({r.x + 4, y + 5, 7, 7}, scopeColor(w.name));
    c.textIn(*t.ui, Rect{cx[0] + 8, y, cx[1] - cx[0] - 12, kRow}, w.name, w.name == m_hoverScope ? t.textBright : t.text);
    c.textIn(*t.ui, Rect{cx[1], y, 70, kRow}, fmt("%.2f", w.self), t.text);
    c.textIn(*t.ui, Rect{cx[2], y, 70, kRow}, fmt("%.2f", w.total), t.textDim);
    c.textIn(*t.ui, Rect{cx[3], y, 50, kRow}, fmt("%.0f", w.calls), t.textDim);
    float share = std::min(1.0f, w.self / total);
    Rect bar{cx[4], y + 5, r.right() - cx[4] - 12, 7};
    c.fillRect(bar, t.field);
    c.fillRect({bar.x, bar.y, bar.w * std::min(1.0f, share * 3.0f), bar.h}, scopeColor(w.name));
  }
  c.popClip();
}

bool Profiler::onMouseDown(const MouseEvent& e) {
  m_mouse = e.pos;
  if (e.button == MouseButton::Left) {
    if (recordRect().contains(e.pos)) { setPaused(!m_paused); return true; }
    if (clearRect().contains(e.pos)) { clear(); return true; }
    if (t60Rect().contains(e.pos)) { setTargetMs(16.667f); return true; }
    if (t30Rect().contains(e.pos)) { setTargetMs(33.333f); return true; }
    int fi = frameAt(e.pos);
    if (fi >= 0) { m_scrub = true; if (e.clicks >= 2) selectFrame(-1); else { selectFrame(fi); m_paused = true; } return true; }
  }
  if (layout().timeline.contains(e.pos) && (e.button == MouseButton::Middle || e.button == MouseButton::Left)) {
    m_panning = true; m_panGrabX = e.pos.x; m_panStart = m_pan; return true;
  }
  return false;
}
bool Profiler::onMouseMove(const MouseEvent& e) {
  m_mouse = e.pos;
  int h = recordRect().contains(e.pos) ? 1 : clearRect().contains(e.pos) ? 2 : t60Rect().contains(e.pos) ? 3 : t30Rect().contains(e.pos) ? 4 : 0;
  if (h != m_hot) m_hot = h;
  if (m_scrub) { int fi = frameAt(e.pos); if (fi >= 0) selectFrame(fi); }
  if (m_panning) {
    const ProfileFrame* f = shownFrame();
    if (f) {
      float total = std::max({f->cpuMs, f->gpuMs, 1.0f});
      for (auto& s : f->scopes) total = std::max(total, s.startMs + s.durMs);
      float msPerPx = (total / m_zoom) / std::max(1.0f, m_rect.w - kTrackLabelW);
      m_pan = std::clamp(m_panStart - (e.pos.x - m_panGrabX) * msPerPx, 0.0f, total - total / m_zoom);
    }
  }
  invalidate();
  return m_scrub || m_panning;
}
bool Profiler::onMouseUp(const MouseEvent&) { m_scrub = m_panning = false; return false; }
bool Profiler::onMouseWheel(const MouseEvent& e) {
  if (!layout().timeline.contains(e.pos)) return false;
  float old = m_zoom;
  m_zoom = std::clamp(m_zoom * (e.wheel.y > 0 ? 1.25f : 0.8f), 1.0f, 24.0f);
  if (m_zoom == 1.0f) m_pan = 0;
  else if (old != m_zoom) {
    const ProfileFrame* f = shownFrame();
    float total = f ? std::max({f->cpuMs, f->gpuMs, 1.0f}) : 1.0f;
    float fx = std::clamp((e.pos.x - m_rect.x - kTrackLabelW) / std::max(1.0f, m_rect.w - kTrackLabelW), 0.0f, 1.0f);
    float anchor = m_pan + fx * total / old;
    m_pan = std::clamp(anchor - fx * total / m_zoom, 0.0f, total - total / m_zoom);
  }
  invalidate();
  return true;
}
void Profiler::onMouseLeave() { m_hot = 0; m_hoverScope.clear(); m_mouse = {-1, -1}; invalidate(); }

const std::string& Profiler::tooltipAt(Vec2 p) const {
  m_tip.clear();
  int fi = frameAt(p);
  if (fi >= 0) {
    const ProfileFrame& f = m_frames[fi];
    m_tip = "Frame " + std::to_string(fi) + "  CPU " + fmt("%.2f ms", f.cpuMs) + "  GPU " + fmt("%.2f ms", f.gpuMs) + "  (click to inspect, double-click to follow live)";
  }
  return m_tip;
}

} // namespace prism
