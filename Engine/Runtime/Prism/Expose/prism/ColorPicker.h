// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Color picker: square or wheel chromatic plane, RGB / HSV / Intensity (HDR) / Alpha channels,
// sRGB <-> Linear display, hex entry, old/new comparison, swatches + recent colours, and a floating,
// draggable, non-modal window (showColorPickerWindow).
#pragma once
#include "Widget.h"
#include "Controls.h"
#include "TextInput.h"
#include <array>
#include <vector>

namespace prism {

float srgbToLinear(float c);   // c in [0,1]
float linearToSrgb(float c);

struct HsvColor {
  float h = 0.0f; // [0, 360)
  float s = 1.0f; // [0, 1]
  float v = 1.0f; // [0, 1]
  float a = 1.0f; // [0, 1]

  static HsvColor fromRgb(Color c);
  Color toRgb() const;
};

// Linear-light colour scaled by intensity (what a renderer / light component consumes).
struct ColorHdr { float r = 1, g = 1, b = 1, a = 1; };

enum class ColorPickerMode {
  Square,   // saturation/value square + hue strip
  Wheel     // hue/saturation disc + value strip
};

enum class ColorChannel { R, G, B, H, S, V, I, A, Count };

class ColorPicker : public Widget {
public:
  explicit ColorPicker(Color initialColor = Color(75, 140, 240));

  void setColor(Color c, bool notify = true);
  Color color() const { return m_color; }
  Color initialColor() const { return m_initialColor; }

  // Intensity is an HDR multiplier on the linear colour (1 = LDR). Does not change color().
  void setIntensity(float i, bool notify = true);
  float intensity() const { return m_intensity; }
  ColorHdr hdr() const;

  void setMode(ColorPickerMode mode) { m_mode = mode; invalidate(); }
  ColorPickerMode mode() const { return m_mode; }

  // Linear = the R/G/B/A fields and sliders show linear-light values instead of sRGB 0..255.
  void setLinear(bool linear);
  bool isLinear() const { return m_isLinear; }

  // Channel access in display units (R/G/B/A: 0..255 or 0..1 when linear, H: degrees, S/V: percent, I: multiplier).
  float channel(ColorChannel ch) const;
  void setChannel(ColorChannel ch, float displayValue, bool notify = true);

  static void addRecent(Color c);                       // process-wide recent colours (most recent first)
  static const std::vector<Color>& recent();

  Callback<Color> onColorChanged;
  Callback<ColorHdr> onHdrChanged;

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  Cursor cursor() const override { return Cursor::Arrow; }
  bool focusable() const override { return true; }

private:
  static constexpr int kCh = int(ColorChannel::Count);
  float norm(ColorChannel ch) const;            // slider position 0..1
  void setNorm(ColorChannel ch, float n);
  std::string text(ColorChannel ch) const;
  void applyText(ColorChannel ch, const std::string& s);
  Color trackColor(ColorChannel ch, float n) const;
  void rgbChanged();                            // m_rgb edited -> refresh HSV (keeps hue on greys)
  void hsvChanged();                            // m_hsv edited -> refresh RGB
  void commit(bool notify = true);
  void syncFields();
  void dragTo(Vec2 p);
  Color rebuild() const;

  Color m_color, m_initialColor;
  std::array<float, 3> m_rgb{};                 // sRGB 0..1
  HsvColor m_hsv;
  float m_intensity = 1.0f, m_initialIntensity = 1.0f;
  ColorPickerMode m_mode = ColorPickerMode::Square;
  bool m_isLinear = false;

  enum class Drag { None, Plane, Strip, Alpha, Track };
  Drag m_drag = Drag::None;
  int m_dragCh = 0;

  Rect m_plane, m_strip, m_alphaStrip, m_preview, m_newHalf, m_oldHalf;
  Rect m_chipSquare, m_chipWheel, m_chipSrgb, m_chipLinear;
  struct Row { Rect label, track; Ref<TextInput> field; };
  std::array<Row, kCh> m_rows;
  Ref<TextInput> m_hex;
  std::vector<Color> m_swatches;
  std::vector<std::pair<Rect, Color>> m_swatchHits;
  bool m_showRecent = false;
};

// Floating, draggable colour window (non-modal). Live changes stream to onLiveChange; Cancel / close restores
// the initial colour through onLiveChange and calls onCancel; OK calls onApply and records a recent colour.
void showColorPickerWindow(Color initialColor,
                           Callback<Color> onApply,
                           Callback<> onCancel = nullptr,
                           Callback<Color> onLiveChange = nullptr);

} // namespace prism
