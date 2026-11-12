// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Icons.h"
#include <cmath>
#include <initializer_list>

namespace prism {
namespace {

// Draws in a 16x16 design space.
struct Pen {
  Canvas& c;
  float ox, oy, s;
  Color col;
  float w;

  Vec2 P(float x, float y) const { return {ox + x * s, oy + y * s}; }
  bool round() const { return w >= 1.15f; }
  void cap(Vec2 v) { if (round()) c.fillCircle(v, w * 0.5f, col); }     // round caps / joins: no gaps at corners
  void line(float x0, float y0, float x1, float y1) { Vec2 a = P(x0, y0), b = P(x1, y1); c.line(a, b, col, w); cap(a); cap(b); }
  void poly(std::initializer_list<Vec2> pts, bool closed = false) {
    if (pts.size() < 2) return;
    auto it = pts.begin();
    Vec2 first = P(it->x, it->y);
    Vec2 prev = first;
    ++it;
    for (; it != pts.end(); ++it) {
      Vec2 curr = P(it->x, it->y);
      c.line(prev, curr, col, w);
      cap(prev);
      prev = curr;
    }
    cap(prev);
    if (closed && pts.size() > 2) { c.line(prev, first, col, w); cap(first); }
  }
  void fillPoly(std::initializer_list<Vec2> pts) {
    Vec2 v[16];
    size_t n = std::min<size_t>(pts.size(), 16);
    size_t i = 0;
    for (auto p : pts) {
      if (i >= n) break;
      v[i++] = P(p.x, p.y);
    }
    c.fillPolygon(std::span<const Vec2>(v, n), col);
  }
  void rect(float x, float y, float ww, float hh) { poly({{x, y}, {x + ww, y}, {x + ww, y + hh}, {x, y + hh}}, true); }
  void fillRect(float x, float y, float ww, float hh) { c.fillRect({ox + x * s, oy + y * s, ww * s, hh * s}, col); }
  void circle(float cx, float cy, float r) { c.strokeCircle(P(cx, cy), r * s, col, w); }
  void dot(float cx, float cy, float r) { c.fillCircle(P(cx, cy), r * s, col); }
  void arc(float cx, float cy, float rx, float ry, float a0, float a1, int seg = 28) {
    Vec2 prev = P(cx + rx * std::cos(a0 * 3.14159265f / 180.f), cy + ry * std::sin(a0 * 3.14159265f / 180.f));
    for (int i = 1; i <= seg; i++) {
      float a = (a0 + (a1 - a0) * float(i) / float(seg)) * 3.14159265f / 180.f;
      Vec2 curr = P(cx + rx * std::cos(a), cy + ry * std::sin(a));
      c.line(prev, curr, col, w);
      prev = curr;
      if (i == seg) cap(curr);
    }
    cap(P(cx + rx * std::cos(a0 * 3.14159265f / 180.f), cy + ry * std::sin(a0 * 3.14159265f / 180.f)));
  }
  void ellipse(float cx, float cy, float rx, float ry) { arc(cx, cy, rx, ry, 0, 360, 28); }
  // rounded rectangle outline (corner radius r, design units)
  void rrect(float x, float y, float ww, float hh, float r) {
    line(x + r, y, x + ww - r, y); line(x + ww, y + r, x + ww, y + hh - r);
    line(x + ww - r, y + hh, x + r, y + hh); line(x, y + hh - r, x, y + r);
    arc(x + ww - r, y + r, r, r, -90, 0, 6); arc(x + ww - r, y + hh - r, r, r, 0, 90, 6);
    arc(x + r, y + hh - r, r, r, 90, 180, 6); arc(x + r, y + r, r, r, 180, 270, 6);
  }
  void fillRRect(float x, float y, float ww, float hh, float r) { c.fillRoundRect({ox + x * s, oy + y * s, ww * s, hh * s}, r * s, col); }
  // cubic bezier wire
  void bez(Vec2 a, Vec2 b, Vec2 cc, Vec2 d, int seg = 14) {
    Vec2 prev = P(a.x, a.y);
    for (int i = 1; i <= seg; i++) {
      float t = float(i) / float(seg), u = 1 - t;
      float x = u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * cc.x + t * t * t * d.x;
      float y = u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * cc.y + t * t * t * d.y;
      Vec2 q = P(x, y);
      c.line(prev, q, col, w);
      prev = q;
    }
    cap(P(a.x, a.y)); cap(prev);
  }
};

} // namespace

void drawIcon(Canvas& c, Icon icon, Rect box, Color color, float stroke) {
  if (icon == Icon::None || color.a == 0) return;
  float s = std::min(box.w, box.h) / 16.f;
  Pen p{c, box.x + (box.w - 16 * s) * 0.5f, box.y + (box.h - 16 * s) * 0.5f, s, color, stroke};
  // One weight for the whole set: ~1.35 design units, never thinner than 1.1px, never heavier than the caller asked
  // (blueprint / graph pictograms used to be forced bold; they now share the line weight of every other icon).
  p.w = std::clamp(stroke, 1.1f, std::max(1.1f, 1.6f * s));
  switch (icon) {
  case Icon::None: break;
  case Icon::TriRight: p.fillPoly({{5.5f, 3.5f}, {11.5f, 8}, {5.5f, 12.5f}}); break;
  case Icon::TriDown: p.fillPoly({{3.5f, 5.5f}, {12.5f, 5.5f}, {8, 11.5f}}); break;
  case Icon::ChevronRight: p.poly({{6, 3.5f}, {10.5f, 8}, {6, 12.5f}}); break;
  case Icon::ChevronLeft: p.poly({{10, 3.5f}, {5.5f, 8}, {10, 12.5f}}); break;
  case Icon::ChevronDown: p.poly({{3.5f, 6}, {8, 10.5f}, {12.5f, 6}}); break;
  case Icon::ChevronUp: p.poly({{3.5f, 10}, {8, 5.5f}, {12.5f, 10}}); break;
  case Icon::Close: p.line(4, 4, 12, 12); p.line(12, 4, 4, 12); break;
  case Icon::Plus: p.line(8, 3, 8, 13); p.line(3, 8, 13, 8); break;
  case Icon::Minus: p.line(3, 8, 13, 8); break;
  case Icon::Check: p.poly({{3, 8.5f}, {6.5f, 12}, {13, 4.5f}}); break;
  case Icon::Search: p.circle(7, 7, 4.3f); p.line(10.3f, 10.3f, 13.5f, 13.5f); break;
  case Icon::Eye:
  case Icon::EyeOff: {
    std::vector<Vec2> top, bot;
    for (int i = 0; i <= 12; i++) {
      float x = -6.5f + 13.f * i / 12.f, t = x / 6.5f, y = 4.2f * (1 - t * t);
      top.push_back(p.P(8 + x, 8 - y));
      bot.push_back(p.P(8 + x, 8 + y));
    }
    for (size_t i = 0; i + 1 < top.size(); i++) { c.line(top[i], top[i + 1], color, stroke); c.line(bot[i], bot[i + 1], color, stroke); }
    p.circle(8, 8, 2.1f);
    if (icon == Icon::EyeOff) p.line(3, 13, 13, 3);
    break;
  }
  case Icon::Lock:
    p.rect(3.5f, 7.5f, 9, 6); p.arc(8, 7.5f, 2.7f, 3.2f, 180, 360, 10); p.dot(8, 10.5f, 0.9f); break;
  case Icon::Unlock:
    p.rect(3.5f, 7.5f, 9, 6); p.arc(10.7f, 7.5f, 2.7f, 3.2f, 180, 360, 10); p.line(8, 7.5f, 8, 5.4f);
    p.dot(8, 10.5f, 0.9f); break;
  case Icon::Cube:
  case Icon::Wireframe:
    p.poly({{8, 1.8f}, {13.5f, 5}, {13.5f, 11}, {8, 14.2f}, {2.5f, 11}, {2.5f, 5}}, true);
    p.poly({{2.5f, 5}, {8, 8.2f}, {13.5f, 5}}); p.line(8, 8.2f, 8, 14.2f); break;
  case Icon::Shaded:
    p.poly({{8, 1.8f}, {13.5f, 5}, {13.5f, 11}, {8, 14.2f}, {2.5f, 11}, {2.5f, 5}}, true);
    p.fillPoly({{2.5f, 5}, {8, 8.2f}, {8, 14.2f}, {2.5f, 11}}); break;
  case Icon::Sphere:
    p.circle(8, 8, 6); p.ellipse(8, 8, 6, 2.3f); p.ellipse(8, 8, 2.6f, 6); break;
  case Icon::Cylinder:
    p.ellipse(8, 4.2f, 5, 2); p.line(3, 4.2f, 3, 11.8f); p.line(13, 4.2f, 13, 11.8f); p.arc(8, 11.8f, 5, 2, 0, 180, 12); break;
  case Icon::Cone:
    p.line(8, 2, 3, 11.8f); p.line(8, 2, 13, 11.8f); p.ellipse(8, 11.8f, 5, 2); break;
  case Icon::Plane:
    p.poly({{4.5f, 4.5f}, {14, 4.5f}, {11.5f, 11.5f}, {2, 11.5f}}, true); break;
  case Icon::Light:
    p.circle(8, 6.4f, 3.8f); p.line(6.2f, 11.2f, 9.8f, 11.2f); p.line(6.6f, 13.2f, 9.4f, 13.2f); break;
  case Icon::Camera:
    p.rect(1.8f, 4.8f, 9, 7); p.poly({{10.8f, 7.6f}, {14.4f, 5.4f}, {14.4f, 11.2f}, {10.8f, 9}}, true); break;
  case Icon::Folder:
    p.poly({{2, 4.5f}, {6.2f, 4.5f}, {7.6f, 6}, {14, 6}, {14, 12.5f}, {2, 12.5f}}, true); break;
  case Icon::FolderOpen:
    p.poly({{2, 12.5f}, {2, 4.5f}, {6.2f, 4.5f}, {7.6f, 6}, {12.5f, 6}, {12.5f, 7.5f}});
    p.poly({{2, 12.5f}, {4, 7.5f}, {14.5f, 7.5f}, {12.5f, 12.5f}}, true); break;
  case Icon::File:
    p.poly({{3.8f, 2}, {9.6f, 2}, {12.4f, 4.8f}, {12.4f, 14}, {3.8f, 14}}, true);
    p.poly({{9.6f, 2}, {9.6f, 4.8f}, {12.4f, 4.8f}}); break;
  case Icon::Script:
    p.poly({{6, 4.5f}, {2.5f, 8}, {6, 11.5f}}); p.poly({{10, 4.5f}, {13.5f, 8}, {10, 11.5f}}); break;
  case Icon::Material:
    p.circle(8, 8, 5.8f); p.fillPoly({{8, 2.2f}, {8, 13.8f}, {10.5f, 13.4f}, {12.8f, 11.6f}, {13.8f, 8}, {12.8f, 4.4f}, {10.5f, 2.6f}}); break;
  case Icon::Blueprint:        // two node cards (title line + pin) joined by an S-wire
    p.rrect(1.4f, 1.8f, 6.2f, 5.4f, 1.3f); p.line(1.4f, 3.9f, 7.6f, 3.9f);
    p.rrect(8.4f, 8.8f, 6.2f, 5.4f, 1.3f); p.line(8.4f, 10.9f, 14.6f, 10.9f);
    p.bez({7.6f, 5.6f}, {11.4f, 5.6f}, {4.6f, 12.6f}, {8.4f, 12.6f}); break;
  case Icon::EventGraph:       // lightning bolt outline with a solid core
    p.poly({{9.6f, 1.6f}, {3.6f, 9.0f}, {7.6f, 9.0f}, {6.4f, 14.4f}, {12.4f, 7.0f}, {8.4f, 7.0f}}, true);
    { Color k = p.col; p.col = k.withAlpha(uint8_t(k.a * 0.45f)); p.fillPoly({{9.0f, 3.4f}, {5.0f, 8.3f}, {8.1f, 8.3f}}); p.fillPoly({{7.0f, 12.6f}, {11.0f, 7.7f}, {7.9f, 7.7f}}); p.col = k; }
    break;
  case Icon::WidgetGraph:      // rounded window, title strip, a button and a toggle
    p.rrect(1.8f, 2.6f, 12.4f, 10.8f, 1.6f); p.fillRRect(1.8f, 2.6f, 12.4f, 2.4f, 1.4f);
    p.rrect(4, 7.6f, 4.6f, 3.4f, 0.9f); p.line(10.4f, 9.3f, 12.2f, 9.3f); break;
  case Icon::AnimGraph: {      // runner in mid-stride (slightly heavier line so the pose reads at 16px)
    p.w = std::min(p.w * 1.3f, std::max(1.4f, 2.0f * s));
    p.dot(10.6f, 2.8f, 1.7f);
    p.poly({{9.6f, 5.4f}, {7.4f, 9.0f}});                         // torso
    p.poly({{9.2f, 6.2f}, {12.0f, 8.0f}, {13.4f, 6.8f}});          // front arm
    p.poly({{9.0f, 6.0f}, {6.0f, 6.0f}, {4.6f, 7.8f}});            // back arm
    p.poly({{7.4f, 9.0f}, {10.4f, 10.6f}, {9.8f, 13.8f}});         // front leg
    p.poly({{7.4f, 9.0f}, {5.4f, 11.6f}, {2.8f, 11.0f}});          // back leg
    break;
  }
  case Icon::StateGraph:       // two state pills and an elbow transition arrow
    p.rrect(1.6f, 1.8f, 8.4f, 4.2f, 2.1f); p.rrect(6.0f, 10.0f, 8.4f, 4.2f, 2.1f);
    p.poly({{3.8f, 6.0f}, {3.8f, 12.1f}, {5.0f, 12.1f}});
    p.poly({{3.8f, 10.6f}, {5.4f, 12.1f}, {3.8f, 13.6f}}); break;
  case Icon::BehaviorGraph:    // root card branching to two child cards
    p.rrect(5.2f, 1.6f, 5.6f, 3.8f, 1.0f); p.fillRRect(5.2f, 1.6f, 5.6f, 3.8f, 1.0f);
    p.rrect(1.6f, 10.6f, 5.2f, 3.8f, 1.0f); p.rrect(9.2f, 10.6f, 5.2f, 3.8f, 1.0f);
    p.line(8, 5.4f, 8, 8); p.poly({{4.2f, 10.6f}, {4.2f, 8}, {11.8f, 8}, {11.8f, 10.6f}}); break;
  case Icon::VfxGraph:         // four-point sparkle outline + two satellites
    p.poly({{6.6f, 2.0f}, {7.9f, 6.0f}, {11.6f, 7.4f}, {7.9f, 8.8f}, {6.6f, 12.8f}, {5.3f, 8.8f}, {1.6f, 7.4f}, {5.3f, 6.0f}}, true);
    p.dot(12.4f, 3.2f, 1.1f); p.dot(12.0f, 12.4f, 1.5f); break;
  case Icon::AudioGraph:       // waveform in a rounded frame
    p.rrect(1.6f, 2.6f, 12.8f, 10.8f, 2.0f);
    for (int i = 0; i < 5; i++) { float h = i == 2 ? 3.4f : (i == 1 || i == 3) ? 2.2f : 1.0f; p.line(4.4f + float(i) * 1.8f, 8 - h, 4.4f + float(i) * 1.8f, 8 + h); }
    break;
  case Icon::Compile:          // cog with a check mark (build + validate)
    for (int i = 0; i < 8; i++) {
      float a = float(i) * 0.7853982f, ca = std::cos(a), sa = std::sin(a);
      p.line(8 + 5.1f * ca, 8 + 5.1f * sa, 8 + 6.6f * ca, 8 + 6.6f * sa);
    }
    p.circle(8, 8, 4.6f); p.poly({{5.8f, 8.1f}, {7.4f, 9.7f}, {10.4f, 6.4f}}); break;
  case Icon::Browse:           // folder + magnifier
    p.poly({{1.8f, 12.4f}, {1.8f, 3.4f}, {5.6f, 3.4f}, {7.0f, 4.9f}, {12.6f, 4.9f}, {12.6f, 6.2f}});
    p.line(1.8f, 12.4f, 6.2f, 12.4f);
    p.circle(9.8f, 10.0f, 2.8f); p.line(11.8f, 12.0f, 14.2f, 14.4f); break;
  case Icon::Properties:       // three sliders with ring knobs
    p.line(2.2f, 4.2f, 8.2f, 4.2f); p.line(11.8f, 4.2f, 13.8f, 4.2f); p.circle(10, 4.2f, 1.7f);
    p.line(2.2f, 8.0f, 3.8f, 8.0f); p.line(7.4f, 8.0f, 13.8f, 8.0f); p.circle(5.6f, 8.0f, 1.7f);
    p.line(2.2f, 11.8f, 7.2f, 11.8f); p.line(10.8f, 11.8f, 13.8f, 11.8f); p.circle(9, 11.8f, 1.7f); break;
  // ---- per-node glyphs
  case Icon::NodeFunction:
    p.line(6.2f, 13.6f, 8.6f, 5.4f); p.arc(11.2f, 5.4f, 2.7f, 2.6f, 180, 268); p.line(5.4f, 8.6f, 11.4f, 8.6f); break;
  case Icon::NodeMath:
    p.line(3, 8, 13, 8); p.dot(8, 3.8f, 1.3f); p.dot(8, 12.2f, 1.3f); break;
  case Icon::NodeCompare:
    p.poly({{4.5f, 3.5f}, {11, 7.2f}, {4.5f, 10.9f}}); p.line(4.5f, 13.6f, 11.5f, 13.6f); break;
  case Icon::NodeLogic:
    p.poly({{7, 3}, {3, 3}, {3, 13}, {7, 13}}); p.arc(7, 8, 5.2f, 5, -90, 90); break;
  case Icon::NodeVector:
    p.line(4, 12.4f, 4, 3); p.line(4, 12.4f, 13, 12.4f); p.line(4, 12.4f, 11, 5.4f);
    p.fillPoly({{4, 2}, {6, 4.6f}, {2, 4.6f}}); p.fillPoly({{14.2f, 12.4f}, {11.6f, 14.4f}, {11.6f, 10.4f}}); break;
  case Icon::NodeRandom:
    p.rect(2.6f, 2.6f, 10.8f, 10.8f); p.dot(5.6f, 5.6f, 1.1f); p.dot(10.4f, 5.6f, 1.1f); p.dot(8, 8, 1.1f); p.dot(5.6f, 10.4f, 1.1f); p.dot(10.4f, 10.4f, 1.1f); break;
  case Icon::NodeLerp:
    p.dot(3.6f, 12, 1.9f); p.dot(12.4f, 4, 1.9f); p.line(4.6f, 11, 11.4f, 5); p.dot(8, 8, 1.1f); break;
  case Icon::NodeClamp:
    p.poly({{5.4f, 3}, {3, 3}, {3, 13}, {5.4f, 13}}); p.poly({{10.6f, 3}, {13, 3}, {13, 13}, {10.6f, 13}}); p.dot(8, 8, 1.7f); break;
  case Icon::NodeBranch:
    p.line(8, 13.4f, 8, 8); p.line(8, 8, 4, 4); p.line(8, 8, 12, 4); p.dot(4, 4, 1.6f); p.dot(12, 4, 1.6f); p.dot(8, 13.4f, 1.6f); break;
  case Icon::NodeSequence:
    p.poly({{2.6f, 12.4f}, {6, 12.4f}, {6, 8}, {10, 8}, {10, 3.6f}, {13.4f, 3.6f}}); break;
  case Icon::NodeLoop:
    p.arc(8, 8, 5, 5, -50, 230); p.fillPoly({{3.1f, 6.2f}, {1.8f, 10.4f}, {6.1f, 9.4f}}); break;
  case Icon::NodeClock:
    p.circle(8, 8, 5.6f); p.line(8, 8, 8, 4.6f); p.line(8, 8, 10.8f, 9.6f); break;
  case Icon::NodeTimer:
    p.circle(8, 9, 4.8f); p.line(8, 9, 10.2f, 6.8f); p.line(6.4f, 2.6f, 9.6f, 2.6f); p.line(8, 2.6f, 8, 4.2f); break;
  case Icon::NodeHourglass:
    p.poly({{4, 2.6f}, {12, 2.6f}, {4, 13.4f}, {12, 13.4f}}, true); break;
  case Icon::NodeText:
    p.line(3, 4, 13, 4); p.line(3, 8, 13, 8); p.line(3, 12, 9, 12); break;
  case Icon::NodeGate:
    p.rect(3.6f, 2.6f, 8.8f, 10.8f); p.dot(10, 8, 1.1f); break;
  case Icon::NodeCast:
    p.line(2.4f, 8, 8.2f, 8); p.fillPoly({{7.6f, 4.8f}, {11, 8}, {7.6f, 11.2f}}); p.poly({{12.2f, 3}, {14, 3}, {14, 13}, {12.2f, 13}}); break;
  case Icon::NodeDamage:
    p.circle(8, 8, 4.4f); p.line(8, 1.8f, 8, 4.4f); p.line(8, 11.6f, 8, 14.2f); p.line(1.8f, 8, 4.4f, 8); p.line(11.6f, 8, 14.2f, 8); p.dot(8, 8, 1.1f); break;
  case Icon::NodeLaunch:
    p.line(8, 13, 8, 3.8f); p.poly({{4.4f, 7.4f}, {8, 3.8f}, {11.6f, 7.4f}}); p.line(4, 13.6f, 12, 13.6f); break;
  case Icon::NodeSound:
    p.fillPoly({{2.4f, 6}, {5.6f, 6}, {9.4f, 3}, {9.4f, 13}, {5.6f, 10}, {2.4f, 10}}); p.arc(9.4f, 8, 3, 3.2f, -50, 50); p.arc(9.4f, 8, 5.6f, 5.8f, -50, 50); break;
  case Icon::NodeInput:
    p.rect(2.6f, 3.6f, 10.8f, 8.8f); p.poly({{5.8f, 9.4f}, {8, 6.2f}, {10.2f, 9.4f}}); break;
  case Icon::NodeSpawn:
    p.rect(3, 3, 10, 10); p.line(8, 5.6f, 8, 10.4f); p.line(5.6f, 8, 10.4f, 8); break;
  case Icon::NodeOverlap:
    p.circle(6, 8, 3.9f); p.circle(10, 8, 3.9f); break;
  case Icon::NodeImpact:
    for (int i = 0; i < 8; i++) { float a = float(i) * 0.7853982f, ca = std::cos(a), sa = std::sin(a); p.line(8 + ca * 2.6f, 8 + sa * 2.6f, 8 + ca * 6, 8 + sa * 6); }
    break;
  case Icon::NodeFlag:
    p.line(4, 2.6f, 4, 13.6f); p.fillPoly({{4.6f, 3}, {12.4f, 5.6f}, {4.6f, 8.2f}}); break;
  case Icon::NodeTick:
    p.poly({{1.8f, 8}, {5, 8}, {6.6f, 3.4f}, {9.2f, 12.6f}, {10.8f, 8}, {14.2f, 8}}); break;
  case Icon::NodeStop:
    p.fillRect(4, 4, 8, 8); break;
  case Icon::NodeToggle:
    p.rect(2.4f, 5.2f, 11.2f, 5.6f); p.dot(10.6f, 8, 1.9f); break;
  case Icon::NodeClip:
    p.rect(2.4f, 3.4f, 11.2f, 9.2f); p.line(5.4f, 3.4f, 5.4f, 12.6f); p.line(10.6f, 3.4f, 10.6f, 12.6f); p.fillPoly({{7, 6.2f}, {9.2f, 8}, {7, 9.8f}}); break;
  case Icon::NodeBlendSpace:
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) p.dot(3.8f + float(i) * 4.2f, 3.8f + float(j) * 4.2f, 1.2f);
    p.poly({{3.8f, 12.2f}, {8, 8}, {12.2f, 3.8f}}); break;
  case Icon::NodeBlend:
    p.poly({{2.4f, 4}, {6.6f, 4}, {9.6f, 8}}); p.poly({{2.4f, 12}, {6.6f, 12}, {9.6f, 8}}); p.line(9.6f, 8, 12, 8); p.fillPoly({{11.6f, 5.2f}, {14.2f, 8}, {11.6f, 10.8f}}); break;
  case Icon::NodeBone:
    p.line(4.8f, 11.2f, 11.2f, 4.8f); p.circle(3.6f, 12.4f, 2.2f); p.circle(12.4f, 3.6f, 2.2f); break;
  case Icon::NodeSlot:
    p.rect(2.4f, 4.6f, 11.2f, 6.8f); p.dot(8, 8, 1.7f); break;
  case Icon::NodeOutput:
    p.line(2.4f, 8, 8.4f, 8); p.fillPoly({{7.8f, 4.8f}, {11, 8}, {7.8f, 11.2f}}); p.line(13, 2.8f, 13, 13.2f); break;
  case Icon::NodeEntry:
    p.circle(8, 8, 5.8f); p.fillPoly({{6.4f, 5.2f}, {10.6f, 8}, {6.4f, 10.8f}}); break;
  case Icon::NodeTransition:
    p.line(2.4f, 8, 11, 8); p.fillPoly({{10.2f, 4.6f}, {14, 8}, {10.2f, 11.4f}}); break;
  case Icon::NodeSelector:
    p.line(2.4f, 8, 7, 8); p.poly({{7, 8}, {10.4f, 3.6f}, {13.6f, 3.6f}}); p.line(7, 8, 13.6f, 8); p.poly({{7, 8}, {10.4f, 12.4f}, {13.6f, 12.4f}}); p.dot(2.8f, 8, 1.4f); break;
  case Icon::NodeChain:
    p.line(3.6f, 8, 12.4f, 8); p.dot(3.6f, 8, 1.9f); p.dot(8, 8, 1.9f); p.dot(12.4f, 8, 1.9f); break;
  case Icon::NodeDecorator:
    p.poly({{8, 2.4f}, {13.6f, 8}, {8, 13.6f}, {2.4f, 8}}, true); p.dot(8, 8, 1.4f); break;
  case Icon::NodeMoveTo:
    p.circle(8, 6.2f, 3.2f); p.poly({{5.4f, 8.2f}, {8, 13.6f}, {10.6f, 8.2f}}); p.dot(8, 6.2f, 1); break;
  case Icon::NodeAttack:
    p.line(4, 12, 12.4f, 3.6f); p.line(4.8f, 7.2f, 8.8f, 11.2f); p.dot(3.4f, 12.6f, 1.3f); break;
  case Icon::NodeRoute:
    p.dot(3.4f, 12, 1.6f); p.dot(12.6f, 4, 1.6f); p.poly({{3.4f, 12}, {7, 12}, {9, 6}, {12.6f, 4}}); break;
  case Icon::NodeEmitter:
    p.dot(4.4f, 11.6f, 2.2f); p.dot(8.6f, 8.4f, 1.3f); p.dot(11, 5.6f, 1.3f); p.dot(13, 3.2f, 1.3f); p.dot(11.2f, 10.6f, 1.1f); p.dot(7.4f, 4.4f, 1.1f); break;
  case Icon::NodeGravity:
    p.line(8, 2.8f, 8, 10.6f); p.fillPoly({{4.4f, 9}, {11.6f, 9}, {8, 12.6f}}); p.line(3.4f, 14, 12.6f, 14); break;
  case Icon::NodeNoise:
    p.poly({{1.8f, 8}, {3.6f, 4.4f}, {6, 8}, {8, 11.6f}, {10.2f, 8}, {12.2f, 4.4f}, {14.2f, 8}}); break;
  case Icon::NodeGradient:
    p.rect(2.4f, 3.6f, 11.2f, 8.8f); p.line(5.2f, 3.6f, 5.2f, 12.4f); p.line(8, 3.6f, 8, 12.4f); p.line(10.8f, 3.6f, 10.8f, 12.4f); break;
  case Icon::NodeScale:
    p.rect(2.4f, 8.6f, 4.8f, 4.8f); p.rect(6.4f, 2.6f, 7.2f, 7.2f); break;
  case Icon::NodeSprite:
    p.rect(2.4f, 3, 11.2f, 10); p.poly({{3.6f, 11.4f}, {7, 7.6f}, {9.4f, 10}, {11, 8.6f}, {12.4f, 11.4f}}); p.dot(10.8f, 5.8f, 1); break;
  case Icon::NodeWave:
    for (int i = 0; i < 5; i++) { float h = (i == 0 || i == 4) ? 1.6f : (i == 2 ? 5.6f : 3.4f); p.line(3 + float(i) * 2.5f, 8 - h, 3 + float(i) * 2.5f, 8 + h); }
    break;
  case Icon::NodeEnvelope:
    p.poly({{2, 12.6f}, {5, 3.4f}, {7.6f, 7.4f}, {11, 7.4f}, {14, 12.6f}}); break;
  case Icon::NodeFilter:
    p.poly({{2, 4.6f}, {8, 4.6f}, {10.6f, 5.8f}, {12, 9}, {14, 13}}); break;
  case Icon::NodeEcho:
    p.dot(3.6f, 8, 1.7f); p.arc(3.6f, 8, 4.2f, 4.2f, -48, 48); p.arc(3.6f, 8, 7.4f, 7.4f, -48, 48); p.arc(3.6f, 8, 10.6f, 10.6f, -36, 36); break;
  case Icon::NodeMixer:
    p.line(4, 2.8f, 4, 13.2f); p.dot(4, 9.4f, 1.8f); p.line(8, 2.8f, 8, 13.2f); p.dot(8, 5.6f, 1.8f); p.line(12, 2.8f, 12, 13.2f); p.dot(12, 10.4f, 1.8f); break;
  case Icon::NodeSlider:
    p.line(2.4f, 8, 13.6f, 8); p.dot(9, 8, 2.3f); break;
  case Icon::NodePalette:
    p.circle(8, 8, 5.7f); p.dot(5.6f, 6.6f, 1); p.dot(8.4f, 5, 1); p.dot(10.8f, 7.4f, 1); p.dot(6.4f, 10.2f, 1); break;
  case Icon::NodeTexture:
    p.rect(3, 3, 10, 10); p.fillRect(3, 3, 5, 5); p.fillRect(8, 8, 5, 5); break;
  case Icon::NodeFresnel:
    p.circle(8, 8, 5.6f); p.arc(8, 8, 3.2f, 3.2f, -70, 110); break;
  case Icon::Play: p.fillPoly({{4.5f, 2.8f}, {13, 8}, {4.5f, 13.2f}}); break;
  case Icon::SkipStart: p.fillRect(3, 3, 2, 10); p.fillPoly({{13, 3}, {13, 13}, {5.5f, 8}}); break;
  case Icon::SkipEnd: p.fillRect(11, 3, 2, 10); p.fillPoly({{3, 3}, {3, 13}, {10.5f, 8}}); break;
  case Icon::StepBack: p.fillRect(3.5f, 4, 1.8f, 8); p.fillPoly({{12, 4}, {12, 12}, {6, 8}}); break;
  case Icon::StepForward: p.fillRect(10.7f, 4, 1.8f, 8); p.fillPoly({{4, 4}, {4, 12}, {10, 8}}); break;
  case Icon::Key: p.fillPoly({{8, 2.5f}, {13.5f, 8}, {8, 13.5f}, {2.5f, 8}}); break;
  case Icon::AnimClip: { p.rrect(2, 3.5f, 12, 9, 1.5f); for (float x : {3.6f, 6.8f, 10.0f, 12.4f}) { p.fillRect(x, 4.6f, 1.1f, 1.1f); p.fillRect(x, 10.3f, 1.1f, 1.1f); } p.fillPoly({{6.6f, 6.4f}, {9.8f, 8}, {6.6f, 9.6f}}); } break;
  case Icon::AnimMontage: { p.rrect(1.5f, 5.5f, 9, 8, 1.2f); p.poly({{4, 5.5f}, {4, 2.5f}, {14.5f, 2.5f}, {14.5f, 10.5f}, {10.5f, 10.5f}}); p.fillPoly({{4.6f, 7.6f}, {7.8f, 9.5f}, {4.6f, 11.4f}}); } break;
  case Icon::AnimLibrary: { p.rect(2, 3, 3, 10.5f); p.rect(6.2f, 3, 3, 10.5f); p.poly({{10.5f, 3.6f}, {13.2f, 3}, {15, 12.8f}, {12.3f, 13.4f}}, true); p.dot(3.5f, 5, 0.7f); p.dot(7.7f, 5, 0.7f); } break;
  case Icon::MontageLibrary: { p.line(1.5f, 13.5f, 14.5f, 13.5f); p.rrect(2, 3, 5, 8, 1); p.rrect(9, 3, 5, 8, 1); p.fillPoly({{3.6f, 5.4f}, {5.6f, 7}, {3.6f, 8.6f}}); p.fillPoly({{10.6f, 5.4f}, {12.6f, 7}, {10.6f, 8.6f}}); } break;
  case Icon::AnimSequencer: { p.rrect(2, 2.5f, 12, 8, 1.5f); p.fillPoly({{6.5f, 4.5f}, {10, 6.5f}, {6.5f, 8.5f}}); p.line(2, 13.2f, 14, 13.2f); p.dot(6, 13.2f, 1.5f); } break;
  case Icon::MontagePlayer: { p.circle(8, 8, 6); p.fillPoly({{6.6f, 5.2f}, {11, 8}, {6.6f, 10.8f}}); p.line(3.8f, 3.8f, 2.2f, 2.2f); } break;
  case Icon::AnimTree: { p.dot(8, 2.8f, 1.4f); p.dot(3.2f, 12.8f, 1.4f); p.dot(8, 12.8f, 1.4f); p.dot(12.8f, 12.8f, 1.4f); p.line(8, 4, 8, 11.5f); p.line(8, 7.5f, 3.4f, 11.6f); p.line(8, 7.5f, 12.6f, 11.6f); } break;
  case Icon::AnimClipNode: { p.rrect(2, 4, 11, 8, 2); p.fillPoly({{6, 6}, {9.5f, 8}, {6, 10}}); p.dot(14.2f, 8, 1.3f); } break;
  case Icon::LayeredBlend: { p.poly({{2, 10.5f}, {8, 13.5f}, {14, 10.5f}}); p.poly({{2, 7}, {8, 10}, {14, 7}, {8, 4}}, true); p.line(8, 1.5f, 8, 4); } break;
  case Icon::TwoBoneIK: { p.line(2.5f, 13.5f, 6, 6.5f); p.line(6, 6.5f, 11, 9.5f); p.dot(2.5f, 13.5f, 1.4f); p.dot(6, 6.5f, 1.4f); p.circle(12.5f, 10.5f, 2.3f); p.line(12.5f, 2, 12.5f, 6.5f); } break;
  case Icon::LookAt: { p.circle(4.8f, 8, 3); p.line(8.5f, 8, 14, 8); p.poly({{11.5f, 5.5f}, {14, 8}, {11.5f, 10.5f}}); p.dot(4.8f, 8, 0.9f); } break;
  case Icon::ModifyBone: { p.line(3, 12, 10, 5); p.dot(2.6f, 12.4f, 1.5f); p.dot(10.4f, 4.6f, 1.5f); p.line(12.5f, 10, 12.5f, 15); p.line(10, 12.5f, 15, 12.5f); } break;
  case Icon::OneShot: { p.circle(8, 8, 6.2f); p.line(8.4f, 4.6f, 8.4f, 11.6f); p.line(6.4f, 6.4f, 8.4f, 4.6f); p.line(6.6f, 11.6f, 10.2f, 11.6f); } break;   // "1", once
  case Icon::TimeScale: { p.circle(7, 8.5f, 5); p.line(7, 8.5f, 7, 5.2f); p.line(7, 8.5f, 9.6f, 9.8f); p.poly({{12.6f, 1.8f}, {14.8f, 4}, {12.6f, 6.2f}}); p.line(10.5f, 4, 14.5f, 4); } break;
  case Icon::TimeSeek: { p.line(2, 12.5f, 14, 12.5f); p.line(11, 2.5f, 11, 14); p.line(2.5f, 6, 8.5f, 6); p.poly({{6.5f, 3.8f}, {8.8f, 6}, {6.5f, 8.2f}}); } break;
  case Icon::AnimSelect: { p.dot(2.8f, 8, 1.6f); p.line(3.5f, 8, 12, 4); p.circle(13.2f, 3.6f, 1.3f); p.circle(13.2f, 12.4f, 1.3f); p.line(10, 12.4f, 11.8f, 12.4f); } break;
  case Icon::BlendByBool: { p.line(2.5f, 3.5f, 8, 8); p.line(2.5f, 12.5f, 8, 8); p.line(8, 8, 14, 8); p.dot(2.5f, 3.5f, 1.5f); p.circle(2.5f, 12.5f, 1.4f); p.poly({{11, 5.5f}, {14, 8}, {11, 10.5f}}); } break;
  case Icon::StateMachine: { p.rrect(1.5f, 2, 6, 5, 1.5f); p.rrect(8.5f, 9, 6, 5, 1.5f); p.poly({{7.8f, 4.5f}, {11.5f, 4.5f}, {11.5f, 8.2f}}); p.poly({{8.2f, 11.5f}, {4.5f, 11.5f}, {4.5f, 7.8f}}); } break;
  case Icon::AnimEvent: { p.line(5, 2, 5, 14.5f); p.fillPoly({{5, 2.5f}, {12.5f, 5}, {5, 7.5f}}); p.line(2, 14.5f, 14, 14.5f); } break;
  case Icon::DopeSheet: { for (float y : {4.0f, 8.0f, 12.0f}) p.line(2, y, 14, y); p.fillPoly({{5, 2.4f}, {6.6f, 4}, {5, 5.6f}, {3.4f, 4}}); p.fillPoly({{10, 6.4f}, {11.6f, 8}, {10, 9.6f}, {8.4f, 8}}); p.fillPoly({{7, 10.4f}, {8.6f, 12}, {7, 13.6f}, {5.4f, 12}}); } break;
  case Icon::BoneTrack: { p.line(4.5f, 6, 11.5f, 6); p.dot(3.6f, 5, 1.2f); p.dot(3.6f, 7, 1.2f); p.dot(12.4f, 5, 1.2f); p.dot(12.4f, 7, 1.2f); p.line(2, 12, 14, 12); p.dot(8, 12, 1.2f); } break;
  case Icon::ParamFloat: { p.arc(8, 11, 6, 6, 180, 360); p.line(8, 11, 11.2f, 6.4f); p.dot(8, 11, 1.2f); p.line(2, 13.5f, 14, 13.5f); } break;
  case Icon::ParamBool: { p.rrect(1.5f, 5, 13, 6, 3); p.dot(11, 8, 2.1f); } break;
  case Icon::ParamTrigger: { p.poly({{9.5f, 1.5f}, {3.5f, 9}, {7.8f, 9}, {6.5f, 14.5f}, {12.5f, 6.5f}, {8.3f, 6.5f}}, true); } break;
  case Icon::ParamVector: { p.line(4, 12, 14, 12); p.line(4, 12, 4, 2); p.line(4, 12, 10.5f, 5.5f); p.poly({{12, 10}, {14, 12}, {12, 14}}); p.poly({{2, 4}, {4, 2}, {6, 4}}); p.poly({{8, 5.5f}, {10.5f, 5.5f}, {10.5f, 8}}); } break;
  case Icon::Fire: { p.circle(8, 8, 2.2f); for (int i = 0; i < 8; ++i) { const float a = 0.7854f * float(i); p.line(8 + 4.2f * std::cos(a), 8 + 4.2f * std::sin(a), 8 + 6.4f * std::cos(a), 8 + 6.4f * std::sin(a)); } } break;
  case Icon::PreviewPose: { p.rect(1.5f, 1.5f, 13, 13); p.circle(8, 4.8f, 1.5f); p.line(8, 6.4f, 8, 10); p.line(5.2f, 7.8f, 10.8f, 7.8f); p.line(8, 10, 6, 13); p.line(8, 10, 10, 13); } break;
  case Icon::Components: { p.rect(2, 1.5f, 6, 4); p.rect(7, 7, 7, 3); p.rect(7, 11.5f, 7, 3); p.line(4, 5.5f, 4, 13); p.line(4, 8.5f, 7, 8.5f); p.line(4, 13, 7, 13); } break;
  case Icon::OutputLog: { p.rrect(3, 1.5f, 10, 13, 1.5f); p.line(5.5f, 5, 10.5f, 5); p.line(5.5f, 8, 10.5f, 8); p.line(5.5f, 11, 8.5f, 11); } break;
  case Icon::CompileResults: { p.line(7, 4, 14, 4); p.line(7, 8, 14, 8); p.line(7, 12, 14, 12); p.poly({{1.8f, 4}, {3, 5.3f}, {5, 2.7f}}); p.line(2, 6.8f, 5, 9.8f); p.line(5, 6.8f, 2, 9.8f); p.dot(3.5f, 12.4f, 1); } break;
  case Icon::FindResults: { p.line(2, 3, 10, 3); p.line(2, 6, 6.5f, 6); p.line(2, 9, 5, 9); p.circle(10, 10, 3); p.line(12.2f, 12.2f, 14.5f, 14.5f); } break;
  case Icon::DiffView: { p.rect(1.5f, 2, 5.5f, 12); p.rect(9, 2, 5.5f, 12); p.line(2.8f, 8, 5.7f, 8); p.line(10.3f, 8, 13.2f, 8); p.line(11.75f, 6.5f, 11.75f, 9.5f); } break;
  case Icon::Profiler: { p.fillRect(2, 9, 2.5f, 5); p.fillRect(6, 4, 2.5f, 10); p.fillRect(10, 7, 2.5f, 7); p.line(1.5f, 14.8f, 14.5f, 14.8f); } break;
  case Icon::LayoutFull: { p.rect(1.5f, 1.5f, 13, 13); p.fillRect(3.2f, 3.2f, 9.6f, 9.6f); } break;
  case Icon::LayoutViewport: { p.rect(1.5f, 1.5f, 13, 13); p.circle(8, 6.5f, 2.2f); p.fillRect(3.2f, 10.8f, 9.6f, 2); } break;
  case Icon::MontageSections: { p.rect(1.5f, 5.5f, 13, 6); p.line(6, 5.5f, 6, 11.5f); p.line(10, 5.5f, 10, 11.5f); p.fillPoly({{1.5f, 1.8f}, {4.5f, 1.8f}, {1.5f, 4.6f}}); p.fillPoly({{6, 1.8f}, {9, 1.8f}, {6, 4.6f}}); } break;
  case Icon::AssetBrowser: { p.rect(2, 2, 5, 5); p.rect(9, 2, 5, 5); p.rect(2, 9, 5, 5); p.rect(9, 9, 5, 5); } break;
  case Icon::PreviewCharacter: { p.circle(8, 3.5f, 1.8f); p.line(8, 5.3f, 8, 10); p.line(3.5f, 7, 12.5f, 7); p.line(8, 10, 5.5f, 14.5f); p.line(8, 10, 10.5f, 14.5f); } break;
  case Icon::Collider: { for (float x : {2.0f, 14.0f}) for (float y : {2.0f, 14.0f}) { const float dx = x < 8 ? 3.5f : -3.5f; const float dy = y < 8 ? 3.5f : -3.5f; p.line(x, y, x + dx, y); p.line(x, y, x, y + dy); } } break;
  case Icon::Rigid3D: { p.rect(4, 1.5f, 8, 7); p.line(8, 10, 8, 14.5f); p.poly({{5.8f, 12.3f}, {8, 14.5f}, {10.2f, 12.3f}}); } break;
  case Icon::Static3D: { p.rect(4, 2, 8, 8); p.line(1.5f, 12.5f, 14.5f, 12.5f); for (float x : {4.0f, 8.0f, 12.0f}) p.line(x, 12.5f, x - 1.5f, 14.8f); } break;
  case Icon::MeshInstance: { p.poly({{8, 1.5f}, {14.5f, 13.5f}, {1.5f, 13.5f}}, true); p.line(8, 1.5f, 8, 13.5f); p.line(4.8f, 7.5f, 11.2f, 7.5f); } break;
  case Icon::Skeleton: { p.circle(8, 2.8f, 1.6f); p.line(8, 4.4f, 8, 14.5f); p.line(4.5f, 6.5f, 11.5f, 6.5f); p.line(5, 9.2f, 11, 9.2f); p.line(5.8f, 11.8f, 10.2f, 11.8f); } break;
  case Icon::BoneAttachment: { p.line(3, 13, 9, 7); p.dot(2.6f, 13.4f, 1.5f); p.dot(9.4f, 6.6f, 1.5f); p.circle(12.2f, 3.8f, 2.3f); } break;
  case Icon::Object3D: { p.line(7, 9, 14, 9); p.line(7, 9, 7, 2); p.line(7, 9, 2.5f, 13.5f); p.dot(7, 9, 1.4f); } break;
  case Icon::Marker: { p.circle(8, 6, 3.6f); p.poly({{5.2f, 8.4f}, {8, 14.5f}, {10.8f, 8.4f}}); p.dot(8, 6, 1.1f); } break;
  case Icon::Environment: { p.arc(8, 13, 6.5f, 9, 180, 360); p.line(1.5f, 13, 14.5f, 13); p.dot(10.6f, 7, 1.3f); } break;
  case Icon::DirectionalLight: { p.circle(4.2f, 4.2f, 2.4f); p.line(5.5f, 14, 9.5f, 10); p.line(8.5f, 14.8f, 13, 10.3f); p.line(9.5f, 11.5f, 14.5f, 6.5f); } break;
  case Icon::Projection: { p.poly({{2, 7}, {14, 2.5f}, {14, 13.5f}, {2, 9}}, true); p.line(8, 4.8f, 8, 11.2f); } break;
  case Icon::Prefab: { p.rect(1.5f, 1.5f, 9, 9); p.rect(5.5f, 5.5f, 9, 9); } break;
  case Icon::Bundle: { p.rect(2, 5.5f, 12, 9); p.poly({{2, 5.5f}, {4, 2}, {12, 2}, {14, 5.5f}}); p.line(8, 2, 8, 5.5f); p.rect(6.5f, 5.5f, 3, 3); } break;
  case Icon::Model: { p.poly({{8, 1.5f}, {11, 4}, {8, 6.5f}, {5, 4}}, true); p.poly({{4, 8}, {12, 8}, {10.5f, 14.5f}, {5.5f, 14.5f}}, true); } break;
  case Icon::Shader: { p.circle(8, 8, 6.2f); p.arc(8, 8, 6.2f, 2.4f, 0, 180); p.arc(8, 8, 2.4f, 6.2f, -90, 90); } break;
  case Icon::EntityAsset: { p.rrect(2, 2, 12, 12, 2.5f); p.dot(8, 8, 2.3f); } break;
  case Icon::MeshAsset: { p.rect(2, 2, 12, 12); p.line(2, 14, 14, 2); p.line(2, 8, 8, 2); p.line(8, 14, 14, 8); } break;
  case Icon::SceneAsset: { p.rect(1.5f, 3, 13, 10); p.poly({{1.5f, 13}, {6, 7.5f}, {9, 10.5f}, {11, 8.5f}, {14.5f, 13}}); } break;
  case Icon::AdditivePose: { p.poly({{2, 10.5f}, {8, 13.5f}, {14, 10.5f}, {8, 7.5f}}, true); p.line(8, 1.5f, 8, 6); p.line(5.5f, 3.8f, 10.5f, 3.8f); } break;
  case Icon::TransformTrack: { p.line(8, 2, 8, 9); p.line(4.5f, 5.5f, 11.5f, 5.5f); p.poly({{6.6f, 3.4f}, {8, 2}, {9.4f, 3.4f}}); p.line(2, 12.5f, 14, 12.5f); p.dot(10, 12.5f, 1.3f); } break;
  case Icon::ProfTimeline: { p.fillRect(2, 3, 7, 2.5f); p.fillRect(5, 7, 8.5f, 2.5f); p.fillRect(3, 11, 5, 2.5f); } break;
  case Icon::ProfSpans: { p.rect(1.5f, 2.5f, 13, 3); p.rect(3.5f, 7, 9, 3); p.rect(5.5f, 11.5f, 5, 3); } break;
  case Icon::ProfFrame: { p.poly({{1.5f, 12}, {4, 9}, {6, 11}, {9, 4}, {11, 8}, {14.5f, 6}}); p.line(1.5f, 14.5f, 14.5f, 14.5f); } break;
  case Icon::ProfCounters: { p.line(5.5f, 2, 4.5f, 14); p.line(11.5f, 2, 10.5f, 14); p.line(2, 6, 14, 6); p.line(2, 10, 14, 10); } break;
  case Icon::ProfBreadcrumbs: { p.dot(2.8f, 8, 1.5f); p.dot(8, 8, 1.5f); p.dot(13.2f, 8, 1.5f); p.poly({{4.8f, 6.6f}, {6.2f, 8}, {4.8f, 9.4f}}); p.poly({{10, 6.6f}, {11.4f, 8}, {10, 9.4f}}); } break;
  case Icon::BlueprintClass: { p.rrect(1.5f, 2.5f, 13, 11, 1.6f); p.rect(3.5f, 5, 4, 3); p.rect(9, 8.5f, 4, 3); p.line(7.5f, 6.5f, 8.2f, 6.5f); p.line(8.2f, 6.5f, 8.2f, 10); p.line(8.2f, 10, 9, 10); } break;
  case Icon::Curve:
    p.line(2, 13, 2, 3); p.line(2, 13, 14, 13);
    p.poly({{3.5f, 11.5f}, {5.5f, 10.8f}, {7, 8.5f}, {8.5f, 6}, {10.5f, 4.6f}, {13, 4.2f}}); break;
  case Icon::Pause: p.fillRect(4, 3, 3, 10); p.fillRect(9, 3, 3, 10); break;
  case Icon::Stop: p.fillRect(3.5f, 3.5f, 9, 9); break;
  case Icon::Undo:
    p.poly({{6, 3.5f}, {2.8f, 6.8f}, {6, 10}}); p.line(3, 6.8f, 9.2f, 6.8f); p.arc(9.2f, 10, 3.2f, 3.2f, -90, 90, 10);
    p.line(9.2f, 13.2f, 6.5f, 13.2f); break;
  case Icon::Redo:
    p.poly({{10, 3.5f}, {13.2f, 6.8f}, {10, 10}}); p.line(13, 6.8f, 6.8f, 6.8f); p.arc(6.8f, 10, 3.2f, 3.2f, -90, -270, 10);
    p.line(6.8f, 13.2f, 9.5f, 13.2f); break;
  case Icon::Move:
    p.line(8, 2, 8, 14); p.line(2, 8, 14, 8);
    p.poly({{6, 4}, {8, 2}, {10, 4}}); p.poly({{6, 12}, {8, 14}, {10, 12}});
    p.poly({{4, 6}, {2, 8}, {4, 10}}); p.poly({{12, 6}, {14, 8}, {12, 10}}); break;
  case Icon::Rotate:
    p.arc(8, 8, 5.5f, 5.5f, -60, 230, 22); p.poly({{10.5f, 2.2f}, {13.4f, 3.6f}, {11, 6.2f}}); break;
  case Icon::Scale:
    p.rect(2.5f, 7.5f, 6, 6); p.line(7, 9, 13.5f, 2.5f); p.poly({{9.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 6.5f}}); break;
  case Icon::Select:
    p.fillPoly({{4, 2}, {4, 12.6f}, {8.2f, 9.2f}, {12.6f, 8.6f}});
    p.fillPoly({{6.6f, 9.6f}, {8.9f, 14}, {10.9f, 13}, {8.6f, 8.8f}}); break;
  case Icon::Grid:
    p.rect(2.5f, 2.5f, 11, 11); p.line(6.2f, 2.5f, 6.2f, 13.5f); p.line(9.8f, 2.5f, 9.8f, 13.5f);
    p.line(2.5f, 6.2f, 13.5f, 6.2f); p.line(2.5f, 9.8f, 13.5f, 9.8f); break;
  case Icon::Save:
    p.poly({{2.5f, 2.5f}, {11.5f, 2.5f}, {13.5f, 4.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true);
    p.rect(5, 2.5f, 5, 3.5f); p.rect(4.5f, 8.5f, 7, 5); break;
  case Icon::Open:
    p.poly({{2, 12.5f}, {2, 3.5f}, {6, 3.5f}, {7.4f, 5}, {12.5f, 5}, {12.5f, 7}});
    p.poly({{2, 12.5f}, {4, 7}, {14.5f, 7}, {12.5f, 12.5f}}, true); break;
  case Icon::Settings: {
    for (int i = 0; i < 8; i++) {
      float a = i * 3.14159265f / 4.f, ca = std::cos(a), sa = std::sin(a);
      p.line(8 + 4.6f * ca, 8 + 4.6f * sa, 8 + 5.8f * ca, 8 + 5.8f * sa);
    }
    p.circle(8, 8, 3.9f); p.circle(8, 8, 1.6f); break;
  }
  case Icon::Trash:
    p.line(2.8f, 4.5f, 13.2f, 4.5f); p.poly({{6.3f, 4.5f}, {6.3f, 2.8f}, {9.7f, 2.8f}, {9.7f, 4.5f}});
    p.poly({{4.2f, 4.5f}, {5, 13.5f}, {11, 13.5f}, {11.8f, 4.5f}}); p.line(6.8f, 6.5f, 6.8f, 11.5f); p.line(9.2f, 6.5f, 9.2f, 11.5f); break;
  case Icon::Copy:
  case Icon::Duplicate:
    p.rect(5.5f, 5.5f, 8, 8); p.poly({{3.5f, 10.5f}, {2.5f, 10.5f}, {2.5f, 2.5f}, {10.5f, 2.5f}, {10.5f, 3.5f}}); break;
  case Icon::Paste:
    p.rect(3, 3.5f, 10, 10); p.rect(5.8f, 2, 4.4f, 3); p.line(5.5f, 8, 10.5f, 8); p.line(5.5f, 10.5f, 10.5f, 10.5f); break;
  case Icon::Info:
    p.circle(8, 8, 6); p.dot(8, 5.2f, 0.9f); p.line(8, 7.4f, 8, 11.4f); break;
  case Icon::Warning:
    p.poly({{8, 2.4f}, {14.6f, 13.6f}, {1.4f, 13.6f}}, true); p.line(8, 6.6f, 8, 10); p.dot(8, 11.9f, 0.8f); break;
  case Icon::Error:
    p.circle(8, 8, 6); p.line(5.6f, 5.6f, 10.4f, 10.4f); p.line(10.4f, 5.6f, 5.6f, 10.4f); break;
  case Icon::Filter:
    p.poly({{2.5f, 3.5f}, {13.5f, 3.5f}, {9, 8.6f}, {9, 13}, {7, 12}, {7, 8.6f}}, true); break;
  case Icon::Layers:
    p.poly({{8, 2.5f}, {14, 6}, {8, 9.5f}, {2, 6}}, true); p.poly({{2, 8.4f}, {8, 11.9f}, {14, 8.4f}});
    p.poly({{2, 10.8f}, {8, 14.3f}, {14, 10.8f}}); break;
  case Icon::Dots: p.dot(3.5f, 8, 1.2f); p.dot(8, 8, 1.2f); p.dot(12.5f, 8, 1.2f); break;
  case Icon::Refresh:
    p.arc(8, 8, 5.2f, 5.2f, -40, 250, 22); p.poly({{9.2f, 1.8f}, {12.4f, 3.2f}, {9.8f, 5.8f}}); break;
  case Icon::Magnet:
    p.arc(8, 7.5f, 5, 5, 180, 360, 14); p.line(3, 7.5f, 3, 13); p.line(13, 7.5f, 13, 13);
    p.line(5.8f, 7.5f, 5.8f, 13); p.line(10.2f, 7.5f, 10.2f, 13); p.line(3, 13, 5.8f, 13); p.line(10.2f, 13, 13, 13); break;
  case Icon::Terminal:
    p.rect(2, 3, 12, 10); p.poly({{4.8f, 6.2f}, {7, 8}, {4.8f, 9.8f}}); p.line(8.4f, 10, 11.4f, 10); break;
  case Icon::Frame:
    p.poly({{2.5f, 6}, {2.5f, 2.5f}, {6, 2.5f}}); p.poly({{10, 2.5f}, {13.5f, 2.5f}, {13.5f, 6}});
    p.poly({{13.5f, 10}, {13.5f, 13.5f}, {10, 13.5f}}); p.poly({{6, 13.5f}, {2.5f, 13.5f}, {2.5f, 10}});
    p.rect(6, 6, 4, 4); break;
  case Icon::Globe:
    p.circle(8, 8, 6); p.ellipse(8, 8, 2.8f, 6); p.line(2, 8, 14, 8); break;
  case Icon::Dot: p.dot(8, 8, 2.2f); break;
  }
}

void drawLogo(Canvas& c, Rect box) {
  float s = std::min(box.w, box.h);
  float ox = box.x + (box.w - s) * 0.5f, oy = box.y + (box.h - s) * 0.5f;
  auto P = [&](float x, float y) { return Vec2{ox + x * s, oy + y * s}; };
  Vec2 apex = P(0.5f, 0.06f), bl = P(0.04f, 0.94f), br = P(0.96f, 0.94f), ctr = P(0.5f, 0.62f);
  Vec2 f1[] = {apex, bl, ctr}, f2[] = {apex, ctr, br}, f3[] = {bl, br, ctr};
  c.fillPolygon(f1, Color::hex(0x7ab8ff));
  c.fillPolygon(f2, Color::hex(0x3b82e0));
  c.fillPolygon(f3, Color::hex(0x1f4f9c));
}

} // namespace prism
