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
///   * Windows: window 0 is the engine's main window. OpenWindowUVE adds an undecorated window
///     (PRISM draws its title bar) sharing the main context's textures, for an asset editor.
/// Platform caption ids from this one up name extra drag areas: kExtraCaptionIdUVE + 100 * window + n.
inline constexpr int kExtraCaptionIdUVE = 100;
[[nodiscard]] constexpr int ExtraCaptionIdUVE(const int window, const int n = 0) noexcept {
    return kExtraCaptionIdUVE + (window * 100) + n;
}

class EditorHostUVE final {
public:
    /// One engine render target to place under an EngineView hole this frame.
    struct ViewImageUVE final {
        prism::Rect rect;              ///< window pixels, top-left origin
        std::uint32_t texture = 0U;    ///< GL texture, bottom-up (render-target) orientation
        float usedWidth = 0.0F;        ///< texels of `texture` that hold the image
        float usedHeight = 0.0F;
        float textureWidth = 0.0F;     ///< full texture size (for UV scaling)
        float textureHeight = 0.0F;
    };
    static constexpr int kMainWindowUVE = 0;

    EditorHostUVE();
    ~EditorHostUVE();
    EditorHostUVE(const EditorHostUVE&) = delete;
    EditorHostUVE& operator=(const EditorHostUVE&) = delete;

    /// Needs the editor's GL context current. `assetDir` holds PRISM's fonts/themes/locales.
    [[nodiscard]] bool InitializeUVE(GLFWwindow* window, const std::filesystem::path& assetDir);
    void ShutdownUVE();
    [[nodiscard]] bool IsReadyUVE() const noexcept { return m_ready; }

    [[nodiscard]] prism::Application& AppUVE() noexcept { return *m_app; }
    void SetRootUVE(prism::Ref<prism::Widget> root);

    /// Opens a native window showing `root` (its own title bar included); returns its PRISM
    /// window id, or 0 when the window could not be made. Main context current.
    [[nodiscard]] int OpenWindowUVE(prism::Ref<prism::Widget> root, const std::string& title, int width, int height);
    void CloseWindowUVE(int id);
    [[nodiscard]] bool HasWindowUVE(int id) const;
    /// The OS close button of window `id` was pressed (it stays open until CloseWindowUVE).
    [[nodiscard]] bool WindowCloseRequestedUVE(int id) const;
    void ClearCloseRequestUVE(int id);
    /// Raises window `id` and gives it the keyboard.
    void FocusWindowUVE(int id);
    /// True when the OS window of `id` (a secondary window) has the keyboard focus.
    [[nodiscard]] bool IsWindowFocusedUVE(int id) const;
    /// Wraps everything window `id`'s input runs (its widgets' handlers and shortcuts): the
    /// editor makes that window's document the active one around it.
    using WindowScopeUVE = std::function<void(const std::function<void()>&)>;
    void SetWindowScopeUVE(int id, WindowScopeUVE scope);

    /// Timers + layout of every window for this frame (call before reading EngineView rects).
    void BeginFrameUVE();
    /// Input for an engine view occupying `rect` of window `id`: hovered only when the pointer
    /// is inside it and no PRISM widget or popup above it claims the pointer.
    [[nodiscard]] EditorViewportInputUVE CaptureViewInputUVE(const prism::Rect& rect) const {
        return CaptureViewInputUVE(kMainWindowUVE, rect);
    }
    [[nodiscard]] EditorViewportInputUVE CaptureViewInputUVE(int id, const prism::Rect& rect) const;
    /// Applies a cursor a view requested while it rendered.
    void ApplyViewCursorUVE(const EditorViewportInputUVE& input) { ApplyViewCursorUVE(kMainWindowUVE, input); }
    void ApplyViewCursorUVE(int id, const EditorViewportInputUVE& input);
    /// Renders the main window's UI and composites `views` under it into its default framebuffer.
    void RenderUVE(std::span<const ViewImageUVE> views);
    /// The same for window `id` (not the main one), presented at once; the main context is
    /// current again when it returns.
    void RenderWindowUVE(int id, std::span<const ViewImageUVE> views);

    /// True once the PRISM close button (or quit) asked the editor to exit.
    [[nodiscard]] bool QuitRequestedUVE() const noexcept;

    /// Saves the main window's current frame as PNG (CPU readback of the GL backend).
    bool SaveScreenshotUVE(const std::filesystem::path& path);
    /// Saves window `id`'s last frame as PNG.
    bool SaveWindowScreenshotUVE(int id, const std::filesystem::path& path);
    /// Another UI drawn over the main window (windows not yet built with PRISM) says when it wants
    /// the pointer or the keyboard; while it does, neither PRISM nor the viewport receives that input.
    void SetForeignInputUVE(std::function<bool()> wantsMouse, std::function<bool()> wantsKeyboard);

    struct WindowUVE;
    // GLFW callback entry points (static trampolines route here).
    void OnCursorPosUVE(WindowUVE& w, double x, double y);
    void OnMouseButtonUVE(WindowUVE& w, int button, int action, int mods);
    void OnScrollUVE(WindowUVE& w, double dx, double dy);
    void OnKeyUVE(WindowUVE& w, int key, int scancode, int action, int mods);
    void OnCharUVE(WindowUVE& w, unsigned int codepoint);
    void OnCursorEnterUVE(WindowUVE& w, int entered);

private:
    class PlatformUVE;
    struct CompositorUVE;
    struct ChainUVE;
    friend class PlatformUVE;

    [[nodiscard]] WindowUVE* FindUVE(int id) const;
    [[nodiscard]] prism::Vec2 ToUiUVE(const WindowUVE& w, double x, double y) const;
    /// Runs `fn` (an input event routed from `source` to `target`) in `target`'s scope; false when it
    /// cannot (`source` is scoped and `target` is not).
    static bool InTargetScopeUVE(const WindowUVE& source, const WindowUVE& target, const std::function<void()>& fn);
    /// While a PRISM drag is in flight: the other window under the pointer, if any, and the pointer
    /// in its UI coordinates.
    [[nodiscard]] WindowUVE* DragTargetUVE(const WindowUVE& source, prism::Vec2& local) const;
    void SyncSizeUVE(WindowUVE& w);
    void UpdateWindowDragUVE(WindowUVE& w);
    void CompositeUVE(WindowUVE& w, std::uint32_t uiTexture, std::uint32_t vao, std::span<const ViewImageUVE> views);
    void EndInputFrameUVE(WindowUVE& w);
    [[nodiscard]] bool ForeignMouseUVE(const WindowUVE& w) const;
    [[nodiscard]] bool ForeignKeyboardUVE(const WindowUVE& w) const;

    std::shared_ptr<PlatformUVE> m_platform;
    std::unique_ptr<prism::Application> m_app;
    std::shared_ptr<prism::GlBackend> m_backend;
    std::unique_ptr<CompositorUVE> m_compositor;
    std::unique_ptr<ChainUVE> m_chain;
    std::unique_ptr<WindowUVE> m_main;
    std::map<int, std::unique_ptr<WindowUVE>> m_windows;   // secondary windows by PRISM id
    bool m_ready = false;
    std::function<bool()> m_foreignMouse;
    std::function<bool()> m_foreignKeyboard;
    /// The main window's UI was last painted at this paint generation and time (see RenderUVE).
    std::uint64_t m_mainPaintedGen = ~0ULL;
    std::chrono::steady_clock::time_point m_mainPaintedAt{};
};

} // namespace UVE::Editor
