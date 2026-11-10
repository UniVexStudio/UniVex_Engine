// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Runtime UI & Game HUD Profile.
// Provides game HUD overlays (Compass strip, Crosshair, Health/Shield bars, Action prompts),
// directional focus navigation for gamepads/keyboards, and safe-zone constraints.
#pragma once
#include "Widget.h"
#include "Controls.h"
#include <string>
#include <vector>

namespace prism {

enum class FocusDirection : uint8_t {
  Up,
  Down,
  Left,
  Right
};

class FocusNavigation {
public:
  static Widget* findNextFocus(Widget* root, Widget* current, FocusDirection dir);
};

class GameHud : public Widget {
public:
  GameHud();

  void setHealth(float current, float max);
  void setShield(float current, float max);
  void setYaw(float yawRad);
  void setActionPrompt(std::string key, std::string text);
  void clearActionPrompt();
  void triggerHitmarker();

  void setCrosshairVisible(bool v) { m_showCrosshair = v; invalidate(); }
  void setCompassVisible(bool v) { m_showCompass = v; invalidate(); }
  void setStatusVisible(bool v) { m_showStatus = v; invalidate(); }

  Callback<> onExitGameMode;

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onKeyDown(const KeyEvent& e) override;
  bool consumesHostInput() const override { return false; }

private:
  void drawCompass(Canvas& c, Rect r);
  void drawCrosshair(Canvas& c, Vec2 center);
  void drawStatusBars(Canvas& c, Rect r);
  void drawActionPrompt(Canvas& c, Rect r);

  float m_health = 85.0f;
  float m_maxHealth = 100.0f;
  float m_shield = 60.0f;
  float m_maxShield = 100.0f;
  float m_yaw = 0.0f;

  bool m_showCrosshair = true;
  bool m_showCompass = true;
  bool m_showStatus = true;

  std::string m_promptKey = "E";
  std::string m_promptText = "Examine Object";
  bool m_hasPrompt = true;

  float m_hitmarkerTime = 0.0f;

  Margin m_safeZone{32.0f, 24.0f};
  Ref<Button> m_exitButton;
};

} // namespace prism
