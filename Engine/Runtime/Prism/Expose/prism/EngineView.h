// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - EngineView: a rectangle that belongs to the host engine (a 3D viewport, a game view, a video).
//
//  Image mode  the host supplies pixels (CPU path); PRISM scales them into the rectangle.
//  Hole mode   PRISM clears the rectangle to transparent; the host draws its GPU render target there and
//              composites the UI frame on top (UI frame is ARGB with straight alpha).
//
// Input: the view never swallows events silently. If `onInput` is set it forwards mouse/keyboard events to the
// host (with positions relative to the view); otherwise the host should check Application::wantsPointerAt().
#pragma once
#include "Widget.h"
#include "Platform.h"
#include <functional>

namespace prism {

struct EngineInput {
  enum class Kind { MouseDown, MouseUp, MouseMove, Wheel, Key, FocusGained, FocusLost } kind = Kind::MouseMove;
  MouseEvent mouse; // pos is relative to the view's top-left
  KeyEvent key;
};

class EngineView : public Widget {
public:
  enum class Mode { Image, Hole };

  Mode mode = Mode::Image;
  Vec2 preferredSize{640, 360};
  Color background = Color(0, 0, 0, 255);

  // Image mode: called each paint with the current view size; return the engine frame (empty = show background).
  std::function<PixelView(Vec2 size)> source;
  // Called whenever the view's pixel rectangle changes (resize the engine render target here).
  std::function<void(Rect)> onRectChanged;
  // Optional input forwarding to the engine.
  std::function<void(const EngineInput&)> onInput;

  Vec2 measure(Vec2) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool consumesHostInput() const override { return false; }
  bool focusable() const override { return onInput != nullptr; }
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
  void onFocusChanged(bool f) override;
  Cursor cursor() const override { return Cursor::Arrow; }

private:
  void forward(EngineInput::Kind k, MouseEvent e);
  Rect m_last;
};

} // namespace prism
