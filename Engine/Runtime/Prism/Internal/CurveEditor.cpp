// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Curve Editor & Keyframe Timeline Implementation.
#include "prism/CurveEditor.h"
#include "prism/Application.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prism {

CurveEditor::CurveEditor() {
  m_channels = {
    {"Location.X", Color(235, 75, 75), true},
    {"Location.Y", Color(60, 200, 110), true},
    {"Location.Z", Color(70, 145, 245), true},
    {"Intensity",  Color(245, 180, 45), true}
  };

  // Default cubic smooth S-curve
  addKeyframe(0.00f, 0.00f, TangentMode::Smooth);
  addKeyframe(0.35f, 0.20f, TangentMode::Smooth);
  addKeyframe(0.65f, 0.80f, TangentMode::Smooth);
  addKeyframe(1.00f, 1.00f, TangentMode::Smooth);
}

void CurveEditor::addKeyframe(float time, float value, TangentMode mode) {
  CurveKeyframe k;
  k.time = std::clamp(time, 0.0f, 1.0f);
  k.value = value;
  k.mode = mode;
  k.inTangent = 0.0f;
  k.outTangent = 0.0f;
  m_keys.push_back(k);
  std::sort(m_keys.begin(), m_keys.end());
  invalidate();
}

void CurveEditor::clearKeyframes() {
  m_keys.clear();
  m_selectedKey = -1;
  m_dragKey = -1;
  invalidate();
}

float CurveEditor::evaluate(float t) const {
  if (m_keys.empty()) return 0.0f;
  if (t <= m_keys.front().time) return m_keys.front().value;
  if (t >= m_keys.back().time) return m_keys.back().value;

  for (size_t i = 0; i + 1 < m_keys.size(); i++) {
    const auto& k0 = m_keys[i];
    const auto& k1 = m_keys[i + 1];
    if (t >= k0.time && t <= k1.time) {
      float dt = k1.time - k0.time;
      if (dt <= 1e-6f) return k0.value;
      float u = (t - k0.time) / dt;

      if (k0.mode == TangentMode::Step) {
        return k0.value;
      }
      if (k0.mode == TangentMode::Linear) {
        return k0.value + u * (k1.value - k0.value);
      }
      // Cubic Hermite Spline
      float u2 = u * u;
      float u3 = u2 * u;
      float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
      float h10 = u3 - 2.0f * u2 + u;
      float h01 = -2.0f * u3 + 3.0f * u2;
      float h11 = u3 - u2;
      return h00 * k0.value + h10 * (dt * k0.outTangent) + h01 * k1.value + h11 * (dt * k1.inTangent);
    }
  }
  return m_keys.back().value;
}

Vec2 CurveEditor::measure(Vec2 avail) {
  return m_desired = constrain(avail);
}

void CurveEditor::arrange(Rect r) {
  m_rect = r;
}

Rect CurveEditor::plotRect() const {
  float px = m_rect.x + m_trackListW + m_marginL;
  float py = m_rect.y + m_toolbarH + m_marginT;
  float pw = std::max(20.0f, m_rect.right() - px - m_marginR);
  float ph = std::max(20.0f, m_rect.bottom() - py - m_marginB);
  return Rect{px, py, pw, ph};
}

Vec2 CurveEditor::toScreen(float time, float val) const {
  Rect pr = plotRect();
  float sx = pr.x + time * pr.w;
  float sy = pr.bottom() - val * pr.h;
  return Vec2(sx, sy);
}

std::pair<float, float> CurveEditor::toCurve(Vec2 screenPos) const {
  Rect pr = plotRect();
  float t = std::clamp((screenPos.x - pr.x) / pr.w, 0.0f, 1.0f);
  float v = (pr.bottom() - screenPos.y) / pr.h;
  return {t, v};
}

int CurveEditor::findKeyAt(Vec2 screenPos, float radius) const {
  for (size_t i = 0; i < m_keys.size(); i++) {
    Vec2 sp = toScreen(m_keys[i].time, m_keys[i].value);
    if ((sp - screenPos).length() <= radius) {
      return int(i);
    }
  }
  return -1;
}

void CurveEditor::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRect(m_rect, t.panel);

  // -------------------------------------------------- Left Track/Channel Sidebar
  Rect trackRect{m_rect.x, m_rect.y, m_trackListW, m_rect.h};
  c.fillRect(trackRect, Color(22, 25, 33));
  c.strokeRect(trackRect, Color(35, 40, 52));

  // Sidebar Header
  Rect trackHeader{trackRect.x, trackRect.y, trackRect.w, m_toolbarH};
  c.fillRect(trackHeader, Color(18, 20, 27));
  c.line(Vec2(trackRect.x, trackHeader.bottom()), Vec2(trackRect.right(), trackHeader.bottom()), Color(35, 40, 52), 1.0f);
  c.textIn(*t.small, trackHeader.deflated(Margin(8.0f, 0.0f)), "CHANNELS", Color(150, 160, 180), HAlign::Left);

  // Channel rows
  float rowY = trackHeader.bottom() + 4.0f;
  for (size_t i = 0; i < m_channels.size(); i++) {
    Rect chRow{trackRect.x + 4.0f, rowY, trackRect.w - 8.0f, 24.0f};
    bool isSel = (int(i) == m_activeChannel);
    if (isSel) {
      c.fillRoundRect(chRow, 3.0f, Color(40, 48, 65));
      c.strokeRoundRect(chRow, 3.0f, Color(70, 130, 220), 1.0f);
    }

    // Channel color dot
    c.fillCircle(Vec2(chRow.x + 10.0f, chRow.center().y), 4.0f, m_channels[i].color);

    // Channel Name
    c.textIn(*t.small, Rect(chRow.x + 22.0f, chRow.y, chRow.w - 55.0f, chRow.h),
             m_channels[i].name, isSel ? Color(255, 255, 255) : Color(200, 205, 215), HAlign::Left);

    // Eye visibility indicator
    Rect eyeBox{chRow.right() - 26.0f, chRow.y + 4.0f, 20.0f, 16.0f};
    c.fillRoundRect(eyeBox, 2.0f, Color(30, 35, 46));
    c.textIn(*t.small, eyeBox, "4k", Color(130, 140, 160), HAlign::Center);

    rowY += 28.0f;
  }

  // -------------------------------------------------- Top Toolbar
  Rect toolbarRect{m_rect.x + m_trackListW, m_rect.y, m_rect.w - m_trackListW, m_toolbarH};
  c.fillRect(toolbarRect, Color(24, 28, 36));
  c.line(Vec2(toolbarRect.x, toolbarRect.bottom()), Vec2(toolbarRect.right(), toolbarRect.bottom()), Color(35, 40, 52), 1.0f);

  auto drawToolBtn = [&](float x, const char* label, bool active) {
    Rect b{x, toolbarRect.y + 3.0f, 65.0f, 22.0f};
    c.fillRoundRect(b, 3.0f, active ? Color(50, 70, 100) : Color(32, 37, 48));
    c.strokeRoundRect(b, 3.0f, active ? Color(80, 150, 240) : Color(45, 52, 68), 1.0f);
    c.textIn(*t.small, b, label, active ? Color(255, 255, 255) : Color(190, 200, 215), HAlign::Center);
    return b.right() + 4.0f;
  };

  float tbx = toolbarRect.x + 8.0f;
  tbx = drawToolBtn(tbx, "Smooth", true);
  tbx = drawToolBtn(tbx, "Linear", false);
  tbx = drawToolBtn(tbx, "Step", false);
  tbx += 8.0f;
  tbx = drawToolBtn(tbx, "+ Key", false);
  tbx = drawToolBtn(tbx, "- Key", false);
  tbx = drawToolBtn(tbx, "Flatten", false);

  // -------------------------------------------------- Right Canvas Plot
  Rect pr = plotRect();

  // Plot background (dark matte engineering grid)
  c.fillRect(pr, Color(16, 19, 26));
  c.strokeRect(pr, Color(40, 48, 62));

  // Minor Grid Lines (0.05 step)
  for (int m = 0; m <= 20; m++) {
    float mx = pr.x + float(m) * 0.05f * pr.w;
    c.line(Vec2(mx, pr.y), Vec2(mx, pr.bottom()), Color(28, 34, 46, 120), 1.0f);
  }

  // Major Grid Lines (Time)
  const float gridSteps[] = {0.0f, 0.20f, 0.40f, 0.60f, 0.80f, 1.00f};
  for (float step : gridSteps) {
    float gx = pr.x + step * pr.w;
    c.line(Vec2(gx, pr.y), Vec2(gx, pr.bottom()), Color(45, 54, 72, 180), 1.0f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2fs", step);
    c.textIn(*t.small, Rect(gx - 20.0f, pr.bottom() + 4.0f, 40.0f, 16.0f), buf, Color(140, 150, 170), HAlign::Center);
  }

  // Horizontal Grid Lines (Value)
  const float valSteps[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
  for (float step : valSteps) {
    float gy = pr.bottom() - step * pr.h;
    Color lineCol = (step == 0.0f) ? Color(90, 110, 140, 240) : Color(45, 54, 72, 180);
    float lineThick = (step == 0.0f) ? 1.5f : 1.0f;
    c.line(Vec2(pr.x, gy), Vec2(pr.right(), gy), lineCol, lineThick);

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", step);
    c.textIn(*t.small, Rect(pr.x - m_marginL + 2.0f, gy - 7.0f, m_marginL - 6.0f, 14.0f),
             buf, Color(140, 150, 170), HAlign::Right);
  }

  // Clip to plot area for curve geometry
  c.pushClip(pr);

  // Soft gradient area fill under the curve down to baseline (Value = 0)
  const int segments = 120;
  float baseY = pr.bottom();
  Vec2 prevPt = toScreen(0.0f, evaluate(0.0f));
  for (int i = 1; i <= segments; i++) {
    float time = float(i) / float(segments);
    float val = evaluate(time);
    Vec2 curPt = toScreen(time, val);

    // Quad slice under curve
    Vec2 slice[4] = {
      Vec2(prevPt.x, baseY),
      prevPt,
      curPt,
      Vec2(curPt.x, baseY)
    };
    c.fillPolygon(std::span<const Vec2>(slice, 4), Color(70, 140, 245, 16));
    prevPt = curPt;
  }

  // Anti-aliased high-fidelity Cubic Hermite Spline Curve
  prevPt = toScreen(0.0f, evaluate(0.0f));
  Color curveCol = Color(75, 155, 255);
  for (int i = 1; i <= segments; i++) {
    float time = float(i) / float(segments);
    float val = evaluate(time);
    Vec2 curPt = toScreen(time, val);
    c.line(prevPt, curPt, curveCol, 2.0f);
    prevPt = curPt;
  }

  // Draw Tangent Handles for selected key
  if (m_selectedKey >= 0 && m_selectedKey < int(m_keys.size())) {
    const auto& sk = m_keys[size_t(m_selectedKey)];
    Vec2 sp = toScreen(sk.time, sk.value);

    // Out-tangent handle
    Vec2 outHandle = sp + Vec2(40.0f, -sk.outTangent * 24.0f);
    c.line(sp, outHandle, Color(140, 185, 255, 220), 1.5f);
    c.fillCircle(outHandle, 4.0f, Color(30, 38, 52));
    c.strokeCircle(outHandle, 4.0f, Color(140, 185, 255), 1.5f);

    // In-tangent handle
    Vec2 inHandle = sp + Vec2(-40.0f, sk.inTangent * 24.0f);
    c.line(sp, inHandle, Color(140, 185, 255, 220), 1.5f);
    c.fillCircle(inHandle, 4.0f, Color(30, 38, 52));
    c.strokeCircle(inHandle, 4.0f, Color(140, 185, 255), 1.5f);
  }

  // Draw Keyframe Diamonds
  for (size_t i = 0; i < m_keys.size(); i++) {
    Vec2 sp = toScreen(m_keys[i].time, m_keys[i].value);
    bool isSel = (int(i) == m_selectedKey);

    float dSize = isSel ? 7.5f : 5.5f;
    Vec2 diamond[4] = {
      Vec2(sp.x, sp.y - dSize),
      Vec2(sp.x + dSize, sp.y),
      Vec2(sp.x, sp.y + dSize),
      Vec2(sp.x - dSize, sp.y)
    };

    if (isSel) {
      c.strokeCircle(sp, 10.0f, Color(255, 195, 45, 150), 1.0f);
    }

    Color fillCol = isSel ? Color(255, 205, 45) : Color(240, 245, 255);
    Color strokeCol = isSel ? Color(255, 255, 255) : Color(40, 80, 150);

    c.fillPolygon(std::span<const Vec2>(diamond, 4), fillCol);
    for (int k = 0; k < 4; k++) {
      c.line(diamond[k], diamond[(k + 1) % 4], strokeCol, 1.5f);
    }
  }

  // Draw Timeline Playhead Needle
  float needleX = pr.x + m_playheadTime * pr.w;
  c.line(Vec2(needleX, pr.y), Vec2(needleX, pr.bottom()), Color(240, 65, 65, 240), 1.5f);
  // Needle top triangle flag
  Vec2 flag[3] = {
    Vec2(needleX - 6.0f, pr.y),
    Vec2(needleX + 6.0f, pr.y),
    Vec2(needleX, pr.y + 10.0f)
  };
  c.fillPolygon(std::span<const Vec2>(flag, 3), Color(240, 65, 65));

  c.popClip();

  // Floating Coordinate Badge for selected key
  if (m_selectedKey >= 0 && m_selectedKey < int(m_keys.size())) {
    const auto& sk = m_keys[size_t(m_selectedKey)];
    Vec2 sp = toScreen(sk.time, sk.value);
    char coordBuf[32];
    std::snprintf(coordBuf, sizeof(coordBuf), "T: %.2fs | V: %.2f", sk.time, sk.value);
    Rect badgeRect{sp.x - 45.0f, sp.y - 28.0f, 90.0f, 18.0f};
    c.fillRoundRect(badgeRect, 3.0f, Color(15, 20, 28, 220));
    c.strokeRoundRect(badgeRect, 3.0f, Color(245, 185, 55, 200), 1.0f);
    c.textIn(*t.small, badgeRect, coordBuf, Color(255, 255, 255), HAlign::Center);
  }
}

bool CurveEditor::onMouseDown(const MouseEvent& e) {
  if (e.button == MouseButton::Left) {
    Rect pr = plotRect();

    // Check click on playhead
    float needleX = pr.x + m_playheadTime * pr.w;
    if (std::abs(e.pos.x - needleX) < 8.0f && e.pos.y >= pr.y - 12.0f && e.pos.y <= pr.bottom()) {
      m_draggingPlayhead = true;
      return true;
    }

    // Bezier-style tangent handles of the selected key (drag mirrors both sides for a smooth join).
    if (m_selectedKey >= 0 && m_selectedKey < int(m_keys.size())) {
      const auto& sk = m_keys[size_t(m_selectedKey)];
      Vec2 sp = toScreen(sk.time, sk.value);
      Vec2 oh = sp + Vec2(40.0f, -sk.outTangent * 24.0f);
      Vec2 ih = sp + Vec2(-40.0f, sk.inTangent * 24.0f);
      auto near = [&](Vec2 a) { return std::abs(a.x - e.pos.x) < 7.0f && std::abs(a.y - e.pos.y) < 7.0f; };
      if (near(oh) || near(ih)) {
        m_dragHandle = near(oh) ? 2 : 1;
        m_dragStartKey = sk;
        return true;
      }
    }

    int keyIdx = findKeyAt(e.pos, 10.0f);
    if (keyIdx >= 0) {
      m_selectedKey = keyIdx;
      m_dragKey = keyIdx;
      m_dragStartPos = e.pos;
      m_dragStartKey = m_keys[size_t(keyIdx)];
      invalidate();
      return true;
    }

    // Clicked in plot area without hitting key: add keyframe
    if (pr.contains(e.pos)) {
      auto [t, v] = toCurve(e.pos);
      addKeyframe(t, v);
      m_selectedKey = findKeyAt(toScreen(t, v), 10.0f);
      invalidate();
      return true;
    }

    // Check track selection click
    if (e.pos.x < m_rect.x + m_trackListW && e.pos.y > m_rect.y + m_toolbarH) {
      int chIdx = int((e.pos.y - (m_rect.y + m_toolbarH + 4.0f)) / 28.0f);
      if (chIdx >= 0 && chIdx < int(m_channels.size())) {
        m_activeChannel = chIdx;
        invalidate();
        return true;
      }
    }
  }
  return false;
}

bool CurveEditor::onMouseMove(const MouseEvent& e) {
  if (m_draggingPlayhead) {
    Rect pr = plotRect();
    m_playheadTime = std::clamp((e.pos.x - pr.x) / pr.w, 0.0f, 1.0f);
    invalidate();
    return true;
  }

  if (m_dragHandle && m_selectedKey >= 0 && m_selectedKey < int(m_keys.size())) {
    auto& k = m_keys[size_t(m_selectedKey)];
    Vec2 sp = toScreen(k.time, k.value);
    float slope = m_dragHandle == 2 ? -(e.pos.y - sp.y) / 24.0f : (e.pos.y - sp.y) / 24.0f;
    slope = std::clamp(slope, -12.0f, 12.0f);
    k.inTangent = k.outTangent = slope;
    if (onCurveModified) onCurveModified(k.value);
    invalidate();
    return true;
  }

  if (m_dragKey >= 0 && m_dragKey < int(m_keys.size())) {
    auto [t, v] = toCurve(e.pos);
    m_keys[size_t(m_dragKey)].time = t;
    m_keys[size_t(m_dragKey)].value = v;

    // Keep sorted
    float curVal = v;
    std::sort(m_keys.begin(), m_keys.end());
    m_dragKey = findKeyAt(toScreen(t, curVal), 12.0f);
    m_selectedKey = m_dragKey;

    if (onCurveModified) onCurveModified(curVal);
    invalidate();
    return true;
  }
  return false;
}

bool CurveEditor::onMouseUp(const MouseEvent&) {
  m_draggingPlayhead = false;
  if (m_dragHandle) {
    m_dragHandle = 0;
    if (m_tx && m_selectedKey >= 0 && m_selectedKey < int(m_keys.size())) {
      CurveKeyframe oldK = m_dragStartKey, newK = m_keys[size_t(m_selectedKey)];
      if (oldK.outTangent != newK.outTangent || oldK.inTangent != newK.inTangent) {
        auto apply = [this](CurveKeyframe k) {
          int idx = findKeyAt(toScreen(k.time, k.value), 12.0f);
          if (idx >= 0) { m_keys[size_t(idx)] = k; invalidate(); }
        };
        m_tx->push("Edit Curve Tangent", [apply, newK]() { apply(newK); }, [apply, oldK]() { apply(oldK); });
      }
    }
    return true;
  }
  if (m_dragKey >= 0) {
    if (m_tx && (m_dragStartKey.time != m_keys[size_t(m_dragKey)].time ||
                 m_dragStartKey.value != m_keys[size_t(m_dragKey)].value)) {
      CurveKeyframe oldK = m_dragStartKey;
      CurveKeyframe newK = m_keys[size_t(m_dragKey)];
      m_tx->push(
        "Modify Curve Keyframe",
        [this, newK]() {
          int idx = findKeyAt(toScreen(newK.time, newK.value), 12.0f);
          if (idx >= 0) {
            m_keys[size_t(idx)] = newK;
            invalidate();
          }
        },
        [this, oldK]() {
          int idx = findKeyAt(toScreen(oldK.time, oldK.value), 12.0f);
          if (idx >= 0) {
            m_keys[size_t(idx)] = oldK;
            invalidate();
          }
        }
      );
    }
    m_dragKey = -1;
  }
  return false;
}

bool CurveEditor::onKeyDown(const KeyEvent& e) {
  if ((e.key == Key::Delete || e.key == Key::Backspace) && m_selectedKey >= 0 && m_keys.size() > 2) {
    m_keys.erase(m_keys.begin() + m_selectedKey);
    m_selectedKey = -1;
    m_dragKey = -1;
    invalidate();
    return true;
  }
  return false;
}

} // namespace prism
