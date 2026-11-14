// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - design tokens: colours, metrics and fonts. One dark theme ships by default.
#pragma once
#include "Core.h"
#include "Font.h"
#include <vector>

namespace prism {

struct Theme {
  // Surfaces (darkest to lightest)
  Color window, panel, header, control, controlHover, controlPress, field;
  // Lines
  Color border, borderSoft, borderStrong;
  // Text
  Color text, textDim, textDisabled, textBright;
  // Accent & states
  Color accent, accentHover, accentDim, accentText;
  Color rowHover, rowSelected, rowSelectedInactive;
  Color success, warning, error, info;
  Color axisX, axisY, axisZ;
  // Special surfaces
  Color logBg, statusBar;

  // Metrics
  float controlH = 22;     // standard control height
  float rowH = 22;         // list / tree row
  float menuBarH = 28;
  float toolbarH = 36;
  float tabH = 28;
  float statusH = 22;
  float pad = 8;           // panel padding
  float gap = 4;           // gap between related controls
  float radius = 3;
  float splitter = 4;      // gap between docked panels
  float gloss = 0.3f;      // 0 = matte, flat chrome ... 1 = glossy highlights / sheens / specular (thumbnails, node headers)

  // Fonts
  Ref<Font> ui, medium, semibold, small, mono, monoLarge;

  static Theme dark();
  static Theme light();
  void loadFonts(const std::string& assetDir);
  std::string fontDir;   // asset directory the fonts were loaded from (lets widgets create extra sizes)

  // Copies colours and metrics from `o`, keeping this theme's fonts (used for live theme switching).
  void adoptStyle(const Theme& o);
  // Applies a flat JSON theme: {"base":"dark|light", "accent":"#3b8eea", "text":"#cfcfd1", "radius":3, ...}.
  // Returns false (with a message in *err) on malformed input; unknown keys are reported in *err but accepted.
  bool applyJson(const std::string& json, std::string* err = nullptr);

  struct ColorToken { const char* name; Color Theme::* member; };
  struct MetricToken { const char* name; float Theme::* member; };
  static const std::vector<ColorToken>& colorTokens();
  static const std::vector<MetricToken>& metricTokens();
};

const Theme& theme();
Theme& themeMutable();

// A colour that is either fixed or resolved from the current theme at paint time, so widgets follow
// live theme changes: `border.setBackground(&Theme::panel)`.
struct ThemeColor {
  Color Theme::* role = nullptr;
  Color fixed{0, 0, 0, 0};
  ThemeColor() = default;
  ThemeColor(Color c) : fixed(c) {}
  ThemeColor(Color Theme::* r) : role(r) {}
  Color get() const { return role ? theme().*role : fixed; }
};

} // namespace prism
