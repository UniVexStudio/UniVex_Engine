// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_viewport_input.h"

#include "prism/Application.h"
#include "prism/Platform.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

struct GLFWwindow;

namespace prism {
class GlBackend;
}

namespace UVE::Editor {

/// Hosts PRISM inside the editor's GLFW window: the engine owns the window and the loop, PRISM
/// owns the UI tree.
///
///   * Platform: clock, clipboard, cursor shapes, window chrome (drag strip, minimize, maximize,
///     close) and content scale, all through GLFW.
///   * Input: GLFW callbacks (chained to whatever was installed before) forward pointer, wheel,
///     keys and text to prism::Application, and keep raw state for the viewport input frames.
///   * Rendering: PRISM records a DrawList each frame and the OpenGL backend renders it into its
///     own texture; the host then draws each engine view texture into its EngineView hole and
///     blends the UI texture over the whole window.
class EditorPrismHostUVE final {
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

    EditorPrismHostUVE();
    ~EditorPrismHostUVE();
    EditorPrismHostUVE(const EditorPrismHostUVE&) = delete;
    EditorPrismHostUVE& operator=(const EditorPrismHostUVE&) = delete;

    /// Needs the editor's GL context current. `assetDir` holds PRISM's fonts/themes/locales.
    [[nodiscard]] bool InitializeUVE(GLFWwindow* window, const std::filesystem::path& assetDir);
    void ShutdownUVE();
    [[nodiscard]] bool IsReadyUVE() const noexcept { return m_ready; }

    [[nodiscard]] prism::Application& AppUVE() noexcept { return *m_app; }
    void SetRootUVE(prism::Ref<prism::Widget> root);

    /// Timers + layout for this frame (call before reading EngineView rects).
    void BeginFrameUVE();
    /// Input for an engine view occupying `rect`: hovered only when the pointer is inside it and
    /// no PRISM widget or popup above it claims the pointer.
    [[nodiscard]] EditorViewportInputUVE CaptureViewInputUVE(const prism::Rect& rect) const;
    /// Applies a cursor a view requested while it rendered.
    void ApplyViewCursorUVE(const EditorViewportInputUVE& input);
    /// Renders the UI, composites `views` under it into the default framebuffer, clears edges.
    void RenderUVE(std::span<const ViewImageUVE> views);

    /// True once the PRISM close button (or quit) asked the editor to exit.
    [[nodiscard]] bool QuitRequestedUVE() const noexcept;

    /// Saves the current UI frame as PNG (CPU readback of the GL backend).
    bool SaveScreenshotUVE(const std::filesystem::path& path);

    // GLFW callback entry points (static trampolines route here).
    void OnCursorPosUVE(double x, double y);
    void OnMouseButtonUVE(int button, int action, int mods);
    void OnScrollUVE(double dx, double dy);
    void OnKeyUVE(int key, int scancode, int action, int mods);
    void OnCharUVE(unsigned int codepoint);
    void OnCursorEnterUVE(int entered);

private:
    class PlatformUVE;
    struct CompositorUVE;
    struct ChainUVE;

    [[nodiscard]] prism::Vec2 ToUiUVE(double x, double y) const;
    void SyncSizeUVE();
    void UpdateWindowDragUVE();

    GLFWwindow* m_window = nullptr;
    std::shared_ptr<PlatformUVE> m_platform;
    std::unique_ptr<prism::Application> m_app;
    std::shared_ptr<prism::GlBackend> m_backend;
    std::unique_ptr<CompositorUVE> m_compositor;
    std::unique_ptr<ChainUVE> m_chain;
    bool m_ready = false;
    int m_fbWidth = 0;
    int m_fbHeight = 0;

    // Raw state for viewport input frames.
    prism::Vec2 m_mouse{0.0F, 0.0F};
    prism::Vec2 m_frameStartMouse{0.0F, 0.0F};
    bool m_mouseInside = true;
    float m_wheel = 0.0F;
    std::uint8_t m_mods = 0;
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> m_down{};
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> m_clicked{};
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> m_released{};
    std::array<prism::Vec2, EditorViewportInputUVE::kButtonCountUVE> m_pressPos{};
    std::array<float, EditorViewportInputUVE::kButtonCountUVE> m_dragDistance{};
    std::bitset<EditorViewportInputUVE::kKeyCountUVE> m_keysDown;
    std::bitset<EditorViewportInputUVE::kKeyCountUVE> m_keysPressed;
    double m_lastFrameTime = 0.0;
    float m_deltaTime = 1.0F / 60.0F;

    // Double-click detection (PRISM wants a click count).
    double m_lastClickTime = -1.0;
    prism::Vec2 m_lastClickPos{0.0F, 0.0F};
    int m_clickCount = 0;

    // Window drag (title strip) and edge resize, done here because GLFW has no hit-test hook.
    enum class DragModeUVE : std::uint8_t { None, Move, Resize };
    DragModeUVE m_dragMode = DragModeUVE::None;
    int m_resizeEdges = 0; // bit 0 left, 1 right, 2 top, 3 bottom
    double m_dragStartScreenX = 0.0;
    double m_dragStartScreenY = 0.0;
    int m_dragStartWinX = 0;
    int m_dragStartWinY = 0;
    int m_dragStartWinW = 0;
    int m_dragStartWinH = 0;
};

} // namespace UVE::Editor
