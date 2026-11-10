// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/EngineView.h"

namespace prism {

Vec2 EngineView::measure(Vec2) { return m_desired = constrain(preferredSize); }

void EngineView::arrange(Rect r) {
  Widget::arrange(r);
  if (r.x != m_last.x || r.y != m_last.y || r.w != m_last.w || r.h != m_last.h) {
    m_last = r;
    if (onRectChanged) onRectChanged(r);
  }
}

void EngineView::paint(Canvas& c) {
  if (mode == Mode::Hole) { c.fillRaw(m_rect, 0x00000000); return; }
  c.fillRect(m_rect, background);
  if (!source) return;
  PixelView v = source({m_rect.w, m_rect.h});
  if (v) c.blitScaled(v.data, v.width, v.height, v.stride, m_rect);
}

void EngineView::forward(EngineInput::Kind k, MouseEvent e) {
  if (!onInput) return;
  e.pos = e.pos - Vec2(m_rect.x, m_rect.y);
  EngineInput in; in.kind = k; in.mouse = e;
  onInput(in);
}

bool EngineView::onMouseDown(const MouseEvent& e) { if (!onInput) return false; forward(EngineInput::Kind::MouseDown, e); return true; }
bool EngineView::onMouseUp(const MouseEvent& e) { if (!onInput) return false; forward(EngineInput::Kind::MouseUp, e); return true; }
bool EngineView::onMouseMove(const MouseEvent& e) { if (!onInput) return false; forward(EngineInput::Kind::MouseMove, e); return true; }
bool EngineView::onMouseWheel(const MouseEvent& e) { if (!onInput) return false; forward(EngineInput::Kind::Wheel, e); return true; }

bool EngineView::onKeyDown(const KeyEvent& e) {
  if (!onInput) return false;
  EngineInput in; in.kind = EngineInput::Kind::Key; in.key = e;
  onInput(in);
  return true;
}

void EngineView::onFocusChanged(bool f) {
  if (!onInput) return;
  EngineInput in; in.kind = f ? EngineInput::Kind::FocusGained : EngineInput::Kind::FocusLost;
  onInput(in);
}

} // namespace prism
