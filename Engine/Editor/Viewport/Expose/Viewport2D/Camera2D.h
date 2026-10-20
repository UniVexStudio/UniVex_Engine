#pragma once
#include "IViewportRenderer.h"

namespace univex::viewport {

struct Rect {
    Vec2 min, max;
    bool contains(Vec2 p) const { return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }
};

// Orthographic 2D camera. zoom = screen pixels per world unit.
// 2D-editor convention by default: +X right, +Y DOWN (like Godot / UI space).
class Camera2D {
public:
    static constexpr float kMinZoom = 0.01f;
    static constexpr float kMaxZoom = 10000.f;

    void setViewportSize(int w, int h);
    int  widthPx()  const { return m_w; }
    int  heightPx() const { return m_h; }

    void  setYUp(bool v) { m_yUp = v; }
    bool  yUp() const { return m_yUp; }

    Vec2  center() const { return m_center; }
    void  setCenter(Vec2 c) { m_center = c; }
    float zoom() const { return m_zoom; }
    void  setZoom(float z);

    Vec2 worldToScreen(Vec2 w) const;
    Vec2 screenToWorld(Vec2 s) const;
    Rect visibleWorldRect() const;

    void panByScreenDelta(Vec2 deltaPx);
    void zoomAt(Vec2 screenPoint, float factor);
    void fitRect(const Rect& r, float marginPx = 24.f);
    // Place the camera so that `world` appears exactly at `screen`.
    void setCenterFromAnchor(Vec2 world, Vec2 screen);
    // Make the world origin land on a whole screen pixel (crisp pixel art at 1:1, 2:1...).
    void alignToPixels();

private:
    float ySign() const { return m_yUp ? -1.f : 1.f; }
    int   m_w = 1, m_h = 1;
    Vec2  m_center{0.f, 0.f};
    float m_zoom = 1.f;
    bool  m_yUp = false;
};

} // namespace univex::viewport
