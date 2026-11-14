// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - framework overlays drawn above everything: toast notifications and the widget inspector.
#include "prism/Application.h"
#include "prism/Icons.h"
#include <algorithm>
#include <cstdio>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif
#include <typeinfo>

namespace prism {

// ------------------------------------------------------------------ toasts
void Application::notify(const std::string& text, ToastLevel level, int durationMs) {
  m_toasts.push_back({text, level, now(), durationMs});
  if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
  invalidate();
  if (!m_toastTimer)
    m_toastTimer = addTimer(33, [this] {
      uint64_t t = now();
      std::erase_if(m_toasts, [&](const Toast& x) { return t >= x.born + uint64_t(x.duration); });
      invalidate();
      if (m_toasts.empty()) { m_toastTimer = 0; return false; }
      return true;
    });
}

void Application::paintToasts(Canvas& c) {
  if (m_toasts.empty()) return;
  const Theme& t = theme();
  uint64_t n = now();
  float y = float(c.height()) - m_toastBottom;
  for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
    float age = float(n - it->born), life = float(it->duration);
    float a = std::min({1.f, age / 160.f, (life - age) / 380.f});
    if (a <= 0) continue;
    a = std::clamp(a, 0.f, 1.f);
    Color accent = it->level == ToastLevel::Success ? t.success : it->level == ToastLevel::Warning ? t.warning
                   : it->level == ToastLevel::Error ? t.error : t.info;
    Icon ic = it->level == ToastLevel::Success ? Icon::Check : it->level == ToastLevel::Warning ? Icon::Warning
              : it->level == ToastLevel::Error ? Icon::Error : Icon::Info;
    float w = std::min(float(c.width()) - 24, std::ceil(t.ui->width(it->text)) + 56), h = 34;
    float slide = (1.f - a) * 14.f;
    Rect r{float(c.width()) - w - 14 + slide, y - h, w, h};
    c.shadow(r, 4, 10, Color(0, 0, 0, 255).alpha(a * 0.8f));
    c.fillRoundRect(r, 4, t.header.alpha(a));
    c.strokeRoundRect(r, 4, t.borderStrong.alpha(a));
    c.fillRect({r.x + 1, r.y + 5, 3, r.h - 10}, accent.alpha(a));
    drawIcon(c, ic, {r.x + 12, r.y + (h - 16) * 0.5f, 16, 16}, accent.alpha(a), 1.4f);
    c.textIn(*t.ui, {r.x + 36, r.y, r.w - 44, r.h}, it->text, t.text.alpha(a));
    y -= h + 8;
  }
}

// ------------------------------------------------------------------ inspector
namespace {
std::string className(const Widget* w) {
  const char* raw = typeid(*w).name();
  std::string s = raw;
#if defined(__GNUC__) || defined(__clang__)
  int status = 0;
  if (char* d = abi::__cxa_demangle(raw, nullptr, nullptr, &status)) {
    if (status == 0) s = d;
    std::free(d);
  }
#endif
  for (const char* pre : {"prism::", "demo::"})
    if (s.rfind(pre, 0) == 0) s = s.substr(std::char_traits<char>::length(pre));
  return s;
}
} // namespace

void Application::paintInspector(Canvas& c) {
  const Theme& t = theme();
  Widget* w = m_hoverChain.empty() ? nullptr : m_hoverChain.front().lock().get();
  if (!w) return;
  Color hi = Color::hex(0xff7a1a);
  Rect r = w->rect();
  c.fillRect(r, hi.alpha(0.12f));
  c.strokeRect(r, hi, 1);
  if (w->m_parent) c.strokeRect(w->m_parent->rect(), hi.alpha(0.35f), 1);

  std::vector<std::string> names;
  for (const Widget* p = w; p; p = p->m_parent) names.push_back(className(p));
  int depth = int(names.size());
  std::reverse(names.begin(), names.end());
  std::string path;
  for (size_t i = 0; i < names.size(); i++) {
    if (names.size() > 6 && i >= 2 && i < names.size() - 3) { if (i == 2) path += " > ..."; continue; }
    path += (i ? " > " : "") + names[i];
  }
  char b[160];
  std::vector<std::string> lines;
  lines.push_back(className(w));
  std::snprintf(b, sizeof b, "rect    %.0f, %.0f   %.0f x %.0f", r.x, r.y, r.w, r.h);
  lines.push_back(b);
  std::snprintf(b, sizeof b, "desired %.0f x %.0f   depth %d   children %zu", w->desired().x, w->desired().y, depth, w->children().size());
  lines.push_back(b);
  std::string flags;
  flags += w->enabled() ? "enabled " : "DISABLED ";
  flags += w->focusable() ? "focusable " : "";
  flags += w->m_focused ? "FOCUSED " : "";
  flags += w->m_hovered ? "hovered " : "";
  flags += w->consumesHostInput() ? "" : "host-owned ";
  lines.push_back("flags   " + flags);
  std::snprintf(b, sizeof b, "frame   %.1f ms   popups %zu   timers %zu", m_frameMs, m_popups.size(), m_timers.size());
  lines.push_back(b);
  lines.push_back(path);

  float wd = 0;
  for (size_t i = 0; i < lines.size(); i++) wd = std::max(wd, std::ceil((i == 0 ? t.semibold : t.mono)->width(lines[i])));
  wd = std::min(wd + 24, float(c.width()) - 16);
  float lh = 17, h = float(lines.size()) * lh + 14;
  Rect box{12, float(c.height()) - h - 34, wd, h};
  c.shadow(box, 4, 10, Color(0, 0, 0, 255));
  c.fillRoundRect(box, 4, Color(18, 18, 20, 242));
  c.strokeRoundRect(box, 4, hi);
  for (size_t i = 0; i < lines.size(); i++) {
    Rect lr{box.x + 12, box.y + 7 + float(i) * lh, box.w - 20, lh};
    if (i == 0) c.textIn(*t.semibold, lr, lines[i], hi);
    else c.textIn(*t.mono, lr, lines[i], Color::hex(0xd8d8dc));
  }
}

} // namespace prism
