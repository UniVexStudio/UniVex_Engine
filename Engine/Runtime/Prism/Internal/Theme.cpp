// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Theme.h"
#include <cctype>
#include <cstdlib>

namespace prism {

Theme Theme::dark() {
  Theme t;
  t.window = Color::hex(0x121212);
  t.panel = Color::hex(0x1e1e1f);
  t.header = Color::hex(0x262627);
  t.control = Color::hex(0x2e2e30);
  t.controlHover = Color::hex(0x39393c);
  t.controlPress = Color::hex(0x262628);
  t.field = Color::hex(0x161617);
  t.border = Color::hex(0x0d0d0e);
  t.borderSoft = Color::hex(0x2c2c2e);
  t.borderStrong = Color::hex(0x424245);
  t.text = Color::hex(0xcfcfd1);
  t.textDim = Color::hex(0x8b8b90);
  t.textDisabled = Color::hex(0x5a5a5e);
  t.textBright = Color::hex(0xf0f0f2);
  t.accent = Color::hex(0x3b8eea);
  t.accentHover = Color::hex(0x5aa2f5);
  t.accentDim = Color::hex(0x2a4a75);
  t.accentText = Color::hex(0xffffff);
  t.rowHover = Color(255, 255, 255, 12);
  t.rowSelected = Color::hex(0x2a4a75);
  t.rowSelectedInactive = Color::hex(0x333338);
  t.success = Color::hex(0x4fb06d);
  t.warning = Color::hex(0xe0a83a);
  t.error = Color::hex(0xe5534b);
  t.info = Color::hex(0x5aa2f5);
  t.axisX = Color::hex(0xe4574c);
  t.axisY = Color::hex(0x6fbf4a);
  t.axisZ = Color::hex(0x4a8fe7);
  t.logBg = Color::hex(0x19191a);
  t.statusBar = Color::hex(0x1a1a1b);
  return t;
}

Theme Theme::light() {
  Theme t = dark();
  t.window = Color::hex(0xe4e4e8);
  t.panel = Color::hex(0xf5f5f7);
  t.header = Color::hex(0xebebee);
  t.control = Color::hex(0xe1e1e5);
  t.controlHover = Color::hex(0xd6d6dc);
  t.controlPress = Color::hex(0xcacad2);
  t.field = Color::hex(0xffffff);
  t.border = Color::hex(0xc2c2c9);
  t.borderSoft = Color::hex(0xd9d9df);
  t.borderStrong = Color::hex(0xa6a6b0);
  t.text = Color::hex(0x2a2a30);
  t.textDim = Color::hex(0x6a6a74);
  t.textDisabled = Color::hex(0xa6a6ae);
  t.textBright = Color::hex(0x000000);
  t.accent = Color::hex(0x2f7fe0);
  t.accentHover = Color::hex(0x4a90ea);
  t.accentDim = Color::hex(0xb9d4f6);
  t.rowHover = Color(0, 0, 0, 14);
  t.rowSelected = Color::hex(0xcde0f8);
  t.rowSelectedInactive = Color::hex(0xdadae0);
  t.success = Color::hex(0x2f9a52);
  t.warning = Color::hex(0xb7791f);
  t.error = Color::hex(0xd0382f);
  t.info = Color::hex(0x2f7fe0);
  t.logBg = Color::hex(0xffffff);
  t.statusBar = Color::hex(0xdcdce1);
  return t;
}

const std::vector<Theme::ColorToken>& Theme::colorTokens() {
  static const std::vector<ColorToken> v = {
#define T(n) {#n, &Theme::n}
      T(window), T(panel), T(header), T(control), T(controlHover), T(controlPress), T(field), T(border), T(borderSoft),
      T(borderStrong), T(text), T(textDim), T(textDisabled), T(textBright), T(accent), T(accentHover), T(accentDim),
      T(accentText), T(rowHover), T(rowSelected), T(rowSelectedInactive), T(success), T(warning), T(error), T(info),
      T(axisX), T(axisY), T(axisZ), T(logBg), T(statusBar)
#undef T
  };
  return v;
}

const std::vector<Theme::MetricToken>& Theme::metricTokens() {
  static const std::vector<MetricToken> v = {
#define T(n) {#n, &Theme::n}
      T(controlH), T(rowH), T(menuBarH), T(toolbarH), T(tabH), T(statusH), T(pad), T(gap), T(radius), T(splitter), T(gloss)
#undef T
  };
  return v;
}

void Theme::adoptStyle(const Theme& o) {
  for (auto& t : colorTokens()) this->*t.member = o.*t.member;
  for (auto& t : metricTokens()) this->*t.member = o.*t.member;
}

namespace {

struct JsonValue { std::string str; double num = 0; bool isStr = false; };

// Tiny parser for a flat JSON object of string/number values (enough for theme files).
bool parseFlatJson(const std::string& s, std::vector<std::pair<std::string, JsonValue>>& out, std::string& err) {
  size_t i = 0;
  auto ws = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++; };
  auto str = [&](std::string& r) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) i++;
      r += s[i++];
    }
    if (i >= s.size()) return false;
    i++;
    return true;
  };
  ws();
  if (i >= s.size() || s[i] != '{') { err = "expected '{'"; return false; }
  i++;
  ws();
  if (i < s.size() && s[i] == '}') return true;
  while (true) {
    ws();
    std::string key;
    if (!str(key)) { err = "expected a quoted key near offset " + std::to_string(i); return false; }
    ws();
    if (i >= s.size() || s[i] != ':') { err = "expected ':' after \"" + key + "\""; return false; }
    i++;
    ws();
    JsonValue v;
    if (i < s.size() && s[i] == '"') {
      v.isStr = true;
      if (!str(v.str)) { err = "unterminated string for \"" + key + "\""; return false; }
    } else {
      size_t st = i;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '-' || s[i] == '+' || s[i] == 'e' || s[i] == 'E')) i++;
      if (st == i) { err = "bad value for \"" + key + "\""; return false; }
      v.num = std::strtod(s.substr(st, i - st).c_str(), nullptr);
    }
    out.emplace_back(std::move(key), std::move(v));
    ws();
    if (i < s.size() && s[i] == ',') { i++; continue; }
    if (i < s.size() && s[i] == '}') return true;
    err = "expected ',' or '}' near offset " + std::to_string(i);
    return false;
  }
}

bool parseColor(const std::string& v, Color& c) {
  if (v.size() < 7 || v[0] != '#' || (v.size() != 7 && v.size() != 9)) return false;
  for (size_t k = 1; k < v.size(); k++) if (!std::isxdigit(static_cast<unsigned char>(v[k]))) return false;
  uint32_t rgb = uint32_t(std::stoul(v.substr(1, 6), nullptr, 16));
  uint8_t a = v.size() == 9 ? uint8_t(std::stoul(v.substr(7, 2), nullptr, 16)) : 255;
  c = Color::hex(rgb, a);
  return true;
}

} // namespace

bool Theme::applyJson(const std::string& json, std::string* err) {
  std::vector<std::pair<std::string, JsonValue>> kv;
  std::string e;
  if (!parseFlatJson(json, kv, e)) { if (err) *err = "theme JSON: " + e; return false; }
  Theme t = *this;
  std::string warn;
  for (auto& [k, v] : kv)
    if (k == "base") {
      Theme b = v.str == "light" ? Theme::light() : Theme::dark();
      t.adoptStyle(b);
    }
  for (auto& [k, v] : kv) {
    if (k == "base" || k == "name") continue;
    bool found = false;
    for (auto& tok : colorTokens())
      if (k == tok.name) {
        Color c;
        if (!v.isStr || !parseColor(v.str, c)) { if (err) *err = "theme JSON: \"" + k + "\" needs a colour like \"#rrggbb\""; return false; }
        t.*tok.member = c;
        found = true;
      }
    for (auto& tok : metricTokens())
      if (k == tok.name) {
        if (v.isStr) { if (err) *err = "theme JSON: \"" + k + "\" needs a number"; return false; }
        t.*tok.member = float(v.num);
        found = true;
      }
    if (!found) warn += (warn.empty() ? "" : ", ") + k;
  }
  adoptStyle(t);
  if (err) *err = warn.empty() ? std::string() : "unknown keys ignored: " + warn;
  return true;
}

void Theme::loadFonts(const std::string& dir) {
  fontDir = dir;
  auto load = [&](const char* file, int px) { return std::make_shared<Font>(dir + "/fonts/" + file, px); };
  ui = load("Inter-Regular.otf", 12);
  medium = load("Inter-Medium.otf", 12);
  semibold = load("Inter-SemiBold.otf", 12);
  small = load("Inter-Regular.otf", 11);
  mono = load("DejaVuSansMono.ttf", 11);
  monoLarge = load("DejaVuSansMono.ttf", 14);
}

namespace {
Theme g_theme = Theme::dark();
}
const Theme& theme() { return g_theme; }
Theme& themeMutable() { return g_theme; }

} // namespace prism
