// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Style Sets & Widget Styling.
// Provides widget style structs, state brushes, style dictionaries, and JSON loaders.
#pragma once
#include "Brush.h"
#include "Core.h"
#include <string>
#include <unordered_map>

namespace prism {

struct ControlStyle {
  Brush normalBrush;
  Brush hoverBrush;
  Brush pressBrush;
  Brush disabledBrush;

  Color normalText = Color(220, 220, 222);
  Color hoverText = Color(255, 255, 255);
  Color pressText = Color(240, 240, 240);
  Color disabledText = Color(110, 110, 115);

  Color normalBorder = Color(35, 38, 48);
  Color hoverBorder = Color(70, 130, 220);
  Color pressBorder = Color(100, 160, 255);

  float cornerRadius = 4.0f;
  Margin padding{8.0f, 4.0f};
  float borderWidth = 1.0f;
};

class StyleSheet {
public:
  static StyleSheet& get();

  void setStyle(std::string name, ControlStyle style);
  ControlStyle getStyle(const std::string& name, const ControlStyle& fallback = {}) const;
  bool hasStyle(const std::string& name) const;
  void clear();

  // Load style definitions from JSON
  bool loadFromJson(const std::string& json);
  bool loadFromFile(const std::string& filePath);

private:
  StyleSheet();
  void initDefaultStyles();

  std::unordered_map<std::string, ControlStyle> m_styles;
};

} // namespace prism
