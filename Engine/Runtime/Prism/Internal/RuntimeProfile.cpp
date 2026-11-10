// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Runtime UI & Game HUD Profile Implementation.
#include "prism/RuntimeProfile.h"
#include "prism/Application.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prism {

Widget* FocusNavigation::findNextFocus(Widget* root, Widget* current, FocusDirection dir) {
  if (!root) return nullptr;

  std::vector<Widget*> focusables;
  std::function<void(Widget*)> collect = [&](Widget* w) {
    if (!w || !w->visible() || !w->enabled()) return;
    if (w->focusable()) focusables.push_back(w);
    for (const auto& child : w->children()) {
      collect(child.get());
    }
  };
  collect(root);

  if (focusables.empty()) return nullptr;
  if (!current) return focusables.front();

  Vec2 curPos = current->rect().center();
  Widget* bestWidget = nullptr;
  float bestDist = 1e9f;

  for (Widget* w : focusables) {
    if (w == current) continue;
    Vec2 candPos = w->rect().center();
    Vec2 delta = candPos - curPos;

    bool validDir = false;
    float primary = 0.0f, ortho = 0.0f;

    switch (dir) {
      case FocusDirection::Up:
        if (delta.y < -4.0f) { validDir = true; primary = -delta.y; ortho = std::abs(delta.x); }
        break;
      case FocusDirection::Down:
        if (delta.y > 4.0f) { validDir = true; primary = delta.y; ortho = std::abs(delta.x); }
        break;
      case FocusDirection::Left:
        if (delta.x < -4.0f) { validDir = true; primary = -delta.x; ortho = std::abs(delta.y); }
        break;
      case FocusDirection::Right:
        if (delta.x > 4.0f) { validDir = true; primary = delta.x; ortho = std::abs(delta.y); }
        break;
    }

    if (validDir) {
      float score = primary + ortho * 2.0f;
      if (score < bestDist) {
        bestDist = score;
        bestWidget = w;
      }
    }
  }

  return bestWidget ? bestWidget : current;
}

GameHud::GameHud() {
  m_exitButton = make<Button>("Exit Game Mode (Esc)", Icon::Close, ButtonStyle::Flat);
  m_exitButton->onClick = [this] {
    if (onExitGameMode) onExitGameMode();
  };
  addChild(m_exitButton);
}

void GameHud::setHealth(float current, float max) {
  m_health = std::clamp(current, 0.0f, max);
  m_maxHealth = max;
  invalidate();
}

void GameHud::setShield(float current, float max) {
  m_shield = std::clamp(current, 0.0f, max);
  m_maxShield = max;
  invalidate();
}

void GameHud::setYaw(float yawRad) {
  m_yaw = yawRad;
  invalidate();
}

void GameHud::setActionPrompt(std::string key, std::string text) {
  m_promptKey = std::move(key);
  m_promptText = std::move(text);
  m_hasPrompt = true;
  invalidate();
}

void GameHud::clearActionPrompt() {
  m_hasPrompt = false;
  invalidate();
}

void GameHud::triggerHitmarker() {
  m_hitmarkerTime = 0.2f;
  invalidate();
}

Vec2 GameHud::measure(Vec2 avail) {
  return m_desired = constrain(avail);
}

void GameHud::arrange(Rect r) {
  m_rect = r;
  float btnW = 150.0f;
  float btnH = 26.0f;
  m_exitButton->arrange(Rect{r.right() - m_safeZone.r - btnW, r.y + m_safeZone.t, btnW, btnH});
}

void GameHud::drawCompass(Canvas& c, Rect r) {
  const Theme& t = theme();
  c.fillRoundRect(r, 4.0f, Color(12, 16, 24, 210));
  c.strokeRoundRect(r, 4.0f, Color(45, 60, 85, 180), 1.0f);

  // Tactical corner tech ticks
  float tk = 6.0f;
  Color tickCol(80, 160, 255, 220);
  c.line(Vec2(r.x, r.y), Vec2(r.x + tk, r.y), tickCol, 1.5f);
  c.line(Vec2(r.x, r.y), Vec2(r.x, r.y + tk), tickCol, 1.5f);
  c.line(Vec2(r.right(), r.y), Vec2(r.right() - tk, r.y), tickCol, 1.5f);
  c.line(Vec2(r.right(), r.y), Vec2(r.right(), r.y + tk), tickCol, 1.5f);

  float midX = r.center().x;
  float yawDeg = m_yaw * 180.0f / 3.14159265f;
  while (yawDeg < 0.0f) yawDeg += 360.0f;
  while (yawDeg >= 360.0f) yawDeg -= 360.0f;

  c.pushClip(r);

  const float pxPerDeg = 2.5f;
  for (int d = -60; d <= 60; d += 15) {
    float deg = std::fmod(yawDeg + float(d) + 360.0f, 360.0f);
    float x = midX + float(d) * pxPerDeg;

    int degInt = int(std::round(deg));
    std::string label;
    bool major = false;
    if (degInt == 0 || degInt == 360) { label = "N"; major = true; }
    else if (degInt == 45) { label = "NE"; }
    else if (degInt == 90) { label = "E"; major = true; }
    else if (degInt == 135) { label = "SE"; }
    else if (degInt == 180) { label = "S"; major = true; }
    else if (degInt == 225) { label = "SW"; }
    else if (degInt == 270) { label = "W"; major = true; }
    else if (degInt == 315) { label = "NW"; }
    else {
      char b[16];
      std::snprintf(b, sizeof(b), "%d", degInt);
      label = b;
    }

    if (major) {
      Color lCol = (label == "N") ? Color(255, 75, 75) : Color(255, 255, 255);
      c.line(Vec2(x, r.y + 3.0f), Vec2(x, r.y + 12.0f), lCol, 1.5f);
      c.textIn(*t.small, Rect(x - 16.0f, r.y + 12.0f, 32.0f, 14.0f), label, lCol, HAlign::Center);
    } else {
      c.line(Vec2(x, r.y + 5.0f), Vec2(x, r.y + 11.0f), Color(140, 160, 190, 180), 1.0f);
      c.textIn(*t.small, Rect(x - 16.0f, r.y + 13.0f, 32.0f, 12.0f), label, Color(130, 150, 180, 180), HAlign::Center);
    }
  }

  c.popClip();

  // Dual Gold Reticle Pointers (top and bottom)
  Vec2 topTri[3] = { Vec2(midX - 5.0f, r.y + 1.0f), Vec2(midX + 5.0f, r.y + 1.0f), Vec2(midX, r.y + 7.0f) };
  Vec2 botTri[3] = { Vec2(midX - 5.0f, r.bottom() - 1.0f), Vec2(midX + 5.0f, r.bottom() - 1.0f), Vec2(midX, r.bottom() - 7.0f) };
  c.fillPolygon(std::span<const Vec2>(topTri, 3), Color(255, 205, 45));
  c.fillPolygon(std::span<const Vec2>(botTri, 3), Color(255, 205, 45));
}

void GameHud::drawCrosshair(Canvas& c, Vec2 center) {
  // Center precision dot
  c.fillCircle(center, 2.0f, Color(255, 255, 255, 240));

  // 4 Tactical Corner Brackets (Cyberpunk / Halo combat reticle)
  float bDist = 12.0f;
  float bLen = 5.0f;
  Color bracketCol(120, 190, 255, 190);

  // Top-Left bracket
  c.line(Vec2(center.x - bDist, center.y - bDist + bLen), Vec2(center.x - bDist, center.y - bDist), bracketCol, 1.5f);
  c.line(Vec2(center.x - bDist, center.y - bDist), Vec2(center.x - bDist + bLen, center.y - bDist), bracketCol, 1.5f);

  // Top-Right bracket
  c.line(Vec2(center.x + bDist - bLen, center.y - bDist), Vec2(center.x + bDist, center.y - bDist), bracketCol, 1.5f);
  c.line(Vec2(center.x + bDist, center.y - bDist), Vec2(center.x + bDist, center.y - bDist + bLen), bracketCol, 1.5f);

  // Bottom-Left bracket
  c.line(Vec2(center.x - bDist, center.y + bDist - bLen), Vec2(center.x - bDist, center.y + bDist), bracketCol, 1.5f);
  c.line(Vec2(center.x - bDist, center.y + bDist), Vec2(center.x - bDist + bLen, center.y + bDist), bracketCol, 1.5f);

  // Bottom-Right bracket
  c.line(Vec2(center.x + bDist - bLen, center.y + bDist), Vec2(center.x + bDist, center.y + bDist), bracketCol, 1.5f);
  c.line(Vec2(center.x + bDist, center.y + bDist), Vec2(center.x + bDist, center.y + bDist - bLen), bracketCol, 1.5f);

  // Fine crosshair tick lines
  float gap = 5.0f;
  float len = 7.0f;
  Color crossCol(255, 255, 255, 180);
  c.line(Vec2(center.x, center.y - gap - len), Vec2(center.x, center.y - gap), crossCol, 1.2f);
  c.line(Vec2(center.x, center.y + gap), Vec2(center.x, center.y + gap + len), crossCol, 1.2f);
  c.line(Vec2(center.x - gap - len, center.y), Vec2(center.x - gap, center.y), crossCol, 1.2f);
  c.line(Vec2(center.x + gap, center.y), Vec2(center.x + gap + len, center.y), crossCol, 1.2f);

  if (m_hitmarkerTime > 0.0f) {
    Color hitCol(255, 60, 60, 240);
    float hDist = 9.0f;
    c.line(Vec2(center.x - hDist, center.y - hDist), Vec2(center.x - 3, center.y - 3), hitCol, 2.0f);
    c.line(Vec2(center.x + hDist, center.y - hDist), Vec2(center.x + 3, center.y - 3), hitCol, 2.0f);
    c.line(Vec2(center.x - hDist, center.y + hDist), Vec2(center.x - 3, center.y + 3), hitCol, 2.0f);
    c.line(Vec2(center.x + hDist, center.y + hDist), Vec2(center.x + 3, center.y + 3), hitCol, 2.0f);
  }
}

void GameHud::drawStatusBars(Canvas& c, Rect r) {
  const Theme& t = theme();
  c.fillRoundRect(r, 4.0f, Color(12, 16, 24, 210));
  c.strokeRoundRect(r, 4.0f, Color(45, 65, 95, 180), 1.0f);

  float padX = 12.0f;
  float barW = r.w - padX * 2.0f;

  // Header Title: Operator / Combat Profile
  c.textIn(*t.small, Rect(r.x + padX, r.y + 5.0f, barW, 12.0f), "PRISM // TACTICAL STATUS", Color(120, 185, 255), HAlign::Left);

  // 1. Shield Bar (Segmented Armor Pips)
  Rect shieldBg{r.x + padX, r.y + 20.0f, barW, 7.0f};
  c.fillRect(shieldBg, Color(18, 25, 38));
  int numPips = 8;
  float pipGap = 2.0f;
  float pipW = (barW - float(numPips - 1) * pipGap) / float(numPips);
  float shieldFrac = m_maxShield > 0.0f ? std::clamp(m_shield / m_maxShield, 0.0f, 1.0f) : 0.0f;
  int activePips = int(std::round(shieldFrac * float(numPips)));

  for (int p = 0; p < numPips; p++) {
    Rect pipRect{shieldBg.x + float(p) * (pipW + pipGap), shieldBg.y, pipW, shieldBg.h};
    Color pipCol = (p < activePips) ? Color(60, 195, 255) : Color(25, 38, 55);
    c.fillRect(pipRect, pipCol);
  }

  // 2. Health Bar (Angled Combat HP)
  Rect healthBg{r.x + padX, r.y + 31.0f, barW, 10.0f};
  c.fillRoundRect(healthBg, 3.0f, Color(30, 20, 22));
  float hpFrac = m_maxHealth > 0.0f ? std::clamp(m_health / m_maxHealth, 0.0f, 1.0f) : 0.0f;
  if (hpFrac > 0.0f) {
    Rect healthFill{healthBg.x, healthBg.y, barW * hpFrac, 10.0f};
    Color hpCol = hpFrac > 0.5f ? Color(55, 210, 110) : (hpFrac > 0.25f ? Color(235, 180, 45) : Color(240, 50, 50));
    c.fillRoundRect(healthFill, 3.0f, hpCol.darken(0.1f));
  }

  // HP numerical readout
  char hpStr[32];
  std::snprintf(hpStr, sizeof(hpStr), "HP %.0f / %.0f", m_health, m_maxHealth);
  c.textIn(*t.small, Rect(r.x + padX, r.y + 44.0f, barW, 12.0f), hpStr, Color(210, 225, 245), HAlign::Right);
}

void GameHud::drawActionPrompt(Canvas& c, Rect r) {
  const Theme& t = theme();
  c.fillRoundRect(r, 4.0f, Color(12, 16, 24, 215));
  c.strokeRoundRect(r, 4.0f, Color(60, 100, 160, 200), 1.0f);

  // Keycap box (flat, matte)
  Rect keyBox{r.x + 8.0f, r.y + 4.0f, 24.0f, 24.0f};
  c.fillRoundRect(keyBox, 4.0f, Color(32, 44, 66));
  c.strokeRoundRect(keyBox, 4.0f, Color(80, 120, 180), 1.0f);
  c.textIn(*t.medium, keyBox, m_promptKey, Color(255, 255, 255), HAlign::Center);

  // Prompt text
  Rect textBox{keyBox.right() + 8.0f, r.y, r.w - keyBox.w - 20.0f, r.h};
  c.textIn(*t.ui, textBox, m_promptText, Color(245, 245, 250), HAlign::Left);
}

void GameHud::paint(Canvas& c) {
  const Theme& t = theme();

  // Top Diagnostic Banner
  c.textIn(*t.small, Rect(m_rect.x + m_safeZone.l, m_rect.y + m_safeZone.t + 4.0f, 220.0f, 16.0f),
           "PRISM GAME HUD | 60 FPS", Color(140, 200, 255, 200), HAlign::Left);

  // 1. Compass Strip
  if (m_showCompass) {
    Rect compassRect{m_rect.x + (m_rect.w - 320.0f) * 0.5f, m_rect.y + m_safeZone.t, 320.0f, 28.0f};
    drawCompass(c, compassRect);
  }

  // 2. Crosshair
  if (m_showCrosshair) {
    drawCrosshair(c, m_rect.center());
  }

  // 3. Status Bars
  if (m_showStatus) {
    Rect statusRect{m_rect.x + m_safeZone.l, m_rect.bottom() - m_safeZone.b - 48.0f, 220.0f, 48.0f};
    drawStatusBars(c, statusRect);
  }

  // 4. Action Prompt
  if (m_hasPrompt) {
    Rect promptRect{m_rect.x + (m_rect.w - 220.0f) * 0.5f, m_rect.bottom() - m_safeZone.b - 50.0f, 220.0f, 32.0f};
    drawActionPrompt(c, promptRect);
  }

  // Paint Exit Button
  m_exitButton->paint(c);
}

Widget* GameHud::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (Widget* w = m_exitButton->hitTest(p)) return w;
  return nullptr;
}

bool GameHud::onKeyDown(const KeyEvent& e) {
  if (e.key == Key::Escape) {
    if (onExitGameMode) onExitGameMode();
    return true;
  }
  return false;
}

} // namespace prism
