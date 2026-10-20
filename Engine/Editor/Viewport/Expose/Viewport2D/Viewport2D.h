#pragma once
#include "Camera2D.h"
#include "GridRuler.h"
#include "Snapping.h"
#include "TransformGizmo.h"
#include <functional>
#include <optional>

namespace univex::viewport {

enum class MouseButton { Left, Middle, Right };
enum class Tool { Select, Hand };               // Hand = Godot-style pan mode (no accidental selects)
enum class Phase { Begin, Update, End };
enum class CursorHint { Default, Pan, MoveAll, ResizeNS, ResizeEW, ResizeNWSE, ResizeNESW,
                        Rotate, Crosshair, GuideH, GuideV };

struct Modifiers { bool shift = false, ctrl = false, alt = false; };

struct ViewportStyle {
    Color background {28, 28, 32, 255};
    Color canvasFill {40, 40, 46, 255};
    Color canvasBorder{140, 140, 152, 255};
    Color gridMinor  {255, 255, 255, 18};
    Color gridMajor  {255, 255, 255, 40};
    Color axisX      {214, 72, 72, 200};
    Color axisY      {84, 190, 96, 200};
    Color guide      {0, 200, 255, 210};
    Color marquee    {86, 156, 255, 255};
    Color rulerBg    {36, 36, 41, 255};
    Color rulerTick  {120, 120, 130, 255};
    Color rulerText  {170, 170, 180, 255};
    Color rulerCursor{255, 190, 60, 255};
    Color hudBg      {0, 0, 0, 150};
    Color hudText    {230, 230, 235, 255};

    float targetGridPx = 48.f;
    float minGridStep  = 1.f;     // 0 = unlimited; 1 = never finer than one game pixel
    int   maxGridLines = 500;
    float rulerPx      = 24.f;

    bool showGrid = true, showAxes = true, showGuides = true;
    bool showRulers = true, showHud = true;
    bool smoothNavigation = true;  // animated zoom / frame; call update(dt) each frame
};

// 2D editor viewport: navigation, grid, rulers + guides, snapping, selection
// transform handles, marquee, HUD. No scene data and no engine types inside.
class Viewport2D {
public:
    using DrawSceneFn = std::function<void(IViewportRenderer&, const Camera2D&)>;
    using PickFn      = std::function<void(Vec2 world, Vec2 screen, bool additive)>;
    using TransformFn = std::function<void(const Box& proposed, Phase)>;
    using MarqueeFn   = std::function<void(const Rect& world, bool additive, Phase)>;

    // --- configuration
    Camera2D&       camera()       { return m_cam; }
    const Camera2D& camera() const { return m_cam; }
    ViewportStyle&  style()        { return m_style; }
    SnapSettings&   snap()         { return m_snap; }
    Guides&         guides()       { return m_guides; }
    TransformGizmo& gizmo()        { return m_gizmo; }

    void setSize(int w, int h) { m_cam.setViewportSize(w, h); }
    void setTool(Tool t) { m_tool = t; }
    Tool tool() const { return m_tool; }
    void setModifiers(Modifiers m) { m_mods = m; }
    void setPanModifier(bool held) { m_panModifier = held; }   // Space held
    void setCanvasRect(std::optional<Rect> r) { m_canvas = r; } // game / project bounds
    void setPivotEditing(bool v) { m_gizmo.setPivotEditing(v); }

    void setDrawScene(DrawSceneFn f) { m_drawScene = std::move(f); }
    void setOnPick(PickFn f)         { m_onPick = std::move(f); }
    void setOnTransform(TransformFn f) { m_onTransform = std::move(f); }
    void setOnMarquee(MarqueeFn f)   { m_onMarquee = std::move(f); }

    void setSelectionBox(const Box& b) { m_gizmo.setBox(b); }
    void clearSelection() { m_gizmo.clear(); m_hover = {}; }
    bool hasSelection() const { return m_gizmo.hasBox(); }

    // --- input (screen px, origin top-left). Wheel in notches, + = zoom in.
    void onMouseDown(MouseButton b, Vec2 pos);
    void onMouseMove(Vec2 pos);
    void onMouseUp(MouseButton b, Vec2 pos);
    void onWheel(float notches, Vec2 pos);
    void onScrollPan(Vec2 deltaPx);              // trackpad two-finger scroll

    // --- navigation
    void zoomToFit(const Rect& world, bool animate = true);
    void zoomTo100(bool animate = true);
    void focusOn(Vec2 world, bool animate = true);
    void update(float dtSeconds);
    bool isAnimating() const { return m_anim != Anim::None; }
    bool isPanning() const { return m_state == State::Panning; }

    CursorHint cursorHint() const;
    Vec2 cursorWorld() const { return m_cursorWorld; }

    void render(IViewportRenderer& r) const;

private:
    enum class State { Idle, Panning, GuideDrag, GizmoDrag, PendingEmpty, Marquee };
    enum class Anim { None, Zoom, Frame };

    int  rulerRegion(Vec2 p) const;     // 0 none, 1 top, 2 left, 3 corner
    bool inCanvasRegion(Vec2 p) const;
    float snapGuideValue(float v) const;
    void startFrameAnim(Vec2 center, float zoom, bool animate);
    DragModifiers dragMods() const { return {m_mods.shift, m_mods.alt}; }

    void drawCanvas(IViewportRenderer& r) const;
    void drawGrid(IViewportRenderer& r) const;
    void drawAxes(IViewportRenderer& r) const;
    void drawGuides(IViewportRenderer& r) const;
    void drawOverlays(IViewportRenderer& r) const;
    void drawRulers(IViewportRenderer& r) const;
    void drawHud(IViewportRenderer& r) const;

    Camera2D      m_cam;
    ViewportStyle m_style;
    SnapSettings  m_snap;
    Guides        m_guides;
    TransformGizmo m_gizmo;
    std::optional<Rect> m_canvas;
    DrawSceneFn   m_drawScene;
    PickFn        m_onPick;
    TransformFn   m_onTransform;
    MarqueeFn     m_onMarquee;

    Tool      m_tool = Tool::Select;
    Modifiers m_mods;
    bool      m_panModifier = false;

    State       m_state = State::Idle;
    MouseButton m_activeButton = MouseButton::Left;
    Vec2        m_last{}, m_downPos{};
    Vec2        m_cursorWorld{};
    bool        m_hasCursor = false;
    Handle      m_hover;

    // guide drag
    bool  m_guideNew = false, m_guideHorizontal = false;
    int   m_guideIndex = -1;
    float m_guidePos = 0.f;
    // marquee
    Vec2  m_marqueeStart{}, m_marqueeEnd{};

    // smooth navigation
    Anim  m_anim = Anim::None;
    float m_targetZoom = 1.f;
    Vec2  m_targetCenter{};
    Vec2  m_anchorWorld{}, m_anchorScreen{};
};

} // namespace univex::viewport
