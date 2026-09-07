// app/main.cpp
// -----------------------------------------------------------------------
// Interactive demo: a resizable GLFW window showing the infinite grid, the
// transform gizmos and the orientation gizmo.
//
//   TRANSFORM GIZMO   Q select · W move · E rotate · R scale · T universal
//   NAVIGATION        left drag orbit · middle/right drag pan · scroll dolly
//   ORIENTATION GIZMO drag it to orbit freely, or click a ball to snap
//   STANDARD VIEWS    7 top · Ctrl+7 bottom · 1 front · Ctrl+1 rear
//                     3 right · Ctrl+3 left   (numpad or the number row)
//   MODES             5 perspective/orthographic · Z cycle shading
//                     G grid · N orientation gizmo · H transform gizmo
//                     B environment · M scene geometry
//   FRAMING           O focus origin · F focus selection · Home reset view
//   ESC               quit
//
// The window title reports the live state: framebuffer size, gizmo mode,
// projection, shading, the grid spacing the LOD has settled on, and fps.
// -----------------------------------------------------------------------
#include <cstdio>
#include <string>

#include "ViewportRenderPass.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/gizmo/NavGizmo.h"
#include "univex/render/GlApi.h"
#include "univex/viewport/ViewportSettings.h"

#include <GLFW/glfw3.h>

namespace {

using univex::app::NavViewportRect;
using univex::app::ViewportRenderPass;
using univex::camera::OrbitCamera;
using univex::gizmo::GizmoMode;
using univex::math::Vec3;
using univex::viewport::DisplayMode;
using univex::viewport::ProjectionMode;
using univex::viewport::StandardView;

// How far the pointer may travel on the nav gizmo and still count as a
// click rather than a drag.
constexpr float kNavClickSlopPixels = 4.f;

struct AppState {
    OrbitCamera camera;
    ViewportRenderPass* pass = nullptr;

    bool orbiting = false;
    bool panning = false;
    bool navDragging = false;
    bool navDragMoved = false;
    float navPressX = 0.f;
    float navPressY = 0.f;

    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    int framebufferWidth = 1600;
    int framebufferHeight = 900;
    int windowWidth = 1600;
    int windowHeight = 900;
};

void ResetView(OrbitCamera& camera) {
    camera.CancelAnimation();
    camera.SetTarget({0.f, 0.f, 0.f});
    camera.SetYawPitch(-0.7553f, 0.4561f);
    camera.SetDistance(11.26f);
    camera.SetOrthographic(false);
}

// GLFW reports the cursor in window coordinates; everything else here works
// in framebuffer pixels, and on a HiDPI display those differ.
void CursorToFramebuffer(const AppState& state, double cursorX, double cursorY,
                         float& outX, float& outY) {
    const float scaleX = (state.windowWidth > 0)
        ? static_cast<float>(state.framebufferWidth) / static_cast<float>(state.windowWidth) : 1.f;
    const float scaleY = (state.windowHeight > 0)
        ? static_cast<float>(state.framebufferHeight) / static_cast<float>(state.windowHeight) : 1.f;
    outX = static_cast<float>(cursorX) * scaleX;
    outY = static_cast<float>(cursorY) * scaleY;
}

// Returns true and fills the nav-local position if the cursor is over the
// orientation gizmo. Nav-local coordinates have their origin at the widget's
// top-left corner.
bool CursorOverNavGizmo(const AppState& state, float fbX, float fbY,
                        float& outLocalX, float& outLocalY) {
    if (state.pass == nullptr || !state.pass->Settings().viewGizmos) return false;
    const NavViewportRect rect = ViewportRenderPass::NavViewportRectFor(
        state.pass->Style(), state.framebufferWidth, state.framebufferHeight);
    if (rect.size <= 0) return false;

    // rect.y is a GL viewport origin (bottom-left); convert to top-left.
    const float top = static_cast<float>(state.framebufferHeight - rect.y - rect.size);
    const float left = static_cast<float>(rect.x);
    if (fbX < left || fbX > left + static_cast<float>(rect.size)) return false;
    if (fbY < top || fbY > top + static_cast<float>(rect.size)) return false;

    outLocalX = fbX - left;
    outLocalY = fbY - top;
    return true;
}

void LeaveStandardView(AppState& state) {
    if (state.pass == nullptr) return;
    auto& settings = state.pass->Settings();
    if (settings.standardView == StandardView::User) return;
    settings.standardView = StandardView::User;
    if (settings.autoOrthogonal) state.camera.SetOrthographic(false);
}

void GoToStandardView(AppState& state, StandardView view) {
    if (state.pass == nullptr) return;
    auto& settings = state.pass->Settings();
    settings.standardView = view;

    float x = 0.f, y = 0.f, z = 0.f;
    univex::viewport::StandardViewDirection(view, x, y, z);
    state.camera.SnapToDirection(Vec3{x, y, z});

    // Snapping to an axis view in perspective is almost never what someone
    // means by "Front", so this follows the usual editor behaviour.
    if (settings.autoOrthogonal) state.camera.SetOrthographic(true);
}

void OnFramebufferSize(GLFWwindow* window, int width, int height) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;
    state->framebufferWidth = width;
    state->framebufferHeight = height;
    glfwGetWindowSize(window, &state->windowWidth, &state->windowHeight);
    // Render inside the callback so a live resize drag keeps painting rather
    // than showing a stretched or blank framebuffer between events.
    if (state->pass != nullptr && width > 0 && height > 0) {
        state->pass->RenderFrame(state->camera, width, height);
        glfwSwapBuffers(window);
    }
}

void OnMouseButton(GLFWwindow* window, int button, int action, int /*mods*/) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;

    double cursorX = 0.0, cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    state->lastCursorX = cursorX;
    state->lastCursorY = cursorY;

    float fbX = 0.f, fbY = 0.f;
    CursorToFramebuffer(*state, cursorX, cursorY, fbX, fbY);

    const bool pressed = (action == GLFW_PRESS);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (pressed) {
            float localX = 0.f, localY = 0.f;
            if (CursorOverNavGizmo(*state, fbX, fbY, localX, localY)) {
                // Press on the orientation gizmo: hold the decision until
                // release, so the same gesture can be either a drag or a click.
                state->navDragging = true;
                state->navDragMoved = false;
                state->navPressX = localX;
                state->navPressY = localY;
                state->camera.CancelAnimation();
                return;
            }
            state->orbiting = true;
        } else {
            if (state->navDragging) {
                state->navDragging = false;
                if (!state->navDragMoved && state->pass != nullptr) {
                    const auto pick = univex::gizmo::PickNavGizmo(
                        state->pass->Style(),
                        ViewportRenderPass::NavViewMatrix(state->camera),
                        state->navPressX, state->navPressY,
                        static_cast<float>(state->pass->Style().navPixelSize));
                    if (pick.hit) {
                        state->camera.SnapToDirection(pick.direction);
                        auto& settings = state->pass->Settings();
                        settings.standardView = StandardView::User;
                        if (settings.autoOrthogonal) state->camera.SetOrthographic(true);
                    }
                }
                return;
            }
            state->orbiting = false;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_MIDDLE || button == GLFW_MOUSE_BUTTON_RIGHT) {
        state->panning = pressed;
    }
}

void OnCursorPos(GLFWwindow* window, double x, double y) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;
    const auto dx = static_cast<float>(x - state->lastCursorX);
    const auto dy = static_cast<float>(y - state->lastCursorY);
    state->lastCursorX = x;
    state->lastCursorY = y;

    if (state->navDragging) {
        if (std::abs(dx) + std::abs(dy) > 0.f) {
            float fbX = 0.f, fbY = 0.f;
            CursorToFramebuffer(*state, x, y, fbX, fbY);
            float localX = 0.f, localY = 0.f;
            CursorOverNavGizmo(*state, fbX, fbY, localX, localY);
            if (std::abs(localX - state->navPressX) + std::abs(localY - state->navPressY)
                    > kNavClickSlopPixels) {
                state->navDragMoved = true;
            }
        }
        // Dragging the orientation gizmo orbits exactly like dragging the scene.
        if (state->navDragMoved) {
            state->camera.Orbit(dx, dy);
            LeaveStandardView(*state);
        }
        return;
    }

    if (state->orbiting) {
        state->camera.Orbit(dx, dy);
        LeaveStandardView(*state);
    }
    if (state->panning) {
        state->camera.Pan(dx, dy, state->framebufferHeight);
    }
}

void OnScroll(GLFWwindow* window, double /*xOffset*/, double yOffset) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) return;
    state->camera.Dolly(static_cast<float>(yOffset));
}

void OnKey(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr || state->pass == nullptr || action != GLFW_PRESS) return;
    auto& settings = state->pass->Settings();
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, GLFW_TRUE); return;

        // ---- transform gizmo modes ----
        case GLFW_KEY_Q: state->pass->SetGizmoMode(GizmoMode::Select); return;
        case GLFW_KEY_W: state->pass->SetGizmoMode(GizmoMode::Move); return;
        case GLFW_KEY_E: state->pass->SetGizmoMode(GizmoMode::Rotate); return;
        case GLFW_KEY_R: state->pass->SetGizmoMode(GizmoMode::Scale); return;
        case GLFW_KEY_T: state->pass->SetGizmoMode(GizmoMode::Universal); return;

        // ---- standard views (numpad or number row) ----
        case GLFW_KEY_KP_7: case GLFW_KEY_7:
            GoToStandardView(*state, ctrl ? StandardView::Bottom : StandardView::Top); return;
        case GLFW_KEY_KP_1: case GLFW_KEY_1:
            GoToStandardView(*state, ctrl ? StandardView::Rear : StandardView::Front); return;
        case GLFW_KEY_KP_3: case GLFW_KEY_3:
            GoToStandardView(*state, ctrl ? StandardView::Left : StandardView::Right); return;
        case GLFW_KEY_KP_5: case GLFW_KEY_5:
            state->camera.SetOrthographic(!state->camera.IsOrthographic());
            settings.projection = state->camera.IsOrthographic() ? ProjectionMode::Orthographic
                                                                 : ProjectionMode::Perspective;
            return;

        // ---- display modes and overlays ----
        case GLFW_KEY_Z: settings.CycleDisplayMode(); return;
        case GLFW_KEY_G: settings.viewGrid = !settings.viewGrid; return;
        case GLFW_KEY_N: settings.viewGizmos = !settings.viewGizmos; return;
        case GLFW_KEY_H: settings.viewTransformGizmo = !settings.viewTransformGizmo; return;
        case GLFW_KEY_B: settings.viewEnvironment = !settings.viewEnvironment; return;
        case GLFW_KEY_M: settings.viewSceneGeometry = !settings.viewSceneGeometry; return;

        // ---- framing ----
        case GLFW_KEY_O: state->camera.Focus(Vec3{0.f, 0.f, 0.f}, 6.f); return;
        case GLFW_KEY_F: state->camera.Focus(Vec3{0.f, 1.f, 0.f}, 3.2f); return;
        case GLFW_KEY_HOME: ResetView(state->camera); settings.standardView = StandardView::User; return;
        default: return;
    }
}

std::string FormatSpacing(float spacing) {
    char buffer[64];
    if (spacing >= 1000.f)      std::snprintf(buffer, sizeof buffer, "%.0f km", spacing / 1000.f);
    else if (spacing >= 1.f)    std::snprintf(buffer, sizeof buffer, "%.0f m", spacing);
    else if (spacing >= 0.01f)  std::snprintf(buffer, sizeof buffer, "%.0f cm", spacing * 100.f);
    else                        std::snprintf(buffer, sizeof buffer, "%.1f mm", spacing * 1000.f);
    return buffer;
}

} // namespace

int main() {
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 8);

    AppState state;
    GLFWwindow* window = glfwCreateWindow(state.framebufferWidth, state.framebufferHeight,
                                          "UNIVEX Viewport", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (const GLenum status = glewInit(); status != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(status));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }
    glGetError(); // GLEW's core-profile probe leaves a benign GL_INVALID_ENUM behind
    glEnable(GL_MULTISAMPLE);

    std::string error;
    auto pass = ViewportRenderPass::Create(error);
    if (!pass.has_value()) {
        std::fprintf(stderr, "viewport init failed: %s\n", error.c_str());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 4;
    }

    ResetView(state.camera);
    state.pass = &pass.value();
    glfwGetFramebufferSize(window, &state.framebufferWidth, &state.framebufferHeight);
    glfwGetWindowSize(window, &state.windowWidth, &state.windowHeight);
    glfwSetWindowUserPointer(window, &state);
    glfwSetFramebufferSizeCallback(window, OnFramebufferSize);
    glfwSetMouseButtonCallback(window, OnMouseButton);
    glfwSetCursorPosCallback(window, OnCursorPos);
    glfwSetScrollCallback(window, OnScroll);
    glfwSetKeyCallback(window, OnKey);

    std::printf("GL_RENDERER: %s\nGL_VERSION : %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    double lastFrameTime = glfwGetTime();
    double lastTitleUpdate = lastFrameTime;
    int framesSinceTitleUpdate = 0;

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        const double now = glfwGetTime();
        const auto deltaSeconds = static_cast<float>(now - lastFrameTime);
        lastFrameTime = now;
        state.camera.Update(deltaSeconds);

        pass->RenderFrame(state.camera, state.framebufferWidth, state.framebufferHeight);
        glfwSwapBuffers(window);

        ++framesSinceTitleUpdate;
        if (now - lastTitleUpdate >= 0.25) {
            const float worldPerPixel =
                univex::render::WorldPerPixelAtPivot(state.camera, state.framebufferHeight);
            const float spacing =
                univex::render::ComputeDisplayGridSpacing(worldPerPixel, pass->Grid().Settings());
            const auto& settings = pass->Settings();

            char title[320];
            std::snprintf(title, sizeof title,
                          "UNIVEX Viewport  |  %dx%d  |  %s  |  %s  |  %s  |  grid %s  |  %.0f fps",
                          state.framebufferWidth, state.framebufferHeight,
                          univex::gizmo::GizmoModeName(pass->Mode()),
                          state.camera.IsOrthographic() ? "Orthographic" : "Perspective",
                          univex::viewport::DisplayModeName(settings.display),
                          FormatSpacing(spacing).c_str(),
                          framesSinceTitleUpdate / (now - lastTitleUpdate));
            glfwSetWindowTitle(window, title);
            lastTitleUpdate = now;
            framesSinceTitleUpdate = 0;
        }
    }

    pass.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
