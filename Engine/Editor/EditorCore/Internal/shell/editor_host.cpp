// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_host.h"

#include "prism/Canvas.h"
#include "prism/GlBackend.h"
#include "prism/Icons.h"
#include "prism/Theme.h"
#include "logging_macros.h"
#include "png_metadata.h"

#include "gl_system_headers.h"
#include <GLFW/glfw3.h>
#if defined(__ANDROID__)
#include "android_glfw.h"
#endif
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>

namespace UVE::Editor {
namespace {

EditorHostUVE* g_activeHostUVE = nullptr;

using ViewKeyUVE = EditorViewportInputUVE::KeyUVE;
using ViewCursorUVE = EditorViewportInputUVE::CursorUVE;

constexpr double kDoubleClickSecondsUVE = 0.4;
constexpr float kResizeBorderUVE = 5.0F;
/// Platforms with one full-screen surface and no OS windows: every extra window is a page over the
/// main one.
#if defined(__ANDROID__)
constexpr bool kSingleSurfaceUVE = true;
#else
constexpr bool kSingleSurfaceUVE = false;
#endif
/// How far a double-click's second press may land from the first: a finger never taps the same pixel.
constexpr float kDoubleClickSlopUVE = kSingleSurfaceUVE ? 12.0F : 4.0F;
/// Bytes kept of a compositor shader or program info log.
constexpr std::size_t kShaderLogCapacityUVE = 1024;

[[nodiscard]] prism::Key ToPrismKeyUVE(const int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::A) + (key - GLFW_KEY_A));
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::N0) + (key - GLFW_KEY_0));
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::F1) + (key - GLFW_KEY_F1));
    }
    switch (key) {
        case GLFW_KEY_BACKSPACE: return prism::Key::Backspace;
        case GLFW_KEY_TAB: return prism::Key::Tab;
        case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return prism::Key::Enter;
        case GLFW_KEY_ESCAPE: return prism::Key::Escape;
        case GLFW_KEY_SPACE: return prism::Key::Space;
        case GLFW_KEY_LEFT: return prism::Key::Left;
        case GLFW_KEY_RIGHT: return prism::Key::Right;
        case GLFW_KEY_UP: return prism::Key::Up;
        case GLFW_KEY_DOWN: return prism::Key::Down;
        case GLFW_KEY_HOME: return prism::Key::Home;
        case GLFW_KEY_END: return prism::Key::End;
        case GLFW_KEY_PAGE_UP: return prism::Key::PageUp;
        case GLFW_KEY_PAGE_DOWN: return prism::Key::PageDown;
        case GLFW_KEY_DELETE: return prism::Key::Delete;
        default: return prism::Key::Unknown;
    }
}

/// Viewport hotkeys (EditorViewportInputUVE::KeyUVE) for a GLFW key, or Count.
[[nodiscard]] ViewKeyUVE ToViewKeyUVE(const int key) {
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return static_cast<ViewKeyUVE>(static_cast<int>(ViewKeyUVE::Num0) + (key - GLFW_KEY_0));
    }
    switch (key) {
        case GLFW_KEY_KP_1: return ViewKeyUVE::Keypad1;
        case GLFW_KEY_KP_3: return ViewKeyUVE::Keypad3;
        case GLFW_KEY_KP_5: return ViewKeyUVE::Keypad5;
        case GLFW_KEY_KP_7: return ViewKeyUVE::Keypad7;
        case GLFW_KEY_A: return ViewKeyUVE::A;
        case GLFW_KEY_C: return ViewKeyUVE::C;
        case GLFW_KEY_D: return ViewKeyUVE::D;
        case GLFW_KEY_F: return ViewKeyUVE::F;
        case GLFW_KEY_S: return ViewKeyUVE::S;
        case GLFW_KEY_W: return ViewKeyUVE::W;
        case GLFW_KEY_E: return ViewKeyUVE::E;
        case GLFW_KEY_Q: return ViewKeyUVE::Q;
        case GLFW_KEY_SPACE: return ViewKeyUVE::Space;
        case GLFW_KEY_HOME: return ViewKeyUVE::Home;
        case GLFW_KEY_ESCAPE: return ViewKeyUVE::Escape;
        case GLFW_KEY_LEFT_SHIFT: return ViewKeyUVE::LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return ViewKeyUVE::LeftCtrl;
        default: return ViewKeyUVE::Count;
    }
}

[[nodiscard]] std::uint8_t ToPrismModsUVE(const int mods) {
    std::uint8_t out = prism::ModNone;
    if ((mods & GLFW_MOD_SHIFT) != 0) { out |= prism::ModShift; }
    if ((mods & GLFW_MOD_CONTROL) != 0) { out |= prism::ModCtrl; }
    if ((mods & GLFW_MOD_ALT) != 0) { out |= prism::ModAlt; }
    return out;
}

/// GLFW button -> EditorViewportInputUVE button slot (Left, Right, Middle), or -1.
[[nodiscard]] int ToViewButtonUVE(const int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return 0;
        case GLFW_MOUSE_BUTTON_RIGHT: return 1;
        case GLFW_MOUSE_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

[[nodiscard]] prism::MouseButton ToPrismButtonUVE(const int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return prism::MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return prism::MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return prism::MouseButton::Middle;
        default: return prism::MouseButton::None;
    }
}

void AppendUtf8UVE(std::string& out, const char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

[[nodiscard]] void* GlLoaderUVE(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

/// Logical UI scale: 1.0 on desktop (pixel-exact), or the display density scale on single-surface
/// mobile screens, clamped so the shorter logical edge stays at least 360 layout units wide.
[[nodiscard]] float ComputeUiScaleUVE(GLFWwindow* const window, const int fbWidth, const int fbHeight,
                                      const bool singleSurface) {
    if (!singleSurface || window == nullptr || fbWidth <= 0 || fbHeight <= 0) {
        return 1.0F;
    }
    float sx = 1.0F;
    float sy = 1.0F;
    glfwGetWindowContentScale(window, &sx, &sy);
    const float rawScale = std::isfinite(sx) && sx > 0.0F ? sx : 1.0F;
    constexpr float kMinLogicalShortEdgeUVE = 500.0F;
    const float maxScale = std::max(1.0F, static_cast<float>(std::min(fbWidth, fbHeight)) / kMinLogicalShortEdgeUVE);
    return std::clamp(rawScale, 1.0F, maxScale);
}

} // namespace

// ---------------------------------------------------------------------------------------- window

/// One OS window: its GLFW window, framebuffer size, drag areas and the raw input state its
/// viewports read. Window 0 is the engine's main window; the others belong to this host.
struct EditorHostUVE::WindowUVE final {
    EditorHostUVE* host = nullptr;
    int id = 0;
    GLFWwindow* window = nullptr;
    int fbWidth = 1;
    int fbHeight = 1;
    int uiWidth = 1;
    int uiHeight = 1;
    float uiScale = 1.0F;
    std::map<int, prism::Rect> captions;
    bool closeRequested = false;
    /// A page on the main surface (single-surface platforms) rather than an OS window of its own.
    bool page = false;
    bool pageHidden = false;
    bool pageFloating = false;
    prism::Rect pageBounds{};
    bool pageDragging = false;
    prism::Vec2 pageDragGrab{};
    std::uint64_t paintedGen = ~0ULL;
    std::chrono::steady_clock::time_point paintedAt{};
    int dragOverWindow = -1;  ///< the other window a PRISM drag from this one is over (-1: none)

    // A secondary window's own UI renderer (its texture lives in the main context) and the
    // vertex array the compositor draws with in that window's context (not shared).
    std::shared_ptr<prism::GlBackend> backend;
    GLuint vao = 0;
    EditorHostUVE::WindowScopeUVE scope;

    prism::Vec2 mouse{0.0F, 0.0F};
    prism::Vec2 frameStartMouse{0.0F, 0.0F};
    bool mouseInside = true;
    float wheel = 0.0F;
    std::uint8_t mods = 0;
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> down{};
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> clicked{};
    std::array<bool, EditorViewportInputUVE::kButtonCountUVE> released{};
    std::array<prism::Vec2, EditorViewportInputUVE::kButtonCountUVE> pressPos{};
    std::array<float, EditorViewportInputUVE::kButtonCountUVE> dragDistance{};
    std::bitset<EditorViewportInputUVE::kKeyCountUVE> keysDown;
    std::bitset<EditorViewportInputUVE::kKeyCountUVE> keysPressed;
    double lastFrameTime = 0.0;
    float deltaTime = 1.0F / 60.0F;

    // Double-click detection (PRISM wants a click count).
    double lastClickTime = -1.0;
    prism::Vec2 lastClickPos{0.0F, 0.0F};
    int clickCount = 0;

    // Window drag (title strip) and edge resize, done here because GLFW has no hit-test hook.
    enum class DragModeUVE : std::uint8_t { None, Move, Resize };
    DragModeUVE dragMode = DragModeUVE::None;
    int resizeEdges = 0; // bit 0 left, 1 right, 2 top, 3 bottom
    double dragStartScreenX = 0.0;
    double dragStartScreenY = 0.0;
    int dragStartWinX = 0;
    int dragStartWinY = 0;
    int dragStartWinW = 0;
    int dragStartWinH = 0;

    [[nodiscard]] bool CaptionContainsUVE(const prism::Vec2 p) const noexcept {
        for (const auto& [captionId, r] : captions) {
            if (r.w > 0.0F && p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h) {
                return true;
            }
        }
        return false;
    }
};

// ------------------------------------------------------------------------------------- platform

class EditorHostUVE::PlatformUVE final : public prism::Platform {
public:
    PlatformUVE(EditorHostUVE& host, GLFWwindow* window) : m_host(host), m_window(window) {
        m_cursors[static_cast<int>(prism::Cursor::Arrow)] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        m_cursors[static_cast<int>(prism::Cursor::IBeam)] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        m_cursors[static_cast<int>(prism::Cursor::SizeWE)] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        m_cursors[static_cast<int>(prism::Cursor::SizeNS)] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
#if defined(GLFW_RESIZE_ALL_CURSOR)
        m_cursors[static_cast<int>(prism::Cursor::SizeAll)] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
#else
        m_cursors[static_cast<int>(prism::Cursor::SizeAll)] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
#endif
        m_cursors[static_cast<int>(prism::Cursor::Hand)] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    }
    ~PlatformUVE() override {
        for (GLFWcursor* cursor : m_cursors) {
            if (cursor != nullptr) {
                glfwDestroyCursor(cursor);
            }
        }
    }

    std::uint64_t nowMs() const override { return static_cast<std::uint64_t>(glfwGetTime() * 1000.0); }
    std::string clipboard() const override {
        const char* text = glfwGetClipboardString(m_window);
        return text != nullptr ? std::string(text) : std::string();
    }
    void setClipboard(const std::string& text) override { glfwSetClipboardString(m_window, text.c_str()); }
    void setCursor(const prism::Cursor c) override {
        const int window = m_host.m_app != nullptr ? m_host.m_app->currentWindow() : 0;
        m_uiCursor[window] = c;
        ApplyCursorUVE(window, m_cursors[static_cast<int>(c)]);
    }
    void setTextInputActive(const bool active) override {
        m_textInput = active;
#if defined(__ANDROID__)
        Window::SetAndroidGlfwTextInputUVE(active);   // the app shows or hides the soft keyboard
#endif
    }
    float dpiScale() const override { return m_host.m_main != nullptr ? m_host.m_main->uiScale : 1.0F; }
    void requestQuit() override { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
    void* nativeWindowHandle() const override { return m_window; }
    /// Drag areas: id < kExtraCaptionIdUVE is a window's title strip (the window's own id); ids
    /// from kExtraCaptionIdUVE up are further empty title-bar space (ExtraCaptionIdUVE).
    void setWindowCaption(const int id, const prism::Rect dragArea) override {
        const int window = id < kExtraCaptionIdUVE ? id : (id - kExtraCaptionIdUVE) / 100;
        if (WindowUVE* w = m_host.FindUVE(window)) {
            w->captions[id] = dragArea;
        }
    }
    void minimizeWindow(const int id) override {
        if (WindowUVE* w = m_host.FindUVE(id)) {
            if (w->page) {
                m_host.HidePageUVE(id);
            } else {
                glfwIconifyWindow(w->window);
            }
        }
    }
    bool singleSurface() const override { return m_host.m_singleSurface; }
    void toggleMaximizeWindow(const int id) override {
        WindowUVE* w = m_host.FindUVE(id);
        if (w == nullptr || (id == kMainWindowUVE && m_host.m_singleSurface)) {
            return;
        }
        if (w->page) {
            w->pageFloating = !w->pageFloating;
            w->pageDragging = false;
            m_host.ApplyPageLayoutUVE(*w);
            if (m_host.m_app != nullptr) {
                m_host.m_app->invalidate();
            }
            return;
        }
        if (glfwGetWindowAttrib(w->window, GLFW_MAXIMIZED) != 0) {
            glfwRestoreWindow(w->window);
        } else {
            glfwMaximizeWindow(w->window);
        }
    }
    bool windowMaximized(const int id) const override {
        const WindowUVE* w = m_host.FindUVE(id);
        if (w == nullptr) {
            return false;
        }
        if (w->page) {
            return !w->pageFloating;
        }
        return glfwGetWindowAttrib(w->window, GLFW_MAXIMIZED) != 0;
    }

    [[nodiscard]] bool TextInputActiveUVE() const noexcept { return m_textInput; }

    void SetViewCursorUVE(const int window, const ViewCursorUVE cursor) {
        prism::Cursor mapped = prism::Cursor::Arrow;
        switch (cursor) {
            case ViewCursorUVE::Arrow: mapped = prism::Cursor::Arrow; break;
            case ViewCursorUVE::Hand: mapped = prism::Cursor::Hand; break;
            case ViewCursorUVE::ResizeAll: mapped = prism::Cursor::SizeAll; break;
            case ViewCursorUVE::ResizeNS: mapped = prism::Cursor::SizeNS; break;
            case ViewCursorUVE::ResizeEW: mapped = prism::Cursor::SizeWE; break;
            case ViewCursorUVE::ResizeNWSE: mapped = prism::Cursor::SizeAll; break;
            case ViewCursorUVE::ResizeNESW: mapped = prism::Cursor::SizeAll; break;
        }
        ApplyCursorUVE(window, m_cursors[static_cast<int>(mapped)]);
    }
    void SetResizeCursorUVE(const int window, const int edges) {
        const bool horizontal = (edges & 3) != 0;
        const bool vertical = (edges & 12) != 0;
        prism::Cursor c = prism::Cursor::Arrow;
        if (horizontal && vertical) { c = prism::Cursor::SizeAll; }
        else if (horizontal) { c = prism::Cursor::SizeWE; }
        else if (vertical) { c = prism::Cursor::SizeNS; }
        ApplyCursorUVE(window, m_cursors[static_cast<int>(c)]);
    }
    void ForgetWindowUVE(const int window) {
        m_applied.erase(window);
        m_uiCursor.erase(window);
    }

private:
    void ApplyCursorUVE(const int window, GLFWcursor* cursor) {
        WindowUVE* w = m_host.FindUVE(window);
        if (w == nullptr) {
            return;
        }
        GLFWcursor*& applied = m_applied[window];
        if (cursor != applied) {
            applied = cursor;
            glfwSetCursor(w->window, cursor);
        }
    }

    EditorHostUVE& m_host;
    GLFWwindow* m_window = nullptr;
    std::array<GLFWcursor*, 6> m_cursors{};
    std::map<int, GLFWcursor*> m_applied;
    std::map<int, prism::Cursor> m_uiCursor;
    bool m_textInput = false;
};

// ------------------------------------------------------------------------------------ compositor

/// Draws textured rectangles into the default framebuffer: engine view textures into their holes,
/// then the PRISM frame (premultiplied, transparent in the holes) over the whole window.
struct EditorHostUVE::CompositorUVE final {
    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
    PFNGLUNIFORM4FPROC glUniform4f = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i = nullptr;
    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
    PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
    PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;

    GLuint program = 0;
    /// GLSL header of the current context (desktop core or GLES), put before each shader source.
    const char* glslHeader = nullptr;
    GLuint vao = 0;
    GLint rectLoc = -1;
    GLint uvLoc = -1;
    GLint texLoc = -1;

    template <typename T>
    bool LoadUVE(T& fn, const char* name) {
        fn = reinterpret_cast<T>(glfwGetProcAddress(name));
        return fn != nullptr;
    }

    [[nodiscard]] bool InitializeUVE() {
        bool ok = true;
        ok &= LoadUVE(glCreateShader, "glCreateShader");
        ok &= LoadUVE(glShaderSource, "glShaderSource");
        ok &= LoadUVE(glCompileShader, "glCompileShader");
        ok &= LoadUVE(glGetShaderiv, "glGetShaderiv");
        ok &= LoadUVE(glDeleteShader, "glDeleteShader");
        ok &= LoadUVE(glCreateProgram, "glCreateProgram");
        ok &= LoadUVE(glAttachShader, "glAttachShader");
        ok &= LoadUVE(glLinkProgram, "glLinkProgram");
        ok &= LoadUVE(glGetProgramiv, "glGetProgramiv");
        ok &= LoadUVE(glDeleteProgram, "glDeleteProgram");
        ok &= LoadUVE(glUseProgram, "glUseProgram");
        ok &= LoadUVE(glGetUniformLocation, "glGetUniformLocation");
        ok &= LoadUVE(glUniform4f, "glUniform4f");
        ok &= LoadUVE(glUniform1i, "glUniform1i");
        ok &= LoadUVE(glGenVertexArrays, "glGenVertexArrays");
        ok &= LoadUVE(glBindVertexArray, "glBindVertexArray");
        ok &= LoadUVE(glDeleteVertexArrays, "glDeleteVertexArrays");
        ok &= LoadUVE(glBindFramebuffer, "glBindFramebuffer");
        ok &= LoadUVE(glActiveTexture, "glActiveTexture");
        ok &= LoadUVE(glBlendFuncSeparate, "glBlendFuncSeparate");
        ok &= LoadUVE(glGetShaderInfoLog, "glGetShaderInfoLog");
        ok &= LoadUVE(glGetProgramInfoLog, "glGetProgramInfoLog");
        if (!ok) {
            UVE_ERROR("Editor host: the GL context lacks a function the compositor needs");
            return false;
        }
        glslHeader = prism::glslHeaderFor(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        // A quad from gl_VertexID: uRect = NDC (x0, y0, x1, y1), uUv = texture (u0, v0, u1, v1).
        // No #version line: CompileUVE puts the context's header first (desktop core or GLES).
        static const char* const kVertex = R"(
uniform vec4 uRect;
uniform vec4 uUv;
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), 0.0, 1.0);
    vUv = mix(uUv.xy, uUv.zw, corner);
})";
        static const char* const kFragment = R"(
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); })";
        const GLuint vs = CompileUVE(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = CompileUVE(GL_FRAGMENT_SHADER, kFragment);
        if (vs == 0 || fs == 0) {
            return false;
        }
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == 0) {
            std::array<char, kShaderLogCapacityUVE> log{};
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            UVE_ERROR("Editor host: the compositor program did not link: {}", log.data());
            glDeleteProgram(program);
            program = 0;
            return false;
        }
        rectLoc = glGetUniformLocation(program, "uRect");
        uvLoc = glGetUniformLocation(program, "uUv");
        texLoc = glGetUniformLocation(program, "uTex");
        glGenVertexArrays(1, &vao);
        return true;
    }

    [[nodiscard]] GLuint CompileUVE(const GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        const std::array<const char*, 2> parts{glslHeader, source};
        glShaderSource(shader, static_cast<GLsizei>(parts.size()), parts.data(), nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == 0) {
            std::array<char, kShaderLogCapacityUVE> log{};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            UVE_ERROR("Editor host: a compositor shader did not compile: {}", log.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void ShutdownUVE() {
        if (vao != 0 && glDeleteVertexArrays != nullptr) {
            glDeleteVertexArrays(1, &vao);
            vao = 0;
        }
        if (program != 0 && glDeleteProgram != nullptr) {
            glDeleteProgram(program);
            program = 0;
        }
    }

    /// Draws `texture` (bottom-up) into window rect `r` (top-left origin, window `w` x `h`).
    void DrawUVE(const GLuint texture, const prism::Rect& r, const int w, const int h, const float u1,
                 const float v1) {
        const float x0 = (r.x / static_cast<float>(w)) * 2.0F - 1.0F;
        const float x1 = ((r.x + r.w) / static_cast<float>(w)) * 2.0F - 1.0F;
        const float yTop = 1.0F - (r.y / static_cast<float>(h)) * 2.0F;
        const float yBottom = 1.0F - ((r.y + r.h) / static_cast<float>(h)) * 2.0F;
        glUniform4f(rectLoc, x0, yBottom, x1, yTop);
        glUniform4f(uvLoc, 0.0F, 0.0F, u1, v1);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
};

// ------------------------------------------------------------------------------------ GPU timer

/// The GPU time of the main window's frame: a ring of time-elapsed queries, each read only once the
/// GPU reports it finished, so the CPU never waits for a result. Nothing is measured where the GL
/// has no timer queries (desktop GL before 3.3, GLES without GL_EXT_disjoint_timer_query).
struct EditorHostUVE::GpuTimerUVE final {
    static constexpr std::size_t kSlotsUVE = 4U;
#if defined(__ANDROID__)
    static constexpr GLenum kTargetUVE = GL_TIME_ELAPSED_EXT;
    PFNGLGETQUERYOBJECTUI64VEXTPROC getQueryObjectui64v = nullptr;
#else
    static constexpr GLenum kTargetUVE = GL_TIME_ELAPSED;
    PFNGLGETQUERYOBJECTUI64VPROC getQueryObjectui64v = nullptr;
#endif
    PFNGLGENQUERIESPROC genQueries = nullptr;
    PFNGLDELETEQUERIESPROC deleteQueries = nullptr;
    PFNGLBEGINQUERYPROC beginQuery = nullptr;
    PFNGLENDQUERYPROC endQuery = nullptr;
    PFNGLGETQUERYOBJECTUIVPROC getQueryObjectuiv = nullptr;

    std::array<GLuint, kSlotsUVE> queries{};
    std::array<bool, kSlotsUVE> pending{};
    /// The GPU's clock jumped (power state change, context reset) while the query ran: no result.
    std::array<bool, kSlotsUVE> tainted{};
    std::size_t next = 0U;   ///< the slot the next frame uses, which is also the oldest one issued
    bool supported = false;
    bool active = false;
    float lastMs = 0.0F;

    template <typename T>
    static bool LoadUVE(T& fn, const std::string& name) {
#if defined(__ANDROID__)
        // GLES: the extension's own entry point, else the ES 3 core one it aliases.
        fn = reinterpret_cast<T>(glfwGetProcAddress((name + "EXT").c_str()));
        if (fn != nullptr) {
            return true;
        }
#endif
        fn = reinterpret_cast<T>(glfwGetProcAddress(name.c_str()));
        return fn != nullptr;
    }

    void InitializeUVE() {
#if defined(__ANDROID__)
        const char* const extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions == nullptr ||
            (" " + std::string(extensions) + " ").find(" GL_EXT_disjoint_timer_query ") == std::string::npos) {
            return;
        }
#else
        const char* const version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        int major = 0;
        int minor = 0;
        if (version == nullptr || std::sscanf(version, "%d.%d", &major, &minor) != 2 || major < 3 ||
            (major == 3 && minor < 3)) {
            return;
        }
#endif
        bool ok = LoadUVE(genQueries, "glGenQueries");
        ok &= LoadUVE(deleteQueries, "glDeleteQueries");
        ok &= LoadUVE(beginQuery, "glBeginQuery");
        ok &= LoadUVE(endQuery, "glEndQuery");
        ok &= LoadUVE(getQueryObjectuiv, "glGetQueryObjectuiv");
        ok &= LoadUVE(getQueryObjectui64v, "glGetQueryObjectui64v");
        if (!ok) {
            return;
        }
        genQueries(static_cast<GLsizei>(kSlotsUVE), queries.data());
        supported = true;
    }

    /// Takes every finished result, oldest first, so lastMs ends on the newest one.
    void PollUVE() {
#if defined(__ANDROID__)
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
#endif
        for (std::size_t k = 0U; k < kSlotsUVE; ++k) {
            const std::size_t i = (next + k) % kSlotsUVE;
            if (!pending[i]) {
                continue;
            }
#if defined(__ANDROID__)
            if (disjoint != 0) {
                tainted[i] = true;
            }
#endif
            GLuint available = 0U;
            getQueryObjectuiv(queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == 0U) {
                continue;
            }
            GLuint64 nanoseconds = 0U;
            getQueryObjectui64v(queries[i], GL_QUERY_RESULT, &nanoseconds);
            pending[i] = false;
            if (!tainted[i]) {
                lastMs = static_cast<float>(static_cast<double>(nanoseconds) / 1.0e6);
            }
        }
    }

    void BeginUVE() {
        if (!supported || active) {
            return;
        }
        PollUVE();
        if (pending[next]) {
            return;   // the GPU is a whole ring behind: this frame goes untimed rather than waited for
        }
        tainted[next] = false;
        beginQuery(kTargetUVE, queries[next]);
        active = true;
    }

    void EndUVE() {
        if (!active) {
            return;
        }
        endQuery(kTargetUVE);
        pending[next] = true;
        next = (next + 1U) % kSlotsUVE;
        active = false;
    }

    void ShutdownUVE() {
        EndUVE();
        if (supported) {
            deleteQueries(static_cast<GLsizei>(kSlotsUVE), queries.data());
            supported = false;
        }
    }
};

// ------------------------------------------------------------------------------- GLFW callbacks

/// The main window's callbacks, chained to what the engine installed before.
struct EditorHostUVE::ChainUVE final {
    GLFWcursorposfun cursorPos = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorenterfun cursorEnter = nullptr;
#if !defined(__ANDROID__)
    GLFWdropfun drop = nullptr;
#endif

    [[nodiscard]] static EditorHostUVE::WindowUVE* MainUVE() {
        return g_activeHostUVE != nullptr ? g_activeHostUVE->m_main.get() : nullptr;
    }
    /// On a single surface the page on screen takes the main window's input; runs `fn` on it (in its
    /// scope) and returns true, or false when the main window has the screen.
    template <typename F>
    [[nodiscard]] static bool ToPageUVE(F&& fn) {
        WindowUVE* const page = g_activeHostUVE != nullptr ? g_activeHostUVE->VisiblePageUVE() : nullptr;
        if (page == nullptr) {
            return false;
        }
        if (page->scope) {
            page->scope(std::function<void()>([&fn, page] { fn(*page); }));
        } else {
            fn(*page);
        }
        return true;
    }
    /// On a single surface a full page takes all input; a floating page takes input inside its
    /// bounds (or while dragging / holding a button / showing a blocking popup) so the main editor
    /// underneath stays interactive beside it.
    template <typename F>
    [[nodiscard]] static bool PointerToPageUVE(const double x, const double y, F&& fn) {
        WindowUVE* const page = g_activeHostUVE != nullptr ? g_activeHostUVE->VisiblePageUVE() : nullptr;
        if (page == nullptr) {
            return false;
        }
        if (page->pageFloating && g_activeHostUVE->m_main != nullptr) {
            const prism::Vec2 mp = g_activeHostUVE->ToUiUVE(*g_activeHostUVE->m_main, x, y);
            const bool held = page->down[0] || page->down[1] || page->down[2];
            const bool inside = page->pageBounds.contains(mp);
            const bool popup = g_activeHostUVE->m_app != nullptr &&
                               g_activeHostUVE->m_app->windowHasBlockingPopup(page->id);
            if (!page->pageDragging && !held && !inside && !popup) {
                return false;
            }
        }
        if (page->scope) {
            page->scope(std::function<void()>([&fn, page] { fn(*page); }));
        } else {
            fn(*page);
        }
        return true;
    }
    static void CursorPosUVE(GLFWwindow* w, const double x, const double y) {
        if (PointerToPageUVE(x, y, [x, y](WindowUVE& page) { g_activeHostUVE->OnCursorPosUVE(page, x, y); })) {
            return;
        }
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->cursorPos != nullptr) { g_activeHostUVE->m_chain->cursorPos(w, x, y); }
            g_activeHostUVE->OnCursorPosUVE(*main, x, y);
        }
    }
    static void MouseButtonUVE(GLFWwindow* w, const int button, const int action, const int mods) {
        double cx = 0.0;
        double cy = 0.0;
        if (w != nullptr) {
            glfwGetCursorPos(w, &cx, &cy);
        }
        if (PointerToPageUVE(cx, cy, [=](WindowUVE& page) { g_activeHostUVE->OnMouseButtonUVE(page, button, action, mods); })) {
            return;
        }
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->mouseButton != nullptr) {
                g_activeHostUVE->m_chain->mouseButton(w, button, action, mods);
            }
            g_activeHostUVE->OnMouseButtonUVE(*main, button, action, mods);
        }
    }
    static void ScrollUVE(GLFWwindow* w, const double dx, const double dy) {
        double cx = 0.0;
        double cy = 0.0;
        if (w != nullptr) {
            glfwGetCursorPos(w, &cx, &cy);
        }
        if (PointerToPageUVE(cx, cy, [=](WindowUVE& page) { g_activeHostUVE->OnScrollUVE(page, dx, dy); })) {
            return;
        }
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->scroll != nullptr) { g_activeHostUVE->m_chain->scroll(w, dx, dy); }
            g_activeHostUVE->OnScrollUVE(*main, dx, dy);
        }
    }
    static void KeyUVE(GLFWwindow* w, const int key, const int scancode, const int action, const int mods) {
        if (ToPageUVE([=](WindowUVE& page) { g_activeHostUVE->OnKeyUVE(page, key, scancode, action, mods); })) {
            return;
        }
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->key != nullptr) { g_activeHostUVE->m_chain->key(w, key, scancode, action, mods); }
            g_activeHostUVE->OnKeyUVE(*main, key, scancode, action, mods);
        }
    }
    static void CharUVE(GLFWwindow* w, const unsigned int codepoint) {
        if (ToPageUVE([=](WindowUVE& page) { g_activeHostUVE->OnCharUVE(page, codepoint); })) {
            return;
        }
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->character != nullptr) { g_activeHostUVE->m_chain->character(w, codepoint); }
            g_activeHostUVE->OnCharUVE(*main, codepoint);
        }
    }
    static void CursorEnterUVE(GLFWwindow* w, const int entered) {
        if (WindowUVE* main = MainUVE()) {
            if (g_activeHostUVE->m_chain->cursorEnter != nullptr) { g_activeHostUVE->m_chain->cursorEnter(w, entered); }
            g_activeHostUVE->OnCursorEnterUVE(*main, entered);
        }
    }
#if !defined(__ANDROID__)
    static void DropUVE(GLFWwindow* w, const int count, const char** paths) {
        if (g_activeHostUVE == nullptr) {
            return;
        }
        if (g_activeHostUVE->m_chain->drop != nullptr) { g_activeHostUVE->m_chain->drop(w, count, paths); }
        if (!g_activeHostUVE->onFilesDropped || count <= 0 || paths == nullptr) {
            return;
        }
        std::vector<std::filesystem::path> files;
        for (int i = 0; i < count; ++i) {
            if (paths[i] != nullptr) {   // GLFW hands UTF-8
                const std::string utf8(paths[i]);
                files.emplace_back(std::u8string(utf8.begin(), utf8.end()));
            }
        }
        g_activeHostUVE->onFilesDropped(std::move(files));
    }
#endif
};

namespace {

/// A secondary window's callbacks: its GLFW user pointer is its WindowUVE.
using HostWindowUVE = EditorHostUVE::WindowUVE;
[[nodiscard]] HostWindowUVE* OwnerUVE(GLFWwindow* w) {
    return static_cast<HostWindowUVE*>(glfwGetWindowUserPointer(w));
}
/// Runs `fn` inside the window's scope, when it has one.
template <typename F>
void ScopedUVE(HostWindowUVE& o, F&& fn) {
    if (o.scope) {
        o.scope(std::function<void()>(std::forward<F>(fn)));
    } else {
        fn();
    }
}
void SecondaryCursorPosUVE(GLFWwindow* w, const double x, const double y) {
    if (HostWindowUVE* o = OwnerUVE(w)) { ScopedUVE(*o, [o, x, y] { o->host->OnCursorPosUVE(*o, x, y); }); }
}
void SecondaryMouseButtonUVE(GLFWwindow* w, const int button, const int action, const int mods) {
    if (HostWindowUVE* o = OwnerUVE(w)) {
        ScopedUVE(*o, [o, button, action, mods] { o->host->OnMouseButtonUVE(*o, button, action, mods); });
    }
}
void SecondaryScrollUVE(GLFWwindow* w, const double dx, const double dy) {
    if (HostWindowUVE* o = OwnerUVE(w)) { ScopedUVE(*o, [o, dx, dy] { o->host->OnScrollUVE(*o, dx, dy); }); }
}
void SecondaryKeyUVE(GLFWwindow* w, const int key, const int scancode, const int action, const int mods) {
    if (HostWindowUVE* o = OwnerUVE(w)) {
        ScopedUVE(*o, [o, key, scancode, action, mods] { o->host->OnKeyUVE(*o, key, scancode, action, mods); });
    }
}
void SecondaryCharUVE(GLFWwindow* w, const unsigned int codepoint) {
    if (HostWindowUVE* o = OwnerUVE(w)) { ScopedUVE(*o, [o, codepoint] { o->host->OnCharUVE(*o, codepoint); }); }
}
void SecondaryCursorEnterUVE(GLFWwindow* w, const int entered) {
    if (HostWindowUVE* o = OwnerUVE(w)) { ScopedUVE(*o, [o, entered] { o->host->OnCursorEnterUVE(*o, entered); }); }
}
void SecondaryCloseUVE(GLFWwindow* w) {
    // The editor decides (unsaved changes ask first); the window stays until it closes it.
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    if (HostWindowUVE* o = OwnerUVE(w)) { o->closeRequested = true; }
}
void SecondaryFocusUVE(GLFWwindow* w, const int focused) {
    if (HostWindowUVE* o = OwnerUVE(w); o != nullptr && focused != 0) {
        o->host->FocusWindowUVE(o->id);
    }
}

} // namespace

// ------------------------------------------------------------------------------------------ host

EditorHostUVE::EditorHostUVE() = default;

EditorHostUVE::~EditorHostUVE() {
    ShutdownUVE();
}

EditorHostUVE::WindowUVE* EditorHostUVE::FindUVE(const int id) const {
    if (id == kMainWindowUVE) {
        return m_main.get();
    }
    const auto it = m_windows.find(id);
    return it != m_windows.end() ? it->second.get() : nullptr;
}

namespace {

/// The UniVex mark every PRISM window shows in its title bar, inside PRISM's asset directory.
inline constexpr std::string_view kBrandMarkFileUVE = "images/univex_logo.png";

/// Hands the brand mark to PRISM; a missing or unreadable image leaves the title bars without one.
void LoadBrandMarkUVE(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(raw.size());
    std::transform(raw.begin(), raw.end(), bytes.begin(), [](const char value) { return static_cast<std::byte>(value); });
    UVE::Asset::PngRgba8ImageUVE image;
    if (bytes.empty() || !UVE::Asset::DecodePngRgba8ImageUVE(bytes, image) || image.width == 0U || image.height == 0U) {
        return;
    }
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t index = 0U; index < argb.size(); ++index) {
        const std::byte* const pixel = image.pixels.data() + index * UVE::Asset::kPngRgba8ChannelsUVE;
        argb[index] = prism::Color(std::to_integer<std::uint8_t>(pixel[0]), std::to_integer<std::uint8_t>(pixel[1]),
                                   std::to_integer<std::uint8_t>(pixel[2]), std::to_integer<std::uint8_t>(pixel[3]))
                          .argb();
    }
    prism::setBrandMark(std::move(argb), static_cast<int>(image.width), static_cast<int>(image.height));
}

} // namespace

bool EditorHostUVE::InitializeUVE(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (m_ready || window == nullptr || g_activeHostUVE != nullptr) {
        return false;
    }
    m_main = std::make_unique<WindowUVE>();
    m_main->host = this;
    m_main->id = kMainWindowUVE;
    m_main->window = window;
    m_singleSurface = kSingleSurfaceUVE;
    glfwGetFramebufferSize(window, &m_main->fbWidth, &m_main->fbHeight);
    m_main->fbWidth = std::max(1, m_main->fbWidth);
    m_main->fbHeight = std::max(1, m_main->fbHeight);
    m_main->uiScale = ComputeUiScaleUVE(window, m_main->fbWidth, m_main->fbHeight, m_singleSurface);
    m_main->uiWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_main->fbWidth) / m_main->uiScale)));
    m_main->uiHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_main->fbHeight) / m_main->uiScale)));

    m_platform = std::make_shared<PlatformUVE>(*this, window);
    m_app = std::make_unique<prism::Application>();
    prism::HostDesc desc{};
    desc.width = m_main->uiWidth;
    desc.height = m_main->uiHeight;
    desc.assetDir = assetDir.string();
    if (!m_app->initHosted(desc, m_platform)) {
        m_app.reset();
        m_main.reset();
        return false;
    }

    {
        prism::Theme& t = prism::themeMutable();
        if (m_singleSurface) {
            t.controlH = 20.0F;
            t.rowH = 19.0F;
            t.menuBarH = 20.0F;
            t.toolbarH = 22.0F;
            t.tabH = 20.0F;
            t.statusH = 19.0F;
            t.pad = 5.0F;
            t.gap = 3.0F;
        } else {
            t.controlH = 22.0F;
            t.rowH = 20.0F;
            t.menuBarH = 20.0F;
            t.toolbarH = 24.0F;
            t.tabH = 22.0F;
            t.statusH = 20.0F;
            t.pad = 6.0F;
            t.gap = 4.0F;
        }
    }

    LoadBrandMarkUVE(assetDir / kBrandMarkFileUVE);

    std::string error;
    m_backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
    if (m_backend == nullptr) {
        std::fprintf(stderr, "PRISM: OpenGL backend unavailable: %s\n", error.c_str());
        m_app.reset();
        m_main.reset();
        return false;
    }
    m_backend->setPixelScale(m_main->uiScale);
    m_backend->resize(m_main->fbWidth, m_main->fbHeight);
    m_app->setRenderBackend(m_backend);

    m_compositor = std::make_unique<CompositorUVE>();
    if (!m_compositor->InitializeUVE()) {
        std::fprintf(stderr, "PRISM: compositor shaders unavailable\n");
        m_compositor.reset();
        m_backend.reset();
        m_app.reset();
        m_main.reset();
        return false;
    }

    g_activeHostUVE = this;
    m_chain = std::make_unique<ChainUVE>();
    m_chain->cursorPos = glfwSetCursorPosCallback(window, &ChainUVE::CursorPosUVE);
    m_chain->mouseButton = glfwSetMouseButtonCallback(window, &ChainUVE::MouseButtonUVE);
    m_chain->scroll = glfwSetScrollCallback(window, &ChainUVE::ScrollUVE);
    m_chain->key = glfwSetKeyCallback(window, &ChainUVE::KeyUVE);
    m_chain->character = glfwSetCharCallback(window, &ChainUVE::CharUVE);
    m_chain->cursorEnter = glfwSetCursorEnterCallback(window, &ChainUVE::CursorEnterUVE);
#if !defined(__ANDROID__)
    m_chain->drop = glfwSetDropCallback(window, &ChainUVE::DropUVE);
#endif

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    m_main->mouse = ToUiUVE(*m_main, x, y);
    m_main->frameStartMouse = m_main->mouse;
    m_main->lastFrameTime = glfwGetTime();
    m_ready = true;
    return true;
}

void EditorHostUVE::ShutdownUVE() {
    if (!m_ready) {
        return;
    }
    while (!m_windows.empty()) {
        CloseWindowUVE(m_windows.begin()->first);
    }
    GLFWwindow* const window = m_main->window;
    if (window != nullptr && m_chain != nullptr) {
        glfwSetCursorPosCallback(window, m_chain->cursorPos);
        glfwSetMouseButtonCallback(window, m_chain->mouseButton);
        glfwSetScrollCallback(window, m_chain->scroll);
        glfwSetKeyCallback(window, m_chain->key);
        glfwSetCharCallback(window, m_chain->character);
        glfwSetCursorEnterCallback(window, m_chain->cursorEnter);
#if !defined(__ANDROID__)
        glfwSetDropCallback(window, m_chain->drop);
#endif
    }
    if (g_activeHostUVE == this) {
        g_activeHostUVE = nullptr;
    }
    m_chain.reset();
    if (m_gpuTimer != nullptr) {
        m_gpuTimer->ShutdownUVE();
        m_gpuTimer.reset();
    }
    if (m_compositor != nullptr) {
        m_compositor->ShutdownUVE();
        m_compositor.reset();
    }
    if (m_app != nullptr) {
        m_app->setRenderBackend(nullptr);
        m_app->setRoot(nullptr);
    }
    m_backend.reset();
    m_app.reset();
    m_platform.reset();
    m_main.reset();
    m_ready = false;
}

void EditorHostUVE::SetRootUVE(prism::Ref<prism::Widget> root) {
    if (m_app != nullptr) {
        m_app->setRoot(std::move(root));
    }
}

int EditorHostUVE::OpenWindowUVE(prism::Ref<prism::Widget> root, const std::string& title, const int width,
                                      const int height) {
    if (!m_ready || root == nullptr) {
        return 0;
    }
    if (m_singleSurface) {
        // A page over the main window: the same surface and context, the full screen, and the main
        // window's input while it is on top.
        auto w = std::make_unique<WindowUVE>();
        w->host = this;
        w->window = m_main->window;
        w->page = true;
        w->fbWidth = m_main->fbWidth;
        w->fbHeight = m_main->fbHeight;
        w->uiScale = m_main->uiScale;
        w->uiWidth = m_main->uiWidth;
        w->uiHeight = m_main->uiHeight;
        std::string error;
        w->backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
        if (w->backend == nullptr) {
            UVE_ERROR("Editor host: no renderer for the page {}: {}", title, error);
            return 0;
        }
        w->backend->setPixelScale(w->uiScale);
        w->backend->resize(w->fbWidth, w->fbHeight);
        prism::OsWindowDesc desc{};
        desc.title = title;
        desc.width = w->uiWidth;
        desc.height = w->uiHeight;
        const int id = m_app->openWindow(std::move(root), desc);
        if (id == 0) {
            return 0;
        }
        w->id = id;
        w->mouse = m_main->mouse;
        w->frameStartMouse = m_main->mouse;
        w->lastFrameTime = glfwGetTime();
        m_app->windowResize(id, w->uiWidth, w->uiHeight);
        m_windows.emplace(id, std::move(w));
        ShowPageUVE(id);
        return id;
    }
    GLFWwindow* const main = m_main->window;
    // The same kind of context as the main window, sharing its objects: textures rendered there
    // (the UI, the engine views) are drawn here.
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, glfwGetWindowAttrib(main, GLFW_CONTEXT_VERSION_MAJOR));
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, glfwGetWindowAttrib(main, GLFW_CONTEXT_VERSION_MINOR));
    glfwWindowHint(GLFW_OPENGL_PROFILE, glfwGetWindowAttrib(main, GLFW_OPENGL_PROFILE));
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, glfwGetWindowAttrib(main, GLFW_OPENGL_FORWARD_COMPAT));
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);   // PRISM draws the title bar
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* const window = glfwCreateWindow(std::max(320, width), std::max(240, height), title.c_str(), nullptr, main);
    glfwDefaultWindowHints();
    glfwMakeContextCurrent(main);
    if (window == nullptr) {
        return 0;
    }

    auto w = std::make_unique<WindowUVE>();
    w->host = this;
    w->window = window;
    glfwGetFramebufferSize(window, &w->fbWidth, &w->fbHeight);
    w->fbWidth = std::max(1, w->fbWidth);
    w->fbHeight = std::max(1, w->fbHeight);
    w->uiScale = 1.0F;
    w->uiWidth = w->fbWidth;
    w->uiHeight = w->fbHeight;
    std::string error;
    w->backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
    if (w->backend == nullptr) {
        glfwDestroyWindow(window);
        return 0;
    }
    w->backend->resize(w->fbWidth, w->fbHeight);

    // Placed beside the main window, offset like a new document window.
    int mainX = 0;
    int mainY = 0;
    glfwGetWindowPos(main, &mainX, &mainY);
    glfwSetWindowPos(window, mainX + 60, mainY + 60);
    glfwSetWindowSizeLimits(window, 640, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);

    prism::OsWindowDesc desc{};
    desc.title = title;
    desc.width = w->fbWidth;
    desc.height = w->fbHeight;
    const int id = m_app->openWindow(std::move(root), desc);
    if (id == 0) {
        glfwDestroyWindow(window);
        return 0;
    }
    w->id = id;
    glfwSetWindowUserPointer(window, w.get());
    glfwSetCursorPosCallback(window, &SecondaryCursorPosUVE);
    glfwSetMouseButtonCallback(window, &SecondaryMouseButtonUVE);
    glfwSetScrollCallback(window, &SecondaryScrollUVE);
    glfwSetKeyCallback(window, &SecondaryKeyUVE);
    glfwSetCharCallback(window, &SecondaryCharUVE);
    glfwSetCursorEnterCallback(window, &SecondaryCursorEnterUVE);
    glfwSetWindowCloseCallback(window, &SecondaryCloseUVE);
    glfwSetWindowFocusCallback(window, &SecondaryFocusUVE);
    w->lastFrameTime = glfwGetTime();
    m_app->windowResize(id, w->fbWidth, w->fbHeight);
    m_windows.emplace(id, std::move(w));
    return id;
}

void EditorHostUVE::CloseWindowUVE(const int id) {
    const auto it = m_windows.find(id);
    if (it == m_windows.end()) {
        return;
    }
    std::unique_ptr<WindowUVE> w = std::move(it->second);
    m_windows.erase(it);
    m_app->closeWindow(id);
    m_platform->ForgetWindowUVE(id);
    if (w->page) {
        // The main surface stays; the page only had its own UI renderer.
        m_pageOrder.erase(std::remove(m_pageOrder.begin(), m_pageOrder.end(), id), m_pageOrder.end());
        w->backend.reset();
        if (WindowUVE* const shown = VisiblePageUVE()) {
            m_app->activateWindow(shown->id);
        } else {
            m_app->activateWindow(kMainWindowUVE);
        }
        return;
    }
    if (w->vao != 0) {
        // The vertex array belongs to that window's context.
        glfwMakeContextCurrent(w->window);
        m_compositor->glDeleteVertexArrays(1, &w->vao);
        glfwMakeContextCurrent(m_main->window);
    }
    w->backend.reset();   // main context: its texture and framebuffer live there
    glfwSetWindowUserPointer(w->window, nullptr);
    glfwDestroyWindow(w->window);
    glfwMakeContextCurrent(m_main->window);
}

void EditorHostUVE::SetWindowScopeUVE(const int id, WindowScopeUVE scope) {
    if (WindowUVE* w = id != kMainWindowUVE ? FindUVE(id) : nullptr) {
        w->scope = std::move(scope);
    }
}

bool EditorHostUVE::HasWindowUVE(const int id) const {
    return FindUVE(id) != nullptr;
}

bool EditorHostUVE::WindowCloseRequestedUVE(const int id) const {
    const WindowUVE* w = FindUVE(id);
    return w != nullptr && w->closeRequested;
}

void EditorHostUVE::ClearCloseRequestUVE(const int id) {
    if (WindowUVE* w = FindUVE(id)) {
        w->closeRequested = false;
    }
}

void EditorHostUVE::FocusWindowUVE(const int id) {
    WindowUVE* w = FindUVE(id);
    if (w == nullptr) {
        return;
    }
    if (w->page) {
        ShowPageUVE(id);
        return;
    }
    m_app->activateWindow(id);
    if (glfwGetWindowAttrib(w->window, GLFW_ICONIFIED) != 0) {
        glfwRestoreWindow(w->window);
    }
    if (glfwGetWindowAttrib(w->window, GLFW_FOCUSED) == 0) {
        glfwShowWindow(w->window);
        glfwFocusWindow(w->window);
    }
}

prism::Vec2 EditorHostUVE::ToUiUVE(const WindowUVE& w, const double x, const double y) const {
    if (w.page && w.pageFloating && m_main != nullptr) {
        const prism::Vec2 mp = ToUiUVE(*m_main, x, y);
        return prism::Vec2{mp.x - w.pageBounds.x, mp.y - w.pageBounds.y};
    }
    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(w.window, &winW, &winH);
    const float sx = winW > 0 ? static_cast<float>(w.uiWidth) / static_cast<float>(winW) : 1.0F;
    const float sy = winH > 0 ? static_cast<float>(w.uiHeight) / static_cast<float>(winH) : 1.0F;
    return prism::Vec2{static_cast<float>(x) * sx, static_cast<float>(y) * sy};
}

bool EditorHostUVE::InTargetScopeUVE(const WindowUVE& source, const WindowUVE& target, const std::function<void()>& fn) {
    // Input of `source` runs inside its scope already; `fn` must run in `target`'s. A scope cannot be
    // left from inside, so a drag out of a scoped window into an unscoped one is not delivered.
    if (target.scope && !source.scope) {
        target.scope(fn);
        return true;
    }
    if (source.scope && !target.scope) {
        return false;
    }
    fn();
    return true;
}

EditorHostUVE::WindowUVE* EditorHostUVE::DragTargetUVE(const WindowUVE& source, prism::Vec2& local) const {
    // A PRISM drag (a Asset Table item, say) can end over another of the editor's windows. The
    // OS keeps sending the pointer to the window the press started in; GLFW_HOVERED asks the OS which
    // window is really under the pointer (its own stacking, not PRISM's order).
    if (!m_app || !m_app->isDragging() || source.window == nullptr || glfwGetWindowAttrib(source.window, GLFW_HOVERED) != 0) {
        return nullptr;
    }
    const auto hovered = [&](WindowUVE* const target) -> WindowUVE* {
        if (target == nullptr || target == &source || target->window == nullptr ||
            glfwGetWindowAttrib(target->window, GLFW_HOVERED) == 0) {
            return nullptr;
        }
        double cx = 0.0;
        double cy = 0.0;
        glfwGetCursorPos(target->window, &cx, &cy);
        local = ToUiUVE(*target, cx, cy);
        return target;
    };
    if (WindowUVE* const target = hovered(m_main.get())) {
        return target;
    }
    for (const auto& [id, window] : m_windows) {
        if (WindowUVE* const target = hovered(window.get())) {
            return target;
        }
    }
    return nullptr;
}

void EditorHostUVE::ApplyPageLayoutUVE(WindowUVE& w) {
    if (!w.page || m_main == nullptr) {
        return;
    }
    const float scale = std::max(1.0F, m_main->uiScale);
    int targetUiW = m_main->uiWidth;
    int targetUiH = m_main->uiHeight;
    if (w.pageFloating) {
        const float maxW = std::max(280.0F, static_cast<float>(m_main->uiWidth) - 16.0F);
        const float maxH = std::max(200.0F, static_cast<float>(m_main->uiHeight) - 16.0F);
        if (w.pageBounds.w <= 1.0F || w.pageBounds.h <= 1.0F) {
            const float fw = std::clamp(std::floor(static_cast<float>(m_main->uiWidth) * 0.86F),
                                        std::min(320.0F, maxW), maxW);
            const float fh = std::clamp(std::floor(static_cast<float>(m_main->uiHeight) * 0.84F),
                                        std::min(220.0F, maxH), maxH);
            w.pageBounds = prism::Rect{
                std::floor((static_cast<float>(m_main->uiWidth) - fw) * 0.5F),
                std::floor((static_cast<float>(m_main->uiHeight) - fh) * 0.5F),
                fw, fh};
        } else {
            w.pageBounds.w = std::clamp(w.pageBounds.w, std::min(280.0F, maxW), maxW);
            w.pageBounds.h = std::clamp(w.pageBounds.h, std::min(200.0F, maxH), maxH);
            w.pageBounds.x = std::clamp(w.pageBounds.x, 4.0F, std::max(4.0F, static_cast<float>(m_main->uiWidth) - w.pageBounds.w - 4.0F));
            w.pageBounds.y = std::clamp(w.pageBounds.y, 4.0F, std::max(4.0F, static_cast<float>(m_main->uiHeight) - w.pageBounds.h - 4.0F));
        }
        targetUiW = std::max(1, static_cast<int>(std::lround(w.pageBounds.w)));
        targetUiH = std::max(1, static_cast<int>(std::lround(w.pageBounds.h)));
    }
    const int targetFbW = std::max(1, static_cast<int>(std::lround(static_cast<float>(targetUiW) * scale)));
    const int targetFbH = std::max(1, static_cast<int>(std::lround(static_cast<float>(targetUiH) * scale)));
    if (targetFbW != w.fbWidth || targetFbH != w.fbHeight || targetUiW != w.uiWidth || targetUiH != w.uiHeight ||
        std::abs(scale - w.uiScale) > 0.001F) {
        w.fbWidth = targetFbW;
        w.fbHeight = targetFbH;
        w.uiScale = scale;
        w.uiWidth = targetUiW;
        w.uiHeight = targetUiH;
        w.paintedGen = ~0ULL;
        m_app->windowResize(w.id, targetUiW, targetUiH);
        if (w.backend != nullptr) {
            w.backend->setPixelScale(scale);
            w.backend->resize(targetFbW, targetFbH);
        }
    }
}

void EditorHostUVE::SyncSizeUVE(WindowUVE& w) {
    if (w.page) {
        ApplyPageLayoutUVE(w);
        return;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(w.window, &width, &height);
    if (width <= 0 || height <= 0) {
        return; // minimized
    }
    const float scale = ComputeUiScaleUVE(w.window, width, height, m_singleSurface);
    const int uiWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / scale)));
    const int uiHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) / scale)));
    if (width != w.fbWidth || height != w.fbHeight || uiWidth != w.uiWidth || uiHeight != w.uiHeight ||
        std::abs(scale - w.uiScale) > 0.001F) {
        w.fbWidth = width;
        w.fbHeight = height;
        w.uiScale = scale;
        w.uiWidth = uiWidth;
        w.uiHeight = uiHeight;
        w.paintedGen = ~0ULL;
        m_app->windowResize(w.id, uiWidth, uiHeight);
        if (w.id == kMainWindowUVE) {
            if (m_backend != nullptr) {
                m_backend->setPixelScale(scale);
                m_backend->resize(width, height);
            }
        } else if (w.backend != nullptr) {
            w.backend->setPixelScale(scale);
            w.backend->resize(width, height);
        }
    }
}

void EditorHostUVE::BeginFrameUVE() {
    if (!m_ready) {
        return;
    }
    const double now = glfwGetTime();
    const auto stamp = [now](WindowUVE& w) {
        w.deltaTime = static_cast<float>(std::clamp(now - w.lastFrameTime, 1.0 / 1000.0, 0.25));
        w.lastFrameTime = now;
    };
    stamp(*m_main);
    SyncSizeUVE(*m_main);
    for (auto& [id, w] : m_windows) {
        stamp(*w);
        SyncSizeUVE(*w);
    }
    static_cast<void>(m_app->tick());
    for (auto& [id, w] : m_windows) {
        m_app->layoutWindow(id);
    }
}

EditorViewportInputUVE EditorHostUVE::CaptureViewInputUVE(const int id, const prism::Rect& rect) const {
    EditorViewportInputUVE input{};
    const WindowUVE* found = FindUVE(id);
    if (found == nullptr) {
        return input;
    }
    const WindowUVE& w = *found;
    const float scale = std::max(1.0F, w.uiScale);
    const bool inside = w.mouseInside && w.mouse.x >= rect.x && w.mouse.y >= rect.y && w.mouse.x < rect.x + rect.w &&
                        w.mouse.y < rect.y + rect.h;
    // A floating panel (Details torn off, ...) leaves the viewport usable; a menu or a modal does not.
    input.hovered = inside && w.dragMode == WindowUVE::DragModeUVE::None && !w.pageDragging &&
                    !m_app->windowHasBlockingPopup(id) && !m_app->windowWantsPointerAt(id, w.mouse);
    input.imageOrigin = Math::Vector2UVE{rect.x * scale, rect.y * scale};
    input.mousePos = Math::Vector2UVE{w.mouse.x * scale, w.mouse.y * scale};
    input.mouseDelta = Math::Vector2UVE{(w.mouse.x - w.frameStartMouse.x) * scale,
                                        (w.mouse.y - w.frameStartMouse.y) * scale};
    input.wheel = w.wheel;
    input.deltaTime = w.deltaTime;
    input.shift = (w.mods & prism::ModShift) != 0;
    input.ctrl = (w.mods & prism::ModCtrl) != 0;
    input.alt = (w.mods & prism::ModAlt) != 0;
#if defined(__ANDROID__)
    input.touchNavigation = m_singleSurface && !Window::AndroidGlfwHasHardwareInputUVE();
#else
    input.touchNavigation = m_singleSurface;
#endif
    input.dpiScale = scale;
    // Keys reach the view only while its window has the keyboard.
    const bool windowHasKeys = m_app->activeWindow() == id;
    input.wantTextInput = !windowHasKeys || m_app->windowWantsKeyboard(id) || m_platform->TextInputActiveUVE();
    for (std::size_t i = 0; i < EditorViewportInputUVE::kButtonCountUVE; ++i) {
        input.down[i] = w.down[i];
        input.clicked[i] = w.clicked[i];
        input.released[i] = w.released[i];
        input.pressPos[i] = Math::Vector2UVE{w.pressPos[i].x * scale, w.pressPos[i].y * scale};
        input.dragDistance[i] = w.dragDistance[i] * scale;
    }
    if (!input.wantTextInput) {
        input.keysDown = w.keysDown;
        input.keysPressed = w.keysPressed;
    }
    return input;
}

void EditorHostUVE::ApplyViewCursorUVE(const int id, const EditorViewportInputUVE& input) {
    const WindowUVE* w = FindUVE(id);
    if (!m_ready || w == nullptr || w->dragMode != WindowUVE::DragModeUVE::None) {
        return;
    }
    if (input.hovered && input.cursorRequested) {
        m_platform->SetViewCursorUVE(id, input.requestedCursor);
    }
}

void EditorHostUVE::CompositeUVE(WindowUVE& w, const std::uint32_t uiTexture, const std::uint32_t vao,
                                      const std::span<const ViewImageUVE> views) {
    CompositorUVE& c = *m_compositor;
    c.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w.fbWidth, w.fbHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    c.glUseProgram(c.program);
    c.glBindVertexArray(vao);
    c.glActiveTexture(GL_TEXTURE0);
    c.glUniform1i(c.texLoc, 0);
    glDisable(GL_BLEND);
    const float scaleX = w.uiWidth > 0 ? static_cast<float>(w.fbWidth) / static_cast<float>(w.uiWidth) : 1.0F;
    const float scaleY = w.uiHeight > 0 ? static_cast<float>(w.fbHeight) / static_cast<float>(w.uiHeight) : 1.0F;
    for (const ViewImageUVE& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{view.rect.x * scaleX, view.rect.y * scaleY, view.rect.w * scaleX,
                                view.rect.h * scaleY};
        c.DrawUVE(view.texture, shown, w.fbWidth, w.fbHeight, u1, v1);
    }
    glEnable(GL_BLEND);
    c.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    c.DrawUVE(uiTexture, prism::Rect{0.0F, 0.0F, static_cast<float>(w.fbWidth), static_cast<float>(w.fbHeight)},
              w.fbWidth, w.fbHeight, 1.0F, 1.0F);
    glDisable(GL_BLEND);
    c.glBindVertexArray(0);
    c.glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorHostUVE::CompositePageOverlayUVE(WindowUVE& w, const std::uint32_t uiTexture,
                                            const std::span<const ViewImageUVE> views) {
    if (m_main == nullptr) {
        return;
    }
    CompositorUVE& c = *m_compositor;
    const int mainFbW = m_main->fbWidth;
    const int mainFbH = m_main->fbHeight;
    const float mainScaleX = m_main->uiWidth > 0 ? static_cast<float>(mainFbW) / static_cast<float>(m_main->uiWidth) : 1.0F;
    const float mainScaleY = m_main->uiHeight > 0 ? static_cast<float>(mainFbH) / static_cast<float>(m_main->uiHeight) : 1.0F;
    const prism::Rect dst{w.pageBounds.x * mainScaleX, w.pageBounds.y * mainScaleY,
                          w.pageBounds.w * mainScaleX, w.pageBounds.h * mainScaleY};
    c.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mainFbW, mainFbH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Subtle crisp frame around the floating page so it separates cleanly from the live Level Editor behind it.
    const int framePad = std::max(1, static_cast<int>(std::lround(mainScaleX)));
    const int sx = std::max(0, static_cast<int>(std::floor(dst.x)) - framePad);
    const int syBottom = std::max(0, mainFbH - static_cast<int>(std::ceil(dst.y + dst.h)) - framePad);
    const int sw = std::min(mainFbW - sx, static_cast<int>(std::ceil(dst.w)) + framePad * 2);
    const int sh = std::min(mainFbH - syBottom, static_cast<int>(std::ceil(dst.h)) + framePad * 2);
    if (sw > 0 && sh > 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, syBottom, sw, sh);
        glClearColor(0.24F, 0.56F, 0.96F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }

    c.glUseProgram(c.program);
    c.glBindVertexArray(c.vao);
    c.glActiveTexture(GL_TEXTURE0);
    c.glUniform1i(c.texLoc, 0);
    glDisable(GL_BLEND);
    const float localScaleX = w.uiWidth > 0 ? dst.w / static_cast<float>(w.uiWidth) : mainScaleX;
    const float localScaleY = w.uiHeight > 0 ? dst.h / static_cast<float>(w.uiHeight) : mainScaleY;
    for (const ViewImageUVE& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{dst.x + view.rect.x * localScaleX, dst.y + view.rect.y * localScaleY,
                                view.rect.w * localScaleX, view.rect.h * localScaleY};
        c.DrawUVE(view.texture, shown, mainFbW, mainFbH, u1, v1);
    }
    glEnable(GL_BLEND);
    c.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    c.DrawUVE(uiTexture, dst, mainFbW, mainFbH, 1.0F, 1.0F);
    glDisable(GL_BLEND);
    c.glBindVertexArray(0);
    c.glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorHostUVE::EndInputFrameUVE(WindowUVE& w) {
    // Edges are per frame.
    w.clicked.fill(false);
    w.released.fill(false);
    w.keysPressed.reset();
    w.wheel = 0.0F;
    w.frameStartMouse = w.mouse;
}

void EditorHostUVE::RenderUVE(const std::span<const ViewImageUVE> views) {
    if (!m_ready) {
        return;
    }
    // UI into the backend's own texture, then composited into the default framebuffer. Only when
    // something in it changed (a widget invalidated, its layout or size moved) - and at least four
    // times a second, for anything that changed without saying so; the texture keeps the last frame
    // in between. No read-back: the frame never leaves the GPU.
    const auto now = std::chrono::steady_clock::now();
    if (m_app->needsRender() || m_app->paintGeneration() != m_mainPaintedGen || now - m_mainPaintedAt > std::chrono::milliseconds(250)) {
        m_app->renderFrame();
        m_mainPaintedGen = m_app->paintGeneration();
        m_mainPaintedAt = now;
    }
    CompositeUVE(*m_main, m_backend->colorTexture(), m_compositor->vao, views);
    EndInputFrameUVE(*m_main);
}

void EditorHostUVE::BeginGpuTimerUVE() {
    if (!m_ready) {
        return;
    }
    if (m_gpuTimer == nullptr) {
        m_gpuTimer = std::make_unique<GpuTimerUVE>();
        m_gpuTimer->InitializeUVE();
    }
    m_gpuTimer->BeginUVE();
}

void EditorHostUVE::EndGpuTimerUVE() {
    if (m_gpuTimer != nullptr) {
        m_gpuTimer->EndUVE();
    }
}

float EditorHostUVE::GpuFrameMsUVE() const noexcept {
    return m_gpuTimer != nullptr ? m_gpuTimer->lastMs : 0.0F;
}

void EditorHostUVE::RenderWindowUVE(const int id, const std::span<const ViewImageUVE> views) {
    WindowUVE* found = id != kMainWindowUVE ? FindUVE(id) : nullptr;
    if (!m_ready || found == nullptr) {
        return;
    }
    WindowUVE& w = *found;
    const auto now = std::chrono::steady_clock::now();
    const auto paintIfNeeded = [&] {
        if (m_app->windowNeedsRender(id) || m_app->paintGeneration() != w.paintedGen ||
            now - w.paintedAt > std::chrono::milliseconds(250)) {
            m_app->renderWindowTo(id, *w.backend);
            w.paintedGen = m_app->paintGeneration();
            w.paintedAt = now;
        }
    };
    if (w.page) {
        // Only the top visible page is drawn over the shared surface.
        if (VisiblePageUVE() == &w) {
            paintIfNeeded();
            if (w.pageFloating) {
                CompositePageOverlayUVE(w, w.backend->colorTexture(), views);
            } else {
                CompositeUVE(w, w.backend->colorTexture(), m_compositor->vao, views);
            }
        }
        EndInputFrameUVE(w);
        return;
    }
    if (glfwGetWindowAttrib(w.window, GLFW_ICONIFIED) != 0) {
        EndInputFrameUVE(w);
        return;
    }
    // The UI is rendered in the main context (where its backend's objects live) ...
    paintIfNeeded();
    const std::uint32_t uiTexture = w.backend->colorTexture();
    // ... and drawn in the window's own, which shares the textures but not the vertex arrays.
    glfwMakeContextCurrent(w.window);
    if (w.vao == 0) {
        glfwSwapInterval(0);   // the main window paces the loop
        m_compositor->glGenVertexArrays(1, &w.vao);
    }
    CompositeUVE(w, uiTexture, w.vao, views);
    glfwSwapBuffers(w.window);
    glfwMakeContextCurrent(m_main->window);
    EndInputFrameUVE(w);
}

EditorHostUVE::WindowUVE* EditorHostUVE::VisiblePageUVE() const {
    for (auto order = m_pageOrder.rbegin(); order != m_pageOrder.rend(); ++order) {
        const auto found = m_windows.find(*order);
        if (found != m_windows.end() && !found->second->pageHidden) {
            return found->second.get();
        }
    }
    return nullptr;
}

void EditorHostUVE::ShowPageUVE(const int id) {
    WindowUVE* const w = FindUVE(id);
    if (w == nullptr || !w->page) {
        return;
    }
    w->pageHidden = false;
    m_pageOrder.erase(std::remove(m_pageOrder.begin(), m_pageOrder.end(), id), m_pageOrder.end());
    m_pageOrder.push_back(id);
    m_app->activateWindow(id);
}

void EditorHostUVE::HidePageUVE(const int id) {
    WindowUVE* const w = FindUVE(id);
    if (w == nullptr || !w->page) {
        return;
    }
    w->pageHidden = true;
    w->pageDragging = false;
    w->down.fill(false);
    if (WindowUVE* const shown = VisiblePageUVE()) {
        m_app->activateWindow(shown->id);
    } else {
        m_app->activateWindow(kMainWindowUVE);
    }
}

bool EditorHostUVE::HasOpaqueFullPageUVE() const {
    if (!m_singleSurface) {
        return false;
    }
    const WindowUVE* const page = VisiblePageUVE();
    return page != nullptr && !page->pageFloating;
}

bool EditorHostUVE::IsWindowFocusedUVE(const int id) const {
    const auto found = m_windows.find(id);
    if (m_ready && found != m_windows.end() && found->second->page) {
        return VisiblePageUVE() == found->second.get() && glfwGetWindowAttrib(m_main->window, GLFW_FOCUSED) != 0;
    }
    return m_ready && found != m_windows.end() && found->second->window != nullptr &&
           glfwGetWindowAttrib(found->second->window, GLFW_FOCUSED) != 0;
}

bool EditorHostUVE::QuitRequestedUVE() const noexcept {
    return m_main != nullptr && m_main->window != nullptr && glfwWindowShouldClose(m_main->window) != 0;
}

namespace {

[[nodiscard]] bool WriteFramebufferPngUVE(const std::filesystem::path& path, const int width, const int height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(height - 1 - y) * width * 4U;
        std::uint32_t* dst = argb.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = src + static_cast<std::size_t>(x) * 4U;
            dst[x] = 0xFF000000U | (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) |
                     static_cast<std::uint32_t>(p[2]);
        }
    }
    return prism::Canvas::writePng(path.string(), argb.data(), width, height, width);
}

} // namespace

bool EditorHostUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    if (!m_ready) {
        return false;
    }
    m_compositor->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return WriteFramebufferPngUVE(path, m_main->fbWidth, m_main->fbHeight);
}

bool EditorHostUVE::SaveWindowScreenshotUVE(const int id, const std::filesystem::path& path) {
    WindowUVE* w = id != kMainWindowUVE ? FindUVE(id) : nullptr;
    if (!m_ready || w == nullptr) {
        return false;
    }
    // The back buffer holds the frame just presented only until the next draw; draw it again.
    m_app->renderWindowTo(id, *w->backend);
    const std::uint32_t uiTexture = w->backend->colorTexture();
    glfwMakeContextCurrent(w->window);
    if (w->vao == 0) {
        m_compositor->glGenVertexArrays(1, &w->vao);
    }
    CompositeUVE(*w, uiTexture, w->vao, {});
    const bool ok = WriteFramebufferPngUVE(path, w->fbWidth, w->fbHeight);
    glfwMakeContextCurrent(m_main->window);
    return ok;
}

// ----------------------------------------------------------------------------------------- input

void EditorHostUVE::SetKeyHookUVE(std::function<bool(int key, int action)> hook) {
    m_keyHook = std::move(hook);
}

void EditorHostUVE::OnCursorPosUVE(WindowUVE& w, const double x, const double y) {
    if (w.page && w.pageFloating && w.pageDragging && m_main != nullptr) {
        const prism::Vec2 mp = ToUiUVE(*m_main, x, y);
        const float maxX = std::max(4.0F, static_cast<float>(m_main->uiWidth) - w.pageBounds.w - 4.0F);
        const float maxY = std::max(4.0F, static_cast<float>(m_main->uiHeight) - w.pageBounds.h - 4.0F);
        w.pageBounds.x = std::clamp(std::floor(mp.x - w.pageDragGrab.x), 4.0F, maxX);
        w.pageBounds.y = std::clamp(std::floor(mp.y - w.pageDragGrab.y), 4.0F, maxY);
        w.mouse = prism::Vec2{mp.x - w.pageBounds.x, mp.y - w.pageBounds.y};
        if (m_app != nullptr) {
            m_app->invalidate();
        }
        return;
    }
    w.mouse = ToUiUVE(w, x, y);
    w.mouseInside = true;
    for (std::size_t i = 0; i < w.down.size(); ++i) {
        if (w.down[i]) {
            const float dx = w.mouse.x - w.pressPos[i].x;
            const float dy = w.mouse.y - w.pressPos[i].y;
            w.dragDistance[i] = std::max(w.dragDistance[i], std::sqrt((dx * dx) + (dy * dy)));
        }
    }
    if (w.dragMode != WindowUVE::DragModeUVE::None) {
        UpdateWindowDragUVE(w);
        return;
    }
    // Edge-resize affordance on an undecorated, restored desktop window.
    int edges = 0;
    if (!w.page && glfwGetWindowAttrib(w.window, GLFW_DECORATED) == 0 && glfwGetWindowAttrib(w.window, GLFW_MAXIMIZED) == 0) {
        const float width = static_cast<float>(w.fbWidth);
        const float height = static_cast<float>(w.fbHeight);
        if (w.mouse.x < kResizeBorderUVE) { edges |= 1; }
        if (w.mouse.x >= width - kResizeBorderUVE) { edges |= 2; }
        if (w.mouse.y < kResizeBorderUVE) { edges |= 4; }
        if (w.mouse.y >= height - kResizeBorderUVE) { edges |= 8; }
    }
    prism::Vec2 dragLocal{};
    if (WindowUVE* const target = DragTargetUVE(w, dragLocal)) {
        // Dragging over another window: that window's widgets see the drag; this one sees it leave.
        if (w.dragOverWindow != target->id) {
            m_app->windowMouseLeave(w.id);
            w.dragOverWindow = target->id;
        }
        target->mouse = dragLocal;
        static_cast<void>(InTargetScopeUVE(w, *target, [this, target, dragLocal] { m_app->windowMouseMove(target->id, dragLocal); }));
        return;
    }
    if (w.dragOverWindow >= 0) {
        if (WindowUVE* const previous = FindUVE(w.dragOverWindow)) {
            m_app->windowMouseLeave(previous->id);
        }
        w.dragOverWindow = -1;
    }
    m_app->windowMouseMove(w.id, w.mouse);
    if (edges != 0) {
        m_platform->SetResizeCursorUVE(w.id, edges);
    }
}

void EditorHostUVE::OnMouseButtonUVE(WindowUVE& w, const int button, const int action, const int mods) {
    const std::uint32_t bit = (button >= 0 && button < 32) ? (1U << static_cast<unsigned>(button)) : 0U;
    w.mods = ToPrismModsUVE(mods);
    m_app->hostSetModifiers(w.mods);
    const int slot = ToViewButtonUVE(button);
    const prism::MouseButton prismButton = ToPrismButtonUVE(button);
    if (action == GLFW_PRESS) {
        const double now = glfwGetTime();
        const float dx = w.mouse.x - w.lastClickPos.x;
        const float dy = w.mouse.y - w.lastClickPos.y;
        if (now - w.lastClickTime <= kDoubleClickSecondsUVE && std::abs(dx) <= kDoubleClickSlopUVE &&
            std::abs(dy) <= kDoubleClickSlopUVE) {
            ++w.clickCount;
        } else {
            w.clickCount = 1;
        }
        w.lastClickTime = now;
        w.lastClickPos = w.mouse;

        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            w.down[i] = true;
            w.clicked[i] = true;
            w.pressPos[i] = w.mouse;
            w.dragDistance[i] = 0.0F;
        }

        if (w.page && w.pageFloating && button == GLFW_MOUSE_BUTTON_LEFT && w.CaptionContainsUVE(w.mouse) &&
            w.clickCount == 1) {
            w.pageDragging = true;
            w.pageDragGrab = w.mouse;
        }

        // Window chrome: edge resize, or a move from the title strip (single click only; a
        // double click toggles maximize inside PRISM's title bar).
        if (!w.page && button == GLFW_MOUSE_BUTTON_LEFT && glfwGetWindowAttrib(w.window, GLFW_DECORATED) == 0) {
            const bool maximized = glfwGetWindowAttrib(w.window, GLFW_MAXIMIZED) != 0;
            int edges = 0;
            if (!maximized) {
                const float width = static_cast<float>(w.fbWidth);
                const float height = static_cast<float>(w.fbHeight);
                if (w.mouse.x < kResizeBorderUVE) { edges |= 1; }
                if (w.mouse.x >= width - kResizeBorderUVE) { edges |= 2; }
                if (w.mouse.y < kResizeBorderUVE) { edges |= 4; }
                if (w.mouse.y >= height - kResizeBorderUVE) { edges |= 8; }
            }
            const bool inCaption = w.CaptionContainsUVE(w.mouse);
#if defined(_WIN32)
            // Windows moves and sizes the window itself from here: the native drag snaps to the screen
            // edges, restores a maximized window as it is dragged off, and keeps following the cursor
            // however fast it moves. The call returns once the button is released.
            if (edges != 0 || (inCaption && w.clickCount == 1)) {
                static constexpr std::array<WPARAM, 16> kHitUVE{
                    HTCAPTION, HTLEFT, HTRIGHT, HTCAPTION, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTCAPTION,
                    HTBOTTOM, HTBOTTOMLEFT, HTBOTTOMRIGHT, HTCAPTION, HTCAPTION, HTCAPTION, HTCAPTION, HTCAPTION};
                HWND hwnd = glfwGetWin32Window(w.window);
                if (slot >= 0) {
                    w.down[static_cast<std::size_t>(slot)] = false;
                }
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, kHitUVE[static_cast<std::size_t>(edges & 15)], 0);
                return;
            }
#endif
            if (inCaption && w.clickCount == 1 && maximized) {
                // Dragging a maximized window off its title bar restores it first (as the OS does),
                // keeping the cursor over the same spot of the bar.
                int fullW = 0;
                int fullH = 0;
                glfwGetWindowSize(w.window, &fullW, &fullH);
                const float along = fullW > 0 ? w.mouse.x / static_cast<float>(fullW) : 0.5F;
                double sx = 0.0;
                double sy = 0.0;
                glfwGetCursorPos(w.window, &sx, &sy);
                int wx = 0;
                int wy = 0;
                glfwGetWindowPos(w.window, &wx, &wy);
                glfwRestoreWindow(w.window);
                int restoredW = 0;
                int restoredH = 0;
                glfwGetWindowSize(w.window, &restoredW, &restoredH);
                const int newX = static_cast<int>(wx + sx - along * static_cast<float>(restoredW));
                const int newY = static_cast<int>(wy + sy - w.mouse.y);
                glfwSetWindowPos(w.window, newX, newY);
            }
            if (edges != 0 || (inCaption && w.clickCount == 1)) {
                double cx = 0.0;
                double cy = 0.0;
                glfwGetCursorPos(w.window, &cx, &cy);
                glfwGetWindowPos(w.window, &w.dragStartWinX, &w.dragStartWinY);
                glfwGetWindowSize(w.window, &w.dragStartWinW, &w.dragStartWinH);
                w.dragStartScreenX = w.dragStartWinX + cx;
                w.dragStartScreenY = w.dragStartWinY + cy;
                w.dragMode = edges != 0 ? WindowUVE::DragModeUVE::Resize : WindowUVE::DragModeUVE::Move;
                w.resizeEdges = edges;
                if (edges != 0) {
                    return; // the resize ring belongs to the OS frame, not to the UI below it
                }
            }
        }
        if (prismButton != prism::MouseButton::None) {
            m_app->windowMouseButton(w.id, w.mouse, prismButton, true, w.clickCount);
        }
    } else if (action == GLFW_RELEASE) {
        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            w.down[i] = false;
            w.released[i] = true;
        }
        const bool wasResize = w.dragMode == WindowUVE::DragModeUVE::Resize;
        const bool wasPageDrag = w.pageDragging;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            w.dragMode = WindowUVE::DragModeUVE::None;
            w.pageDragging = false;
        }
        if (!wasResize && !wasPageDrag && prismButton != prism::MouseButton::None) {
            prism::Vec2 dragLocal{};
            if (WindowUVE* const target = DragTargetUVE(w, dragLocal)) {
                // The drop lands in the window under the pointer; the press's own window then sees
                // its release (outside its widgets) so its capture ends.
                const int clicks = w.clickCount;
                static_cast<void>(InTargetScopeUVE(w, *target, [this, target, dragLocal, prismButton, clicks] {
                    m_app->windowMouseButton(target->id, dragLocal, prismButton, false, clicks);
                }));
                m_app->cancelDragDrop();
            }
            w.dragOverWindow = -1;
            m_app->windowMouseButton(w.id, w.mouse, prismButton, false, w.clickCount);
        }
    }
}

void EditorHostUVE::UpdateWindowDragUVE(WindowUVE& w) {
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(w.window, &cx, &cy);
    int winX = 0;
    int winY = 0;
    glfwGetWindowPos(w.window, &winX, &winY);
    const double screenX = winX + cx;
    const double screenY = winY + cy;
    const int dx = static_cast<int>(std::lround(screenX - w.dragStartScreenX));
    const int dy = static_cast<int>(std::lround(screenY - w.dragStartScreenY));
    if (w.dragMode == WindowUVE::DragModeUVE::Move) {
        glfwSetWindowPos(w.window, w.dragStartWinX + dx, w.dragStartWinY + dy);
        return;
    }
    int x = w.dragStartWinX;
    int y = w.dragStartWinY;
    int width = w.dragStartWinW;
    int height = w.dragStartWinH;
    constexpr int kMinW = 640;
    constexpr int kMinH = 400;
    if ((w.resizeEdges & 1) != 0) { const int nw = std::max(kMinW, width - dx); x += width - nw; width = nw; }
    if ((w.resizeEdges & 2) != 0) { width = std::max(kMinW, width + dx); }
    if ((w.resizeEdges & 4) != 0) { const int nh = std::max(kMinH, height - dy); y += height - nh; height = nh; }
    if ((w.resizeEdges & 8) != 0) { height = std::max(kMinH, height + dy); }
    glfwSetWindowPos(w.window, x, y);
    glfwSetWindowSize(w.window, width, height);
}

void EditorHostUVE::OnScrollUVE(WindowUVE& w, const double dx, const double dy) {
    w.wheel += static_cast<float>(dy);
    m_app->windowMouseWheel(w.id, w.mouse, prism::Vec2{static_cast<float>(dx), static_cast<float>(dy)});
}

void EditorHostUVE::OnKeyUVE(WindowUVE& w, const int key, const int /*scancode*/, const int action, const int mods) {
    if (w.id == kMainWindowUVE && m_keyHook && m_keyHook(key, action)) {
        return;   // taken before the UI sees it (Escape giving the editor its mouse back)
    }
    w.mods = ToPrismModsUVE(mods);
    m_app->hostSetModifiers(w.mods);
    const ViewKeyUVE viewKey = ToViewKeyUVE(key);
    if (viewKey != ViewKeyUVE::Count) {
        const auto index = static_cast<std::size_t>(viewKey);
        if (action == GLFW_PRESS) {
            w.keysDown.set(index);
            w.keysPressed.set(index);
        } else if (action == GLFW_RELEASE) {
            w.keysDown.reset(index);
        }
    }
    const prism::Key prismKey = ToPrismKeyUVE(key);
    if (prismKey == prism::Key::Unknown) {
        return;
    }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        m_app->windowKey(w.id, prismKey, w.mods, true, action == GLFW_REPEAT);
    } else if (action == GLFW_RELEASE) {
        m_app->windowKey(w.id, prismKey, w.mods, false, false);
    }
}

void EditorHostUVE::OnCharUVE(WindowUVE& w, const unsigned int codepoint) {
    std::string utf8;
    AppendUtf8UVE(utf8, static_cast<char32_t>(codepoint));
    m_app->windowText(w.id, utf8);
}

void EditorHostUVE::OnCursorEnterUVE(WindowUVE& w, const int entered) {
    w.mouseInside = entered != 0;
    if (!w.mouseInside && w.dragMode == WindowUVE::DragModeUVE::None) {
        m_app->windowMouseLeave(w.id);
    }
}

bool EditorHostUVE::IsAnyWindowFocusedUVE() const {
    if (m_main != nullptr && m_main->window != nullptr && glfwGetWindowAttrib(m_main->window, GLFW_FOCUSED) == GLFW_TRUE) {
        return true;
    }
    for (const auto& [id, window] : m_windows) {
        if (window != nullptr && window->window != nullptr && glfwGetWindowAttrib(window->window, GLFW_FOCUSED) == GLFW_TRUE) {
            return true;
        }
    }
    return false;
}

} // namespace UVE::Editor
