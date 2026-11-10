// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/IconAtlas.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace prism {
namespace {
std::string trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
// value of attribute `key="..."` inside tag text starting at `from`; empty if absent
std::string attr(const std::string& tag, const std::string& key) {
  size_t p = 0;
  while ((p = tag.find(key, p)) != std::string::npos) {
    bool boundary = p == 0 || std::isspace((unsigned char)tag[p - 1]);
    size_t q = p + key.size();
    while (q < tag.size() && tag[q] == ' ') q++;
    if (boundary && q < tag.size() && tag[q] == '=') {
      q++;
      while (q < tag.size() && tag[q] == ' ') q++;
      if (q < tag.size() && (tag[q] == '"' || tag[q] == '\'')) {
        char qc = tag[q];
        size_t e = tag.find(qc, q + 1);
        if (e != std::string::npos) return tag.substr(q + 1, e - q - 1);
      }
    }
    p += key.size();
  }
  return "";
}
} // namespace

void IconAtlas::add(const std::string& name, std::vector<std::string> paths, float vb, float stroke, bool fill) {
  Src s; s.vb = vb > 0 ? vb : 24; s.stroke = stroke; s.fill = fill;
  for (auto& d : paths) { SvgPath p = SvgPath::parse(d); if (!p.empty()) s.paths.push_back(std::move(p)); }
  if (s.paths.empty()) return;
  m_src[name] = std::move(s);
  m_w = m_h = 0; m_entries.clear(); m_cov.clear();     // invalidates a previous bake
}

bool IconAtlas::addSvg(const std::string& name, const std::string& svg) {
  float vb = 24, stroke = 1.5f; bool fill = false;
  size_t sp = svg.find("<svg");
  if (sp != std::string::npos) {
    std::string tag = svg.substr(sp, svg.find('>', sp) - sp);
    std::string v = attr(tag, "viewBox");
    if (!v.empty()) { std::istringstream is(v); float a, b, w, h; if (is >> a >> b >> w >> h) vb = std::max(w, h); }
    std::string sw = attr(tag, "stroke-width");
    if (!sw.empty()) stroke = std::strtof(sw.c_str(), nullptr);
    std::string f = attr(tag, "fill");
    fill = !f.empty() && f != "none";
  }
  std::vector<std::string> ds;
  for (size_t p = 0; (p = svg.find("<path", p)) != std::string::npos;) {
    size_t e = svg.find('>', p);
    if (e == std::string::npos) break;
    std::string tag = svg.substr(p, e - p);
    std::string d = attr(tag, "d");
    if (!d.empty()) ds.push_back(d);
    std::string sw = attr(tag, "stroke-width");
    if (!sw.empty()) stroke = std::strtof(sw.c_str(), nullptr);
    p = e;
  }
  if (ds.empty()) return false;
  size_t before = m_src.size();
  add(name, ds, vb, stroke, fill);
  return m_src.size() > before || m_src.count(name);
}

int IconAtlas::loadManifest(const std::string& text) {
  int n = 0; std::istringstream in(text); std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> f; std::stringstream ss(line); std::string part;
    while (f.size() < 4 && std::getline(ss, part, '|')) f.push_back(trim(part));
    std::string rest; std::getline(ss, rest);
    if (f.size() < 4 || rest.empty()) continue;
    std::vector<std::string> ds; std::stringstream ds2(rest);
    while (std::getline(ds2, part, ';')) if (!trim(part).empty()) ds.push_back(trim(part));
    size_t before = m_src.size();
    add(f[0], ds, std::strtof(f[1].c_str(), nullptr), std::strtof(f[2].c_str(), nullptr), f[3] == "1");
    if (m_src.size() > before || m_src.count(f[0])) n++;
  }
  return n;
}

std::vector<std::string> IconAtlas::names() const {
  std::vector<std::string> v; for (auto& [k, _] : m_src) v.push_back(k); return v;
}

void IconAtlas::bake(const std::vector<float>& scales, int baseSize) {
  struct Job { std::string name; float scale; int px; };
  std::vector<Job> jobs;
  for (auto& [name, s] : m_src) for (float sc : scales) jobs.push_back({name, sc, std::max(4, int(std::lround(float(baseSize) * sc)))});
  // shelf packing, tallest first, 1px gutter so resampling never bleeds between icons
  std::stable_sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.px > b.px; });
  int W = 256;
  for (;;) {
    int x = 1, y = 1, rowH = 0; bool ok = true;
    for (auto& j : jobs) {
      if (x + j.px + 1 > W) { x = 1; y += rowH + 1; rowH = 0; }
      if (j.px + 2 > W) { ok = false; break; }
      x += j.px + 1; rowH = std::max(rowH, j.px);
    }
    if (ok) break;
    W *= 2;
  }
  m_entries.clear();
  int x = 1, y = 1, rowH = 0;
  for (auto& j : jobs) {
    if (x + j.px + 1 > W) { x = 1; y += rowH + 1; rowH = 0; }
    m_entries[j.name].push_back({x, y, j.px, j.px, j.scale});
    x += j.px + 1; rowH = std::max(rowH, j.px);
  }
  m_w = W; m_h = y + rowH + 1;
  m_cov.assign(size_t(m_w) * size_t(m_h), 0);
  for (auto& [name, list] : m_entries) {
    const Src& s = m_src.at(name);
    for (auto& e : list) {
      Canvas cv; cv.resize(e.w, e.h); cv.clear(Color(0, 0, 0));
      float k = float(e.w) / s.vb;
      for (auto& p : s.paths)
        p.draw(cv, s.fill ? Color(0, 0, 0, 0) : Color(255, 255, 255), std::max(1.f, s.stroke * k),
               s.fill ? Color(255, 255, 255) : Color(0, 0, 0, 0), {0, 0}, k);
      const uint32_t* px = cv.pixels();
      for (int yy = 0; yy < e.h; yy++)
        for (int xx = 0; xx < e.w; xx++) m_cov[size_t(e.y + yy) * size_t(m_w) + size_t(e.x + xx)] = uint8_t((px[yy * e.w + xx] >> 16) & 0xFF);
    }
  }
}

const IconAtlas::Entry* IconAtlas::find(const std::string& name, float scale) const {
  auto it = m_entries.find(name);
  if (it == m_entries.end() || it->second.empty()) return nullptr;
  const Entry* best = nullptr;
  for (auto& e : it->second) if (!best || std::abs(e.scale - scale) < std::abs(best->scale - scale)) best = &e;
  return best;
}

bool IconAtlas::draw(Canvas& c, const std::string& name, Rect box, Color tint, float scale) const {
  const Entry* e = find(name, scale);
  if (!e) return false;
  int dw = std::max(1, int(std::lround(box.w))), dh = std::max(1, int(std::lround(box.h)));
  int ox = int(std::lround(box.x)), oy = int(std::lround(box.y));
  for (int y = 0; y < dh; y++) {
    int sy = std::min(e->h - 1, y * e->h / dh);
    int runStart = -1; uint8_t runA = 0;
    auto flush = [&](int endX) {
      if (runStart >= 0 && runA) c.fillRect({float(ox + runStart), float(oy + y), float(endX - runStart), 1}, tint.withAlpha(uint8_t(tint.a * runA / 255)));
    };
    for (int x = 0; x < dw; x++) {
      uint8_t a = coverage(*e, std::min(e->w - 1, x * e->w / dw), sy);
      if (runStart < 0 || a != runA) { flush(x); runStart = x; runA = a; }
    }
    flush(dw);
  }
  return true;
}

bool IconAtlas::savePng(const std::string& path) const {
  if (!baked()) return false;
  std::vector<uint32_t> px(m_cov.size());
  for (size_t i = 0; i < px.size(); i++) px[i] = (uint32_t(m_cov[i]) << 24) | 0x00FFFFFFu;
  return Canvas::writePng(path, px.data(), m_w, m_h, m_w);
}

} // namespace prism
