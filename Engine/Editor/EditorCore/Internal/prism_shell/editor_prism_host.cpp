// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_host.h"

#include "prism/Canvas.h"
#include "prism/GlBackend.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

EditorPrismHostUVE* g_activeHostUVE = nullptr;

using ViewKeyUVE = EditorViewportInputUVE::KeyUVE;
using ViewCursorUVE = EditorViewportInputUVE::CursorUVE;

constexpr double kDoubleClickSecondsUVE = 0.4;
constexpr float kDoubleClickSlopUVE = 4.0F;
constexpr float kResizeBorderUVE = 5.0F;

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

} // namespace

// ------------------------------------------------------------------------------------- platform

class EditorPrismHostUVE::PlatformUVE final : public prism::Platform {
public:
    explicit PlatformUVE(GLFWwindow* window) : m_window(window) {
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
        m_uiCursor = c;
        ApplyCursorUVE(m_cursors[static_cast<int>(c)]);
    }
    void setTextInputActive(const bool active) override { m_textInput = active; }
    float dpiScale() const override { return 1.0F; }
    void requestQuit() override { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
    void* nativeWindowHandle() const override { return m_window; }
    void setWindowCaption(const int id, const prism::Rect dragArea) override {
        if (id == 0) {
            m_caption = dragArea;
        }
    }
    void minimizeWindow(const int id) override {
        if (id == 0) {
            glfwIconifyWindow(m_window);
        }
    }
    void toggleMaximizeWindow(const int id) override {
        if (id != 0) {
            return;
        }
        if (glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0) {
            glfwRestoreWindow(m_window);
        } else {
            glfwMaximizeWindow(m_window);
        }
    }
    bool windowMaximized(const int id) const override {
        return id == 0 && glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
    }

    [[nodiscard]] const prism::Rect& CaptionUVE() const noexcept { return m_caption; }
    [[nodiscard]] bool TextInputActiveUVE() const noexcept { return m_textInput; }

    void SetViewCursorUVE(const ViewCursorUVE cursor) {
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
        ApplyCursorUVE(m_cursors[static_cast<int>(mapped)]);
    }
    void SetResizeCursorUVE(const int edges) {
        const bool horizontal = (edges & 3) != 0;
        const bool vertical = (edges & 12) != 0;
        prism::Cursor c = prism::Cursor::Arrow;
        if (horizontal && vertical) { c = prism::Cursor::SizeAll; }
        else if (horizontal) { c = prism::Cursor::SizeWE; }
        else if (vertical) { c = prism::Cursor::SizeNS; }
        ApplyCursorUVE(m_cursors[static_cast<int>(c)]);
    }
    void RestoreUiCursorUVE() { ApplyCursorUVE(m_cursors[static_cast<int>(m_uiCursor)]); }

private:
    void ApplyCursorUVE(GLFWcursor* cursor) {
        if (cursor != m_applied) {
            m_applied = cursor;
            glfwSetCursor(m_window, cursor);
        }
    }

    GLFWwindow* m_window = nullptr;
    std::array<GLFWcursor*, 6> m_cursors{};
    GLFWcursor* m_applied = nullptr;
    prism::Cursor m_uiCursor = prism::Cursor::Arrow;
    prism::Rect m_caption{};
    bool m_textInput = false;
};

// ------------------------------------------------------------------------------------ compositor

/// Draws textured rectangles into the default framebuffer: engine view textures into their holes,
/// then the PRISM frame (premultiplied, transparent in the holes) over the whole window.
struct EditorPrismHostUVE::CompositorUVE final {
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

    GLuint program = 0;
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
        if (!ok) {
            return false;
        }
        // A quad from gl_VertexID: uRect = NDC (x0, y0, x1, y1), uUv = texture (u0, v0, u1, v1).
        static const char* const kVertex = R"(#version 330 core
uniform vec4 uRect;
uniform vec4 uUv;
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), 0.0, 1.0);
    vUv = mix(uUv.xy, uUv.zw, corner);
})";
        static const char* const kFragment = R"(#version 330 core
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
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == 0) {
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

// ------------------------------------------------------------------------------- GLFW callbacks

struct EditorPrismHostUVE::ChainUVE final {
    GLFWcursorposfun cursorPos = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorenterfun cursorEnter = nullptr;

    static void CursorPosUVE(GLFWwindow* w, const double x, const double y) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->cursorPos != nullptr) { g_activeHostUVE->m_chain->cursorPos(w, x, y); }
            g_activeHostUVE->OnCursorPosUVE(x, y);
        }
    }
    static void MouseButtonUVE(GLFWwindow* w, const int button, const int action, const int mods) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->mouseButton != nullptr) {
                g_activeHostUVE->m_chain->mouseButton(w, button, action, mods);
            }
            g_activeHostUVE->OnMouseButtonUVE(button, action, mods);
        }
    }
    static void ScrollUVE(GLFWwindow* w, const double dx, const double dy) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->scroll != nullptr) { g_activeHostUVE->m_chain->scroll(w, dx, dy); }
            g_activeHostUVE->OnScrollUVE(dx, dy);
        }
    }
    static void KeyUVE(GLFWwindow* w, const int key, const int scancode, const int action, const int mods) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->key != nullptr) { g_activeHostUVE->m_chain->key(w, key, scancode, action, mods); }
            g_activeHostUVE->OnKeyUVE(key, scancode, action, mods);
        }
    }
    static void CharUVE(GLFWwindow* w, const unsigned int codepoint) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->character != nullptr) { g_activeHostUVE->m_chain->character(w, codepoint); }
            g_activeHostUVE->OnCharUVE(codepoint);
        }
    }
    static void CursorEnterUVE(GLFWwindow* w, const int entered) {
        if (g_activeHostUVE != nullptr) {
            if (g_activeHostUVE->m_chain->cursorEnter != nullptr) { g_activeHostUVE->m_chain->cursorEnter(w, entered); }
            g_activeHostUVE->OnCursorEnterUVE(entered);
        }
    }
};

// ------------------------------------------------------------------------------------------ host

EditorPrismHostUVE::EditorPrismHostUVE() = default;

EditorPrismHostUVE::~EditorPrismHostUVE() {
    ShutdownUVE();
}

bool EditorPrismHostUVE::InitializeUVE(GLFWwindow* const window, const std::filesystem::path& assetDir) {
    if (m_ready || window == nullptr || g_activeHostUVE != nullptr) {
        return false;
    }
    m_window = window;
    glfwGetFramebufferSize(window, &m_fbWidth, &m_fbHeight);
    m_fbWidth = std::max(1, m_fbWidth);
    m_fbHeight = std::max(1, m_fbHeight);

    m_platform = std::make_shared<PlatformUVE>(window);
    m_app = std::make_unique<prism::Application>();
    prism::HostDesc desc{};
    desc.width = m_fbWidth;
    desc.height = m_fbHeight;
    desc.assetDir = assetDir.string();
    if (!m_app->initHosted(desc, m_platform)) {
        m_app.reset();
        return false;
    }

    std::string error;
    m_backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
    if (m_backend == nullptr) {
        std::fprintf(stderr, "PRISM: OpenGL backend unavailable: %s\n", error.c_str());
        m_app.reset();
        return false;
    }
    m_app->setRenderBackend(m_backend);

    m_compositor = std::make_unique<CompositorUVE>();
    if (!m_compositor->InitializeUVE()) {
        std::fprintf(stderr, "PRISM: compositor shaders unavailable\n");
        m_compositor.reset();
        m_backend.reset();
        m_app.reset();
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

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    m_mouse = ToUiUVE(x, y);
    m_frameStartMouse = m_mouse;
    m_lastFrameTime = glfwGetTime();
    m_ready = true;
    return true;
}

void EditorPrismHostUVE::ShutdownUVE() {
    if (!m_ready) {
        return;
    }
    if (m_window != nullptr && m_chain != nullptr) {
        glfwSetCursorPosCallback(m_window, m_chain->cursorPos);
        glfwSetMouseButtonCallback(m_window, m_chain->mouseButton);
        glfwSetScrollCallback(m_window, m_chain->scroll);
        glfwSetKeyCallback(m_window, m_chain->key);
        glfwSetCharCallback(m_window, m_chain->character);
        glfwSetCursorEnterCallback(m_window, m_chain->cursorEnter);
    }
    if (g_activeHostUVE == this) {
        g_activeHostUVE = nullptr;
    }
    m_chain.reset();
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
    m_ready = false;
}

void EditorPrismHostUVE::SetRootUVE(prism::Ref<prism::Widget> root) {
    if (m_app != nullptr) {
        m_app->setRoot(std::move(root));
    }
}

prism::Vec2 EditorPrismHostUVE::ToUiUVE(const double x, const double y) const {
    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(m_window, &winW, &winH);
    const float sx = winW > 0 ? static_cast<float>(m_fbWidth) / static_cast<float>(winW) : 1.0F;
    const float sy = winH > 0 ? static_cast<float>(m_fbHeight) / static_cast<float>(winH) : 1.0F;
    return prism::Vec2{static_cast<float>(x) * sx, static_cast<float>(y) * sy};
}

void EditorPrismHostUVE::SyncSizeUVE() {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    if (w <= 0 || h <= 0) {
        return; // minimized
    }
    if (w != m_fbWidth || h != m_fbHeight) {
        m_fbWidth = w;
        m_fbHeight = h;
        m_app->hostResize(w, h);
        if (m_backend != nullptr) {
            m_backend->resize(w, h);
        }
    }
}

void EditorPrismHostUVE::BeginFrameUVE() {
    if (!m_ready) {
        return;
    }
    const double now = glfwGetTime();
    m_deltaTime = static_cast<float>(std::clamp(now - m_lastFrameTime, 1.0 / 1000.0, 0.25));
    m_lastFrameTime = now;
    SyncSizeUVE();
    static_cast<void>(m_app->tick());
}

EditorViewportInputUVE EditorPrismHostUVE::CaptureViewInputUVE(const prism::Rect& rect) const {
    EditorViewportInputUVE input{};
    const bool inside = m_mouseInside && m_mouse.x >= rect.x && m_mouse.y >= rect.y &&
                        m_mouse.x < rect.x + rect.w && m_mouse.y < rect.y + rect.h;
    input.hovered = inside && m_dragMode == DragModeUVE::None && !m_app->hasPopup() &&
                    !m_app->wantsPointerAt(m_mouse);
    input.imageOrigin = Math::Vector2UVE{rect.x, rect.y};
    input.mousePos = Math::Vector2UVE{m_mouse.x, m_mouse.y};
    input.mouseDelta = Math::Vector2UVE{m_mouse.x - m_frameStartMouse.x, m_mouse.y - m_frameStartMouse.y};
    input.wheel = m_wheel;
    input.deltaTime = m_deltaTime;
    input.shift = (m_mods & prism::ModShift) != 0;
    input.ctrl = (m_mods & prism::ModCtrl) != 0;
    input.alt = (m_mods & prism::ModAlt) != 0;
    input.wantTextInput = m_app->wantsKeyboard() || m_platform->TextInputActiveUVE();
    for (std::size_t i = 0; i < EditorViewportInputUVE::kButtonCountUVE; ++i) {
        input.down[i] = m_down[i];
        input.clicked[i] = m_clicked[i];
        input.released[i] = m_released[i];
        input.pressPos[i] = Math::Vector2UVE{m_pressPos[i].x, m_pressPos[i].y};
        input.dragDistance[i] = m_dragDistance[i];
    }
    if (!input.wantTextInput) {
        input.keysDown = m_keysDown;
        input.keysPressed = m_keysPressed;
    }
    return input;
}

void EditorPrismHostUVE::ApplyViewCursorUVE(const EditorViewportInputUVE& input) {
    if (!m_ready || m_dragMode != DragModeUVE::None) {
        return;
    }
    if (input.hovered && input.cursorRequested) {
        m_platform->SetViewCursorUVE(input.requestedCursor);
    }
}

void EditorPrismHostUVE::RenderUVE(const std::span<const ViewImageUVE> views) {
    if (!m_ready) {
        return;
    }
    // UI into the backend's own texture.
    static_cast<void>(m_app->render());

    // Composite into the default framebuffer.
    CompositorUVE& c = *m_compositor;
    c.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_fbWidth, m_fbHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    c.glUseProgram(c.program);
    c.glBindVertexArray(c.vao);
    c.glActiveTexture(GL_TEXTURE0);
    c.glUniform1i(c.texLoc, 0);
    glDisable(GL_BLEND);
    for (const ViewImageUVE& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{view.rect.x, view.rect.y, std::min(view.rect.w, view.usedWidth),
                                std::min(view.rect.h, view.usedHeight)};
        c.DrawUVE(view.texture, shown, m_fbWidth, m_fbHeight, u1, v1);
    }
    glEnable(GL_BLEND);
    c.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    c.DrawUVE(m_backend->colorTexture(), prism::Rect{0.0F, 0.0F, static_cast<float>(m_fbWidth),
                                                     static_cast<float>(m_fbHeight)},
              m_fbWidth, m_fbHeight, 1.0F, 1.0F);
    glDisable(GL_BLEND);
    c.glBindVertexArray(0);
    c.glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Edges are per frame.
    m_clicked.fill(false);
    m_released.fill(false);
    m_keysPressed.reset();
    m_wheel = 0.0F;
    m_frameStartMouse = m_mouse;
}

bool EditorPrismHostUVE::QuitRequestedUVE() const noexcept {
    return m_window != nullptr && glfwWindowShouldClose(m_window) != 0;
}

bool EditorPrismHostUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    if (!m_ready) {
        return false;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(m_fbWidth) * static_cast<std::size_t>(m_fbHeight) * 4U);
    m_compositor->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_fbWidth, m_fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<std::uint32_t> argb(static_cast<std::size_t>(m_fbWidth) * static_cast<std::size_t>(m_fbHeight));
    for (int y = 0; y < m_fbHeight; ++y) {
        const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(m_fbHeight - 1 - y) * m_fbWidth * 4U;
        std::uint32_t* dst = argb.data() + static_cast<std::size_t>(y) * m_fbWidth;
        for (int x = 0; x < m_fbWidth; ++x) {
            const std::uint8_t* p = src + static_cast<std::size_t>(x) * 4U;
            dst[x] = 0xFF000000U | (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) |
                     static_cast<std::uint32_t>(p[2]);
        }
    }
    return prism::Canvas::writePng(path.string(), argb.data(), m_fbWidth, m_fbHeight, m_fbWidth);
}

// ----------------------------------------------------------------------------------------- input

void EditorPrismHostUVE::OnCursorPosUVE(const double x, const double y) {
    m_mouse = ToUiUVE(x, y);
    m_mouseInside = true;
    for (std::size_t i = 0; i < m_down.size(); ++i) {
        if (m_down[i]) {
            const float dx = m_mouse.x - m_pressPos[i].x;
            const float dy = m_mouse.y - m_pressPos[i].y;
            m_dragDistance[i] = std::max(m_dragDistance[i], std::sqrt((dx * dx) + (dy * dy)));
        }
    }
    if (m_dragMode != DragModeUVE::None) {
        UpdateWindowDragUVE();
        return;
    }
    // Edge-resize affordance on an undecorated, restored window.
    int edges = 0;
    if (glfwGetWindowAttrib(m_window, GLFW_DECORATED) == 0 && glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) == 0) {
        const float w = static_cast<float>(m_fbWidth);
        const float h = static_cast<float>(m_fbHeight);
        if (m_mouse.x < kResizeBorderUVE) { edges |= 1; }
        if (m_mouse.x >= w - kResizeBorderUVE) { edges |= 2; }
        if (m_mouse.y < kResizeBorderUVE) { edges |= 4; }
        if (m_mouse.y >= h - kResizeBorderUVE) { edges |= 8; }
    }
    m_app->hostMouseMove(m_mouse);
    if (edges != 0) {
        m_platform->SetResizeCursorUVE(edges);
    }
}

void EditorPrismHostUVE::OnMouseButtonUVE(const int button, const int action, const int mods) {
    m_mods = ToPrismModsUVE(mods);
    m_app->hostSetModifiers(m_mods);
    const int slot = ToViewButtonUVE(button);
    const prism::MouseButton prismButton = ToPrismButtonUVE(button);
    if (action == GLFW_PRESS) {
        const double now = glfwGetTime();
        const float dx = m_mouse.x - m_lastClickPos.x;
        const float dy = m_mouse.y - m_lastClickPos.y;
        if (now - m_lastClickTime <= kDoubleClickSecondsUVE && std::abs(dx) <= kDoubleClickSlopUVE &&
            std::abs(dy) <= kDoubleClickSlopUVE) {
            ++m_clickCount;
        } else {
            m_clickCount = 1;
        }
        m_lastClickTime = now;
        m_lastClickPos = m_mouse;

        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            m_down[i] = true;
            m_clicked[i] = true;
            m_pressPos[i] = m_mouse;
            m_dragDistance[i] = 0.0F;
        }

        // Window chrome: edge resize, or a move from the title strip (single click only; a
        // double click toggles maximize inside PRISM's title bar).
        if (button == GLFW_MOUSE_BUTTON_LEFT && glfwGetWindowAttrib(m_window, GLFW_DECORATED) == 0) {
            const bool maximized = glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) != 0;
            int edges = 0;
            if (!maximized) {
                const float w = static_cast<float>(m_fbWidth);
                const float h = static_cast<float>(m_fbHeight);
                if (m_mouse.x < kResizeBorderUVE) { edges |= 1; }
                if (m_mouse.x >= w - kResizeBorderUVE) { edges |= 2; }
                if (m_mouse.y < kResizeBorderUVE) { edges |= 4; }
                if (m_mouse.y >= h - kResizeBorderUVE) { edges |= 8; }
            }
            const prism::Rect& cap = m_platform->CaptionUVE();
            const bool inCaption = m_mouse.x >= cap.x && m_mouse.y >= cap.y && m_mouse.x < cap.x + cap.w &&
                                   m_mouse.y < cap.y + cap.h;
            if (edges != 0 || (inCaption && m_clickCount == 1 && !maximized)) {
                double cx = 0.0;
                double cy = 0.0;
                glfwGetCursorPos(m_window, &cx, &cy);
                glfwGetWindowPos(m_window, &m_dragStartWinX, &m_dragStartWinY);
                glfwGetWindowSize(m_window, &m_dragStartWinW, &m_dragStartWinH);
                m_dragStartScreenX = m_dragStartWinX + cx;
                m_dragStartScreenY = m_dragStartWinY + cy;
                m_dragMode = edges != 0 ? DragModeUVE::Resize : DragModeUVE::Move;
                m_resizeEdges = edges;
                if (edges != 0) {
                    return; // the resize ring belongs to the OS frame, not to the UI below it
                }
            }
        }
        if (prismButton != prism::MouseButton::None) {
            m_app->hostMouseButton(m_mouse, prismButton, true, m_clickCount);
        }
    } else if (action == GLFW_RELEASE) {
        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            m_down[i] = false;
            m_released[i] = true;
        }
        const bool wasResize = m_dragMode == DragModeUVE::Resize;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            m_dragMode = DragModeUVE::None;
        }
        if (!wasResize && prismButton != prism::MouseButton::None) {
            m_app->hostMouseButton(m_mouse, prismButton, false, m_clickCount);
        }
    }
}

void EditorPrismHostUVE::UpdateWindowDragUVE() {
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(m_window, &cx, &cy);
    int winX = 0;
    int winY = 0;
    glfwGetWindowPos(m_window, &winX, &winY);
    const double screenX = winX + cx;
    const double screenY = winY + cy;
    const int dx = static_cast<int>(std::lround(screenX - m_dragStartScreenX));
    const int dy = static_cast<int>(std::lround(screenY - m_dragStartScreenY));
    if (m_dragMode == DragModeUVE::Move) {
        glfwSetWindowPos(m_window, m_dragStartWinX + dx, m_dragStartWinY + dy);
        return;
    }
    int x = m_dragStartWinX;
    int y = m_dragStartWinY;
    int w = m_dragStartWinW;
    int h = m_dragStartWinH;
    constexpr int kMinW = 640;
    constexpr int kMinH = 400;
    if ((m_resizeEdges & 1) != 0) { const int nw = std::max(kMinW, w - dx); x += w - nw; w = nw; }
    if ((m_resizeEdges & 2) != 0) { w = std::max(kMinW, w + dx); }
    if ((m_resizeEdges & 4) != 0) { const int nh = std::max(kMinH, h - dy); y += h - nh; h = nh; }
    if ((m_resizeEdges & 8) != 0) { h = std::max(kMinH, h + dy); }
    glfwSetWindowPos(m_window, x, y);
    glfwSetWindowSize(m_window, w, h);
}

void EditorPrismHostUVE::OnScrollUVE(const double dx, const double dy) {
    m_wheel += static_cast<float>(dy);
    m_app->hostMouseWheel(m_mouse, prism::Vec2{static_cast<float>(dx), static_cast<float>(dy)});
}

void EditorPrismHostUVE::OnKeyUVE(const int key, const int /*scancode*/, const int action, const int mods) {
    m_mods = ToPrismModsUVE(mods);
    m_app->hostSetModifiers(m_mods);
    const ViewKeyUVE viewKey = ToViewKeyUVE(key);
    if (viewKey != ViewKeyUVE::Count) {
        const auto index = static_cast<std::size_t>(viewKey);
        if (action == GLFW_PRESS) {
            m_keysDown.set(index);
            m_keysPressed.set(index);
        } else if (action == GLFW_RELEASE) {
            m_keysDown.reset(index);
        }
    }
    const prism::Key prismKey = ToPrismKeyUVE(key);
    if (prismKey == prism::Key::Unknown) {
        return;
    }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        m_app->hostKey(prismKey, m_mods, true, action == GLFW_REPEAT);
    } else if (action == GLFW_RELEASE) {
        m_app->hostKey(prismKey, m_mods, false, false);
    }
}

void EditorPrismHostUVE::OnCharUVE(const unsigned int codepoint) {
    std::string utf8;
    AppendUtf8UVE(utf8, static_cast<char32_t>(codepoint));
    m_app->hostText(utf8);
}

void EditorPrismHostUVE::OnCursorEnterUVE(const int entered) {
    m_mouseInside = entered != 0;
    if (!m_mouseInside && m_dragMode == DragModeUVE::None) {
        m_app->hostMouseLeave();
    }
}

} // namespace UVE::Editor
