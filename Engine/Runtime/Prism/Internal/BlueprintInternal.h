// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - internal helpers shared by the BlueprintGraph translation units (not installed).
#pragma once
#include "prism/BlueprintGraph.h"
#include "prism/Font.h"
#include "prism/Theme.h"

namespace prism::bp {

// ---- layout metrics in world units (zoom 1.0)
constexpr float kRowH = 24.f;
constexpr float kPinX = 14.f;         // pin centre from the node edge
constexpr float kPinR = 5.5f;
constexpr float kHeaderTall = 44.f;   // title + subtitle
constexpr float kHeaderShort = 30.f;
constexpr float kBodyPad = 8.f;
constexpr float kPreviewH = 46.f;     // material preview band (colour swatch / value ramp / texture)
constexpr float kRadius = 6.f;
constexpr float kLabelPx = 13.f, kTitlePx = 14.f, kSubPx = 10.f, kBigPx = 22.f;

enum class Weight { Regular, Medium, Semibold };

// Font of `basePx * zoom` pixels. Cached; falls back to the theme UI font if the asset is missing.
const Font& font(float basePx, Weight w, float zoom);
float textWidth(std::string_view s, float basePx, Weight w);

// Colours of the node chrome.
struct Chrome { Color left, right, body, bodyDark, sub; };
Chrome chromeFor(BlueprintNodeCategory c);

inline bool isExec(const BlueprintPin& p) { return p.type == BlueprintPinType::Exec; }

} // namespace prism::bp
