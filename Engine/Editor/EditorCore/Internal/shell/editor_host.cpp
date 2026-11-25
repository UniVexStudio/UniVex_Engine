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

EditorHost* g_activeHost = nullptr;

using ViewKey = EditorViewportInput::Key;
using ViewCursor = EditorViewportInput::Cursor;

constexpr double kDoubleClickSeconds = 0.4;
constexpr float kResizeBorder = 5.0F;
/// Platforms with one full-screen surface and no OS windows: every extra window is a page over the
/// main one.
#if defined(__ANDROID__)
constexpr bool kSingleSurface = true;
#else
constexpr bool kSingleSurface = false;
#endif
/// How far a double-click's second press may land from the first: a finger never taps the same pixel.
constexpr float kDoubleClickSlop = kSingleSurface ? 12.0F : 4.0F;
/// Bytes kept of a compositor shader or program info log.
constexpr std::size_t kShaderLogCapacity = 1024;

[[nodiscard]] prism::Key ToPrismKey(const int key) {
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

/// Viewport hotkeys (EditorViewportInput::Key) for a GLFW key, or Count.
[[nodiscard]] ViewKey ToViewKey(const int key) {
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return static_cast<ViewKey>(static_cast<int>(ViewKey::Num0) + (key - GLFW_KEY_0));
    }
    switch (key) {
        case GLFW_KEY_KP_1: return ViewKey::Keypad1;
        case GLFW_KEY_KP_3: return ViewKey::Keypad3;
        case GLFW_KEY_KP_5: return ViewKey::Keypad5;
        case GLFW_KEY_KP_7: return ViewKey::Keypad7;
        case GLFW_KEY_A: return ViewKey::A;
        case GLFW_KEY_C: return ViewKey::C;
        case GLFW_KEY_D: return ViewKey::D;
        case GLFW_KEY_F: return ViewKey::F;
        case GLFW_KEY_S: return ViewKey::S;
        case GLFW_KEY_W: return ViewKey::W;
        case GLFW_KEY_E: return ViewKey::E;
        case GLFW_KEY_Q: return ViewKey::Q;
        case GLFW_KEY_SPACE: return ViewKey::Space;
        case GLFW_KEY_HOME: return ViewKey::Home;
        case GLFW_KEY_ESCAPE: return ViewKey::Escape;
        case GLFW_KEY_LEFT_SHIFT: return ViewKey::LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return ViewKey::LeftCtrl;
        default: return ViewKey::Count;
    }
}

[[nodiscard]] std::uint8_t ToPrismMods(const int mods) {
    std::uint8_t out = prism::ModNone;
    if ((mods & GLFW_MOD_SHIFT) != 0) { out |= prism::ModShift; }
    if ((mods & GLFW_MOD_CONTROL) != 0) { out |= prism::ModCtrl; }
    if ((mods & GLFW_MOD_ALT) != 0) { out |= prism::ModAlt; }
    return out;
}

/// GLFW button -> EditorViewportInput button slot (Left, Right, Middle), or -1.
[[nodiscard]] int ToViewButton(const int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return 0;
        case GLFW_MOUSE_BUTTON_RIGHT: return 1;
        case GLFW_MOUSE_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

[[nodiscard]] prism::MouseButton ToPrismButton(const int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return prism::MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return prism::MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return prism::MouseButton::Middle;
        default: return prism::MouseButton::None;
    }
}

void AppendUtf8(std::string& out, const char32_t cp) {
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

[[nodiscard]] void* GlLoader(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

/// Logical UI scale: 1.0 on desktop (pixel-exact), or the display density scale on single-surface
/// mobile screens, clamped so the shorter logical edge stays at least 360 layout units wide.
[[nodiscard]] float ComputeUiScale(GLFWwindow* const window, const int fbWidth, const int fbHeight,
                                      const bool singleSurface) {
    if (!singleSurface || window == nullptr || fbWidth <= 0 || fbHeight <= 0) {
        return 1.0F;
    }
    float sx = 1.0F;
    float sy = 1.0F;
    glfwGetWindowContentScale(window, &sx, &sy);
    const float rawScale = std::isfinite(sx) && sx > 0.0F ? sx : 1.0F;
    constexpr float kMinLogicalShortEdge = 500.0F;
    const float maxScale = std::max(1.0F, static_cast<float>(std::min(fbWidth, fbHeight)) / kMinLogicalShortEdge);
    return std::clamp(rawScale, 1.0F, maxScale);
}

} // namespace

// ---------------------------------------------------------------------------------------- window

/// One OS window: its GLFW window, framebuffer size, drag areas and the raw input state its
/// viewports read. Window 0 is the engine's main window; the others belong to this host.
struct EditorHost::Window final {
    EditorHost* host = nullptr;
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
    EditorHost::WindowScope scope;

    prism::Vec2 mouse{0.0F, 0.0F};
    prism::Vec2 frameStartMouse{0.0F, 0.0F};
    bool mouseInside = true;
    float wheel = 0.0F;
    std::uint8_t mods = 0;
    std::array<bool, EditorViewportInput::kButtonCount> down{};
    std::array<bool, EditorViewportInput::kButtonCount> clicked{};
    std::array<bool, EditorViewportInput::kButtonCount> released{};
    std::array<prism::Vec2, EditorViewportInput::kButtonCount> pressPos{};
    std::array<float, EditorViewportInput::kButtonCount> dragDistance{};
    std::bitset<EditorViewportInput::kKeyCount> keysDown;
    std::bitset<EditorViewportInput::kKeyCount> keysPressed;
    double lastFrameTime = 0.0;
    float deltaTime = 1.0F / 60.0F;

    // Double-click detection (PRISM wants a click count).
    double lastClickTime = -1.0;
    prism::Vec2 lastClickPos{0.0F, 0.0F};
    int clickCount = 0;

    // Window drag (title strip) and edge resize, done here because GLFW has no hit-test hook.
    enum class DragMode : std::uint8_t { None, Move, Resize };
    DragMode dragMode = DragMode::None;
    int resizeEdges = 0; // bit 0 left, 1 right, 2 top, 3 bottom
    double dragStartScreenX = 0.0;
    double dragStartScreenY = 0.0;
    int dragStartWinX = 0;
    int dragStartWinY = 0;
    int dragStartWinW = 0;
    int dragStartWinH = 0;

    [[nodiscard]] bool CaptionContains(const prism::Vec2 p) const noexcept {
        for (const auto& [captionId, r] : captions) {
            if (r.w > 0.0F && p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h) {
                return true;
            }
        }
        return false;
    }
};

// ------------------------------------------------------------------------------------- platform

class EditorHost::Platform final : public prism::Platform {
public:
    Platform(EditorHost& host, GLFWwindow* window) : m_host(host), m_window(window) {
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
    ~Platform() override {
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
        ApplyCursor(window, m_cursors[static_cast<int>(c)]);
    }
    void setTextInputActive(const bool active) override {
        m_textInput = active;
#if defined(__ANDROID__)
        UVE::Window::SetAndroidGlfwTextInput(active);   // the app shows or hides the soft keyboard
#endif
    }
    float dpiScale() const override { return m_host.m_main != nullptr ? m_host.m_main->uiScale : 1.0F; }
    void requestQuit() override { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
    void* nativeWindowHandle() const override { return m_window; }
    /// Drag areas: id < kExtraCaptionId is a window's title strip (the window's own id); ids
    /// from kExtraCaptionId up are further empty title-bar space (ExtraCaptionId).
    void setWindowCaption(const int id, const prism::Rect dragArea) override {
        const int window = id < kExtraCaptionId ? id : (id - kExtraCaptionId) / 100;
        if (Window* w = m_host.Find(window)) {
            w->captions[id] = dragArea;
        }
    }
    void minimizeWindow(const int id) override {
        if (Window* w = m_host.Find(id)) {
            if (w->page) {
                m_host.HidePage(id);
            } else {
                glfwIconifyWindow(w->window);
            }
        }
    }
    bool singleSurface() const override { return m_host.m_singleSurface; }
    void toggleMaximizeWindow(const int id) override {
        Window* w = m_host.Find(id);
        if (w == nullptr || (id == kMainWindow && m_host.m_singleSurface)) {
            return;
        }
        if (w->page) {
            w->pageFloating = !w->pageFloating;
            w->pageDragging = false;
            m_host.ApplyPageLayout(*w);
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
        const Window* w = m_host.Find(id);
        if (w == nullptr) {
            return false;
        }
        if (w->page) {
            return !w->pageFloating;
        }
        return glfwGetWindowAttrib(w->window, GLFW_MAXIMIZED) != 0;
    }

    [[nodiscard]] bool TextInputActive() const noexcept { return m_textInput; }

    void SetViewCursor(const int window, const ViewCursor cursor) {
        prism::Cursor mapped = prism::Cursor::Arrow;
        switch (cursor) {
            case ViewCursor::Arrow: mapped = prism::Cursor::Arrow; break;
            case ViewCursor::Hand: mapped = prism::Cursor::Hand; break;
            case ViewCursor::ResizeAll: mapped = prism::Cursor::SizeAll; break;
            case ViewCursor::ResizeNS: mapped = prism::Cursor::SizeNS; break;
            case ViewCursor::ResizeEW: mapped = prism::Cursor::SizeWE; break;
            case ViewCursor::ResizeNWSE: mapped = prism::Cursor::SizeAll; break;
            case ViewCursor::ResizeNESW: mapped = prism::Cursor::SizeAll; break;
        }
        ApplyCursor(window, m_cursors[static_cast<int>(mapped)]);
    }
    void SetResizeCursor(const int window, const int edges) {
        const bool horizontal = (edges & 3) != 0;
        const bool vertical = (edges & 12) != 0;
        prism::Cursor c = prism::Cursor::Arrow;
        if (horizontal && vertical) { c = prism::Cursor::SizeAll; }
        else if (horizontal) { c = prism::Cursor::SizeWE; }
        else if (vertical) { c = prism::Cursor::SizeNS; }
        ApplyCursor(window, m_cursors[static_cast<int>(c)]);
    }
    void ForgetWindow(const int window) {
        m_applied.erase(window);
        m_uiCursor.erase(window);
    }

private:
    void ApplyCursor(const int window, GLFWcursor* cursor) {
        Window* w = m_host.Find(window);
        if (w == nullptr) {
            return;
        }
        GLFWcursor*& applied = m_applied[window];
        if (cursor != applied) {
            applied = cursor;
            glfwSetCursor(w->window, cursor);
        }
    }

    EditorHost& m_host;
    GLFWwindow* m_window = nullptr;
    std::array<GLFWcursor*, 6> m_cursors{};
    std::map<int, GLFWcursor*> m_applied;
    std::map<int, prism::Cursor> m_uiCursor;
    bool m_textInput = false;
};

// ------------------------------------------------------------------------------------ compositor

/// Draws textured rectangles into the default framebuffer: engine view textures into their holes,
/// then the PRISM frame (premultiplied, transparent in the holes) over the whole window.
struct EditorHost::Compositor final {
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
    bool Load(T& fn, const char* name) {
        fn = reinterpret_cast<T>(glfwGetProcAddress(name));
        return fn != nullptr;
    }

    [[nodiscard]] bool Initialize() {
        bool ok = true;
        ok &= Load(glCreateShader, "glCreateShader");
        ok &= Load(glShaderSource, "glShaderSource");
        ok &= Load(glCompileShader, "glCompileShader");
        ok &= Load(glGetShaderiv, "glGetShaderiv");
        ok &= Load(glDeleteShader, "glDeleteShader");
        ok &= Load(glCreateProgram, "glCreateProgram");
        ok &= Load(glAttachShader, "glAttachShader");
        ok &= Load(glLinkProgram, "glLinkProgram");
        ok &= Load(glGetProgramiv, "glGetProgramiv");
        ok &= Load(glDeleteProgram, "glDeleteProgram");
        ok &= Load(glUseProgram, "glUseProgram");
        ok &= Load(glGetUniformLocation, "glGetUniformLocation");
        ok &= Load(glUniform4f, "glUniform4f");
        ok &= Load(glUniform1i, "glUniform1i");
        ok &= Load(glGenVertexArrays, "glGenVertexArrays");
        ok &= Load(glBindVertexArray, "glBindVertexArray");
        ok &= Load(glDeleteVertexArrays, "glDeleteVertexArrays");
        ok &= Load(glBindFramebuffer, "glBindFramebuffer");
        ok &= Load(glActiveTexture, "glActiveTexture");
        ok &= Load(glBlendFuncSeparate, "glBlendFuncSeparate");
        ok &= Load(glGetShaderInfoLog, "glGetShaderInfoLog");
        ok &= Load(glGetProgramInfoLog, "glGetProgramInfoLog");
        if (!ok) {
            UVE_ERROR("Editor host: the GL context lacks a function the compositor needs");
            return false;
        }
        glslHeader = prism::glslHeaderFor(reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        // A quad from gl_VertexID: uRect = NDC (x0, y0, x1, y1), uUv = texture (u0, v0, u1, v1).
        // No #version line: Compile puts the context's header first (desktop core or GLES).
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
        const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragment);
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
            std::array<char, kShaderLogCapacity> log{};
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

    [[nodiscard]] GLuint Compile(const GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        const std::array<const char*, 2> parts{glslHeader, source};
        glShaderSource(shader, static_cast<GLsizei>(parts.size()), parts.data(), nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == 0) {
            std::array<char, kShaderLogCapacity> log{};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
            UVE_ERROR("Editor host: a compositor shader did not compile: {}", log.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void Shutdown() {
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
    void Draw(const GLuint texture, const prism::Rect& r, const int w, const int h, const float u1,
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
struct EditorHost::GpuTimer final {
    static constexpr std::size_t kSlots = 4U;
#if defined(__ANDROID__)
    static constexpr GLenum kTarget = GL_TIME_ELAPSED_EXT;
    PFNGLGETQUERYOBJECTUI64VEXTPROC getQueryObjectui64v = nullptr;
#else
    static constexpr GLenum kTarget = GL_TIME_ELAPSED;
    PFNGLGETQUERYOBJECTUI64VPROC getQueryObjectui64v = nullptr;
#endif
    PFNGLGENQUERIESPROC genQueries = nullptr;
    PFNGLDELETEQUERIESPROC deleteQueries = nullptr;
    PFNGLBEGINQUERYPROC beginQuery = nullptr;
    PFNGLENDQUERYPROC endQuery = nullptr;
    PFNGLGETQUERYOBJECTUIVPROC getQueryObjectuiv = nullptr;

    std::array<GLuint, kSlots> queries{};
    std::array<bool, kSlots> pending{};
    /// The GPU's clock jumped (power state change, context reset) while the query ran: no result.
    std::array<bool, kSlots> tainted{};
    std::size_t next = 0U;   ///< the slot the next frame uses, which is also the oldest one issued
    bool supported = false;
    bool active = false;
    float lastMs = 0.0F;

    template <typename T>
    static bool Load(T& fn, const std::string& name) {
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

    void Initialize() {
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
        bool ok = Load(genQueries, "glGenQueries");
        ok &= Load(deleteQueries, "glDeleteQueries");
        ok &= Load(beginQuery, "glBeginQuery");
        ok &= Load(endQuery, "glEndQuery");
        ok &= Load(getQueryObjectuiv, "glGetQueryObjectuiv");
        ok &= Load(getQueryObjectui64v, "glGetQueryObjectui64v");
        if (!ok) {
            return;
        }
        genQueries(static_cast<GLsizei>(kSlots), queries.data());
        supported = true;
    }

    /// Takes every finished result, oldest first, so lastMs ends on the newest one.
    void Poll() {
#if defined(__ANDROID__)
        GLint disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
#endif
        for (std::size_t k = 0U; k < kSlots; ++k) {
            const std::size_t i = (next + k) % kSlots;
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

    void Begin() {
        if (!supported || active) {
            return;
        }
        Poll();
        if (pending[next]) {
            return;   // the GPU is a whole ring behind: this frame goes untimed rather than waited for
        }
        tainted[next] = false;
        beginQuery(kTarget, queries[next]);
        active = true;
    }

    void End() {
        if (!active) {
            return;
        }
        endQuery(kTarget);
        pending[next] = true;
        next = (next + 1U) % kSlots;
        active = false;
    }

    void Shutdown() {
        End();
        if (supported) {
            deleteQueries(static_cast<GLsizei>(kSlots), queries.data());
            supported = false;
        }
    }
};

// ------------------------------------------------------------------------------- GLFW callbacks

/// The main window's callbacks, chained to what the engine installed before.
struct EditorHost::Chain final {
    GLFWcursorposfun cursorPos = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorenterfun cursorEnter = nullptr;
#if !defined(__ANDROID__)
    GLFWdropfun drop = nullptr;
#endif

    [[nodiscard]] static EditorHost::Window* Main() {
        return g_activeHost != nullptr ? g_activeHost->m_main.get() : nullptr;
    }
    /// On a single surface the page on screen takes the main window's input; runs `fn` on it (in its
    /// scope) and returns true, or false when the main window has the screen.
    template <typename F>
    [[nodiscard]] static bool ToPage(F&& fn) {
        Window* const page = g_activeHost != nullptr ? g_activeHost->VisiblePage() : nullptr;
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
    [[nodiscard]] static bool PointerToPage(const double x, const double y, F&& fn) {
        Window* const page = g_activeHost != nullptr ? g_activeHost->VisiblePage() : nullptr;
        if (page == nullptr) {
            return false;
        }
        if (page->pageFloating && g_activeHost->m_main != nullptr) {
            const prism::Vec2 mp = g_activeHost->ToUi(*g_activeHost->m_main, x, y);
            const bool held = page->down[0] || page->down[1] || page->down[2];
            const bool inside = page->pageBounds.contains(mp);
            const bool popup = g_activeHost->m_app != nullptr &&
                               g_activeHost->m_app->windowHasBlockingPopup(page->id);
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
    static void CursorPos(GLFWwindow* w, const double x, const double y) {
        if (PointerToPage(x, y, [x, y](Window& page) { g_activeHost->OnCursorPos(page, x, y); })) {
            return;
        }
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->cursorPos != nullptr) { g_activeHost->m_chain->cursorPos(w, x, y); }
            g_activeHost->OnCursorPos(*main, x, y);
        }
    }
    static void MouseButton(GLFWwindow* w, const int button, const int action, const int mods) {
        double cx = 0.0;
        double cy = 0.0;
        if (w != nullptr) {
            glfwGetCursorPos(w, &cx, &cy);
        }
        if (PointerToPage(cx, cy, [=](Window& page) { g_activeHost->OnMouseButton(page, button, action, mods); })) {
            return;
        }
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->mouseButton != nullptr) {
                g_activeHost->m_chain->mouseButton(w, button, action, mods);
            }
            g_activeHost->OnMouseButton(*main, button, action, mods);
        }
    }
    static void Scroll(GLFWwindow* w, const double dx, const double dy) {
        double cx = 0.0;
        double cy = 0.0;
        if (w != nullptr) {
            glfwGetCursorPos(w, &cx, &cy);
        }
        if (PointerToPage(cx, cy, [=](Window& page) { g_activeHost->OnScroll(page, dx, dy); })) {
            return;
        }
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->scroll != nullptr) { g_activeHost->m_chain->scroll(w, dx, dy); }
            g_activeHost->OnScroll(*main, dx, dy);
        }
    }
    static void Key(GLFWwindow* w, const int key, const int scancode, const int action, const int mods) {
        if (ToPage([=](Window& page) { g_activeHost->OnKey(page, key, scancode, action, mods); })) {
            return;
        }
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->key != nullptr) { g_activeHost->m_chain->key(w, key, scancode, action, mods); }
            g_activeHost->OnKey(*main, key, scancode, action, mods);
        }
    }
    static void Char(GLFWwindow* w, const unsigned int codepoint) {
        if (ToPage([=](Window& page) { g_activeHost->OnChar(page, codepoint); })) {
            return;
        }
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->character != nullptr) { g_activeHost->m_chain->character(w, codepoint); }
            g_activeHost->OnChar(*main, codepoint);
        }
    }
    static void CursorEnter(GLFWwindow* w, const int entered) {
        if (Window* main = Main()) {
            if (g_activeHost->m_chain->cursorEnter != nullptr) { g_activeHost->m_chain->cursorEnter(w, entered); }
            g_activeHost->OnCursorEnter(*main, entered);
        }
    }
#if !defined(__ANDROID__)
    static void Drop(GLFWwindow* w, const int count, const char** paths) {
        if (g_activeHost == nullptr) {
            return;
        }
        if (g_activeHost->m_chain->drop != nullptr) { g_activeHost->m_chain->drop(w, count, paths); }
        if (!g_activeHost->onFilesDropped || count <= 0 || paths == nullptr) {
            return;
        }
        std::vector<std::filesystem::path> files;
        for (int i = 0; i < count; ++i) {
            if (paths[i] != nullptr) {   // GLFW hands UTF-8
                const std::string utf8(paths[i]);
                files.emplace_back(std::u8string(utf8.begin(), utf8.end()));
            }
        }
        g_activeHost->onFilesDropped(std::move(files));
    }
#endif
};

namespace {

/// A secondary window's callbacks: its GLFW user pointer is its Window.
using HostWindow = EditorHost::Window;
[[nodiscard]] HostWindow* Owner(GLFWwindow* w) {
    return static_cast<HostWindow*>(glfwGetWindowUserPointer(w));
}
/// Runs `fn` inside the window's scope, when it has one.
template <typename F>
void Scoped(HostWindow& o, F&& fn) {
    if (o.scope) {
        o.scope(std::function<void()>(std::forward<F>(fn)));
    } else {
        fn();
    }
}
void SecondaryCursorPos(GLFWwindow* w, const double x, const double y) {
    if (HostWindow* o = Owner(w)) { Scoped(*o, [o, x, y] { o->host->OnCursorPos(*o, x, y); }); }
}
void SecondaryMouseButton(GLFWwindow* w, const int button, const int action, const int mods) {
    if (HostWindow* o = Owner(w)) {
        Scoped(*o, [o, button, action, mods] { o->host->OnMouseButton(*o, button, action, mods); });
    }
}
void SecondaryScroll(GLFWwindow* w, const double dx, const double dy) {
    if (HostWindow* o = Owner(w)) { Scoped(*o, [o, dx, dy] { o->host->OnScroll(*o, dx, dy); }); }
}
void SecondaryKey(GLFWwindow* w, const int key, const int scancode, const int action, const int mods) {
    if (HostWindow* o = Owner(w)) {
        Scoped(*o, [o, key, scancode, action, mods] { o->host->OnKey(*o, key, scancode, action, mods); });
    }
}
void SecondaryChar(GLFWwindow* w, const unsigned int codepoint) {
    if (HostWindow* o = Owner(w)) { Scoped(*o, [o, codepoint] { o->host->OnChar(*o, codepoint); }); }
}
void SecondaryCursorEnter(GLFWwindow* w, const int entered) {
    if (HostWindow* o = Owner(w)) { Scoped(*o, [o, entered] { o->host->OnCursorEnter(*o, entered); }); }
}
void SecondaryClose(GLFWwindow* w) {
    // The editor decides (unsaved changes ask first); the window stays until it closes it.
    glfwSetWindowShouldClose(w, GLFW_FALSE);
    if (HostWindow* o = Owner(w)) { o->closeRequested = true; }
}
void SecondaryFocus(GLFWwindow* w, const int focused) {
    if (HostWindow* o = Owner(w); o != nullptr && focused != 0) {
        o->host->FocusWindow(o->id);
    }
}

} // namespace

// ------------------------------------------------------------------------------------------ host

EditorHost::EditorHost() = default;

EditorHost::~EditorHost() {
    Shutdown();
}

EditorHost::Window* EditorHost::Find(const int id) const {
    if (id == kMainWindow) {
        return m_main.get();
    }
    const auto it = m_windows.find(id);
    return it != m_windows.end() ? it->second.get() : nullptr;
}

namespace {

/// The UniVex mark every PRISM window shows in its title bar, inside PRISM's asset directory.
inline constexpr std::string_view kBrandMarkFile = "images/univex_logo.png";

/// Hands the brand mark to PRISM; a missing or unreadable image leaves the title bars without one.
void LoadBrandMark(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(raw.size());
    std::transform(raw.begin(), raw.end(), bytes.begin(), [](const char value) { return static_cast<std::byte>(value); });
    UVE::Asset::PngRgba8Image image;
    if (bytes.empty() || !UVE::Asset::DecodePngRgba8Image(bytes, image) || image.width == 0U || image.height == 0U) {
        return;
    }
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t index = 0U; index < argb.size(); ++index) {
        const std::byte* const pixel = image.pixels.data() + index * UVE::Asset::kPngRgba8Channels;
        argb[index] = prism::Color(std::to_integer<std::uint8_t>(pixel[0]), std::to_integer<std::uint8_t>(pixel[1]),
                                   std::to_integer<std::uint8_t>(pixel[2]), std::to_integer<std::uint8_t>(pixel[3]))
                          .argb();
    }
    prism::setBrandMark(std::move(argb), static_cast<int>(image.width), static_cast<int>(image.height));
}

} // namespace

bool EditorHost::Initialize(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (m_ready || window == nullptr || g_activeHost != nullptr) {
        return false;
    }
    m_main = std::make_unique<Window>();
    m_main->host = this;
    m_main->id = kMainWindow;
    m_main->window = window;
    m_singleSurface = kSingleSurface;
    glfwGetFramebufferSize(window, &m_main->fbWidth, &m_main->fbHeight);
    m_main->fbWidth = std::max(1, m_main->fbWidth);
    m_main->fbHeight = std::max(1, m_main->fbHeight);
    m_main->uiScale = ComputeUiScale(window, m_main->fbWidth, m_main->fbHeight, m_singleSurface);
    m_main->uiWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_main->fbWidth) / m_main->uiScale)));
    m_main->uiHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(m_main->fbHeight) / m_main->uiScale)));

    m_platform = std::make_shared<Platform>(*this, window);
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

    LoadBrandMark(assetDir / kBrandMarkFile);

    std::string error;
    m_backend = prism::GlBackend::create(&GlLoader, {}, /*blitToScreen*/ false, &error);
    if (m_backend == nullptr) {
        std::fprintf(stderr, "PRISM: OpenGL backend unavailable: %s\n", error.c_str());
        m_app.reset();
        m_main.reset();
        return false;
    }
    m_backend->setPixelScale(m_main->uiScale);
    m_backend->resize(m_main->fbWidth, m_main->fbHeight);
    m_app->setRenderBackend(m_backend);

    m_compositor = std::make_unique<Compositor>();
    if (!m_compositor->Initialize()) {
        std::fprintf(stderr, "PRISM: compositor shaders unavailable\n");
        m_compositor.reset();
        m_backend.reset();
        m_app.reset();
        m_main.reset();
        return false;
    }

    g_activeHost = this;
    m_chain = std::make_unique<Chain>();
    m_chain->cursorPos = glfwSetCursorPosCallback(window, &Chain::CursorPos);
    m_chain->mouseButton = glfwSetMouseButtonCallback(window, &Chain::MouseButton);
    m_chain->scroll = glfwSetScrollCallback(window, &Chain::Scroll);
    m_chain->key = glfwSetKeyCallback(window, &Chain::Key);
    m_chain->character = glfwSetCharCallback(window, &Chain::Char);
    m_chain->cursorEnter = glfwSetCursorEnterCallback(window, &Chain::CursorEnter);
#if !defined(__ANDROID__)
    m_chain->drop = glfwSetDropCallback(window, &Chain::Drop);
#endif

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    m_main->mouse = ToUi(*m_main, x, y);
    m_main->frameStartMouse = m_main->mouse;
    m_main->lastFrameTime = glfwGetTime();
    m_ready = true;
    return true;
}

void EditorHost::Shutdown() {
    if (!m_ready) {
        return;
    }
    while (!m_windows.empty()) {
        CloseWindow(m_windows.begin()->first);
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
    if (g_activeHost == this) {
        g_activeHost = nullptr;
    }
    m_chain.reset();
    if (m_gpuTimer != nullptr) {
        m_gpuTimer->Shutdown();
        m_gpuTimer.reset();
    }
    if (m_compositor != nullptr) {
        m_compositor->Shutdown();
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

void EditorHost::SetRoot(prism::Ref<prism::Widget> root) {
    if (m_app != nullptr) {
        m_app->setRoot(std::move(root));
    }
}

int EditorHost::OpenWindow(prism::Ref<prism::Widget> root, const std::string& title, const int width,
                                      const int height) {
    if (!m_ready || root == nullptr) {
        return 0;
    }
    if (m_singleSurface) {
        // A page over the main window: the same surface and context, the full screen, and the main
        // window's input while it is on top.
        auto w = std::make_unique<Window>();
        w->host = this;
        w->window = m_main->window;
        w->page = true;
        w->fbWidth = m_main->fbWidth;
        w->fbHeight = m_main->fbHeight;
        w->uiScale = m_main->uiScale;
        w->uiWidth = m_main->uiWidth;
        w->uiHeight = m_main->uiHeight;
        std::string error;
        w->backend = prism::GlBackend::create(&GlLoader, {}, /*blitToScreen*/ false, &error);
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
        ShowPage(id);
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

    auto w = std::make_unique<Window>();
    w->host = this;
    w->window = window;
    glfwGetFramebufferSize(window, &w->fbWidth, &w->fbHeight);
    w->fbWidth = std::max(1, w->fbWidth);
    w->fbHeight = std::max(1, w->fbHeight);
    w->uiScale = 1.0F;
    w->uiWidth = w->fbWidth;
    w->uiHeight = w->fbHeight;
    std::string error;
    w->backend = prism::GlBackend::create(&GlLoader, {}, /*blitToScreen*/ false, &error);
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
    glfwSetCursorPosCallback(window, &SecondaryCursorPos);
    glfwSetMouseButtonCallback(window, &SecondaryMouseButton);
    glfwSetScrollCallback(window, &SecondaryScroll);
    glfwSetKeyCallback(window, &SecondaryKey);
    glfwSetCharCallback(window, &SecondaryChar);
    glfwSetCursorEnterCallback(window, &SecondaryCursorEnter);
    glfwSetWindowCloseCallback(window, &SecondaryClose);
    glfwSetWindowFocusCallback(window, &SecondaryFocus);
    w->lastFrameTime = glfwGetTime();
    m_app->windowResize(id, w->fbWidth, w->fbHeight);
    m_windows.emplace(id, std::move(w));
    return id;
}

void EditorHost::CloseWindow(const int id) {
    const auto it = m_windows.find(id);
    if (it == m_windows.end()) {
        return;
    }
    std::unique_ptr<Window> w = std::move(it->second);
    m_windows.erase(it);
    m_app->closeWindow(id);
    m_platform->ForgetWindow(id);
    if (w->page) {
        // The main surface stays; the page only had its own UI renderer.
        m_pageOrder.erase(std::remove(m_pageOrder.begin(), m_pageOrder.end(), id), m_pageOrder.end());
        w->backend.reset();
        if (Window* const shown = VisiblePage()) {
            m_app->activateWindow(shown->id);
        } else {
            m_app->activateWindow(kMainWindow);
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

void EditorHost::SetWindowScope(const int id, WindowScope scope) {
    if (Window* w = id != kMainWindow ? Find(id) : nullptr) {
        w->scope = std::move(scope);
    }
}

bool EditorHost::HasWindow(const int id) const {
    return Find(id) != nullptr;
}

bool EditorHost::WindowCloseRequested(const int id) const {
    const Window* w = Find(id);
    return w != nullptr && w->closeRequested;
}

void EditorHost::ClearCloseRequest(const int id) {
    if (Window* w = Find(id)) {
        w->closeRequested = false;
    }
}

void EditorHost::FocusWindow(const int id) {
    Window* w = Find(id);
    if (w == nullptr) {
        return;
    }
    if (w->page) {
        ShowPage(id);
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

prism::Vec2 EditorHost::ToUi(const Window& w, const double x, const double y) const {
    if (w.page && w.pageFloating && m_main != nullptr) {
        const prism::Vec2 mp = ToUi(*m_main, x, y);
        return prism::Vec2{mp.x - w.pageBounds.x, mp.y - w.pageBounds.y};
    }
    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(w.window, &winW, &winH);
    const float sx = winW > 0 ? static_cast<float>(w.uiWidth) / static_cast<float>(winW) : 1.0F;
    const float sy = winH > 0 ? static_cast<float>(w.uiHeight) / static_cast<float>(winH) : 1.0F;
    return prism::Vec2{static_cast<float>(x) * sx, static_cast<float>(y) * sy};
}

bool EditorHost::InTargetScope(const Window& source, const Window& target, const std::function<void()>& fn) {
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

EditorHost::Window* EditorHost::DragTarget(const Window& source, prism::Vec2& local) const {
    // A PRISM drag (a Asset Table item, say) can end over another of the editor's windows. The
    // OS keeps sending the pointer to the window the press started in; GLFW_HOVERED asks the OS which
    // window is really under the pointer (its own stacking, not PRISM's order).
    if (!m_app || !m_app->isDragging() || source.window == nullptr || glfwGetWindowAttrib(source.window, GLFW_HOVERED) != 0) {
        return nullptr;
    }
    const auto hovered = [&](Window* const target) -> Window* {
        if (target == nullptr || target == &source || target->window == nullptr ||
            glfwGetWindowAttrib(target->window, GLFW_HOVERED) == 0) {
            return nullptr;
        }
        double cx = 0.0;
        double cy = 0.0;
        glfwGetCursorPos(target->window, &cx, &cy);
        local = ToUi(*target, cx, cy);
        return target;
    };
    if (Window* const target = hovered(m_main.get())) {
        return target;
    }
    for (const auto& [id, window] : m_windows) {
        if (Window* const target = hovered(window.get())) {
            return target;
        }
    }
    return nullptr;
}

void EditorHost::ApplyPageLayout(Window& w) {
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

void EditorHost::SyncSize(Window& w) {
    if (w.page) {
        ApplyPageLayout(w);
        return;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(w.window, &width, &height);
    if (width <= 0 || height <= 0) {
        return; // minimized
    }
    const float scale = ComputeUiScale(w.window, width, height, m_singleSurface);
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
        if (w.id == kMainWindow) {
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

void EditorHost::BeginFrame() {
    if (!m_ready) {
        return;
    }
    const double now = glfwGetTime();
    const auto stamp = [now](Window& w) {
        w.deltaTime = static_cast<float>(std::clamp(now - w.lastFrameTime, 1.0 / 1000.0, 0.25));
        w.lastFrameTime = now;
    };
    stamp(*m_main);
    SyncSize(*m_main);
    for (auto& [id, w] : m_windows) {
        stamp(*w);
        SyncSize(*w);
    }
    static_cast<void>(m_app->tick());
    for (auto& [id, w] : m_windows) {
        m_app->layoutWindow(id);
    }
}

EditorViewportInput EditorHost::CaptureViewInput(const int id, const prism::Rect& rect) const {
    EditorViewportInput input{};
    const Window* found = Find(id);
    if (found == nullptr) {
        return input;
    }
    const Window& w = *found;
    const float scale = std::max(1.0F, w.uiScale);
    const bool inside = w.mouseInside && w.mouse.x >= rect.x && w.mouse.y >= rect.y && w.mouse.x < rect.x + rect.w &&
                        w.mouse.y < rect.y + rect.h;
    // A floating panel (Details torn off, ...) leaves the viewport usable; a menu or a modal does not.
    input.hovered = inside && w.dragMode == Window::DragMode::None && !w.pageDragging &&
                    !m_app->windowHasBlockingPopup(id) && !m_app->windowWantsPointerAt(id, w.mouse);
    input.imageOrigin = Math::Vector2{rect.x * scale, rect.y * scale};
    input.mousePos = Math::Vector2{w.mouse.x * scale, w.mouse.y * scale};
    input.mouseDelta = Math::Vector2{(w.mouse.x - w.frameStartMouse.x) * scale,
                                        (w.mouse.y - w.frameStartMouse.y) * scale};
    input.wheel = w.wheel;
    input.deltaTime = w.deltaTime;
    input.shift = (w.mods & prism::ModShift) != 0;
    input.ctrl = (w.mods & prism::ModCtrl) != 0;
    input.alt = (w.mods & prism::ModAlt) != 0;
#if defined(__ANDROID__)
    input.touchNavigation = m_singleSurface && !UVE::Window::AndroidGlfwHasHardwareInput();
#else
    input.touchNavigation = m_singleSurface;
#endif
    input.dpiScale = scale;
    // Keys reach the view only while its window has the keyboard.
    const bool windowHasKeys = m_app->activeWindow() == id;
    input.wantTextInput = !windowHasKeys || m_app->windowWantsKeyboard(id) || m_platform->TextInputActive();
    for (std::size_t i = 0; i < EditorViewportInput::kButtonCount; ++i) {
        input.down[i] = w.down[i];
        input.clicked[i] = w.clicked[i];
        input.released[i] = w.released[i];
        input.pressPos[i] = Math::Vector2{w.pressPos[i].x * scale, w.pressPos[i].y * scale};
        input.dragDistance[i] = w.dragDistance[i] * scale;
    }
    if (!input.wantTextInput) {
        input.keysDown = w.keysDown;
        input.keysPressed = w.keysPressed;
    }
    return input;
}

void EditorHost::ApplyViewCursor(const int id, const EditorViewportInput& input) {
    const Window* w = Find(id);
    if (!m_ready || w == nullptr || w->dragMode != Window::DragMode::None) {
        return;
    }
    if (input.hovered && input.cursorRequested) {
        m_platform->SetViewCursor(id, input.requestedCursor);
    }
}

void EditorHost::Composite(Window& w, const std::uint32_t uiTexture, const std::uint32_t vao,
                                      const std::span<const ViewImage> views) {
    Compositor& c = *m_compositor;
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
    for (const ViewImage& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{view.rect.x * scaleX, view.rect.y * scaleY, view.rect.w * scaleX,
                                view.rect.h * scaleY};
        c.Draw(view.texture, shown, w.fbWidth, w.fbHeight, u1, v1);
    }
    glEnable(GL_BLEND);
    c.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    c.Draw(uiTexture, prism::Rect{0.0F, 0.0F, static_cast<float>(w.fbWidth), static_cast<float>(w.fbHeight)},
              w.fbWidth, w.fbHeight, 1.0F, 1.0F);
    glDisable(GL_BLEND);
    c.glBindVertexArray(0);
    c.glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorHost::CompositePageOverlay(Window& w, const std::uint32_t uiTexture,
                                            const std::span<const ViewImage> views) {
    if (m_main == nullptr) {
        return;
    }
    Compositor& c = *m_compositor;
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
    for (const ViewImage& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{dst.x + view.rect.x * localScaleX, dst.y + view.rect.y * localScaleY,
                                view.rect.w * localScaleX, view.rect.h * localScaleY};
        c.Draw(view.texture, shown, mainFbW, mainFbH, u1, v1);
    }
    glEnable(GL_BLEND);
    c.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    c.Draw(uiTexture, dst, mainFbW, mainFbH, 1.0F, 1.0F);
    glDisable(GL_BLEND);
    c.glBindVertexArray(0);
    c.glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorHost::EndInputFrame(Window& w) {
    // Edges are per frame.
    w.clicked.fill(false);
    w.released.fill(false);
    w.keysPressed.reset();
    w.wheel = 0.0F;
    w.frameStartMouse = w.mouse;
}

void EditorHost::Render(const std::span<const ViewImage> views) {
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
    Composite(*m_main, m_backend->colorTexture(), m_compositor->vao, views);
    EndInputFrame(*m_main);
}

void EditorHost::BeginGpuTimer() {
    if (!m_ready) {
        return;
    }
    if (m_gpuTimer == nullptr) {
        m_gpuTimer = std::make_unique<GpuTimer>();
        m_gpuTimer->Initialize();
    }
    m_gpuTimer->Begin();
}

void EditorHost::EndGpuTimer() {
    if (m_gpuTimer != nullptr) {
        m_gpuTimer->End();
    }
}

float EditorHost::GpuFrameMs() const noexcept {
    return m_gpuTimer != nullptr ? m_gpuTimer->lastMs : 0.0F;
}

void EditorHost::RenderWindow(const int id, const std::span<const ViewImage> views) {
    Window* found = id != kMainWindow ? Find(id) : nullptr;
    if (!m_ready || found == nullptr) {
        return;
    }
    Window& w = *found;
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
        if (VisiblePage() == &w) {
            paintIfNeeded();
            if (w.pageFloating) {
                CompositePageOverlay(w, w.backend->colorTexture(), views);
            } else {
                Composite(w, w.backend->colorTexture(), m_compositor->vao, views);
            }
        }
        EndInputFrame(w);
        return;
    }
    if (glfwGetWindowAttrib(w.window, GLFW_ICONIFIED) != 0) {
        EndInputFrame(w);
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
    Composite(w, uiTexture, w.vao, views);
    glfwSwapBuffers(w.window);
    glfwMakeContextCurrent(m_main->window);
    EndInputFrame(w);
}

EditorHost::Window* EditorHost::VisiblePage() const {
    for (auto order = m_pageOrder.rbegin(); order != m_pageOrder.rend(); ++order) {
        const auto found = m_windows.find(*order);
        if (found != m_windows.end() && !found->second->pageHidden) {
            return found->second.get();
        }
    }
    return nullptr;
}

void EditorHost::ShowPage(const int id) {
    Window* const w = Find(id);
    if (w == nullptr || !w->page) {
        return;
    }
    w->pageHidden = false;
    m_pageOrder.erase(std::remove(m_pageOrder.begin(), m_pageOrder.end(), id), m_pageOrder.end());
    m_pageOrder.push_back(id);
    m_app->activateWindow(id);
}

void EditorHost::HidePage(const int id) {
    Window* const w = Find(id);
    if (w == nullptr || !w->page) {
        return;
    }
    w->pageHidden = true;
    w->pageDragging = false;
    w->down.fill(false);
    if (Window* const shown = VisiblePage()) {
        m_app->activateWindow(shown->id);
    } else {
        m_app->activateWindow(kMainWindow);
    }
}

bool EditorHost::HasOpaqueFullPage() const {
    if (!m_singleSurface) {
        return false;
    }
    const Window* const page = VisiblePage();
    return page != nullptr && !page->pageFloating;
}

bool EditorHost::IsWindowFocused(const int id) const {
    const auto found = m_windows.find(id);
    if (m_ready && found != m_windows.end() && found->second->page) {
        return VisiblePage() == found->second.get() && glfwGetWindowAttrib(m_main->window, GLFW_FOCUSED) != 0;
    }
    return m_ready && found != m_windows.end() && found->second->window != nullptr &&
           glfwGetWindowAttrib(found->second->window, GLFW_FOCUSED) != 0;
}

bool EditorHost::QuitRequested() const noexcept {
    return m_main != nullptr && m_main->window != nullptr && glfwWindowShouldClose(m_main->window) != 0;
}

namespace {

[[nodiscard]] bool WriteFramebufferPng(const std::filesystem::path& path, const int width, const int height) {
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

bool EditorHost::SaveScreenshot(const std::filesystem::path& path) {
    if (!m_ready) {
        return false;
    }
    m_compositor->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return WriteFramebufferPng(path, m_main->fbWidth, m_main->fbHeight);
}

bool EditorHost::SaveWindowScreenshot(const int id, const std::filesystem::path& path) {
    Window* w = id != kMainWindow ? Find(id) : nullptr;
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
    Composite(*w, uiTexture, w->vao, {});
    const bool ok = WriteFramebufferPng(path, w->fbWidth, w->fbHeight);
    glfwMakeContextCurrent(m_main->window);
    return ok;
}

// ----------------------------------------------------------------------------------------- input

void EditorHost::SetKeyHook(std::function<bool(int key, int action)> hook) {
    m_keyHook = std::move(hook);
}

void EditorHost::OnCursorPos(Window& w, const double x, const double y) {
    if (w.page && w.pageFloating && w.pageDragging && m_main != nullptr) {
        const prism::Vec2 mp = ToUi(*m_main, x, y);
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
    w.mouse = ToUi(w, x, y);
    w.mouseInside = true;
    for (std::size_t i = 0; i < w.down.size(); ++i) {
        if (w.down[i]) {
            const float dx = w.mouse.x - w.pressPos[i].x;
            const float dy = w.mouse.y - w.pressPos[i].y;
            w.dragDistance[i] = std::max(w.dragDistance[i], std::sqrt((dx * dx) + (dy * dy)));
        }
    }
    if (w.dragMode != Window::DragMode::None) {
        UpdateWindowDrag(w);
        return;
    }
    // Edge-resize affordance on an undecorated, restored desktop window.
    int edges = 0;
    if (!w.page && glfwGetWindowAttrib(w.window, GLFW_DECORATED) == 0 && glfwGetWindowAttrib(w.window, GLFW_MAXIMIZED) == 0) {
        const float width = static_cast<float>(w.fbWidth);
        const float height = static_cast<float>(w.fbHeight);
        if (w.mouse.x < kResizeBorder) { edges |= 1; }
        if (w.mouse.x >= width - kResizeBorder) { edges |= 2; }
        if (w.mouse.y < kResizeBorder) { edges |= 4; }
        if (w.mouse.y >= height - kResizeBorder) { edges |= 8; }
    }
    prism::Vec2 dragLocal{};
    if (Window* const target = DragTarget(w, dragLocal)) {
        // Dragging over another window: that window's widgets see the drag; this one sees it leave.
        if (w.dragOverWindow != target->id) {
            m_app->windowMouseLeave(w.id);
            w.dragOverWindow = target->id;
        }
        target->mouse = dragLocal;
        static_cast<void>(InTargetScope(w, *target, [this, target, dragLocal] { m_app->windowMouseMove(target->id, dragLocal); }));
        return;
    }
    if (w.dragOverWindow >= 0) {
        if (Window* const previous = Find(w.dragOverWindow)) {
            m_app->windowMouseLeave(previous->id);
        }
        w.dragOverWindow = -1;
    }
    m_app->windowMouseMove(w.id, w.mouse);
    if (edges != 0) {
        m_platform->SetResizeCursor(w.id, edges);
    }
}

void EditorHost::OnMouseButton(Window& w, const int button, const int action, const int mods) {
    const std::uint32_t bit = (button >= 0 && button < 32) ? (1U << static_cast<unsigned>(button)) : 0U;
    w.mods = ToPrismMods(mods);
    m_app->hostSetModifiers(w.mods);
    const int slot = ToViewButton(button);
    const prism::MouseButton prismButton = ToPrismButton(button);
    if (action == GLFW_PRESS) {
        const double now = glfwGetTime();
        const float dx = w.mouse.x - w.lastClickPos.x;
        const float dy = w.mouse.y - w.lastClickPos.y;
        if (now - w.lastClickTime <= kDoubleClickSeconds && std::abs(dx) <= kDoubleClickSlop &&
            std::abs(dy) <= kDoubleClickSlop) {
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

        if (w.page && w.pageFloating && button == GLFW_MOUSE_BUTTON_LEFT && w.CaptionContains(w.mouse) &&
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
                if (w.mouse.x < kResizeBorder) { edges |= 1; }
                if (w.mouse.x >= width - kResizeBorder) { edges |= 2; }
                if (w.mouse.y < kResizeBorder) { edges |= 4; }
                if (w.mouse.y >= height - kResizeBorder) { edges |= 8; }
            }
            const bool inCaption = w.CaptionContains(w.mouse);
#if defined(_WIN32)
            // Windows moves and sizes the window itself from here: the native drag snaps to the screen
            // edges, restores a maximized window as it is dragged off, and keeps following the cursor
            // however fast it moves. The call returns once the button is released.
            if (edges != 0 || (inCaption && w.clickCount == 1)) {
                static constexpr std::array<WPARAM, 16> kHit{
                    HTCAPTION, HTLEFT, HTRIGHT, HTCAPTION, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTCAPTION,
                    HTBOTTOM, HTBOTTOMLEFT, HTBOTTOMRIGHT, HTCAPTION, HTCAPTION, HTCAPTION, HTCAPTION, HTCAPTION};
                HWND hwnd = glfwGetWin32Window(w.window);
                if (slot >= 0) {
                    w.down[static_cast<std::size_t>(slot)] = false;
                }
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, kHit[static_cast<std::size_t>(edges & 15)], 0);
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
                w.dragMode = edges != 0 ? Window::DragMode::Resize : Window::DragMode::Move;
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
        const bool wasResize = w.dragMode == Window::DragMode::Resize;
        const bool wasPageDrag = w.pageDragging;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            w.dragMode = Window::DragMode::None;
            w.pageDragging = false;
        }
        if (!wasResize && !wasPageDrag && prismButton != prism::MouseButton::None) {
            prism::Vec2 dragLocal{};
            if (Window* const target = DragTarget(w, dragLocal)) {
                // The drop lands in the window under the pointer; the press's own window then sees
                // its release (outside its widgets) so its capture ends.
                const int clicks = w.clickCount;
                static_cast<void>(InTargetScope(w, *target, [this, target, dragLocal, prismButton, clicks] {
                    m_app->windowMouseButton(target->id, dragLocal, prismButton, false, clicks);
                }));
                m_app->cancelDragDrop();
            }
            w.dragOverWindow = -1;
            m_app->windowMouseButton(w.id, w.mouse, prismButton, false, w.clickCount);
        }
    }
}

void EditorHost::UpdateWindowDrag(Window& w) {
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
    if (w.dragMode == Window::DragMode::Move) {
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

void EditorHost::OnScroll(Window& w, const double dx, const double dy) {
    w.wheel += static_cast<float>(dy);
    m_app->windowMouseWheel(w.id, w.mouse, prism::Vec2{static_cast<float>(dx), static_cast<float>(dy)});
}

void EditorHost::OnKey(Window& w, const int key, const int /*scancode*/, const int action, const int mods) {
    if (w.id == kMainWindow && m_keyHook && m_keyHook(key, action)) {
        return;   // taken before the UI sees it (Escape giving the editor its mouse back)
    }
    w.mods = ToPrismMods(mods);
    m_app->hostSetModifiers(w.mods);
    const ViewKey viewKey = ToViewKey(key);
    if (viewKey != ViewKey::Count) {
        const auto index = static_cast<std::size_t>(viewKey);
        if (action == GLFW_PRESS) {
            w.keysDown.set(index);
            w.keysPressed.set(index);
        } else if (action == GLFW_RELEASE) {
            w.keysDown.reset(index);
        }
    }
    const prism::Key prismKey = ToPrismKey(key);
    if (prismKey == prism::Key::Unknown) {
        return;
    }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        m_app->windowKey(w.id, prismKey, w.mods, true, action == GLFW_REPEAT);
    } else if (action == GLFW_RELEASE) {
        m_app->windowKey(w.id, prismKey, w.mods, false, false);
    }
}

void EditorHost::OnChar(Window& w, const unsigned int codepoint) {
    std::string utf8;
    AppendUtf8(utf8, static_cast<char32_t>(codepoint));
    m_app->windowText(w.id, utf8);
}

void EditorHost::OnCursorEnter(Window& w, const int entered) {
    w.mouseInside = entered != 0;
    if (!w.mouseInside && w.dragMode == Window::DragMode::None) {
        m_app->windowMouseLeave(w.id);
    }
}

bool EditorHost::IsAnyWindowFocused() const {
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
