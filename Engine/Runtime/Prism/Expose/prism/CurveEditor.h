// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Curve Editor & Keyframe Timeline.
// Interactive cubic Bézier animation curve editor for properties and cinematics.
#pragma once
#include "Widget.h"
#include "Transaction.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace prism {

enum class TangentMode : uint8_t {
  Linear,
  Smooth,
  Step
};

struct CurveKeyframe {
  float time = 0.0f;
  float value = 0.0f;
  TangentMode mode = TangentMode::Smooth;
  float inTangent = 0.0f;
  float outTangent = 0.0f;

  bool operator<(const CurveKeyframe& o) const { return time < o.time; }
};

struct CurveChannel {
  std::string name;
  Color color;
  bool visible = true;
};

class CurveEditor : public Widget {
public:
  CurveEditor();

  void addKeyframe(float time, float value, TangentMode mode = TangentMode::Smooth);
  void clearKeyframes();
  const std::vector<CurveKeyframe>& keyframes() const { return m_keys; }

  // Evaluate the curve at time t in [0, 1]
  float evaluate(float t) const;

  float playhead() const { return m_playheadTime; }
  void setPlayhead(float t) { m_playheadTime = std::clamp(t, 0.0f, 1.0f); invalidate(); }
  int activeChannel() const { return m_activeChannel; }

  void setTransactionService(TransactionService* tx) { m_tx = tx; }

  Callback<float> onCurveModified;

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
  bool focusable() const override { return true; }

private:
  Vec2 toScreen(float time, float val) const;
  std::pair<float, float> toCurve(Vec2 screenPos) const;
  int findKeyAt(Vec2 screenPos, float radius = 8.0f) const;
  Rect plotRect() const;

  std::vector<CurveChannel> m_channels;
  int m_activeChannel = 0;

  std::vector<CurveKeyframe> m_keys;
  int m_selectedKey = -1;
  int m_dragKey = -1;
  int m_dragHandle = 0;   // 0 none, 1 in-tangent, 2 out-tangent (bezier-style handle drag)
  Vec2 m_dragStartPos;
  CurveKeyframe m_dragStartKey;

  float m_playheadTime = 0.35f;
  bool m_draggingPlayhead = false;

  TransactionService* m_tx = nullptr;

  float m_trackListW = 160.0f;
  float m_toolbarH = 28.0f;
  float m_marginL = 40.0f;
  float m_marginR = 20.0f;
  float m_marginT = 16.0f;
  float m_marginB = 26.0f;
};

} // namespace prism
