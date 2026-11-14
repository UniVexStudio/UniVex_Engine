// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Profiler: frame-time history, per-frame timeline (flame graph over CPU / Render / GPU tracks)
// and a hot-scope table. Feed it with pushFrame(); it owns no timers and no engine hooks.
#pragma once
#include "Widget.h"
#include <string>
#include <vector>

namespace prism {

struct ProfileScope {
  std::string name;
  int track = 0;          // 0 = CPU main, 1 = Render thread, 2 = GPU
  int depth = 0;          // nesting level inside its track
  float startMs = 0;      // relative to frame start
  float durMs = 0;
};

struct ProfileFrame {
  float cpuMs = 0;
  float gpuMs = 0;
  int drawCalls = 0;
  int triangles = 0;
  std::vector<ProfileScope> scopes;
};

class Profiler : public Widget {
public:
  Profiler();
  void pushFrame(ProfileFrame f);          // ignored while paused
  void clear();
  void setPaused(bool p);
  bool paused() const { return m_paused; }
  void setTargetMs(float ms) { m_targetMs = ms; invalidate(); }
  float targetMs() const { return m_targetMs; }
  size_t frameCount() const { return m_frames.size(); }
  int selectedFrame() const { return m_selected; }   // -1 = follow newest
  void selectFrame(int i);
  const ProfileFrame* shownFrame() const;
  std::string hoveredScope() const { return m_hoverScope; }
  static constexpr size_t kHistory = 240;

  Vec2 measure(Vec2 avail) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  void onMouseLeave() override;
  const std::string& tooltipAt(Vec2 p) const override;

private:
  struct Layout { Rect bar, cards, graph, timeline, table; };
  Layout layout() const;
  Rect recordRect() const; Rect clearRect() const; Rect t60Rect() const; Rect t30Rect() const;
  int frameAt(Vec2 p) const;
  void paintTimeline(Canvas& c, Rect r, const ProfileFrame& f);
  void paintTable(Canvas& c, Rect r, const ProfileFrame& f);
  static Color scopeColor(const std::string& name);

  std::vector<ProfileFrame> m_frames;
  int m_selected = -1;
  bool m_paused = false;
  float m_targetMs = 16.667f;
  float m_zoom = 1.0f, m_pan = 0.0f;       // timeline view (zoom >= 1, pan in ms-fraction)
  bool m_panning = false, m_scrub = false;
  float m_panGrabX = 0, m_panStart = 0;
  int m_hot = 0;                           // toolbar hover id
  std::string m_hoverScope;
  mutable std::string m_tip;
  Vec2 m_mouse;
};

} // namespace prism
