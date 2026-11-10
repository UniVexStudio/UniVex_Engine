// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Color picker implementation (square/wheel plane, RGB/HSV/I/A channels, Linear view, floating window).
#include "prism/ColorPicker.h"
#include "prism/Docking.h"
#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Layouts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace prism {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSliderMaxI = 10.0f;     // intensity slider range (text entry accepts more)
constexpr float kMaxI = 1000.0f;
std::vector<Color> g_recent;

std::string formatHex(Color c) {
  char buf[16];
  if (c.a == 255) std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  else std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
  return buf;
}

bool parseHex(const std::string& str, Color& out) {
  std::string s = str;
  if (!s.empty() && s[0] == '#') s = s.substr(1);
  unsigned r, g, b, a = 255;
  if (s.size() == 6 && std::sscanf(s.c_str(), "%02x%02x%02x", &r, &g, &b) == 3) { out = Color(uint8_t(r), uint8_t(g), uint8_t(b), 255); return true; }
  if (s.size() == 8 && std::sscanf(s.c_str(), "%02x%02x%02x%02x", &r, &g, &b, &a) == 4) { out = Color(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)); return true; }
  return false;
}

void hsvToRgbF(float h, float s, float v, float out[3]) {
  h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
  s = std::clamp(s, 0.0f, 1.0f); v = std::clamp(v, 0.0f, 1.0f);
  float hh = h / 60.0f; int i = int(std::floor(hh)); float f = hh - float(i);
  float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (i % 6) {
    case 0: out[0] = v; out[1] = t; out[2] = p; break;
    case 1: out[0] = q; out[1] = v; out[2] = p; break;
    case 2: out[0] = p; out[1] = v; out[2] = t; break;
    case 3: out[0] = p; out[1] = q; out[2] = v; break;
    case 4: out[0] = t; out[1] = p; out[2] = v; break;
    default: out[0] = v; out[1] = p; out[2] = q; break;
  }
}

Color rgbF(const float c[3], float a = 1.0f) {
  auto u = [](float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
  return Color(u(c[0]), u(c[1]), u(c[2]), u(a));
}

Color hsvColor(float h, float s, float v) { float c[3]; hsvToRgbF(h, s, v, c); return rgbF(c); }

void checker(Canvas& c, Rect r, float sz = 5.0f) {
  c.pushClip(r);
  c.fillRect(r, Color(58, 62, 72));
  int iy = 0;
  for (float y = r.y; y < r.bottom(); y += sz, ++iy) {
    int ix = 0;
    for (float x = r.x; x < r.right(); x += sz, ++ix)
      if ((ix + iy) % 2 == 0) c.fillRect(Rect{x, y, sz, sz}, Color(92, 97, 108));
  }
  c.popClip();
}

const char* kLabels[8] = {"R", "G", "B", "H", "S", "V", "I", "A"};

} // namespace

float srgbToLinear(float c) {
  c = std::clamp(c, 0.0f, 1.0f);
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float linearToSrgb(float c) {
  c = std::clamp(c, 0.0f, 1.0f);
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

HsvColor HsvColor::fromRgb(Color c) {
  float r = float(c.r) / 255.0f;
  float g = float(c.g) / 255.0f;
  float b = float(c.b) / 255.0f;
  float a = float(c.a) / 255.0f;

  float maxC = std::max({r, g, b});
  float minC = std::min({r, g, b});
  float delta = maxC - minC;

  HsvColor hsv;
  hsv.v = maxC;
  hsv.a = a;

  if (maxC > 1e-5f) {
    hsv.s = delta / maxC;
  } else {
    hsv.s = 0.0f;
    hsv.h = 0.0f;
    return hsv;
  }

  if (delta < 1e-5f) {
    hsv.h = 0.0f;
    return hsv;
  }

  if (r >= maxC) {
    hsv.h = (g - b) / delta;
  } else if (g >= maxC) {
    hsv.h = 2.0f + (b - r) / delta;
  } else {
    hsv.h = 4.0f + (r - g) / delta;
  }

  hsv.h *= 60.0f;
  if (hsv.h < 0.0f) hsv.h += 360.0f;
  return hsv;
}

Color HsvColor::toRgb() const {
  float hue = std::fmod(h, 360.0f);
  if (hue < 0.0f) hue += 360.0f;
  float sat = std::clamp(s, 0.0f, 1.0f);
  float val = std::clamp(v, 0.0f, 1.0f);
  uint8_t aInt = uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f));

  if (sat < 1e-5f) {
    uint8_t gray = uint8_t(std::lround(val * 255.0f));
    return Color(gray, gray, gray, aInt);
  }

  float hh = hue / 60.0f;
  int i = int(std::floor(hh));
  float ff = hh - float(i);
  float p = val * (1.0f - sat);
  float q = val * (1.0f - (sat * ff));
  float t = val * (1.0f - (sat * (1.0f - ff)));

  float r = 0, g = 0, b = 0;
  switch (i % 6) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    case 5: default: r = val; g = p; b = q; break;
  }

  return Color(uint8_t(std::lround(std::clamp(r * 255.0f, 0.0f, 255.0f))),
               uint8_t(std::lround(std::clamp(g * 255.0f, 0.0f, 255.0f))),
               uint8_t(std::lround(std::clamp(b * 255.0f, 0.0f, 255.0f))),
               aInt);
}


void ColorPicker::addRecent(Color c) {
  c.a = c.a;
  g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), c), g_recent.end());
  g_recent.insert(g_recent.begin(), c);
  if (g_recent.size() > 16) g_recent.resize(16);
}
const std::vector<Color>& ColorPicker::recent() { return g_recent; }

ColorPicker::ColorPicker(Color initialColor) : m_color(initialColor), m_initialColor(initialColor) {
  m_rgb = {float(initialColor.r) / 255.f, float(initialColor.g) / 255.f, float(initialColor.b) / 255.f};
  m_hsv = HsvColor::fromRgb(initialColor);

  for (int i = 0; i < kCh; i++) {
    auto f = make<TextInput>("", "");
    f->setCentered(true);
    f->setFont(FontRole::Small);
    ColorChannel ch = ColorChannel(i);
    auto apply = [this, ch, w = f.get()] { applyText(ch, w->text()); };
    f->onCommit = [apply](const std::string&) { apply(); };
    f->onBlur = [apply] { apply(); };
    addChild(f);
    m_rows[size_t(i)].field = f;
  }

  m_hex = make<TextInput>(formatHex(initialColor), "#RRGGBB");
  m_hex->setCentered(true);
  m_hex->setFont(FontRole::Small);
  auto applyHex = [this] {
    Color c;
    if (parseHex(m_hex->text(), c)) setColor(c, true);
    else m_hex->setText(formatHex(m_color));
  };
  m_hex->onCommit = [applyHex](const std::string&) { applyHex(); };
  m_hex->onBlur = [applyHex] { applyHex(); };
  m_hex->onChanged = [this](const std::string& v) {
    Color c;
    if ((v.size() == 7 || v.size() == 9) && v[0] == '#' && parseHex(v, c) && c != m_color) setColor(c, true);
  };
  addChild(m_hex);

  m_swatches = {
    Color(255, 59, 48), Color(255, 149, 0), Color(255, 204, 0), Color(52, 199, 89),
    Color(0, 199, 190), Color(48, 176, 199), Color(0, 122, 255), Color(88, 86, 214),
    Color(175, 82, 222), Color(255, 45, 85), Color(255, 255, 255), Color(142, 142, 147),
    Color(99, 99, 102), Color(58, 58, 60), Color(28, 28, 30), Color(0, 0, 0)
  };
  syncFields();
}

Color ColorPicker::rebuild() const {
  Color c = rgbF(m_rgb.data(), m_hsv.a);
  return c;
}

ColorHdr ColorPicker::hdr() const {
  return {srgbToLinear(m_rgb[0]) * m_intensity, srgbToLinear(m_rgb[1]) * m_intensity,
          srgbToLinear(m_rgb[2]) * m_intensity, m_hsv.a};
}

void ColorPicker::rgbChanged() {
  float keepH = m_hsv.h, keepS = m_hsv.s;
  Color tmp = rgbF(m_rgb.data());
  // Use float precision so greys/blacks keep their hue.
  float mx = std::max({m_rgb[0], m_rgb[1], m_rgb[2]}), mn = std::min({m_rgb[0], m_rgb[1], m_rgb[2]});
  float d = mx - mn;
  m_hsv.v = mx;
  m_hsv.s = mx > 1e-5f ? d / mx : keepS;
  if (d < 1e-5f) { m_hsv.h = keepH; if (mx > 1e-5f) m_hsv.s = 0.0f; }
  else {
    float h;
    if (mx == m_rgb[0]) h = (m_rgb[1] - m_rgb[2]) / d;
    else if (mx == m_rgb[1]) h = 2.0f + (m_rgb[2] - m_rgb[0]) / d;
    else h = 4.0f + (m_rgb[0] - m_rgb[1]) / d;
    h *= 60.0f; if (h < 0) h += 360.0f;
    m_hsv.h = h;
  }
  (void)tmp;
}

void ColorPicker::hsvChanged() {
  float c[3];
  hsvToRgbF(m_hsv.h, m_hsv.s, m_hsv.v, c);
  m_rgb = {c[0], c[1], c[2]};
}

void ColorPicker::syncFields() {
  for (int i = 0; i < kCh; i++) {
    auto& f = m_rows[size_t(i)].field;
    if (f && !f->focused()) f->setText(text(ColorChannel(i)), false);
  }
  if (m_hex && !m_hex->focused()) m_hex->setText(formatHex(m_color), false);
}

void ColorPicker::commit(bool notify) {
  m_color = rebuild();
  syncFields();
  invalidate();
  if (!notify) return;
  if (onColorChanged) onColorChanged(m_color);
  if (onHdrChanged) onHdrChanged(hdr());
}

void ColorPicker::setColor(Color c, bool notify) {
  m_rgb = {float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f};
  rgbChanged();
  m_hsv.a = float(c.a) / 255.f;
  m_color = c;
  syncFields();
  invalidate();
  if (notify) {
    if (onColorChanged) onColorChanged(m_color);
    if (onHdrChanged) onHdrChanged(hdr());
  }
}

void ColorPicker::setIntensity(float i, bool notify) {
  m_intensity = std::clamp(i, 0.0f, kMaxI);
  syncFields();
  invalidate();
  if (notify && onHdrChanged) onHdrChanged(hdr());
}

void ColorPicker::setLinear(bool linear) {
  m_isLinear = linear;
  syncFields();
  invalidate();
}

// ---------------------------------------------------------------- channel model
float ColorPicker::norm(ColorChannel ch) const {
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B: {
      float v = m_rgb[size_t(ch)]; return m_isLinear ? srgbToLinear(v) : v; }
    case ColorChannel::H: return std::clamp(m_hsv.h / 360.0f, 0.0f, 1.0f);
    case ColorChannel::S: return std::clamp(m_hsv.s, 0.0f, 1.0f);
    case ColorChannel::V: return std::clamp(m_hsv.v, 0.0f, 1.0f);
    case ColorChannel::I: return std::clamp(m_intensity / kSliderMaxI, 0.0f, 1.0f);
    case ColorChannel::A: return std::clamp(m_hsv.a, 0.0f, 1.0f);
    default: return 0.0f;
  }
}

void ColorPicker::setNorm(ColorChannel ch, float n) {
  n = std::clamp(n, 0.0f, 1.0f);
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B:
      m_rgb[size_t(ch)] = m_isLinear ? linearToSrgb(n) : n; rgbChanged(); break;
    case ColorChannel::H: m_hsv.h = std::min(n * 360.0f, 359.999f); hsvChanged(); break;
    case ColorChannel::S: m_hsv.s = n; hsvChanged(); break;
    case ColorChannel::V: m_hsv.v = n; hsvChanged(); break;
    case ColorChannel::I: m_intensity = n * kSliderMaxI; break;
    case ColorChannel::A: m_hsv.a = n; break;
    default: break;
  }
}

std::string ColorPicker::text(ColorChannel ch) const {
  char b[32];
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B: case ColorChannel::A: {
      float n = norm(ch);
      if (m_isLinear) std::snprintf(b, sizeof(b), "%.3f", n); else std::snprintf(b, sizeof(b), "%d", int(std::lround(n * 255.0f)));
      break; }
    case ColorChannel::H: std::snprintf(b, sizeof(b), "%.0f", m_hsv.h); break;
    case ColorChannel::S: std::snprintf(b, sizeof(b), "%.0f", m_hsv.s * 100.0f); break;
    case ColorChannel::V: std::snprintf(b, sizeof(b), "%.0f", m_hsv.v * 100.0f); break;
    case ColorChannel::I: std::snprintf(b, sizeof(b), "%.2f", m_intensity); break;
    default: b[0] = 0;
  }
  return b;
}

float ColorPicker::channel(ColorChannel ch) const {
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B: case ColorChannel::A:
      return m_isLinear ? norm(ch) : norm(ch) * 255.0f;
    case ColorChannel::H: return m_hsv.h;
    case ColorChannel::S: return m_hsv.s * 100.0f;
    case ColorChannel::V: return m_hsv.v * 100.0f;
    case ColorChannel::I: return m_intensity;
    default: return 0.0f;
  }
}

void ColorPicker::setChannel(ColorChannel ch, float v, bool notify) {
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B: case ColorChannel::A:
      setNorm(ch, m_isLinear ? v : v / 255.0f); break;
    case ColorChannel::H: setNorm(ch, v / 360.0f); break;
    case ColorChannel::S: case ColorChannel::V: setNorm(ch, v / 100.0f); break;
    case ColorChannel::I: m_intensity = std::clamp(v, 0.0f, kMaxI); break;
    default: break;
  }
  commit(notify);
}

void ColorPicker::applyText(ColorChannel ch, const std::string& s) {
  char* end = nullptr;
  float v = std::strtof(s.c_str(), &end);
  if (end == s.c_str()) { syncFields(); return; }
  setChannel(ch, v, true);
}

Color ColorPicker::trackColor(ColorChannel ch, float n) const {
  std::array<float, 3> c = m_rgb;
  switch (ch) {
    case ColorChannel::R: case ColorChannel::G: case ColorChannel::B:
      c[size_t(ch)] = m_isLinear ? linearToSrgb(n) : n; return rgbF(c.data());
    case ColorChannel::H: return hsvColor(n * 360.0f, 1.0f, 1.0f);
    case ColorChannel::S: return hsvColor(m_hsv.h, n, std::max(m_hsv.v, 0.25f));
    case ColorChannel::V: return hsvColor(m_hsv.h, m_hsv.s, n);
    case ColorChannel::I: { float k = std::clamp(n * kSliderMaxI, 0.0f, 1.0f); return Color(uint8_t(c[0] * 255 * k), uint8_t(c[1] * 255 * k), uint8_t(c[2] * 255 * k)); }
    case ColorChannel::A: return rgbF(c.data(), n);
    default: return Color(0, 0, 0);
  }
}

// ---------------------------------------------------------------- layout
Vec2 ColorPicker::measure(Vec2 avail) {
  return m_desired = constrain(Vec2(std::max(avail.x, 440.0f), std::max(avail.y, 260.0f)));
}

void ColorPicker::arrange(Rect full) {
  m_rect = full;
  // Keep the controls a comfortable width when the picker is docked in a very wide panel.
  const float maxW = 640.0f;
  Rect r = full;
  if (full.w > maxW) { r.x = full.x + std::floor((full.w - maxW) * 0.5f); r.w = maxW; }
  const float pad = 10.0f;

  m_chipSquare = Rect{r.x + pad, r.y + 4.0f, 52.0f, 18.0f};
  m_chipWheel = Rect{m_chipSquare.right() + 4.0f, r.y + 4.0f, 48.0f, 18.0f};
  m_chipLinear = Rect{r.right() - pad - 48.0f, r.y + 4.0f, 48.0f, 18.0f};
  m_chipSrgb = Rect{m_chipLinear.x - 4.0f - 44.0f, r.y + 4.0f, 44.0f, 18.0f};

  m_showRecent = r.h >= 300.0f;
  float footH = (m_showRecent ? 2 : 1) * 18.0f + 4.0f;
  float bodyY = r.y + 28.0f;
  float bodyH = r.h - 28.0f - footH - 4.0f;
  float S = std::clamp(std::min(bodyH, 176.0f), 96.0f, 176.0f);

  m_plane = Rect{r.x + pad, bodyY, S, S};
  m_strip = Rect{m_plane.right() + 8.0f, bodyY, 14.0f, S};
  m_alphaStrip = Rect{m_strip.right() + 6.0f, bodyY, 14.0f, S};

  float colX = m_alphaStrip.right() + 16.0f;
  float colW = std::max(120.0f, r.right() - pad - colX);
  float hexW = 82.0f;
  m_preview = Rect{colX, bodyY, colW - hexW - 6.0f, 26.0f};
  m_newHalf = Rect{m_preview.x, m_preview.y, std::floor(m_preview.w / 2), m_preview.h};
  m_oldHalf = Rect{m_newHalf.right(), m_preview.y, m_preview.w - m_newHalf.w, m_preview.h};
  m_hex->arrange(Rect{m_preview.right() + 6.0f, bodyY + 3.0f, hexW, 20.0f});

  float rowsTop = bodyY + m_preview.h + 8.0f;
  float avail = bodyY + bodyH - rowsTop;
  float rh = std::clamp((avail - 12.0f) / 8.0f, 16.0f, 22.0f);
  float y = rowsTop;
  float fieldW = 52.0f;
  for (int i = 0; i < kCh; i++) {
    Row& row = m_rows[size_t(i)];
    row.label = Rect{colX, y, 14.0f, rh};
    row.field->arrange(Rect{colX + colW - fieldW, y + 1.0f, fieldW, rh - 2.0f});
    row.track = Rect{colX + 20.0f, y + rh * 0.5f - 5.0f, std::max(40.0f, colW - 20.0f - fieldW - 8.0f), 10.0f};
    y += rh;
    if (i == 2 || i == 5) y += 6.0f;
  }

  // swatch grid (16 per row)
  m_swatchHits.clear();
  float gx = r.x + pad, gw = r.w - 2 * pad, gap = 4.0f;
  float sw = (gw - 15 * gap) / 16.0f, sh = 14.0f;
  float sy = r.bottom() - footH + 2.0f;
  for (size_t i = 0; i < m_swatches.size(); i++)
    m_swatchHits.push_back({Rect{gx + float(i) * (sw + gap), sy, sw, sh}, m_swatches[i]});
  if (m_showRecent)
    for (size_t i = 0; i < g_recent.size() && i < 16; i++)
      m_swatchHits.push_back({Rect{gx + float(i) * (sw + gap), sy + 18.0f, sw, sh}, g_recent[i]});
}

// ---------------------------------------------------------------- paint
void ColorPicker::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRect(m_rect, t.panel);

  auto chip = [&](Rect r, const char* label, bool on) {
    c.fillRoundRect(r, 3.0f, on ? t.accent : t.control);
    c.strokeRoundRect(r, 3.0f, on ? t.accent : t.border, 1.0f);
    c.textIn(*t.small, r, label, on ? Color(255, 255, 255) : t.textDim, HAlign::Center);
  };
  chip(m_chipSquare, "Square", m_mode == ColorPickerMode::Square);
  chip(m_chipWheel, "Wheel", m_mode == ColorPickerMode::Wheel);
  chip(m_chipSrgb, "sRGB", !m_isLinear);
  chip(m_chipLinear, "Linear", m_isLinear);

  // ---- plane
  const float h = m_hsv.h, s = m_hsv.s, v = m_hsv.v;
  Vec2 reticle;
  if (m_mode == ColorPickerMode::Square) {
    int S = int(m_plane.h);
    for (int y = 0; y < S; y += 2) {
      float vv = 1.0f - (float(y) + 1.0f) / float(S);
      c.gradientH(Rect{m_plane.x, m_plane.y + float(y), m_plane.w, std::min(2.0f, float(S - y))},
                  hsvColor(h, 0.0f, vv), hsvColor(h, 1.0f, vv));
    }
    reticle = Vec2(m_plane.x + s * m_plane.w, m_plane.y + (1.0f - v) * m_plane.h);
    c.strokeRect(m_plane, t.border);
  } else {
    Vec2 C = m_plane.center();
    float R = m_plane.w * 0.5f;
    const int rings = 14, sectors = 72;
    for (int ri = 0; ri < rings; ri++) {
      float r0 = R * float(ri) / float(rings), r1 = R * float(ri + 1) / float(rings);
      float sat = (float(ri) + 0.5f) / float(rings);
      for (int si = 0; si < sectors; si++) {
        float a0 = 2 * kPi * float(si) / float(sectors), a1 = 2 * kPi * (float(si) + 1.04f) / float(sectors);
        Vec2 q[4] = {C + Vec2(std::cos(a0), std::sin(a0)) * r0, C + Vec2(std::cos(a0), std::sin(a0)) * r1,
                     C + Vec2(std::cos(a1), std::sin(a1)) * r1, C + Vec2(std::cos(a1), std::sin(a1)) * r0};
        c.fillPolygon(std::span<const Vec2>(q, 4), hsvColor((a0 + a1) * 0.5f * 180.0f / kPi, sat, v), false);
      }
    }
    c.strokeCircle(C, R, t.border, 1.0f);
    float a = h * kPi / 180.0f;
    reticle = C + Vec2(std::cos(a), std::sin(a)) * (s * R);
  }
  c.fillCircle(reticle, 5.0f, rgbF(m_rgb.data()));
  c.strokeCircle(reticle, 5.5f, Color(255, 255, 255), 1.5f);
  c.strokeCircle(reticle, 7.0f, Color(0, 0, 0, 160), 1.0f);

  // ---- strip (hue in Square mode, value in Wheel mode)
  {
    const int seg = 36;
    for (int i = 0; i < seg; i++) {
      float n0 = float(i) / seg, n1 = float(i + 1) / seg;
      Rect sr{m_strip.x, m_strip.y + n0 * m_strip.h, m_strip.w, std::ceil(m_strip.h / seg) + 0.5f};
      if (m_mode == ColorPickerMode::Square) c.gradientV(sr, hsvColor(n0 * 360.0f, 1, 1), hsvColor(n1 * 360.0f, 1, 1));
      else c.gradientV(sr, hsvColor(h, s, 1.0f - n0), hsvColor(h, s, 1.0f - n1));
    }
    c.strokeRect(m_strip, t.border);
    float pos = m_mode == ColorPickerMode::Square ? h / 360.0f : 1.0f - v;
    float y = m_strip.y + pos * m_strip.h;
    Rect mk{m_strip.x - 2.0f, y - 2.0f, m_strip.w + 4.0f, 4.0f};
    c.fillRect(mk, Color(255, 255, 255)); c.strokeRect(mk, Color(0, 0, 0, 200));
  }
  // ---- alpha strip
  checker(c, m_alphaStrip, 5.0f);
  c.gradientV(m_alphaStrip, rgbF(m_rgb.data(), 1.0f), rgbF(m_rgb.data(), 0.0f));
  c.strokeRect(m_alphaStrip, t.border);
  {
    float y = m_alphaStrip.y + (1.0f - m_hsv.a) * m_alphaStrip.h;
    Rect mk{m_alphaStrip.x - 2.0f, y - 2.0f, m_alphaStrip.w + 4.0f, 4.0f};
    c.fillRect(mk, Color(255, 255, 255)); c.strokeRect(mk, Color(0, 0, 0, 200));
  }

  // ---- old / new preview
  checker(c, m_preview, 6.0f);
  c.fillRect(m_newHalf, m_color);
  c.fillRect(m_oldHalf, m_initialColor);
  c.strokeRect(m_preview, t.border);
  auto tag = [&](Rect r, const char* s2, Color col) {
    c.textIn(*t.small, r, s2, col.luminance() > 0.55f && col.a > 140 ? Color(10, 10, 10) : Color(245, 245, 245), HAlign::Center);
  };
  tag(m_newHalf, "New", m_color);
  tag(m_oldHalf, "Old", m_initialColor);
  if (m_intensity > 1.001f) {
    char b[32]; std::snprintf(b, sizeof(b), "HDR x%.2f", m_intensity);
    Rect hb{m_preview.x, m_preview.bottom() + 1.0f, 0, 0}; (void)hb;
  }

  // ---- channel rows
  for (int i = 0; i < kCh; i++) {
    const Row& row = m_rows[size_t(i)];
    Color lc = i == 0 ? Color(235, 105, 100) : i == 1 ? Color(110, 205, 120) : i == 2 ? Color(105, 150, 240) : t.textDim;
    c.textIn(*t.small, row.label, kLabels[i], lc, HAlign::Left);
    ColorChannel ch = ColorChannel(i);
    // track body
    if (ch == ColorChannel::A) checker(c, row.track, 5.0f);
    else c.fillRoundRect(row.track, 2.0f, t.control);
    const int seg = 24;
    for (int k = 0; k < seg; k++) {
      float n0 = float(k) / seg, n1 = float(k + 1) / seg;
      Rect sr{row.track.x + n0 * row.track.w, row.track.y, std::ceil(row.track.w / seg) + 0.5f, row.track.h};
      if (ch == ColorChannel::A) { // alpha ramp
        c.gradientH(sr, trackColor(ch, n0), trackColor(ch, n1));
      } else c.gradientH(sr, trackColor(ch, n0), trackColor(ch, n1));
    }
    c.strokeRoundRect(row.track, 2.0f, t.border, 1.0f);
    if (ch == ColorChannel::I) {   // tick at 1.0 = LDR white point
      float tx = row.track.x + row.track.w * (1.0f / kSliderMaxI);
      c.line(Vec2(tx, row.track.y), Vec2(tx, row.track.bottom()), Color(255, 190, 70, 200), 1.0f);
    }
    float hx = row.track.x + norm(ch) * row.track.w;
    Rect mk{hx - 2.0f, row.track.y - 3.0f, 4.0f, row.track.h + 6.0f};
    c.fillRect(mk, Color(245, 247, 252));
    c.strokeRect(mk, Color(0, 0, 0, 200));
    row.field->paint(c);
  }
  m_hex->paint(c);

  // ---- swatches
  for (auto& [r, col] : m_swatchHits) {
    c.fillRoundRect(r, 2.0f, col);
    c.strokeRoundRect(r, 2.0f, col == m_color ? Color(255, 255, 255) : t.border, 1.0f);
  }
}

Widget* ColorPicker::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (Widget* w = m_hex->hitTest(p)) return w;
  for (auto& row : m_rows) if (Widget* w = row.field->hitTest(p)) return w;
  return this;
}

// ---------------------------------------------------------------- input
void ColorPicker::dragTo(Vec2 p) {
  switch (m_drag) {
    case Drag::Plane:
      if (m_mode == ColorPickerMode::Square) {
        m_hsv.s = std::clamp((p.x - m_plane.x) / m_plane.w, 0.0f, 1.0f);
        m_hsv.v = std::clamp(1.0f - (p.y - m_plane.y) / m_plane.h, 0.0f, 1.0f);
      } else {
        Vec2 d = p - m_plane.center();
        float R = m_plane.w * 0.5f;
        float deg = std::atan2(d.y, d.x) * 180.0f / kPi; if (deg < 0) deg += 360.0f;
        m_hsv.h = deg;
        m_hsv.s = std::clamp(std::sqrt(d.x * d.x + d.y * d.y) / R, 0.0f, 1.0f);
      }
      hsvChanged(); break;
    case Drag::Strip: {
      float n = std::clamp((p.y - m_strip.y) / m_strip.h, 0.0f, 1.0f);
      if (m_mode == ColorPickerMode::Square) m_hsv.h = std::min(n * 360.0f, 359.999f); else m_hsv.v = 1.0f - n;
      hsvChanged(); break; }
    case Drag::Alpha: m_hsv.a = std::clamp(1.0f - (p.y - m_alphaStrip.y) / m_alphaStrip.h, 0.0f, 1.0f); break;
    case Drag::Track: {
      const Rect& tr = m_rows[size_t(m_dragCh)].track;
      setNorm(ColorChannel(m_dragCh), (p.x - tr.x) / tr.w); break; }
    default: return;
  }
  commit(true);
}

bool ColorPicker::onMouseDown(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return false;
  if (m_chipSquare.contains(e.pos)) { setMode(ColorPickerMode::Square); return true; }
  if (m_chipWheel.contains(e.pos)) { setMode(ColorPickerMode::Wheel); return true; }
  if (m_chipSrgb.contains(e.pos)) { setLinear(false); return true; }
  if (m_chipLinear.contains(e.pos)) { setLinear(true); return true; }
  if (m_oldHalf.contains(e.pos)) { setColor(m_initialColor, true); setIntensity(m_initialIntensity); return true; }
  for (auto& [r, col] : m_swatchHits)
    if (r.contains(e.pos)) { setColor(col, true); return true; }

  bool inPlane = m_mode == ColorPickerMode::Square ? m_plane.contains(e.pos)
                 : (e.pos - m_plane.center()).length() <= m_plane.w * 0.5f + 3.0f;
  if (inPlane) { m_drag = Drag::Plane; dragTo(e.pos); return true; }
  Rect sh = m_strip, ah = m_alphaStrip;
  sh.x -= 3; sh.w += 6; ah.x -= 3; ah.w += 6;
  if (sh.contains(e.pos)) { m_drag = Drag::Strip; dragTo(e.pos); return true; }
  if (ah.contains(e.pos)) { m_drag = Drag::Alpha; dragTo(e.pos); return true; }
  for (int i = 0; i < kCh; i++) {
    Rect hit = m_rows[size_t(i)].track; hit.x -= 4; hit.w += 8; hit.y -= 4; hit.h += 8;
    if (hit.contains(e.pos)) { m_drag = Drag::Track; m_dragCh = i; dragTo(e.pos); return true; }
  }
  return false;
}

bool ColorPicker::onMouseMove(const MouseEvent& e) {
  if (m_drag == Drag::None) return false;
  dragTo(e.pos);
  return true;
}

bool ColorPicker::onMouseUp(const MouseEvent&) {
  m_drag = Drag::None;
  return false;
}

// ---------------------------------------------------------------- floating window
namespace {

} // namespace

void showColorPickerWindow(Color initialColor, Callback<Color> onApply, Callback<> onCancel, Callback<Color> onLiveChange) {
  const Theme& t = theme();
  auto root = make<Border>();
  root->setBackground(t.panel);
  root->setBorder(t.borderStrong, Margin(1));
  root->setRadius(6);
  root->setFixedWidth(500.0f);

  std::weak_ptr<Widget> wr = root;
  auto done = std::make_shared<bool>(false);
  auto close = [wr] { if (auto r = wr.lock()) Application::get().closePopup(r.get()); };

  auto col = vbox();

  col->add(makeWindowTitleBar("Color Picker", Icon::Material, false, root.get(), close));

  auto picker = make<ColorPicker>(initialColor);
  ColorPicker* pk = picker.get();
  if (onLiveChange) picker->onColorChanged = onLiveChange;
  auto body = make<Border>();
  body->setFixedHeight(304.0f);
  body->setChild(picker);
  col->add(body);

  auto foot = make<Border>();
  foot->setBackground(t.header);
  foot->setPadding(Margin(10, 6));
  auto frow = hbox(8);
  frow->add(make<Spacer>(), {.fill = 1});
  auto cancelBtn = make<Button>("Cancel", Icon::None, ButtonStyle::Default);
  cancelBtn->setMinSize(70, 24);
  cancelBtn->onClick = [close] { close(); };
  frow->add(cancelBtn);
  auto okBtn = make<Button>("OK", Icon::None, ButtonStyle::Primary);
  okBtn->setMinSize(70, 24);
  okBtn->onClick = [close, pk, onApply, done] {
    Color sel = pk->color();
    *done = true;
    ColorPicker::addRecent(sel);
    close();
    if (onApply) onApply(sel);
  };
  frow->add(okBtn);
  foot->setChild(frow);
  col->add(foot);
  root->setChild(col);

  PopupOptions o;
  o.placement = PopupPlacement::Center;
  o.modal = false;               // floating: the editor behind stays usable
  o.dismissOnOutside = false;
  o.shadow = true;
  o.onClose = [onCancel, onLiveChange, initialColor, done] {
    if (*done) return;
    *done = true;
    if (onLiveChange) onLiveChange(initialColor);
    if (onCancel) onCancel();
  };
  Application::get().showPopup(root, o);
}

} // namespace prism
