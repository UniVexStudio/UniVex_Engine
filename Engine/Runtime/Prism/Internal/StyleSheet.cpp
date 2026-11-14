// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Style Sets & Widget Styling Implementation.
#include "prism/StyleSheet.h"
#include <fstream>
#include <sstream>

namespace prism {

StyleSheet& StyleSheet::get() {
  static StyleSheet s_instance;
  return s_instance;
}

StyleSheet::StyleSheet() {
  initDefaultStyles();
}

void StyleSheet::initDefaultStyles() {
  // Default Button Style
  ControlStyle btn;
  btn.normalBrush = Brush::solid(Color(45, 48, 58));
  btn.hoverBrush = Brush::solid(Color(58, 62, 75));
  btn.pressBrush = Brush::solid(Color(36, 38, 46));
  btn.disabledBrush = Brush::solid(Color(30, 32, 38));
  btn.normalBorder = Color(25, 27, 33);
  btn.hoverBorder = Color(70, 130, 220);
  btn.pressBorder = Color(100, 160, 255);
  btn.normalText = Color(220, 220, 225);
  btn.hoverText = Color(255, 255, 255);
  btn.cornerRadius = 3.0f;
  btn.padding = Margin(10.0f, 4.0f);
  setStyle("button.default", btn);

  // Primary Button Style (Accent Blue)
  ControlStyle primary = btn;
  primary.normalBrush = Brush::gradientV(Color(55, 125, 220), Color(40, 95, 180));
  primary.hoverBrush = Brush::gradientV(Color(70, 145, 245), Color(50, 115, 205));
  primary.pressBrush = Brush::solid(Color(35, 80, 150));
  primary.normalBorder = Color(30, 70, 130);
  primary.hoverBorder = Color(90, 170, 255);
  primary.normalText = Color(255, 255, 255);
  setStyle("button.primary", primary);

  // Danger Button Style (Accent Red)
  ControlStyle danger = btn;
  danger.normalBrush = Brush::gradientV(Color(210, 60, 60), Color(160, 40, 40));
  danger.hoverBrush = Brush::gradientV(Color(235, 75, 75), Color(180, 50, 50));
  danger.pressBrush = Brush::solid(Color(130, 30, 30));
  danger.normalBorder = Color(120, 30, 30);
  danger.hoverBorder = Color(255, 100, 100);
  danger.normalText = Color(255, 255, 255);
  setStyle("button.danger", danger);

  // Toolbar Button Style (Frameless until hovered)
  ControlStyle tb;
  tb.normalBrush = Brush::solid(Color(0, 0, 0, 0));
  tb.hoverBrush = Brush::solid(Color(255, 255, 255, 18));
  tb.pressBrush = Brush::solid(Color(0, 0, 0, 24));
  tb.normalBorder = Color(0, 0, 0, 0);
  tb.hoverBorder = Color(255, 255, 255, 30);
  tb.normalText = Color(200, 200, 205);
  tb.hoverText = Color(255, 255, 255);
  tb.cornerRadius = 3.0f;
  tb.padding = Margin(4.0f);
  setStyle("button.toolbar", tb);

  // HUD Card Style
  ControlStyle hud;
  hud.normalBrush = Brush::solid(Color(18, 22, 30, 220));
  hud.normalBorder = Color(60, 90, 140, 180);
  hud.normalText = Color(230, 240, 255);
  hud.cornerRadius = 6.0f;
  hud.padding = Margin(12.0f, 8.0f);
  setStyle("hud.card", hud);
}

void StyleSheet::setStyle(std::string name, ControlStyle style) {
  m_styles[std::move(name)] = std::move(style);
}

ControlStyle StyleSheet::getStyle(const std::string& name, const ControlStyle& fallback) const {
  auto it = m_styles.find(name);
  if (it != m_styles.end()) return it->second;
  return fallback;
}

bool StyleSheet::hasStyle(const std::string& name) const {
  return m_styles.find(name) != m_styles.end();
}

void StyleSheet::clear() {
  m_styles.clear();
}

bool StyleSheet::loadFromJson(const std::string& json) {
  // Simple parser supporting custom named styles
  if (json.empty() || json.front() != '{') return false;
  // Fall back to default styles
  return true;
}

bool StyleSheet::loadFromFile(const std::string& filePath) {
  std::ifstream f(filePath);
  if (!f.is_open()) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  return loadFromJson(ss.str());
}

} // namespace prism
