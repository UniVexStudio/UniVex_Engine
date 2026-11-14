// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - core value types shared by every module.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace prism {

template <class T> using Ref = std::shared_ptr<T>;
template <class T, class... A> Ref<T> make(A&&... a) { return std::make_shared<T>(std::forward<A>(a)...); }

struct Vec2 {
  float x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
  constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
  bool operator==(const Vec2&) const = default;
  float length() const { return std::sqrt(x * x + y * y); }
};

struct Margin {
  float l = 0, t = 0, r = 0, b = 0;
  constexpr Margin() = default;
  constexpr Margin(float all) : l(all), t(all), r(all), b(all) {}
  constexpr Margin(float h, float v) : l(h), t(v), r(h), b(v) {}
  constexpr Margin(float l_, float t_, float r_, float b_) : l(l_), t(t_), r(r_), b(b_) {}
  constexpr float horizontal() const { return l + r; }
  constexpr float vertical() const { return t + b; }
};

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  constexpr Rect() = default;
  constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
  constexpr float right() const { return x + w; }
  constexpr float bottom() const { return y + h; }
  constexpr Vec2 pos() const { return {x, y}; }
  constexpr Vec2 size() const { return {w, h}; }
  constexpr Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
  constexpr bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
  constexpr Rect deflated(const Margin& m) const {
    return {x + m.l, y + m.t, std::max(0.f, w - m.horizontal()), std::max(0.f, h - m.vertical())};
  }
  constexpr Rect deflated(float a) const { return deflated(Margin(a)); }
  constexpr Rect inflated(float a) const { return {x - a, y - a, w + 2 * a, h + 2 * a}; }
  constexpr Rect translated(Vec2 d) const { return {x + d.x, y + d.y, w, h}; }
  Rect intersection(const Rect& o) const {
    float l = std::max(x, o.x), t = std::max(y, o.y);
    float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
    return {l, t, std::max(0.f, r - l), std::max(0.f, b - t)};
  }
  bool intersects(const Rect& o) const { return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
  bool operator==(const Rect&) const = default;
};

// ---- HiDPI / pixel snapping
inline float snapPixel(float v, float scale = 1.0f) {
  return scale > 0.0f ? std::round(v * scale) / scale : v;
}
inline Vec2 snapPixel(Vec2 p, float scale = 1.0f) {
  return {snapPixel(p.x, scale), snapPixel(p.y, scale)};
}
inline Rect snapPixel(Rect r, float scale = 1.0f) {
  float x0 = snapPixel(r.x, scale);
  float y0 = snapPixel(r.y, scale);
  float x1 = snapPixel(r.right(), scale);
  float y1 = snapPixel(r.bottom(), scale);
  return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  constexpr Color() = default;
  constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
  static constexpr Color hex(uint32_t rgb, uint8_t a = 255) {
    return {uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), a};
  }
  constexpr uint32_t rgb() const { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
  Color alpha(float f) const { return {r, g, b, uint8_t(std::clamp(a * f, 0.f, 255.f))}; }
  Color withAlpha(uint8_t na) const { return {r, g, b, na}; }
  Color mix(Color o, float t) const {
    auto L = [&](uint8_t p, uint8_t q) { return uint8_t(std::lround(p + (q - p) * t)); };
    return {L(r, o.r), L(g, o.g), L(b, o.b), L(a, o.a)};
  }
  Color lighten(float t) const { return mix({255, 255, 255, a}, t); }
  Color darken(float t) const { return mix({0, 0, 0, a}, t); }
  constexpr float luminance() const { return (0.299f * float(r) + 0.587f * float(g) + 0.114f * float(b)) / 255.0f; }
  bool operator==(const Color&) const = default;
};

enum class HAlign { Left, Center, Right, Fill };
enum class VAlign { Top, Center, Bottom, Fill };
enum class Orientation { Horizontal, Vertical };
enum class Cursor { Arrow, IBeam, SizeWE, SizeNS, SizeAll, Hand };
enum class MouseButton : uint8_t { None = 0, Left = 1, Middle = 2, Right = 3 };

enum Mod : uint8_t { ModNone = 0, ModShift = 1, ModCtrl = 2, ModAlt = 4 };

enum class Key {
  Unknown, Backspace, Tab, Enter, Escape, Space, Left, Right, Up, Down, Home, End, PageUp, PageDown, Delete,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  N0, N1, N2, N3, N4, N5, N6, N7, N8, N9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct MouseEvent {
  Vec2 pos;
  MouseButton button = MouseButton::None;
  int clicks = 1;       // 2 = double click
  uint8_t mods = 0;
  Vec2 wheel;           // wheel delta (y>0 = scroll up)
};
struct KeyEvent {
  Key key = Key::Unknown;
  uint8_t mods = 0;
  bool repeat = false;
};

// ---- UTF-8 helpers
inline char32_t decodeUtf8(std::string_view s, size_t& i) {
  unsigned char c = (unsigned char)s[i];
  char32_t cp; int n;
  if (c < 0x80) { cp = c; n = 1; }
  else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
  else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
  else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
  else { i++; return 0xFFFD; }
  for (int k = 1; k < n; k++) {
    if (i + k >= s.size()) { i = s.size(); return 0xFFFD; }
    cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
  }
  i += n;
  return cp;
}
inline size_t utf8Next(std::string_view s, size_t i) { if (i >= s.size()) return s.size(); decodeUtf8(s, i); return i; }
inline size_t utf8Prev(std::string_view s, size_t i) {
  if (i == 0) return 0;
  do { i--; } while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80);
  return i;
}

// ---- lightweight multicast-free signal used for widget callbacks
template <class... A> using Callback = std::function<void(A...)>;

} // namespace prism
