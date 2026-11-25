// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_viewport_input.h"

#include "prism/Application.h"
#include "prism/Platform.h"

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>
#include <memory>
#include <span>
#include <string>

struct GLFWwindow;

namespace prism {
class GlBackend;
}

namespace UVE::Editor {

/// Hosts PRISM inside the editor's GLFW windows: the engine owns the main window and the loop,
/// PRISM owns the UI trees.
///
///   * Platform: clock, clipboard, cursor shapes, window chrome (drag strips, minimize, maximize,
///     close) and content scale, all through GLFW.
///   * Input: GLFW callbacks forward pointer, wheel, keys and text of each window to its PRISM
///     window, and keep that window's raw state for its viewport input frames. The main window's
///     callbacks are chained to whatever was installed before.
///   * Rendering: PRISM records a DrawList per window and an OpenGL backend renders it into that
///     window's own texture (in the main context); the host then draws the window's engine view
///     textures into their EngineView holes and blends the UI texture over the whole window.
///   * Windows: window 0 is the engine's main window. OpenWindow adds an undecorated window
///     (PRISM draws its title bar) sharing the main context's textures, for an asset editor.
/// Platform caption ids from this one up name extra drag areas: kExtraCaptionId + 100 * window + n.
inline constexpr int kExtraCaptionId = 100;
[[nodiscard]] constexpr int ExtraCaptionId(const int window, const int n = 0) noexcept {
    return kExtraCaptionId + (window * 100) + n;
}

class EditorHost final {
public:
    /// Files dragged from the system's file manager and dropped on the main window (desktop).
    std::function<void(std::vector<std::filesystem::path>)> onFilesDropped;
    /// One engine render target to place under an EngineView hole this frame.
    struct ViewImage final {
        prism::Rect rect;              ///< window pixels, top-left origin
        std::uint32_t texture = 0U;    ///< GL texture, bottom-up (render-target) orientation
        float usedWidth = 0.0F;        ///< texels of `texture` that hold the image
        float usedHeight = 0.0F;
        float textureWidth = 0.0F;     ///< full texture size (for UV scaling)
        float textureHeight = 0.0F;
    };
    static constexpr int kMainWindow = 0;

    EditorHost();
    ~EditorHost();
    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    /// Needs the editor's GL context current. `assetDir` holds PRISM's fonts/themes/locales.
    [[nodiscard]] bool Initialize(GLFWwindow* window, const std::filesystem::path& assetDir);
    void Shutdown();
    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }

    [[nodiscard]] prism::Application& App() noexcept { return *m_app; }
    /// Whether any of the editor's windows (the main one or one it opened) has the keyboard focus.
    [[nodiscard]] bool IsAnyWindowFocused() const;
    void SetRoot(prism::Ref<prism::Widget> root);

    /// Opens a native window showing `root` (its own title bar included); returns its PRISM
    /// window id, or 0 when the window could not be made. Main context current.
    [[nodiscard]] int OpenWindow(prism::Ref<prism::Widget> root, const std::string& title, int width, int height);
    void CloseWindow(int id);
    [[nodiscard]] bool HasWindow(int id) const;
    /// The OS close button of window `id` was pressed (it stays open until CloseWindow).
    [[nodiscard]] bool WindowCloseRequested(int id) const;
    void ClearCloseRequest(int id);
    /// Raises window `id` and gives it the keyboard.
    void FocusWindow(int id);
    /// True when the OS window of `id` (a secondary window) has the keyboard focus.
    [[nodiscard]] bool IsWindowFocused(int id) const;
    /// Wraps everything window `id`'s input runs (its widgets' handlers and shortcuts): the
    /// editor makes that window's document the active one around it.
    using WindowScope = std::function<void(const std::function<void()>&)>;
    void SetWindowScope(int id, WindowScope scope);

    /// Timers + layout of every window for this frame (call before reading EngineView rects).
    void BeginFrame();
    /// Input for an engine view occupying `rect` of window `id`: hovered only when the pointer
    /// is inside it and no PRISM widget or popup above it claims the pointer.
    [[nodiscard]] EditorViewportInput CaptureViewInput(const prism::Rect& rect) const {
        return CaptureViewInput(kMainWindow, rect);
    }
    [[nodiscard]] EditorViewportInput CaptureViewInput(int id, const prism::Rect& rect) const;
    /// Applies a cursor a view requested while it rendered.
    void ApplyViewCursor(const EditorViewportInput& input) { ApplyViewCursor(kMainWindow, input); }
    void ApplyViewCursor(int id, const EditorViewportInput& input);
    /// Renders the main window's UI and composites `views` under it into its default framebuffer.
    void Render(std::span<const ViewImage> views);
    /// The same for window `id` (not the main one), presented at once; the main context is
    /// current again when it returns.
    void RenderWindow(int id, std::span<const ViewImage> views);
    /// Times the GPU work the main context gets between these two calls; the result is read a few
    /// frames later, never waited for. GpuFrameMs: the latest one, 0 where the GL cannot time.
    void BeginGpuTimer();
    void EndGpuTimer();
    [[nodiscard]] float GpuFrameMs() const noexcept;

    /// True once the PRISM close button (or quit) asked the editor to exit.
    [[nodiscard]] bool QuitRequested() const noexcept;
    /// True on single-surface platforms when a full-screen sub-editor/tool page completely covers
    /// the main window (so the Level Editor can skip rendering its occluded 3D viewport).
    [[nodiscard]] bool HasOpaqueFullPage() const;

    /// Saves the main window's current frame as PNG (CPU readback of the GL backend).
    bool SaveScreenshot(const std::filesystem::path& path);
    /// Saves window `id`'s last frame as PNG.
    bool SaveWindowScreenshot(int id, const std::filesystem::path& path);
    /// Sees every key of the main window first (GLFW key and action); returning true keeps it from
    /// the UI and the view.
    void SetKeyHook(std::function<bool(int key, int action)> hook);

    struct Window;
    // GLFW callback entry points (static trampolines route here).
    void OnCursorPos(Window& w, double x, double y);
    void OnMouseButton(Window& w, int button, int action, int mods);
    void OnScroll(Window& w, double dx, double dy);
    void OnKey(Window& w, int key, int scancode, int action, int mods);
    void OnChar(Window& w, unsigned int codepoint);
    void OnCursorEnter(Window& w, int entered);

private:
    class Platform;
    struct Compositor;
    struct GpuTimer;
    struct Chain;
    friend class Platform;

    [[nodiscard]] Window* Find(int id) const;
    [[nodiscard]] prism::Vec2 ToUi(const Window& w, double x, double y) const;
    /// Runs `fn` (an input event routed from `source` to `target`) in `target`'s scope; false when it
    /// cannot (`source` is scoped and `target` is not).
    static bool InTargetScope(const Window& source, const Window& target, const std::function<void()>& fn);
    /// While a PRISM drag is in flight: the other window under the pointer, if any, and the pointer
    /// in its UI coordinates.
    [[nodiscard]] Window* DragTarget(const Window& source, prism::Vec2& local) const;
    void SyncSize(Window& w);
    void ApplyPageLayout(Window& w);
    void UpdateWindowDrag(Window& w);
    void Composite(Window& w, std::uint32_t uiTexture, std::uint32_t vao, std::span<const ViewImage> views);
    void CompositePageOverlay(Window& w, std::uint32_t uiTexture, std::span<const ViewImage> views);
    void EndInputFrame(Window& w);
    /// Single-surface platforms (Android): another window is a page over the main one. The page on
    /// top, unless hidden (its title bar's minimize), takes the screen and the input.
    [[nodiscard]] Window* VisiblePage() const;
    void ShowPage(int id);
    void HidePage(int id);

    std::shared_ptr<Platform> m_platform;
    std::unique_ptr<prism::Application> m_app;
    std::shared_ptr<prism::GlBackend> m_backend;
    std::unique_ptr<Compositor> m_compositor;
    std::unique_ptr<GpuTimer> m_gpuTimer;
    std::unique_ptr<Chain> m_chain;
    std::unique_ptr<Window> m_main;
    std::map<int, std::unique_ptr<Window>> m_windows;   // secondary windows by PRISM id
    /// Every window shares the main surface (no OS windows): secondary windows are pages, in the
    /// order they were last shown (the last one is on top).
    bool m_singleSurface = false;
    std::vector<int> m_pageOrder;
    bool m_ready = false;
    std::function<bool(int, int)> m_keyHook;
    /// The main window's UI was last painted at this paint generation and time (see Render).
    std::uint64_t m_mainPaintedGen = ~0ULL;
    std::chrono::steady_clock::time_point m_mainPaintedAt{};
};

} // namespace UVE::Editor
