// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_host.h"
#include "android_editor_bridge.h"

#include "prism/Canvas.h"
#include "prism/GlBackend.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

EditorHostUVE* g_activeAndroidHostUVE = nullptr;
AndroidEditorCallbacksUVE g_androidCallbacksUVE{};

using ViewKeyUVE = EditorViewportInputUVE::KeyUVE;
using ViewCursorUVE = EditorViewportInputUVE::CursorUVE;

constexpr double kDoubleTapSecondsUVE = 0.4;
constexpr float kDoubleTapSlopUVE = 24.0F;
constexpr double kLongPressSecondsUVE = 0.48;
constexpr float kTouchSlopUVE = 14.0F;
constexpr float kTwoFingerTapSlopUVE = 22.0F;
constexpr double kTwoFingerTapSecondsUVE = 0.28;

[[nodiscard]] double NowSecondsUVE() noexcept {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

[[nodiscard]] void* GlLoaderUVE(const char* const name) {
    if (name == nullptr) {
        return nullptr;
    }
    if (void* const sym = reinterpret_cast<void*>(eglGetProcAddress(name)); sym != nullptr) {
        return sym;
    }
    return dlsym(RTLD_DEFAULT, name);
}

[[nodiscard]] prism::Key ToPrismKeyUVE(const std::int32_t keyCode) noexcept {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::A) + (keyCode - AKEYCODE_A));
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::N0) + (keyCode - AKEYCODE_0));
    }
    if (keyCode >= AKEYCODE_NUMPAD_0 && keyCode <= AKEYCODE_NUMPAD_9) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::N0) + (keyCode - AKEYCODE_NUMPAD_0));
    }
    if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) {
        return static_cast<prism::Key>(static_cast<int>(prism::Key::F1) + (keyCode - AKEYCODE_F1));
    }
    switch (keyCode) {
        case AKEYCODE_DEL: return prism::Key::Backspace;
        case AKEYCODE_TAB: return prism::Key::Tab;
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: return prism::Key::Enter;
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BACK: return prism::Key::Escape;
        case AKEYCODE_SPACE: return prism::Key::Space;
        case AKEYCODE_DPAD_LEFT: return prism::Key::Left;
        case AKEYCODE_DPAD_RIGHT: return prism::Key::Right;
        case AKEYCODE_DPAD_UP: return prism::Key::Up;
        case AKEYCODE_DPAD_DOWN: return prism::Key::Down;
        case AKEYCODE_MOVE_HOME: return prism::Key::Home;
        case AKEYCODE_MOVE_END: return prism::Key::End;
        case AKEYCODE_PAGE_UP: return prism::Key::PageUp;
        case AKEYCODE_PAGE_DOWN: return prism::Key::PageDown;
        case AKEYCODE_FORWARD_DEL: return prism::Key::Delete;
        default: return prism::Key::Unknown;
    }
}

[[nodiscard]] ViewKeyUVE ToViewKeyUVE(const std::int32_t keyCode) noexcept {
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return static_cast<ViewKeyUVE>(static_cast<int>(ViewKeyUVE::Num0) + (keyCode - AKEYCODE_0));
    }
    switch (keyCode) {
        case AKEYCODE_NUMPAD_1: return ViewKeyUVE::Keypad1;
        case AKEYCODE_NUMPAD_3: return ViewKeyUVE::Keypad3;
        case AKEYCODE_NUMPAD_5: return ViewKeyUVE::Keypad5;
        case AKEYCODE_NUMPAD_7: return ViewKeyUVE::Keypad7;
        case AKEYCODE_A: return ViewKeyUVE::A;
        case AKEYCODE_C: return ViewKeyUVE::C;
        case AKEYCODE_D: return ViewKeyUVE::D;
        case AKEYCODE_F: return ViewKeyUVE::F;
        case AKEYCODE_S: return ViewKeyUVE::S;
        case AKEYCODE_W: return ViewKeyUVE::W;
        case AKEYCODE_SPACE: return ViewKeyUVE::Space;
        case AKEYCODE_MOVE_HOME: return ViewKeyUVE::Home;
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BACK: return ViewKeyUVE::Escape;
        case AKEYCODE_SHIFT_LEFT:
        case AKEYCODE_SHIFT_RIGHT: return ViewKeyUVE::LeftShift;
        case AKEYCODE_CTRL_LEFT:
        case AKEYCODE_CTRL_RIGHT: return ViewKeyUVE::LeftCtrl;
        default: return ViewKeyUVE::Count;
    }
}

[[nodiscard]] std::uint8_t ToPrismModsUVE(const std::int32_t metaState) noexcept {
    std::uint8_t out = prism::ModNone;
    if ((metaState & AMETA_SHIFT_ON) != 0) {
        out |= prism::ModShift;
    }
    if ((metaState & AMETA_CTRL_ON) != 0) {
        out |= prism::ModCtrl;
    }
    if ((metaState & AMETA_ALT_ON) != 0) {
        out |= prism::ModAlt;
    }
    return out;
}

[[nodiscard]] int ToViewButtonUVE(const prism::MouseButton button) noexcept {
    switch (button) {
        case prism::MouseButton::Left: return 0;
        case prism::MouseButton::Right: return 1;
        case prism::MouseButton::Middle: return 2;
        case prism::MouseButton::None: break;
    }
    return -1;
}

void AppendUtf8UVE(std::string& out, const char32_t cp) {
    if (cp < 0x80U) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

[[nodiscard]] bool WriteFramebufferPngUVE(const std::filesystem::path& path, const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    std::vector<std::uint8_t> rgba(w * h * 4U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<std::uint32_t> argb(w * h);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = rgba.data() + static_cast<std::size_t>(height - 1 - y) * w * 4U;
        std::uint32_t* dst = argb.data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* p = src + static_cast<std::size_t>(x) * 4U;
            dst[x] = 0xFF000000U | (static_cast<std::uint32_t>(p[0]) << 16U) |
                     (static_cast<std::uint32_t>(p[1]) << 8U) | static_cast<std::uint32_t>(p[2]);
        }
    }
    return prism::Canvas::writePng(path.string(), argb.data(), width, height, width);
}

} // namespace

// ---------------------------------------------------------------------------------------- window

struct EditorHostUVE::WindowUVE final {
    EditorHostUVE* host = nullptr;
    int id = 0;
    ANativeWindow* nativeWindow = nullptr;
    int fbWidth = 1;
    int fbHeight = 1;
    std::map<int, prism::Rect> captions;
    bool closeRequested = false;

    std::shared_ptr<prism::GlBackend> backend;
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

    double lastClickTime = -1.0;
    prism::Vec2 lastClickPos{0.0F, 0.0F};
    int clickCount = 0;
};

// ------------------------------------------------------------------------------------- platform

class EditorHostUVE::PlatformUVE final : public prism::Platform {
public:
    explicit PlatformUVE(EditorHostUVE& host) : m_host(host) {}

    std::uint64_t nowMs() const override {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }
    std::string clipboard() const override { return m_clipboard; }
    void setClipboard(const std::string& text) override { m_clipboard = text; }
    void setCursor(const prism::Cursor /*c*/) override {}
    void setTextInputActive(const bool active) override {
        if (m_textInput != active) {
            m_textInput = active;
            if (g_androidCallbacksUVE.setTextInputActive) {
                g_androidCallbacksUVE.setTextInputActive(active);
            }
        }
    }
    float dpiScale() const override { return 1.0F; }
    void requestQuit() override {
        m_quitRequested = true;
        if (g_androidCallbacksUVE.requestQuit) {
            g_androidCallbacksUVE.requestQuit();
        }
    }
    void* nativeWindowHandle() const override { return m_nativeWindow; }
    void setWindowCaption(const int id, const prism::Rect dragArea) override {
        const int window = id < kExtraCaptionIdUVE ? id : (id - kExtraCaptionIdUVE) / 100;
        if (WindowUVE* w = m_host.FindUVE(window)) {
            w->captions[id] = dragArea;
        }
    }
    void minimizeWindow(const int /*id*/) override {}
    void toggleMaximizeWindow(const int /*id*/) override {}
    bool windowMaximized(const int /*id*/) const override { return true; }

    void SetNativeWindowUVE(ANativeWindow* const window) noexcept { m_nativeWindow = window; }
    [[nodiscard]] bool TextInputActiveUVE() const noexcept { return m_textInput; }
    [[nodiscard]] bool QuitRequestedUVE() const noexcept { return m_quitRequested; }

private:
    EditorHostUVE& m_host;
    ANativeWindow* m_nativeWindow = nullptr;
    std::string m_clipboard;
    bool m_textInput = false;
    bool m_quitRequested = false;
};

// ------------------------------------------------------------------------------------ compositor

struct EditorHostUVE::CompositorUVE final {
    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
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
    bool LoadUVE(T& fn, const char* const name) {
        fn = reinterpret_cast<T>(GlLoaderUVE(name));
        if (fn == nullptr) {
            __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "Compositor missing GLES3 symbol: %s", name);
        }
        return fn != nullptr;
    }

    [[nodiscard]] bool InitializeUVE() {
        bool ok = true;
        ok &= LoadUVE(glCreateShader, "glCreateShader");
        ok &= LoadUVE(glShaderSource, "glShaderSource");
        ok &= LoadUVE(glCompileShader, "glCompileShader");
        ok &= LoadUVE(glGetShaderiv, "glGetShaderiv");
        ok &= LoadUVE(glGetShaderInfoLog, "glGetShaderInfoLog");
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
        static const char* const kVertex = R"(#version 300 es
precision highp float;
uniform vec4 uRect;
uniform vec4 uUv;
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), 0.0, 1.0);
    vUv = mix(uUv.xy, uUv.zw, corner);
})";
        static const char* const kFragment = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); })";
        const GLuint vs = CompileUVE(GL_VERTEX_SHADER, kVertex);
        const GLuint fs = CompileUVE(GL_FRAGMENT_SHADER, kFragment);
        if (vs == 0 || fs == 0) {
            if (vs != 0) {
                glDeleteShader(vs);
            }
            if (fs != 0) {
                glDeleteShader(fs);
            }
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

    [[nodiscard]] GLuint CompileUVE(const GLenum type, const char* const source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == 0) {
            char log[512]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "Compositor shader compile failed: %s", log);
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

    void DrawUVE(const GLuint texture, const prism::Rect& r, const int w, const int h, const float u1,
                 const float v1) {
        const float fw = static_cast<float>(std::max(1, w));
        const float fh = static_cast<float>(std::max(1, h));
        const float x0 = (r.x / fw) * 2.0F - 1.0F;
        const float x1 = ((r.x + r.w) / fw) * 2.0F - 1.0F;
        const float yTop = 1.0F - (r.y / fh) * 2.0F;
        const float yBottom = 1.0F - ((r.y + r.h) / fh) * 2.0F;
        glUniform4f(rectLoc, x0, yBottom, x1, yTop);
        glUniform4f(uvLoc, 0.0F, 0.0F, u1, v1);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
};

// ----------------------------------------------------------------------------- touch/key state

struct AndroidEditorHostBridgeUVE final {
    struct InputStateUVE final {
        bool surfaceAttached = true;
        std::vector<AndroidTouchContactUVE> contacts;
        std::int32_t primaryPointerId = -1;
        prism::MouseButton primaryButton = prism::MouseButton::Left;

        bool longPressPending = false;
        bool longPressFired = false;
        double longPressStartTime = 0.0;
        prism::Vec2 longPressStartPos{};
        float longPressMaxTravel = 0.0F;

        bool twoFingerActive = false;
        double twoFingerStartTime = 0.0;
        prism::Vec2 twoFingerCentroidStart{};
        prism::Vec2 twoFingerPrevCentroid{};
        float twoFingerPrevSpan = 0.0F;
        float twoFingerMaxTravel = 0.0F;
    };

    static InputStateUVE s_state;

    [[nodiscard]] static EditorHostUVE::WindowUVE* ActiveTargetWindowUVE(EditorHostUVE& host) {
        if (!host.m_ready || host.m_app == nullptr) {
            return nullptr;
        }
        const int activeId = host.m_app->activeWindow();
        if (EditorHostUVE::WindowUVE* found = host.FindUVE(activeId)) {
            return found;
        }
        return host.m_main.get();
    }

    template <typename F>
    static void RunScopedUVE(EditorHostUVE::WindowUVE& w, F&& fn) {
        if (w.scope) {
            w.scope(std::function<void()>(std::forward<F>(fn)));
        } else {
            fn();
        }
    }

    [[nodiscard]] static const AndroidTouchContactUVE* FindContactUVE(
        const std::vector<AndroidTouchContactUVE>& list, const std::int32_t pointerId) noexcept {
        for (const AndroidTouchContactUVE& c : list) {
            if (c.pointerId == pointerId) {
                return &c;
            }
        }
        return nullptr;
    }

    static void PressMouseButtonUVE(EditorHostUVE& host, EditorHostUVE::WindowUVE& w,
                                    const prism::MouseButton button, const prism::Vec2 pos) {
        w.mouse = pos;
        w.mouseInside = true;
        host.m_app->hostSetModifiers(w.mods);
        host.m_app->windowMouseMove(w.id, w.mouse);

        const double now = NowSecondsUVE();
        const float dx = w.mouse.x - w.lastClickPos.x;
        const float dy = w.mouse.y - w.lastClickPos.y;
        if (now - w.lastClickTime <= kDoubleTapSecondsUVE && std::abs(dx) <= kDoubleTapSlopUVE &&
            std::abs(dy) <= kDoubleTapSlopUVE) {
            ++w.clickCount;
        } else {
            w.clickCount = 1;
        }
        w.lastClickTime = now;
        w.lastClickPos = w.mouse;

        const int slot = ToViewButtonUVE(button);
        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            w.down[i] = true;
            w.clicked[i] = true;
            w.pressPos[i] = w.mouse;
            w.dragDistance[i] = 0.0F;
        }
        if (button != prism::MouseButton::None) {
            host.m_app->windowMouseButton(w.id, w.mouse, button, true, w.clickCount);
        }
    }

    static void ReleaseMouseButtonUVE(EditorHostUVE& host, EditorHostUVE::WindowUVE& w,
                                      const prism::MouseButton button, const prism::Vec2 pos,
                                      const bool notifyApp = true) {
        w.mouse = pos;
        const int slot = ToViewButtonUVE(button);
        if (slot >= 0) {
            const auto i = static_cast<std::size_t>(slot);
            w.down[i] = false;
            w.released[i] = true;
        }
        if (notifyApp && button != prism::MouseButton::None) {
            host.m_app->windowMouseButton(w.id, w.mouse, button, false, w.clickCount);
        }
    }

    static void UpdateMoveDragDistancesUVE(EditorHostUVE::WindowUVE& w) {
        for (std::size_t i = 0; i < w.down.size(); ++i) {
            if (w.down[i]) {
                const float dx = w.mouse.x - w.pressPos[i].x;
                const float dy = w.mouse.y - w.pressPos[i].y;
                w.dragDistance[i] = std::max(w.dragDistance[i], std::sqrt((dx * dx) + (dy * dy)));
            }
        }
    }

    static void ResetGestureStateUVE() noexcept {
        s_state.contacts.clear();
        s_state.primaryPointerId = -1;
        s_state.primaryButton = prism::MouseButton::Left;
        s_state.longPressPending = false;
        s_state.longPressFired = false;
        s_state.twoFingerActive = false;
    }

    static void PollLongPressUVE(EditorHostUVE& host) {
        if (!s_state.longPressPending || s_state.longPressFired || s_state.contacts.size() != 1U ||
            s_state.primaryButton != prism::MouseButton::Left) {
            return;
        }
        const double now = NowSecondsUVE();
        if (now - s_state.longPressStartTime < kLongPressSecondsUVE || s_state.longPressMaxTravel > kTouchSlopUVE) {
            return;
        }
        EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
        if (target == nullptr) {
            return;
        }
        if (host.m_app != nullptr && host.m_app->isDragging()) {
            s_state.longPressPending = false;
            return;
        }
        s_state.longPressFired = true;
        s_state.longPressPending = false;
        RunScopedUVE(*target, [&host, target] {
            const prism::Vec2 at = s_state.longPressStartPos;
            // Cancel the tentative left press without clicking a scene action, then raise Right click.
            ReleaseMouseButtonUVE(host, *target, prism::MouseButton::Left, at, false);
            host.m_app->windowMouseButton(target->id, prism::Vec2{-10000.0F, -10000.0F},
                                          prism::MouseButton::Left, false, 1);
            target->clickCount = 1;
            PressMouseButtonUVE(host, *target, prism::MouseButton::Right, at);
            ReleaseMouseButtonUVE(host, *target, prism::MouseButton::Right, at, true);
        });
    }

    static void CancelAllInputUVE(EditorHostUVE& host) {
        EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
        if (target != nullptr && host.m_app != nullptr) {
            RunScopedUVE(*target, [&host, target] {
                for (std::size_t i = 0; i < target->down.size(); ++i) {
                    if (target->down[i]) {
                        const prism::MouseButton btn = i == 0 ? prism::MouseButton::Left
                                                       : i == 1 ? prism::MouseButton::Right
                                                                : prism::MouseButton::Middle;
                        ReleaseMouseButtonUVE(host, *target, btn, target->mouse, true);
                    }
                }
                host.m_app->cancelDragDrop();
                target->keysDown.reset();
                target->keysPressed.reset();
                host.m_app->windowMouseLeave(target->id);
            });
        }
        ResetGestureStateUVE();
    }

    static void SyncAllSizesUVE(EditorHostUVE& host, const int width, const int height) {
        if (!host.m_ready || host.m_app == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const auto apply = [&host, width, height](EditorHostUVE::WindowUVE& w) {
            if (w.fbWidth == width && w.fbHeight == height) {
                return;
            }
            w.fbWidth = width;
            w.fbHeight = height;
            host.m_app->windowResize(w.id, width, height);
            if (w.id == EditorHostUVE::kMainWindowUVE) {
                if (host.m_backend != nullptr) {
                    host.m_backend->resize(width, height);
                }
            } else if (w.backend != nullptr) {
                w.backend->resize(width, height);
            }
        };
        if (host.m_main != nullptr) {
            apply(*host.m_main);
        }
        for (const auto& [id, win] : host.m_windows) {
            if (win != nullptr) {
                apply(*win);
            }
        }
    }

    static void NotifyAttachedUVE(void* nativeWindow, int width, int height);
    static void NotifyDetachedUVE();
    static void NotifyResizedUVE(int width, int height);
    [[nodiscard]] static bool DispatchTouchUVE(AndroidTouchActionUVE action,
                                               std::int32_t actionIndex,
                                               std::span<const AndroidTouchContactUVE> contacts,
                                               std::int32_t buttonState,
                                               float scrollX,
                                               float scrollY);
    [[nodiscard]] static bool DispatchKeyUVE(std::int32_t keyCode,
                                             std::int32_t action,
                                             std::int32_t metaState,
                                             std::uint32_t unicodeChar,
                                             bool repeat);
    static void DispatchTextUVE(std::string_view utf8Text);
};

AndroidEditorHostBridgeUVE::InputStateUVE AndroidEditorHostBridgeUVE::s_state{};

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

bool EditorHostUVE::InitializeUVE(void* const windowHandle, const std::filesystem::path& assetDir) {
    auto* const window = static_cast<ANativeWindow*>(windowHandle);
    if (m_ready || window == nullptr || g_activeAndroidHostUVE != nullptr) {
        return false;
    }
    m_main = std::make_unique<WindowUVE>();
    m_main->host = this;
    m_main->id = kMainWindowUVE;
    m_main->nativeWindow = window;
    m_main->fbWidth = std::max(1, ANativeWindow_getWidth(window));
    m_main->fbHeight = std::max(1, ANativeWindow_getHeight(window));

    m_platform = std::make_shared<PlatformUVE>(*this);
    m_platform->SetNativeWindowUVE(window);
    m_app = std::make_unique<prism::Application>();
    prism::HostDesc desc{};
    desc.width = m_main->fbWidth;
    desc.height = m_main->fbHeight;
    desc.assetDir = assetDir.string();
    if (!m_app->initHosted(desc, m_platform)) {
        m_app.reset();
        m_main.reset();
        m_platform.reset();
        return false;
    }

    std::string error;
    m_backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
    if (m_backend == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "PRISM GLES3 backend unavailable: %s", error.c_str());
        m_app.reset();
        m_main.reset();
        m_platform.reset();
        return false;
    }
    m_backend->resize(m_main->fbWidth, m_main->fbHeight);
    m_app->setRenderBackend(m_backend);

    m_compositor = std::make_unique<CompositorUVE>();
    if (!m_compositor->InitializeUVE()) {
        __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "PRISM compositor shaders unavailable");
        m_compositor.reset();
        m_backend.reset();
        m_app.reset();
        m_main.reset();
        m_platform.reset();
        return false;
    }

    g_activeAndroidHostUVE = this;
    AndroidEditorHostBridgeUVE::s_state = {};
    AndroidEditorHostBridgeUVE::s_state.surfaceAttached = true;

    m_main->mouse = prism::Vec2{static_cast<float>(m_main->fbWidth) * 0.5F,
                                static_cast<float>(m_main->fbHeight) * 0.5F};
    m_main->frameStartMouse = m_main->mouse;
    m_main->lastFrameTime = NowSecondsUVE();
    m_ready = true;
    return true;
}

void EditorHostUVE::ShutdownUVE() {
    if (!m_ready) {
        return;
    }
    AndroidEditorHostBridgeUVE::ResetGestureStateUVE();
    while (!m_windows.empty()) {
        CloseWindowUVE(m_windows.begin()->first);
    }
    if (g_activeAndroidHostUVE == this) {
        g_activeAndroidHostUVE = nullptr;
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

int EditorHostUVE::OpenWindowUVE(prism::Ref<prism::Widget> root, const std::string& title, const int /*width*/,
                                 const int /*height*/) {
    if (!m_ready || root == nullptr || m_main == nullptr || m_app == nullptr) {
        return 0;
    }
    auto w = std::make_unique<WindowUVE>();
    w->host = this;
    w->fbWidth = std::max(1, m_main->fbWidth);
    w->fbHeight = std::max(1, m_main->fbHeight);
    std::string error;
    w->backend = prism::GlBackend::create(&GlLoaderUVE, {}, /*blitToScreen*/ false, &error);
    if (w->backend == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor",
                            "Secondary virtual window GLES3 backend failed: %s", error.c_str());
        return 0;
    }
    w->backend->resize(w->fbWidth, w->fbHeight);

    prism::OsWindowDesc desc{};
    desc.title = title;
    desc.width = w->fbWidth;
    desc.height = w->fbHeight;
    const int id = m_app->openWindow(std::move(root), desc);
    if (id == 0) {
        return 0;
    }
    w->id = id;
    w->mouse = m_main->mouse;
    w->frameStartMouse = w->mouse;
    w->lastFrameTime = NowSecondsUVE();
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
    if (m_app != nullptr) {
        m_app->closeWindow(id);
        m_app->invalidate();
    }
    w->backend.reset();
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
    if (w == nullptr || m_app == nullptr) {
        return;
    }
    m_app->activateWindow(id);
    m_app->invalidate();
    SyncSizeUVE(*w);
}

void EditorHostUVE::SyncSizeUVE(WindowUVE& w) {
    if (m_main == nullptr || m_app == nullptr) {
        return;
    }
    int width = m_main->fbWidth;
    int height = m_main->fbHeight;
    if (m_main->nativeWindow != nullptr && AndroidEditorHostBridgeUVE::s_state.surfaceAttached) {
        const int nw = ANativeWindow_getWidth(m_main->nativeWindow);
        const int nh = ANativeWindow_getHeight(m_main->nativeWindow);
        if (nw > 0 && nh > 0) {
            width = nw;
            height = nh;
        }
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    if (w.fbWidth != width || w.fbHeight != height) {
        w.fbWidth = width;
        w.fbHeight = height;
        m_app->windowResize(w.id, width, height);
        if (w.id == kMainWindowUVE) {
            if (m_backend != nullptr) {
                m_backend->resize(width, height);
            }
        } else if (w.backend != nullptr) {
            w.backend->resize(width, height);
        }
    }
}

void EditorHostUVE::BeginFrameUVE() {
    if (!m_ready || m_app == nullptr || m_main == nullptr) {
        return;
    }
    AndroidEditorHostBridgeUVE::PollLongPressUVE(*this);

    const double now = NowSecondsUVE();
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
    if (found == nullptr || m_app == nullptr || m_platform == nullptr) {
        return input;
    }
    const WindowUVE& w = *found;
    const bool inside = w.mouseInside && w.mouse.x >= rect.x && w.mouse.y >= rect.y && w.mouse.x < rect.x + rect.w &&
                        w.mouse.y < rect.y + rect.h;
    input.hovered = inside && !m_app->windowHasBlockingPopup(id) && !m_app->windowWantsPointerAt(id, w.mouse);
    input.imageOrigin = Math::Vector2UVE{rect.x, rect.y};
    input.mousePos = Math::Vector2UVE{w.mouse.x, w.mouse.y};
    input.mouseDelta = Math::Vector2UVE{w.mouse.x - w.frameStartMouse.x, w.mouse.y - w.frameStartMouse.y};
    input.wheel = w.wheel;
    input.deltaTime = w.deltaTime;
    input.shift = (w.mods & prism::ModShift) != 0U;
    input.ctrl = (w.mods & prism::ModCtrl) != 0U;
    input.alt = (w.mods & prism::ModAlt) != 0U;
    const bool windowHasKeys = m_app->activeWindow() == id;
    input.wantTextInput = !windowHasKeys || m_app->windowWantsKeyboard(id) || m_platform->TextInputActiveUVE();
    for (std::size_t i = 0; i < EditorViewportInputUVE::kButtonCountUVE; ++i) {
        input.down[i] = w.down[i];
        input.clicked[i] = w.clicked[i];
        input.released[i] = w.released[i];
        input.pressPos[i] = Math::Vector2UVE{w.pressPos[i].x, w.pressPos[i].y};
        input.dragDistance[i] = w.dragDistance[i];
    }
    if (!input.wantTextInput) {
        input.keysDown = w.keysDown;
        input.keysPressed = w.keysPressed;
    }
    return input;
}

void EditorHostUVE::ApplyViewCursorUVE(const int /*id*/, const EditorViewportInputUVE& /*input*/) {}

void EditorHostUVE::CompositeUVE(WindowUVE& w, const std::uint32_t uiTexture, const std::uint32_t vao,
                                 const std::span<const ViewImageUVE> views) {
    if (m_compositor == nullptr || !AndroidEditorHostBridgeUVE::s_state.surfaceAttached) {
        return;
    }
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
    for (const ViewImageUVE& view : views) {
        if (view.texture == 0U || view.rect.w <= 0.0F || view.rect.h <= 0.0F) {
            continue;
        }
        const float u1 = view.textureWidth > 0.0F ? view.usedWidth / view.textureWidth : 1.0F;
        const float v1 = view.textureHeight > 0.0F ? view.usedHeight / view.textureHeight : 1.0F;
        const prism::Rect shown{view.rect.x, view.rect.y, std::min(view.rect.w, view.usedWidth),
                                std::min(view.rect.h, view.usedHeight)};
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

void EditorHostUVE::EndInputFrameUVE(WindowUVE& w) {
    w.clicked.fill(false);
    w.released.fill(false);
    w.keysPressed.reset();
    w.wheel = 0.0F;
    w.frameStartMouse = w.mouse;
}

void EditorHostUVE::RenderUVE(const std::span<const ViewImageUVE> views) {
    if (!m_ready || m_main == nullptr || m_app == nullptr || m_backend == nullptr || m_compositor == nullptr) {
        return;
    }
    if (AndroidEditorHostBridgeUVE::s_state.surfaceAttached) {
        const auto now = std::chrono::steady_clock::now();
        if (m_app->needsRender() || m_app->paintGeneration() != m_mainPaintedGen ||
            now - m_mainPaintedAt > std::chrono::milliseconds(250)) {
            m_app->renderFrame();
            m_mainPaintedGen = m_app->paintGeneration();
            m_mainPaintedAt = now;
        }
        // Composite the main editor frame unconditionally so closing an active secondary window mid-frame
        // never leaves the backbuffer stale or blank; an active secondary window overlays cleanly right after.
        CompositeUVE(*m_main, m_backend->colorTexture(), m_compositor->vao, views);
    }
    EndInputFrameUVE(*m_main);
}

void EditorHostUVE::RenderWindowUVE(const int id, const std::span<const ViewImageUVE> views) {
    WindowUVE* found = id != kMainWindowUVE ? FindUVE(id) : nullptr;
    if (!m_ready || found == nullptr || m_app == nullptr || m_compositor == nullptr) {
        return;
    }
    WindowUVE& w = *found;
    if (AndroidEditorHostBridgeUVE::s_state.surfaceAttached && m_app->activeWindow() == id && w.backend != nullptr) {
        m_app->renderWindowTo(id, *w.backend);
        CompositeUVE(w, w.backend->colorTexture(), m_compositor->vao, views);
    }
    EndInputFrameUVE(w);
}

bool EditorHostUVE::IsWindowFocusedUVE(const int id) const {
    return m_ready && m_app != nullptr && FindUVE(id) != nullptr && m_app->activeWindow() == id;
}

bool EditorHostUVE::QuitRequestedUVE() const noexcept {
    return m_platform != nullptr && m_platform->QuitRequestedUVE();
}

bool EditorHostUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    if (!m_ready || m_main == nullptr || m_compositor == nullptr || !AndroidEditorHostBridgeUVE::s_state.surfaceAttached) {
        return false;
    }
    m_compositor->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return WriteFramebufferPngUVE(path, m_main->fbWidth, m_main->fbHeight);
}

bool EditorHostUVE::SaveWindowScreenshotUVE(const int id, const std::filesystem::path& path) {
    WindowUVE* w = id != kMainWindowUVE ? FindUVE(id) : nullptr;
    if (!m_ready || w == nullptr || m_app == nullptr || m_compositor == nullptr || w->backend == nullptr ||
        !AndroidEditorHostBridgeUVE::s_state.surfaceAttached) {
        return false;
    }
    m_app->renderWindowTo(id, *w->backend);
    CompositeUVE(*w, w->backend->colorTexture(), m_compositor->vao, {});
    return WriteFramebufferPngUVE(path, w->fbWidth, w->fbHeight);
}

void EditorHostUVE::SetKeyHookUVE(std::function<bool(int key, int action)> hook) {
    m_keyHook = std::move(hook);
}

// --------------------------------------------------------------------------------------- bridge

void AndroidEditorHostBridgeUVE::NotifyAttachedUVE(void* const nativeWindow, const int width, const int height) {
    s_state.surfaceAttached = (nativeWindow != nullptr);
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE()) {
        return;
    }
    auto* const nw = static_cast<ANativeWindow*>(nativeWindow);
    if (g_activeAndroidHostUVE->m_main != nullptr) {
        g_activeAndroidHostUVE->m_main->nativeWindow = nw;
    }
    if (g_activeAndroidHostUVE->m_platform != nullptr) {
        g_activeAndroidHostUVE->m_platform->SetNativeWindowUVE(nw);
    }
    const int effW = width > 0 ? width : (nw != nullptr ? ANativeWindow_getWidth(nw) : 0);
    const int effH = height > 0 ? height : (nw != nullptr ? ANativeWindow_getHeight(nw) : 0);
    SyncAllSizesUVE(*g_activeAndroidHostUVE, effW, effH);
    if (g_activeAndroidHostUVE->m_app != nullptr) {
        g_activeAndroidHostUVE->m_app->invalidate();
    }
}

void AndroidEditorHostBridgeUVE::NotifyDetachedUVE() {
    s_state.surfaceAttached = false;
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE()) {
        return;
    }
    CancelAllInputUVE(*g_activeAndroidHostUVE);
    if (g_activeAndroidHostUVE->m_main != nullptr) {
        g_activeAndroidHostUVE->m_main->nativeWindow = nullptr;
    }
    if (g_activeAndroidHostUVE->m_platform != nullptr) {
        g_activeAndroidHostUVE->m_platform->SetNativeWindowUVE(nullptr);
    }
}

void AndroidEditorHostBridgeUVE::NotifyResizedUVE(const int width, const int height) {
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE()) {
        return;
    }
    SyncAllSizesUVE(*g_activeAndroidHostUVE, width, height);
    if (g_activeAndroidHostUVE->m_app != nullptr) {
        g_activeAndroidHostUVE->m_app->invalidateLayout();
    }
}

bool AndroidEditorHostBridgeUVE::DispatchTouchUVE(const AndroidTouchActionUVE action,
                                                    const std::int32_t actionIndex,
                                                    const std::span<const AndroidTouchContactUVE> contacts,
                                                    const std::int32_t buttonState,
                                                    const float scrollX,
                                                    const float scrollY) {
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE()) {
        return false;
    }
    EditorHostUVE& host = *g_activeAndroidHostUVE;
    EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
    if (target == nullptr || host.m_app == nullptr) {
        return false;
    }

    if (action == AndroidTouchActionUVE::Cancel) {
        CancelAllInputUVE(host);
        return true;
    }

    if (action == AndroidTouchActionUVE::Scroll && !contacts.empty()) {
        const prism::Vec2 at{contacts.front().x, contacts.front().y};
        RunScopedUVE(*target, [&host, target, at, scrollX, scrollY] {
            target->mouse = at;
            target->mouseInside = true;
            host.m_app->windowMouseMove(target->id, at);
            target->wheel += scrollY;
            host.m_app->windowMouseWheel(target->id, at, prism::Vec2{scrollX, scrollY});
        });
        return true;
    }

    if (action == AndroidTouchActionUVE::HoverMove && !contacts.empty()) {
        const prism::Vec2 at{contacts.front().x, contacts.front().y};
        RunScopedUVE(*target, [&host, target, at] {
            target->mouse = at;
            target->mouseInside = true;
            UpdateMoveDragDistancesUVE(*target);
            host.m_app->windowMouseMove(target->id, at);
        });
        return true;
    }

    // Rebuild active contact set from the platform snapshot, excluding the pointer being lifted on Up/PointerUp.
    const std::int32_t liftedPointerId =
        ((action == AndroidTouchActionUVE::Up || action == AndroidTouchActionUVE::PointerUp) &&
         actionIndex >= 0 && static_cast<std::size_t>(actionIndex) < contacts.size())
            ? contacts[static_cast<std::size_t>(actionIndex)].pointerId
            : -1;

    std::vector<AndroidTouchContactUVE> remaining;
    remaining.reserve(contacts.size());
    for (const AndroidTouchContactUVE& c : contacts) {
        if (c.pointerId != liftedPointerId) {
            remaining.push_back(c);
        }
    }

    const double now = NowSecondsUVE();

    if (action == AndroidTouchActionUVE::Down && !contacts.empty()) {
        const AndroidTouchContactUVE& primary = contacts.front();
        s_state.contacts = std::move(remaining);
        s_state.primaryPointerId = primary.pointerId;
        s_state.primaryButton = prism::MouseButton::Left;
        if ((buttonState & AMOTION_EVENT_BUTTON_SECONDARY) != 0) {
            s_state.primaryButton = prism::MouseButton::Right;
        } else if ((buttonState & AMOTION_EVENT_BUTTON_TERTIARY) != 0) {
            s_state.primaryButton = prism::MouseButton::Middle;
        }
        s_state.twoFingerActive = false;
        const prism::Vec2 at{primary.x, primary.y};
        s_state.longPressPending = (s_state.primaryButton == prism::MouseButton::Left);
        s_state.longPressFired = false;
        s_state.longPressStartTime = now;
        s_state.longPressStartPos = at;
        s_state.longPressMaxTravel = 0.0F;

        RunScopedUVE(*target, [&host, target, at] {
            PressMouseButtonUVE(host, *target, s_state.primaryButton, at);
        });
        return true;
    }

    if (action == AndroidTouchActionUVE::PointerDown) {
        s_state.contacts = std::move(remaining);
        s_state.longPressPending = false;
        if (s_state.contacts.size() >= 2U && !s_state.twoFingerActive) {
            const prism::Vec2 p0{s_state.contacts[0].x, s_state.contacts[0].y};
            const prism::Vec2 p1{s_state.contacts[1].x, s_state.contacts[1].y};
            const prism::Vec2 centroid{(p0.x + p1.x) * 0.5F, (p0.y + p1.y) * 0.5F};
            const float span = std::sqrt(((p1.x - p0.x) * (p1.x - p0.x)) + ((p1.y - p0.y) * (p1.y - p0.y)));
            s_state.twoFingerActive = true;
            s_state.twoFingerStartTime = now;
            s_state.twoFingerCentroidStart = centroid;
            s_state.twoFingerPrevCentroid = centroid;
            s_state.twoFingerPrevSpan = span;
            s_state.twoFingerMaxTravel = 0.0F;

            // Cancel tentative single-finger press before multi-touch pinch/scroll/tap takes over.
            const int primarySlot = ToViewButtonUVE(s_state.primaryButton);
            if (primarySlot >= 0 && target->down[static_cast<std::size_t>(primarySlot)] && !host.m_app->isDragging()) {
                RunScopedUVE(*target, [&host, target, centroid] {
                    ReleaseMouseButtonUVE(host, *target, s_state.primaryButton, centroid, false);
                    host.m_app->windowMouseButton(target->id, prism::Vec2{-10000.0F, -10000.0F},
                                                  s_state.primaryButton, false, 1);
                });
            }
        }
        return true;
    }

    if (action == AndroidTouchActionUVE::Move) {
        s_state.contacts = std::move(remaining);
        if (s_state.twoFingerActive && s_state.contacts.size() >= 2U) {
            const prism::Vec2 p0{s_state.contacts[0].x, s_state.contacts[0].y};
            const prism::Vec2 p1{s_state.contacts[1].x, s_state.contacts[1].y};
            const prism::Vec2 centroid{(p0.x + p1.x) * 0.5F, (p0.y + p1.y) * 0.5F};
            const float span = std::sqrt(((p1.x - p0.x) * (p1.x - p0.x)) + ((p1.y - p0.y) * (p1.y - p0.y)));
            const float dSpan = span - s_state.twoFingerPrevSpan;
            const prism::Vec2 dCentroid{centroid.x - s_state.twoFingerPrevCentroid.x,
                                        centroid.y - s_state.twoFingerPrevCentroid.y};
            const float travelX = centroid.x - s_state.twoFingerCentroidStart.x;
            const float travelY = centroid.y - s_state.twoFingerCentroidStart.y;
            s_state.twoFingerMaxTravel =
                std::max(s_state.twoFingerMaxTravel, std::sqrt((travelX * travelX) + (travelY * travelY)));
            s_state.twoFingerMaxTravel = std::max(s_state.twoFingerMaxTravel, std::abs(dSpan));

            const float wheelY = (dSpan / 28.0F) + (dCentroid.y / 36.0F);
            const float wheelX = dCentroid.x / 36.0F;
            s_state.twoFingerPrevCentroid = centroid;
            s_state.twoFingerPrevSpan = span;

            if (std::abs(wheelY) > 0.02F || std::abs(wheelX) > 0.02F) {
                RunScopedUVE(*target, [&host, target, centroid, wheelX, wheelY] {
                    target->mouse = centroid;
                    target->mouseInside = true;
                    target->wheel += wheelY;
                    host.m_app->windowMouseWheel(target->id, centroid, prism::Vec2{wheelX, wheelY});
                });
            }
            return true;
        }

        const AndroidTouchContactUVE* active = FindContactUVE(s_state.contacts, s_state.primaryPointerId);
        if (active == nullptr && !s_state.contacts.empty()) {
            active = &s_state.contacts.front();
            s_state.primaryPointerId = active->pointerId;
        }
        if (active != nullptr) {
            const prism::Vec2 at{active->x, active->y};
            if (s_state.longPressPending) {
                const float dx = at.x - s_state.longPressStartPos.x;
                const float dy = at.y - s_state.longPressStartPos.y;
                s_state.longPressMaxTravel = std::max(s_state.longPressMaxTravel, std::sqrt((dx * dx) + (dy * dy)));
                if (s_state.longPressMaxTravel > kTouchSlopUVE) {
                    s_state.longPressPending = false;
                }
            }
            RunScopedUVE(*target, [&host, target, at] {
                target->mouse = at;
                target->mouseInside = true;
                UpdateMoveDragDistancesUVE(*target);
                host.m_app->windowMouseMove(target->id, at);
            });
        }
        return true;
    }

    if (action == AndroidTouchActionUVE::PointerUp || action == AndroidTouchActionUVE::Up) {
        const prism::Vec2 liftedPos =
            (actionIndex >= 0 && static_cast<std::size_t>(actionIndex) < contacts.size())
                ? prism::Vec2{contacts[static_cast<std::size_t>(actionIndex)].x,
                              contacts[static_cast<std::size_t>(actionIndex)].y}
                : target->mouse;
        s_state.contacts = std::move(remaining);

        if (s_state.twoFingerActive) {
            if (s_state.contacts.empty()) {
                const bool quickTwoFingerTap = (now - s_state.twoFingerStartTime <= kTwoFingerTapSecondsUVE) &&
                                               (s_state.twoFingerMaxTravel <= kTwoFingerTapSlopUVE);
                const prism::Vec2 tapPos = s_state.twoFingerCentroidStart;
                s_state.twoFingerActive = false;
                s_state.primaryPointerId = -1;
                if (quickTwoFingerTap) {
                    RunScopedUVE(*target, [&host, target, tapPos] {
                        target->clickCount = 1;
                        PressMouseButtonUVE(host, *target, prism::MouseButton::Right, tapPos);
                        ReleaseMouseButtonUVE(host, *target, prism::MouseButton::Right, tapPos, true);
                    });
                }
            } else {
                // Keep tracking until the last finger lifts so no stray Left release fires.
                s_state.primaryPointerId = s_state.contacts.front().pointerId;
            }
            return true;
        }

        if (action == AndroidTouchActionUVE::Up || liftedPointerId == s_state.primaryPointerId) {
            s_state.longPressPending = false;
            const prism::MouseButton releaseBtn = s_state.primaryButton;
            const bool suppressRelease = s_state.longPressFired;
            s_state.longPressFired = false;
            if (s_state.contacts.empty()) {
                s_state.primaryPointerId = -1;
            } else {
                s_state.primaryPointerId = s_state.contacts.front().pointerId;
            }
            if (!suppressRelease) {
                RunScopedUVE(*target, [&host, target, releaseBtn, liftedPos] {
                    ReleaseMouseButtonUVE(host, *target, releaseBtn, liftedPos, true);
                    if (releaseBtn == prism::MouseButton::Left && host.m_app->windowWantsKeyboard(target->id) &&
                        g_androidCallbacksUVE.setTextInputActive) {
                        g_androidCallbacksUVE.setTextInputActive(true);
                    }
                });
            }
        }
        return true;
    }

    return false;
}

bool AndroidEditorHostBridgeUVE::DispatchKeyUVE(const std::int32_t keyCode,
                                                  const std::int32_t action,
                                                  const std::int32_t metaState,
                                                  const std::uint32_t unicodeChar,
                                                  const bool repeat) {
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE()) {
        return false;
    }
    EditorHostUVE& host = *g_activeAndroidHostUVE;
    if (host.m_app == nullptr) {
        return false;
    }

    // Android Back button: dismiss blocking popup -> close active secondary window -> keyHook/Escape.
    if (keyCode == AKEYCODE_BACK) {
        if (action == AKEY_EVENT_ACTION_DOWN && !repeat) {
            if (host.m_main != nullptr && host.m_keyHook && host.m_keyHook(keyCode, action)) {
                return true;
            }
            if (host.m_app->hasBlockingPopup()) {
                host.m_app->closeAllPopups();
                return true;
            }
            const int activeId = host.m_app->activeWindow();
            if (activeId != EditorHostUVE::kMainWindowUVE) {
                if (EditorHostUVE::WindowUVE* activeWin = host.FindUVE(activeId)) {
                    activeWin->closeRequested = true;
                    return true;
                }
            }
        }
        if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_UP) {
            EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
            if (target != nullptr) {
                RunScopedUVE(*target, [&host, target, action, metaState, repeat] {
                    target->mods = ToPrismModsUVE(metaState);
                    host.m_app->hostSetModifiers(target->mods);
                    host.m_app->windowKey(target->id, prism::Key::Escape, target->mods,
                                          action == AKEY_EVENT_ACTION_DOWN, repeat);
                });
            }
            return true;
        }
    }

    EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
    if (target == nullptr) {
        return false;
    }
    if (target->id == EditorHostUVE::kMainWindowUVE && host.m_keyHook && host.m_keyHook(keyCode, action)) {
        return true;
    }

    bool handled = false;
    RunScopedUVE(*target, [&host, target, keyCode, action, metaState, unicodeChar, repeat, &handled] {
        target->mods = ToPrismModsUVE(metaState);
        host.m_app->hostSetModifiers(target->mods);

        const ViewKeyUVE viewKey = ToViewKeyUVE(keyCode);
        if (viewKey != ViewKeyUVE::Count) {
            const auto index = static_cast<std::size_t>(viewKey);
            if (action == AKEY_EVENT_ACTION_DOWN) {
                if (!repeat) {
                    target->keysDown.set(index);
                    target->keysPressed.set(index);
                }
            } else if (action == AKEY_EVENT_ACTION_UP) {
                target->keysDown.reset(index);
            }
        }

        const prism::Key prismKey = ToPrismKeyUVE(keyCode);
        if (prismKey != prism::Key::Unknown) {
            if (action == AKEY_EVENT_ACTION_DOWN) {
                host.m_app->windowKey(target->id, prismKey, target->mods, true, repeat);
                handled = true;
            } else if (action == AKEY_EVENT_ACTION_UP) {
                host.m_app->windowKey(target->id, prismKey, target->mods, false, false);
                handled = true;
            }
        }

        if (action == AKEY_EVENT_ACTION_DOWN && unicodeChar >= 32U && unicodeChar != 127U) {
            std::string utf8;
            AppendUtf8UVE(utf8, static_cast<char32_t>(unicodeChar));
            host.m_app->windowText(target->id, utf8);
            handled = true;
        }
    });
    return handled;
}

void AndroidEditorHostBridgeUVE::DispatchTextUVE(const std::string_view utf8Text) {
    if (g_activeAndroidHostUVE == nullptr || !g_activeAndroidHostUVE->IsReadyUVE() || utf8Text.empty()) {
        return;
    }
    EditorHostUVE& host = *g_activeAndroidHostUVE;
    EditorHostUVE::WindowUVE* target = ActiveTargetWindowUVE(host);
    if (target == nullptr || host.m_app == nullptr) {
        return;
    }
    RunScopedUVE(*target, [&host, target, utf8Text] {
        host.m_app->windowText(target->id, std::string{utf8Text});
    });
}

void SetAndroidEditorCallbacksUVE(AndroidEditorCallbacksUVE callbacks) {
    g_androidCallbacksUVE = std::move(callbacks);
}

void NotifyAndroidWindowAttachedUVE(void* const nativeWindow, const int width, const int height) {
    AndroidEditorHostBridgeUVE::NotifyAttachedUVE(nativeWindow, width, height);
}

void NotifyAndroidWindowDetachedUVE() {
    AndroidEditorHostBridgeUVE::NotifyDetachedUVE();
}

void NotifyAndroidWindowResizedUVE(const int width, const int height) {
    AndroidEditorHostBridgeUVE::NotifyResizedUVE(width, height);
}

bool DispatchAndroidTouchEventUVE(const AndroidTouchActionUVE action,
                                  const std::int32_t actionIndex,
                                  const std::span<const AndroidTouchContactUVE> contacts,
                                  const std::int32_t buttonState,
                                  const float scrollX,
                                  const float scrollY) {
    return AndroidEditorHostBridgeUVE::DispatchTouchUVE(action, actionIndex, contacts, buttonState, scrollX, scrollY);
}

bool DispatchAndroidKeyEventUVE(const std::int32_t keyCode,
                                const std::int32_t action,
                                const std::int32_t metaState,
                                const std::uint32_t unicodeChar,
                                const bool repeat) {
    return AndroidEditorHostBridgeUVE::DispatchKeyUVE(keyCode, action, metaState, unicodeChar, repeat);
}

void DispatchAndroidTextInputUVE(const std::string_view utf8Text) {
    AndroidEditorHostBridgeUVE::DispatchTextUVE(utf8Text);
}

void CancelAndroidEditorInputUVE() {
    if (g_activeAndroidHostUVE != nullptr && g_activeAndroidHostUVE->IsReadyUVE()) {
        AndroidEditorHostBridgeUVE::CancelAllInputUVE(*g_activeAndroidHostUVE);
    } else {
        AndroidEditorHostBridgeUVE::ResetGestureStateUVE();
    }
}

} // namespace UVE::Editor
