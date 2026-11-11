// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - application object: window (SDL2), event loop, focus/capture, popups, timers, tooltips.
#pragma once
#include "Platform.h"
#include "RenderBackend.h"
#include "Widget.h"
#include <memory>

namespace prism {

struct WindowDesc {
  std::string title = "PRISM";
  int width = 1600, height = 900;
  int minWidth = 1100, minHeight = 680;
  std::string assetDir = "assets";
  bool opengl = false; // render through the OpenGL backend (needs PRISM_WITH_GL); otherwise the CPU rasterizer
  bool hidden = false; // create the window hidden (still renders; for automated captures)
  bool prismChrome = false; // borderless window with PRISM's own title bar (minimize / maximize / close); needs a host that
                            // implements Platform::setWindowCaption (SDL does). Applied to the root passed to setRoot().
  int icon = 0;        // prism::Icon shown in the title tab (0 = none)
};

// Settings for embedding PRISM in a host that owns the window and the main loop (see Application::initHosted).
// A native OS window hosting its own widget tree (torn-off panels, tool windows). Window 0 is the main window.
struct OsWindowDesc {
  std::string title = "PRISM";
  int width = 640, height = 480;
  int x = -1, y = -1;          // screen position, -1 = let the OS place it
  bool prismChrome = false;    // borderless + PRISM title bar around the root (windowRoot() then returns the frame)
  int icon = 0;                // prism::Icon shown in the title tab (0 = none)
};

struct HostDesc {
  int width = 1280, height = 720;
  std::string assetDir = "assets";
};

enum class ToastLevel { Info, Success, Warning, Error };

enum class PopupPlacement { Below, Right, AtPoint, Center };

struct PopupOptions {
  PopupPlacement placement = PopupPlacement::Below;
  Rect anchor;                    // anchor rect (Below / Right) or point (AtPoint: x,y)
  bool modal = false;             // dims the window and blocks other input
  bool dismissOnOutside = true;
  bool shadow = true;
  std::function<void()> onClose;
};

class AccessibilityBridge;

class Application {
public:
  Application();
  ~Application();
  static Application& get() { return *s_instance; }
  static bool exists() { return s_instance != nullptr; }

  // --- windowed mode (needs PRISM_WITH_SDL): PRISM owns the SDL window and the event loop.
  bool init(const WindowDesc& desc);
  int run();

  // --- hosted mode: the host owns window, loop and presentation. Typical frame:
  //   host events -> hostMouse*/hostKey/hostText ; app.tick(dt) ; if (app.needsRender()) upload(app.render())
  // `platform` may be null (HeadlessPlatform is used). The Application shares ownership of it.
  bool initHosted(const HostDesc& desc, std::shared_ptr<Platform> platform = nullptr);
  void hostResize(int width, int height);
  void hostMouseMove(Vec2 p);
  void hostMouseButton(Vec2 p, MouseButton b, bool down, int clicks = 1);
  void hostMouseWheel(Vec2 p, Vec2 delta);
  void hostMouseLeave();
  void hostKey(Key k, uint8_t mods, bool down = true, bool repeat = false);
  void hostText(const std::string& utf8);
  void hostSetModifiers(uint8_t mods) { m_mods = mods; }
  // Runs timers and layout. Returns true when the UI changed and a new frame should be rendered.
  bool tick();
  bool needsRender() const { return m_dirty || m_layoutDirty; }
  // --- rendering back ends. By default the built-in CPU canvas is used. With a backend set, frames are recorded
  // to a DrawList and submitted to it. Hosts with their own renderer can skip backends and call renderToDrawList().
  void setRenderBackend(std::shared_ptr<RenderBackend> backend);
  RenderBackend* renderBackend() const { return m_backend.get(); }
  const DrawList& renderToDrawList();   // lay out + paint into a DrawList (valid until the next frame)
  PixelView render();                 // lays out if needed, paints, returns the frame (valid until next render/resize)
  PixelView frame() const;            // last rendered frame without re-rendering
  // Routing helpers so the host can decide who gets an input event:
  bool wantsPointerAt(Vec2 p);        // true if a UI widget (not an EngineView hole / empty space) is under p, or holds the mouse
  bool wantsKeyboard() const;         // true while a text field has focus or a popup is open
  Platform& platform() { return *m_platform; }
  void setRoot(Ref<Widget> root);
  Widget* root() const { return m_root.get(); }
  void quit();

  // HiDPI and UI scaling
  float uiScale() const { return m_uiScale; }
  void setUiScale(float scale) { m_uiScale = std::clamp(scale, 0.25f, 4.0f); invalidateLayout(); }
  float dpiScale() const { return m_platform ? m_platform->dpiScale() : 1.0f; }
  float effectiveScale() const { return dpiScale() * m_uiScale; }
  float snap(float val) const { return snapPixel(val, effectiveScale()); }
  Vec2 snap(Vec2 p) const { return snapPixel(p, effectiveScale()); }
  Rect snap(Rect r) const { return snapPixel(r, effectiveScale()); }

  // invalidation
  void invalidate() { m_dirty = true; m_paintGen++; }
  void invalidateLayout() { m_layoutDirty = m_dirty = true; m_paintGen++; }

  // OS accessibility (screen readers, UI automation): see AccessibilityBridge.h
  void setAccessibilityBridge(std::shared_ptr<AccessibilityBridge> b) { m_a11y = std::move(b); }
  AccessibilityBridge* accessibilityBridge() const { return m_a11y.get(); }
  void accessNotify(AccessEvent e, Widget* w);

  // focus & capture
  void setFocus(Widget* w);
  void focusNext(bool reverse = false);   // Tab / Shift+Tab traversal in reading order
  Widget* focus() const { return m_focus.lock().get(); }
  void captureMouse(Widget* w);
  void releaseMouse();

  // popups
  void showPopup(Ref<Widget> w, const PopupOptions& o);
  void closePopup(const Widget* w);
  void movePopup(const Widget* w, Vec2 topLeft);   // reposition a popup (floating windows); clamped on screen
  void closeAllPopups();
  bool hasPopup() const { return !m_popups.empty(); }
  // A popup that takes the whole window's input while it is open: a modal one, or one that closes on an outside
  // click (menus, dropdowns). A floating window is neither, so the rest of the window keeps working beside it.
  bool hasBlockingPopup() const {
    for (const auto& p : m_popups) if (p.opt.modal || p.opt.dismissOnOutside) return true;
    return false;
  }
  bool popupJustDismissed() const { return m_dismissedThisEvent; } // an outside click closed popups during this event
  Widget* topPopup() const { return m_popups.empty() ? nullptr : m_popups.back().widget.get(); }

  // timers (callback returns false to stop)
  int addTimer(int ms, std::function<bool()> cb);
  void cancelTimer(int id);

  // shortcuts
  void addShortcut(Key key, uint8_t mods, std::function<void()> fn);
  // Fallback invoked for key presses nothing else handled (set it to CommandList::dispatch). Return true if handled.
  void setShortcutHandler(std::function<bool(Key, uint8_t)> fn) { m_shortcutHandler = std::move(fn); }

  // toast notifications (non-blocking, click-through, stacked bottom-right, auto-dismiss)
  void notify(const std::string& text, ToastLevel level = ToastLevel::Info, int durationMs = 3200);
  void setToastInset(float bottom) { m_toastBottom = bottom; }

  // Widget inspector overlay: outlines the widget under the pointer and shows class, rect, flags and path.
  void setInspector(bool on) { m_inspector = on; invalidate(); }
  bool inspector() const { return m_inspector; }

  // Topmost widget under p (popups first), or null. Used by drag & drop to find drop targets.
  Widget* widgetAt(Vec2 p) { return hitTestAll(p); }
  // Painter run above popups and below toasts; used for drag ghosts and drop indicators. Pass {} to clear.
  void setDragPainter(std::function<void(Canvas&)> fn) { m_dragPainter = std::move(fn); invalidate(); }

  // Drag and Drop framework
  void startDragDrop(std::shared_ptr<DragDropOperation> op);
  void cancelDragDrop();
  bool isDragging() const { return m_activeDragDrop != nullptr; }
  std::shared_ptr<DragDropOperation> activeDragDrop() const { return m_activeDragDrop; }

  // --- native multi-window. Every window has its own root, canvas, popups, hover, mouse capture and keyboard focus.
  // Popups open in the window that was handling the event; a click in one window dismisses (dismissOnOutside) popups
  // of the others. Hosts route OS events with the window id; in windowed mode (init/run) PRISM creates the SDL windows.
  int openWindow(Ref<Widget> root, const OsWindowDesc& desc);   // returns the new window id (> 0), 0 on failure
  void closeWindow(int id);
  bool hasWindow(int id) const;
  std::vector<int> windowOrder() const;                          // back to front (z-order), includes 0
  int activeWindow() const { return m_activeWindow; }            // receives keyboard input
  void activateWindow(int id);                                   // OS focus moved to `id`: raise it, route keys to it
  int currentWindow() const { return m_ctxWindow; }              // window whose event / paint is running now
  Widget* windowRoot(int id) const;
  Vec2 windowSizeOf(int id);
  void windowResize(int id, int w, int h);
  void windowMouseMove(int id, Vec2 p);
  void windowMouseButton(int id, Vec2 p, MouseButton b, bool down, int clicks = 1);
  void windowMouseWheel(int id, Vec2 p, Vec2 delta);
  void windowMouseLeave(int id);
  void windowKey(int id, Key k, uint8_t mods, bool down = true, bool repeat = false);
  void windowText(int id, const std::string& utf8);
  bool windowNeedsRender(int id);
  PixelView renderWindow(int id);                                // CPU frame of that window (window 0 = render())
  Widget* windowFocus(int id);
  Callback<int> onWindowClosed;                                  // after closeWindow (host destroys its OS window)

  // misc
  Vec2 windowSize() const { return {float(m_canvas.width()), float(m_canvas.height())}; }
  Vec2 mousePos() const { return m_mouse; }
  void setCursor(Cursor c);
  std::string clipboard() const;
  void setClipboard(const std::string& s);
  Canvas& canvas() { return m_canvas; }
  double frameMs() const { return m_frameMs; }
  uint64_t now() const;

  // Rendering / automation helpers
  void renderFrame();
  bool saveScreenshot(const std::string& path);
  void injectMouseMove(Vec2 p);
  void injectMouseButton(Vec2 p, MouseButton b, bool down, int clicks = 1);
  void injectKey(Key k, uint8_t mods = 0);
  void setOnIdleFrame(std::function<void(int)> fn) { m_onFrame = std::move(fn); }

private:
  struct Popup { Ref<Widget> widget; PopupOptions opt; };
  // Per-window UI state. The active window's state lives in the members below; other windows keep theirs here and
  // are swapped in while their events / layout / paint run, so all dispatch code is shared by every window.
  struct WinState {
    int id = 0;
    OsWindowDesc desc;
    Ref<Widget> root;
    Canvas canvas;
    std::vector<Popup> popups;
    std::vector<std::weak_ptr<Widget>> hoverChain;
    std::weak_ptr<Widget> focus, capture;
    Vec2 mouse;
    bool dirty = true, layoutDirty = true;
    uint64_t paintedGen = ~0ull;      // m_paintGen at its last paint (widgets invalidate through the shared app)
  };
  void swapState(WinState& w);
  template <class F> auto inWindow(int id, F&& fn);
  WinState* findWindow(int id);
  const WinState* findWindow(int id) const;
  std::vector<WinState> m_windows;      // secondary windows (state stored while not current)
  std::vector<int> m_zOrder{0};
  int m_activeWindow = 0, m_ctxWindow = 0, m_nextWindowId = 1;
  uint64_t m_paintGen = 0;
  std::shared_ptr<AccessibilityBridge> m_a11y;
  struct Toast { std::string text; ToastLevel level; uint64_t born; int duration; };
  struct Timer { int id; uint64_t due; int interval; std::function<bool()> cb; };

  void layoutIfNeeded();
  void arrangePopup(Popup& p);
  void paintAll(Canvas& c);
  void paintToasts(Canvas& c);
  void paintInspector(Canvas& c);
  void dispatchMouseDown(MouseEvent e);
  void dispatchMouseUp(MouseEvent e);
  void dispatchMouseMove(MouseEvent e);
  void dispatchWheel(MouseEvent e);
  void dispatchKey(KeyEvent e);
  Widget* hitTestAll(Vec2 p, bool* inPopup = nullptr);
  void updateHover(Widget* target);
  void showTooltip();
  void hideTooltip();
  void runTimers();
  int nextWaitMs() const;

  friend struct WindowHost; // SDL window backend (src/SdlWindow.cpp)
  static Application* s_instance;
  std::shared_ptr<Platform> m_platform;
  std::shared_ptr<void> m_windowHost; // type-erased SDL window state, null when hosted
  Canvas m_canvas;
  RecordingCanvas m_rec;
  std::shared_ptr<RenderBackend> m_backend;
  Ref<Widget> m_root;
  std::vector<Popup> m_popups;
  std::vector<Timer> m_timers;
  std::vector<int> m_dueTimers;
  std::vector<Toast> m_toasts;
  std::function<void(Canvas&)> m_dragPainter;
  std::shared_ptr<DragDropOperation> m_activeDragDrop;
  std::weak_ptr<Widget> m_dragTarget;
  Ref<Widget> m_activeTooltipWidget;
  int m_toastTimer = 0;
  float m_toastBottom = 34;
  bool m_inspector = false;
  float m_uiScale = 1.0f;
  std::vector<std::weak_ptr<Widget>> m_hoverChain;
  std::weak_ptr<Widget> m_focus, m_capture;
  std::string m_tooltipText;
  Rect m_tooltipRect;
  int m_tooltipTimer = 0;
  Vec2 m_mouse;
  uint8_t m_mods = 0;
  bool m_running = false, m_dirty = true, m_layoutDirty = true, m_dismissedThisEvent = false;
  int m_nextTimerId = 1;
  Cursor m_cursor = Cursor::Arrow;
  double m_frameMs = 0;
  int m_frameCount = 0;
  std::function<void(int)> m_onFrame;
  struct Shortcut { Key key; uint8_t mods; std::function<void()> fn; };
  std::vector<Shortcut> m_shortcuts;
  std::function<bool(Key, uint8_t)> m_shortcutHandler;
  WindowDesc m_desc;
  bool m_mainChrome = false;           // main window wears PRISM chrome: setRoot() wraps the root
};

} // namespace prism
