#include "Viewport2D/Viewport2D.h"
#include "Viewport2D/DrawUtil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace univex::viewport {
namespace {
constexpr float kClickSlopPx = 4.f;
constexpr float kGuideHitPx = 4.f;
constexpr float kSmoothRate = 18.f;   // 1/s, exponential approach
constexpr float kPi = 3.14159265358979f;
}

// ------------------------------------------------------------------ regions
int Viewport2D::rulerRegion(Vec2 p) const {
    if (!m_style.showRulers) return 0;
    const bool top = p.y < m_style.rulerPx, left = p.x < m_style.rulerPx;
    if (top && left) return 3;
    return top ? 1 : (left ? 2 : 0);
}

bool Viewport2D::inCanvasRegion(Vec2 p) const {
    return rulerRegion(p) == 0 && p.x >= 0.f && p.y >= 0.f &&
           p.x <= float(m_cam.widthPx()) && p.y <= float(m_cam.heightPx());
}

float Viewport2D::snapGuideValue(float v) const {
    if (m_snap.grid)  return snapToStep(v, m_snap.gridStep);
    if (m_snap.pixel) return std::round(v);
    return v;
}

// -------------------------------------------------------------------- input
void Viewport2D::onMouseDown(MouseButton b, Vec2 pos) {
    m_last = pos; m_downPos = pos;
    if (m_state != State::Idle) return;
    m_activeButton = b;

    const bool panButton = b == MouseButton::Middle ||
                           (b == MouseButton::Left && (m_tool == Tool::Hand || m_panModifier));
    if (panButton) { m_state = State::Panning; m_anim = Anim::None; return; }
    if (b != MouseButton::Left) return;

    const int reg = rulerRegion(pos);
    if (reg == 3) return;
    if (reg == 1 || reg == 2) {                       // drag a new guide out of a ruler
        m_state = State::GuideDrag; m_guideNew = true; m_guideHorizontal = (reg == 1);
        const Vec2 w = m_cam.screenToWorld(pos);
        m_guidePos = snapGuideValue(m_guideHorizontal ? w.y : w.x);
        return;
    }
    if (m_gizmo.hasBox()) {
        const Handle h = m_gizmo.hitTest(pos, m_cam);
        if (h.kind != Handle::Kind::None) {
            m_gizmo.beginDrag(h, pos, m_cam);
            m_state = State::GizmoDrag;
            if (m_onTransform) m_onTransform(m_gizmo.box(), Phase::Begin);
            return;
        }
    }
    if (m_style.showGuides) {                         // grab an existing guide
        for (int i = 0; i < int(m_guides.horizontal.size()); ++i) {
            const float sy = m_cam.worldToScreen({0.f, m_guides.horizontal[i]}).y;
            if (std::fabs(sy - pos.y) <= kGuideHitPx) {
                m_state = State::GuideDrag; m_guideNew = false; m_guideHorizontal = true; m_guideIndex = i; return;
            }
        }
        for (int i = 0; i < int(m_guides.vertical.size()); ++i) {
            const float sx = m_cam.worldToScreen({m_guides.vertical[i], 0.f}).x;
            if (std::fabs(sx - pos.x) <= kGuideHitPx) {
                m_state = State::GuideDrag; m_guideNew = false; m_guideHorizontal = false; m_guideIndex = i; return;
            }
        }
    }
    m_state = State::PendingEmpty;
}

void Viewport2D::onMouseMove(Vec2 pos) {
    m_cursorWorld = m_cam.screenToWorld(pos);
    m_hasCursor = true;
    switch (m_state) {
    case State::Panning:
        m_cam.panByScreenDelta(pos - m_last);
        break;
    case State::GuideDrag: {
        const Vec2 w = m_cursorWorld;
        const float v = snapGuideValue(m_guideHorizontal ? w.y : w.x);
        if (m_guideNew) m_guidePos = v;
        else (m_guideHorizontal ? m_guides.horizontal : m_guides.vertical)[m_guideIndex] = v;
        break;
    }
    case State::GizmoDrag: {
        const Box nb = m_gizmo.updateDrag(pos, m_cam, m_snap, m_guides, dragMods());
        if (m_onTransform) m_onTransform(nb, Phase::Update);
        break;
    }
    case State::PendingEmpty: {
        const Vec2 d = pos - m_downPos;
        if (d.x * d.x + d.y * d.y <= kClickSlopPx * kClickSlopPx) break;
        m_state = State::Marquee;
        m_marqueeStart = m_cam.screenToWorld(m_downPos);
        m_marqueeEnd = m_cursorWorld;
        if (m_onMarquee) m_onMarquee({{std::min(m_marqueeStart.x, m_marqueeEnd.x), std::min(m_marqueeStart.y, m_marqueeEnd.y)},
                                      {std::max(m_marqueeStart.x, m_marqueeEnd.x), std::max(m_marqueeStart.y, m_marqueeEnd.y)}},
                                     m_mods.shift, Phase::Begin);
        break;
    }
    case State::Marquee:
        m_marqueeEnd = m_cursorWorld;
        if (m_onMarquee) m_onMarquee({{std::min(m_marqueeStart.x, m_marqueeEnd.x), std::min(m_marqueeStart.y, m_marqueeEnd.y)},
                                      {std::max(m_marqueeStart.x, m_marqueeEnd.x), std::max(m_marqueeStart.y, m_marqueeEnd.y)}},
                                     m_mods.shift, Phase::Update);
        break;
    case State::Idle:
        m_hover = (m_tool == Tool::Select && !m_panModifier && m_gizmo.hasBox() && inCanvasRegion(pos))
                      ? m_gizmo.hitTest(pos, m_cam) : Handle{};
        break;
    }
    m_last = pos;
}

void Viewport2D::onMouseUp(MouseButton b, Vec2 pos) {
    if (m_state == State::Idle || b != m_activeButton) return;
    switch (m_state) {
    case State::GuideDrag: {
        const bool inside = inCanvasRegion(pos);
        if (m_guideNew) {
            if (inside) (m_guideHorizontal ? m_guides.horizontal : m_guides.vertical).push_back(m_guidePos);
        } else if (!inside) {                          // dragged back onto the ruler = delete
            auto& v = m_guideHorizontal ? m_guides.horizontal : m_guides.vertical;
            v.erase(v.begin() + m_guideIndex);
        }
        break;
    }
    case State::GizmoDrag:
        m_gizmo.endDrag();
        if (m_onTransform) m_onTransform(m_gizmo.box(), Phase::End);
        break;
    case State::PendingEmpty:
        if (m_onPick) m_onPick(m_cam.screenToWorld(pos), pos, m_mods.shift);
        break;
    case State::Marquee:
        m_marqueeEnd = m_cam.screenToWorld(pos);
        if (m_onMarquee) m_onMarquee({{std::min(m_marqueeStart.x, m_marqueeEnd.x), std::min(m_marqueeStart.y, m_marqueeEnd.y)},
                                      {std::max(m_marqueeStart.x, m_marqueeEnd.x), std::max(m_marqueeStart.y, m_marqueeEnd.y)}},
                                     m_mods.shift, Phase::End);
        break;
    default: break;
    }
    m_state = State::Idle;
    m_hover = {};
}

void Viewport2D::onWheel(float notches, Vec2 pos) {
    if (notches == 0.f) return;
    const float factor = std::pow(1.1f, notches);
    if (!m_style.smoothNavigation) { m_cam.zoomAt(pos, factor); return; }
    const float base = (m_anim == Anim::Zoom) ? m_targetZoom : m_cam.zoom();
    m_targetZoom = std::clamp(base * factor, Camera2D::kMinZoom, Camera2D::kMaxZoom);
    m_anchorScreen = pos;
    m_anchorWorld = m_cam.screenToWorld(pos);
    m_anim = Anim::Zoom;
}

void Viewport2D::onScrollPan(Vec2 d) { m_anim = Anim::None; m_cam.panByScreenDelta(d); }

// --------------------------------------------------------------- navigation
void Viewport2D::startFrameAnim(Vec2 center, float zoom, bool animate) {
    zoom = std::clamp(zoom, Camera2D::kMinZoom, Camera2D::kMaxZoom);
    if (!animate || !m_style.smoothNavigation) {
        m_cam.setZoom(zoom); m_cam.setCenter(center); m_anim = Anim::None; return;
    }
    m_targetZoom = zoom; m_targetCenter = center; m_anim = Anim::Frame;
}

void Viewport2D::zoomToFit(const Rect& r, bool animate) {
    Camera2D t = m_cam; t.fitRect(r, 48.f);
    startFrameAnim(t.center(), t.zoom(), animate);
}

void Viewport2D::zoomTo100(bool animate) {
    Camera2D t = m_cam; t.setZoom(1.f); t.alignToPixels();
    startFrameAnim(t.center(), 1.f, animate);
}

void Viewport2D::focusOn(Vec2 world, bool animate) { startFrameAnim(world, m_cam.zoom(), animate); }

void Viewport2D::update(float dt) {
    if (m_anim == Anim::None || dt <= 0.f) return;
    const float a = 1.f - std::exp(-kSmoothRate * dt);
    const float z = m_cam.zoom();
    float nz = z * std::pow(m_targetZoom / z, a);
    const bool zoomDone = std::fabs(std::log(m_targetZoom / nz)) < 1e-3f;
    if (zoomDone) nz = m_targetZoom;
    m_cam.setZoom(nz);
    if (m_anim == Anim::Zoom) {
        m_cam.setCenterFromAnchor(m_anchorWorld, m_anchorScreen);
        if (zoomDone) m_anim = Anim::None;
    } else {
        const Vec2 c = m_cam.center();
        const Vec2 nc{c.x + (m_targetCenter.x - c.x) * a, c.y + (m_targetCenter.y - c.y) * a};
        const float dpx = std::max(std::fabs(m_targetCenter.x - nc.x), std::fabs(m_targetCenter.y - nc.y)) * nz;
        if (zoomDone && dpx < 0.01f) { m_cam.setCenter(m_targetCenter); m_anim = Anim::None; }
        else m_cam.setCenter(nc);
    }
}

CursorHint Viewport2D::cursorHint() const {
    if (m_state == State::Panning || m_tool == Tool::Hand || m_panModifier) return CursorHint::Pan;
    if (m_state == State::GuideDrag) return m_guideHorizontal ? CursorHint::GuideH : CursorHint::GuideV;
    if (m_state == State::Marquee || m_state == State::PendingEmpty) return CursorHint::Crosshair;
    const Handle h = (m_state == State::GizmoDrag) ? m_gizmo.dragHandle() : m_hover;
    switch (h.kind) {
    case Handle::Kind::Body:   return CursorHint::MoveAll;
    case Handle::Kind::Rotate: return CursorHint::Rotate;
    case Handle::Kind::Pivot:  return CursorHint::Crosshair;
    case Handle::Kind::Scale: {
        const float c = std::cos(m_gizmo.box().rotation), s = std::sin(m_gizmo.box().rotation);
        const float wx = float(h.hx) * c - float(h.hy) * s, wy = float(h.hx) * s + float(h.hy) * c;
        const float sy = m_cam.yUp() ? -wy : wy;
        float deg = std::atan2(sy, wx) * 180.f / kPi;
        deg = std::fmod(deg + 360.f, 180.f);
        switch (int(std::floor((deg + 22.5f) / 45.f)) % 4) {
        case 0: return CursorHint::ResizeEW;
        case 1: return CursorHint::ResizeNWSE;
        case 2: return CursorHint::ResizeNS;
        default: return CursorHint::ResizeNESW;
        }
    }
    default: break;
    }
    const int reg = rulerRegion(m_last);
    if (reg == 1) return CursorHint::GuideH;
    if (reg == 2) return CursorHint::GuideV;
    return CursorHint::Default;
}

// ---------------------------------------------------------------- rendering
void Viewport2D::drawCanvas(IViewportRenderer& r) const {
    if (!m_canvas) return;
    const Vec2 a = m_cam.worldToScreen(m_canvas->min), b = m_cam.worldToScreen(m_canvas->max);
    r.fillRect({std::min(a.x, b.x), std::min(a.y, b.y)}, {std::fabs(b.x - a.x), std::fabs(b.y - a.y)}, m_style.canvasFill);
}

void Viewport2D::drawGrid(IViewportRenderer& r) const {
    const GridLevels g = computeGridLevels(m_cam.zoom(), m_style.targetGridPx, m_style.minGridStep);
    const Rect vis = m_cam.visibleWorldRect();
    const float w = float(m_cam.widthPx()), h = float(m_cam.heightPx());

    auto pass = [&](float step, Color col, bool skipMajor) {
        for (int axis = 0; axis < 2; ++axis) {
            const float lo = axis == 0 ? vis.min.x : vis.min.y, hi = axis == 0 ? vis.max.x : vis.max.y;
            const long long i0 = (long long)std::floor(lo / step), i1 = (long long)std::ceil(hi / step);
            if (i1 - i0 > m_style.maxGridLines) continue;     // too dense: skip, never flood the backend
            for (long long i = i0; i <= i1; ++i) {
                if (skipMajor && i % 10 == 0) continue;
                const float c = float(i) * step;
                if (axis == 0) {
                    const float sx = std::round(m_cam.worldToScreen({c, 0.f}).x) + 0.5f;
                    r.drawLine({sx, 0.f}, {sx, h}, col, 1.f);
                } else {
                    const float sy = std::round(m_cam.worldToScreen({0.f, c}).y) + 0.5f;
                    r.drawLine({0.f, sy}, {w, sy}, col, 1.f);
                }
            }
        }
    };
    if (g.minorAlpha > 0.01f) pass(g.minor, withAlpha(m_style.gridMinor, g.minorAlpha), true);
    pass(g.major, m_style.gridMajor, false);
}

void Viewport2D::drawAxes(IViewportRenderer& r) const {
    const Vec2 o = m_cam.worldToScreen({0.f, 0.f});
    const float w = float(m_cam.widthPx()), h = float(m_cam.heightPx());
    if (o.y >= 0.f && o.y <= h) r.drawLine({0.f, o.y}, {w, o.y}, m_style.axisX, 1.f);
    if (o.x >= 0.f && o.x <= w) r.drawLine({o.x, 0.f}, {o.x, h}, m_style.axisY, 1.f);
}

void Viewport2D::drawGuides(IViewportRenderer& r) const {
    const float w = float(m_cam.widthPx()), h = float(m_cam.heightPx());
    for (float y : m_guides.horizontal) { const float sy = std::round(m_cam.worldToScreen({0.f, y}).y) + 0.5f; r.drawLine({0.f, sy}, {w, sy}, m_style.guide, 1.f); }
    for (float x : m_guides.vertical)   { const float sx = std::round(m_cam.worldToScreen({x, 0.f}).x) + 0.5f; r.drawLine({sx, 0.f}, {sx, h}, m_style.guide, 1.f); }
    if (m_state == State::GuideDrag && m_guideNew) {
        if (m_guideHorizontal) { const float sy = std::round(m_cam.worldToScreen({0.f, m_guidePos}).y) + 0.5f; dashedLine(r, {0.f, sy}, {w, sy}, m_style.guide, 1.f, 6.f, 4.f); }
        else                   { const float sx = std::round(m_cam.worldToScreen({m_guidePos, 0.f}).x) + 0.5f; dashedLine(r, {sx, 0.f}, {sx, h}, m_style.guide, 1.f, 6.f, 4.f); }
    }
}

void Viewport2D::drawOverlays(IViewportRenderer& r) const {
    if (m_canvas) {
        const Vec2 a = m_cam.worldToScreen(m_canvas->min), b = m_cam.worldToScreen(m_canvas->max);
        strokeRect(r, a, b, m_style.canvasBorder, 1.f);
    }
    if (m_state == State::Marquee) {
        const Vec2 a = m_cam.worldToScreen(m_marqueeStart), b = m_cam.worldToScreen(m_marqueeEnd);
        const Vec2 tl{std::min(a.x, b.x), std::min(a.y, b.y)};
        r.fillRect(tl, {std::fabs(b.x - a.x), std::fabs(b.y - a.y)}, withAlpha(m_style.marquee, 0.14f));
        strokeRect(r, a, b, m_style.marquee, 1.f);
    }
}

void Viewport2D::drawRulers(IViewportRenderer& r) const {
    const float R = m_style.rulerPx, w = float(m_cam.widthPx()), h = float(m_cam.heightPx());
    r.fillRect({0.f, 0.f}, {w, R}, m_style.rulerBg);
    r.fillRect({0.f, R}, {R, h - R}, m_style.rulerBg);

    { // top ruler (X)
        float lo = m_cam.screenToWorld({R, 0.f}).x, hi = m_cam.screenToWorld({w, 0.f}).x;
        if (lo > hi) std::swap(lo, hi);
        const RulerScale rs = computeRulerTicks(lo, hi, m_cam.zoom(), 90.f);
        for (const RulerTick& t : rs.ticks) {
            const float sx = std::round(m_cam.worldToScreen({t.value, 0.f}).x) + 0.5f;
            if (sx < R || sx > w) continue;
            r.drawLine({sx, R}, {sx, R - (t.labeled ? R * 0.65f : R * 0.28f)}, m_style.rulerTick, 1.f);
            if (t.labeled) { const std::string s = formatRulerValue(t.value, rs.decimals);
                r.drawText({sx + 3.f, 3.f}, s.c_str(), m_style.rulerText, 7.f, TextOrient::Horizontal); }
        }
    }
    { // left ruler (Y), labels rotated so they fit in a narrow strip
        float lo = m_cam.screenToWorld({0.f, R}).y, hi = m_cam.screenToWorld({0.f, h}).y;
        if (lo > hi) std::swap(lo, hi);
        const RulerScale rs = computeRulerTicks(lo, hi, m_cam.zoom(), 90.f);
        for (const RulerTick& t : rs.ticks) {
            const float sy = std::round(m_cam.worldToScreen({0.f, t.value}).y) + 0.5f;
            if (sy < R || sy > h) continue;
            r.drawLine({R, sy}, {R - (t.labeled ? R * 0.65f : R * 0.28f), sy}, m_style.rulerTick, 1.f);
            if (t.labeled) { const std::string s = formatRulerValue(t.value, rs.decimals);
                r.drawText({4.f, sy - 3.f}, s.c_str(), m_style.rulerText, 7.f, TextOrient::VerticalUp); }
        }
    }
    r.fillRect({0.f, 0.f}, {R, R}, m_style.rulerBg);
    r.drawLine({R, 0.f}, {R, h}, m_style.rulerTick, 1.f);
    r.drawLine({0.f, R}, {w, R}, m_style.rulerTick, 1.f);
    if (m_hasCursor) {   // live cursor marker on both rulers
        const Vec2 c = m_cam.worldToScreen(m_cursorWorld);
        if (c.x >= R && c.x <= w) r.drawLine({c.x, 0.f}, {c.x, R}, m_style.rulerCursor, 1.f);
        if (c.y >= R && c.y <= h) r.drawLine({0.f, c.y}, {R, c.y}, m_style.rulerCursor, 1.f);
    }
}

void Viewport2D::drawHud(IViewportRenderer& r) const {
    const Vec2 cw = m_hasCursor ? m_cursorWorld : m_cam.center();
    auto clean = [](float v) { return std::fabs(v) < 0.05f ? 0.f : v; };
    char buf[96];
    std::snprintf(buf, sizeof buf, "X %.1f  Y %.1f  Z %d%%", double(clean(cw.x)), double(clean(cw.y)),
                  int(std::lround(m_cam.zoom() * 100.f)));
    const float x0 = (m_style.showRulers ? m_style.rulerPx : 0.f) + 8.f;
    const float y0 = float(m_cam.heightPx()) - 26.f;
    r.fillRect({x0, y0}, {float(std::char_traits<char>::length(buf)) * 12.f + 8.f, 20.f}, m_style.hudBg);
    r.drawText({x0 + 4.f, y0 + 3.f}, buf, m_style.hudText, 14.f, TextOrient::Horizontal);
}

void Viewport2D::render(IViewportRenderer& r) const {
    r.beginFrame(m_cam.widthPx(), m_cam.heightPx(), m_style.background);
    drawCanvas(r);
    if (m_style.showGrid) drawGrid(r);
    if (m_style.showAxes) drawAxes(r);
    if (m_style.showGuides) drawGuides(r);
    if (m_drawScene) m_drawScene(r, m_cam);
    m_gizmo.draw(r, m_cam, m_hover);
    drawOverlays(r);
    if (m_style.showRulers) drawRulers(r);
    if (m_style.showHud) drawHud(r);
    r.endFrame();
}

} // namespace univex::viewport
