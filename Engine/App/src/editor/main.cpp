// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/GlApi.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "ViewportRenderPass.h"
#include "integration/EntityManagerEntitySource.h"
#include "univex/camera/OrbitCamera.h"

#include "uve/core/engine_core_uve.h"
#include "uve/debug/logging_macros_uve.h"
#include "uve/editor/editor_bridge_stdio_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/scene/components/world_transform_component_uve.h"
#include "uve/scene/i_entity_manager_uve.h"

namespace {

// Bridges Engine/Editor/Viewport's real GL renderer (grid + orbit camera + transform/orientation
// gizmos + one proxy cube per live scene entity) into EditorUVE's generic, viewport-agnostic
// "Viewport" panel hook (EditorUVE::SetViewportPanelRendererUVE) - see that method's own doc
// comment in editor_uve.h for why EditorCore itself stays free of any Viewport-module/GL/ImGui
// types. Renders off-screen into an MSAA framebuffer, resolves into a plain 2D texture
// ImGui::Image() can sample, and recreates both whenever the panel's reported size changes -
// the same multisample-then-resolve shape tools/headless_capture.cpp already uses (that file's
// own comment explains why: the solid gizmo/cube geometry has nothing but MSAA to soften its
// silhouettes) and the same resize-on-demand shape app/main.cpp's interactive demo already uses
// for its own window-sized framebuffer, just driven by the ImGui panel's reported size instead of
// a GLFW framebuffer-resize callback.
class ViewportPanelBackendUVE final {
public:
    ViewportPanelBackendUVE(UVE::Editor::EditorUVE& editor, UVE::Scene::IEntityManagerUVE& entityManager)
        : editor_(editor), entityManager_(entityManager), entitySource_(entityManager) {}

    ~ViewportPanelBackendUVE() { DestroyFramebuffersUVE(); }

    ViewportPanelBackendUVE(const ViewportPanelBackendUVE&) = delete;
    ViewportPanelBackendUVE& operator=(const ViewportPanelBackendUVE&) = delete;

    [[nodiscard]] std::uint64_t RenderUVE(const UVE::Math::Vector2UVE& availableSize,
                                          UVE::Math::Vector2UVE& outUsedSize,
                                          const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        if (!EnsureGlewInitializedUVE() || !EnsureRenderPassUVE()) {
            return 0U;
        }

        const int width = std::max(1, static_cast<int>(availableSize.x));
        const int height = std::max(1, static_cast<int>(availableSize.y));
        if (!EnsureFramebuffersUVE(width, height)) {
            return 0U;
        }

        ApplyOverlayStateUVE(overlayState);
        UpdateSelectionGizmoUVE();
        UpdateCameraFromMouseUVE(height);

        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glEnable(GL_MULTISAMPLE);
        renderPass_->RenderFrame(camera_, width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

        outUsedSize = UVE::Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};
        return static_cast<std::uint64_t>(resolveColorTexture_);
    }

private:
    [[nodiscard]] bool EnsureGlewInitializedUVE() {
        if (glewInitialized_) {
            return true;
        }
        glewExperimental = GL_TRUE;
        if (const GLenum status = glewInit(); status != GLEW_OK) {
            UVE_ERROR("uve_editor_app: glewInit failed for the viewport panel: {}",
                      reinterpret_cast<const char*>(glewGetErrorString(status)));
            return false;
        }
        glGetError(); // discard GLEW's benign core-profile extension-probe error
        glewInitialized_ = true;
        return true;
    }

    [[nodiscard]] bool EnsureRenderPassUVE() {
        if (renderPass_.has_value()) {
            return true;
        }
        std::string error;
        renderPass_ = univex::app::ViewportRenderPass::Create(error);
        if (!renderPass_.has_value()) {
            UVE_ERROR("uve_editor_app: viewport render pass init failed: {}", error);
            return false;
        }
        renderPass_->SetEntitySource(&entitySource_);
        return true;
    }

    [[nodiscard]] bool EnsureFramebuffersUVE(const int width, const int height) {
        if (width == framebufferWidth_ && height == framebufferHeight_ && resolveFbo_ != 0U) {
            return true;
        }
        DestroyFramebuffersUVE();
        framebufferWidth_ = width;
        framebufferHeight_ = height;

        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        const GLsizei samples = std::min(maxSamples, 8);

        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glGenRenderbuffers(1, &msaaColorRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRb_);
        glGenRenderbuffers(1, &msaaDepthRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRb_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            UVE_ERROR("uve_editor_app: viewport MSAA framebuffer incomplete");
            return false;
        }

        glGenFramebuffers(1, &resolveFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
        glGenTextures(1, &resolveColorTexture_);
        glBindTexture(GL_TEXTURE_2D, resolveColorTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColorTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!complete) {
            UVE_ERROR("uve_editor_app: viewport resolve framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void DestroyFramebuffersUVE() {
        if (resolveColorTexture_ != 0U) {
            glDeleteTextures(1, &resolveColorTexture_);
        }
        if (resolveFbo_ != 0U) {
            glDeleteFramebuffers(1, &resolveFbo_);
        }
        if (msaaColorRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaColorRb_);
        }
        if (msaaDepthRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaDepthRb_);
        }
        if (msaaFbo_ != 0U) {
            glDeleteFramebuffers(1, &msaaFbo_);
        }
        resolveColorTexture_ = resolveFbo_ = msaaColorRb_ = msaaDepthRb_ = msaaFbo_ = 0U;
        framebufferWidth_ = framebufferHeight_ = 0;
    }

    // Applies EditorUVE's own generic overlay-toolbar state (see ViewportOverlayStateUVE's doc
    // comment on why it's plain enums/bools rather than any Viewport-module type) to the real
    // ViewportRenderPass each frame. Snap is stored and reflected in the bubble's highlight but
    // has no behavioral effect yet: there is no drag-to-move gizmo interaction implemented in this
    // slice for it to snap - the gizmo is currently a visual overlay only, not yet draggable.
    void ApplyOverlayStateUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        auto& settings = renderPass_->Settings();
        settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                        : univex::viewport::ProjectionMode::Perspective;
        // The Game workspace tab previews what a player would see - no editor-only grid overlay.
        settings.viewGrid = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        gameWorkspaceActive_ = overlayState.gameWorkspaceActive;
        using UVE::Editor::EditorUVE;
        switch (overlayState.gizmoMode) {
            case EditorUVE::ViewportGizmoModeUVE::Move:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Move);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Rotate:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Rotate);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Scale:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Scale);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Universal:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Universal);
                break;
        }
    }

    // Only shows the transform gizmo (and its center pivot cube) while a real entity is selected
    // in EditorUVE, like a Node3D-style engine - the reference standalone demo always draws it at
    // the camera's own orbit target since it has no independent "selected object" concept, which
    // read as a stray gizmo floating with nothing selected once wired into a real editor.
    // Repositions the gizmo to the selected entity's actual world transform via
    // SetGizmoPivotOverride() (see that method's own comment on why camera.Target() alone isn't
    // enough - orbiting the camera must not drag a selected object's gizmo along with it).
    void UpdateSelectionGizmoUVE() {
        const UVE::Scene::EntityUVE selected = editor_.GetSelectedEntityUVE();
        const bool hasSelection = selected != UVE::Scene::kInvalidEntityUVE &&
                                  entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(selected);
        renderPass_->Settings().viewTransformGizmo = hasSelection && !gameWorkspaceActive_;
        if (hasSelection) {
            const auto& worldTransform =
                entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(selected);
            renderPass_->SetGizmoPivotOverride(
                univex::math::Vec3{worldTransform.worldPosition.x, worldTransform.worldPosition.y,
                                   worldTransform.worldPosition.z});
        } else {
            renderPass_->SetGizmoPivotOverride(std::nullopt);
        }
    }

    // Mirrors app/main.cpp's own GLFW mouse-button/scroll wiring (left-drag orbits, middle/right
    // drag pans, wheel dollies) but sourced from ImGui's IO instead of GLFW callbacks, since input
    // flows through ImGui while the viewport is docked. Called from inside EditorUVE's own
    // ImGui::Begin("Viewport")/End() block (via the render callback this class provides to
    // SetViewportPanelRendererUVE), so ImGui::IsWindowHovered() here correctly reports hover over
    // the Viewport panel specifically. Known simplification versus the GLFW demo: a drag that
    // began inside the panel stops orbiting the moment the cursor leaves it, rather than
    // continuing to track a global drag - acceptable for this integration slice.
    void UpdateCameraFromMouseUVE(const int framebufferHeight) {
        if (!ImGui::IsWindowHovered()) {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
            camera_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F)) {
            camera_.Pan(io.MouseDelta.x, io.MouseDelta.y, framebufferHeight);
        }
        if (io.MouseWheel != 0.0F) {
            camera_.Dolly(io.MouseWheel);
        }
    }

    UVE::Editor::EditorUVE& editor_;
    UVE::Scene::IEntityManagerUVE& entityManager_;
    univex::integration::EntityManagerEntitySource entitySource_;
    std::optional<univex::app::ViewportRenderPass> renderPass_;
    // Set each frame by ApplyOverlayStateUVE(), read by UpdateSelectionGizmoUVE() so it can force
    // the transform gizmo off while the Game workspace tab is active (see ApplyOverlayStateUVE's
    // own comment - it already forces the grid off directly, but the gizmo's visibility is decided
    // later in the same frame by selection state, so it needs this stored flag instead).
    bool gameWorkspaceActive_ = false;
    univex::camera::OrbitCamera camera_;
    bool glewInitialized_ = false;
    GLuint msaaFbo_ = 0U;
    GLuint msaaColorRb_ = 0U;
    GLuint msaaDepthRb_ = 0U;
    GLuint resolveFbo_ = 0U;
    GLuint resolveColorTexture_ = 0U;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
};

struct EditorLaunchOptionsUVE final {
    std::filesystem::path scenePath = "editor_scene.uvescene";
    std::optional<int> frameLimit;
    std::optional<std::uint32_t> glMajor;
    std::optional<std::uint32_t> glMinor;
    bool headless = false;
    bool bridgeStdio = false;
};

[[nodiscard]] bool ParseGlVersionUVE(const std::string_view value, std::uint32_t& major,
                                     std::uint32_t& minor) {
    const std::size_t separator = value.find('.');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U >= value.size()) {
        return false;
    }

    const std::string_view majorText = value.substr(0U, separator);
    const std::string_view minorText = value.substr(separator + 1U);
    const auto [majorEnd, majorError] =
        std::from_chars(majorText.data(), majorText.data() + majorText.size(), major);
    const auto [minorEnd, minorError] =
        std::from_chars(minorText.data(), minorText.data() + minorText.size(), minor);
    return majorError == std::errc{} && minorError == std::errc{} &&
           majorEnd == majorText.data() + majorText.size() && minorEnd == minorText.data() + minorText.size();
}

[[nodiscard]] EditorLaunchOptionsUVE ParseOptionsUVE(const int argc, char** argv) {
    EditorLaunchOptionsUVE options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--bridge-stdio") {
            options.bridgeStdio = true;
            options.headless = true;
            continue;
        }
        if (argument == "--scene" && index + 1 < argc) {
            options.scenePath = argv[++index];
            continue;
        }
        if (argument == "--frames" && index + 1 < argc) {
            int frameLimit = 0;
            const std::string_view value{argv[++index]};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
            if (error == std::errc{} && end == value.data() + value.size() && frameLimit >= 0) {
                options.frameLimit = frameLimit;
            }
            continue;
        }
        if (argument == "--gl-version" && index + 1 < argc) {
            std::uint32_t major = 0;
            std::uint32_t minor = 0;
            if (ParseGlVersionUVE(argv[++index], major, minor)) {
                options.glMajor = major;
                options.glMinor = minor;
            }
        }
    }
    if (options.headless && !options.frameLimit.has_value()) {
        options.frameLimit = 1;
    }
    return options;
}

} // namespace

/// Starts the standalone UniVex Editor Foundation v1. `--scene <path>` selects the `.uvescene`
/// document. `--frames <n>` bounds a run for automation, while the normal windowed invocation runs
/// until the user closes the editor. `--gl-version <major.minor>` overrides the requested desktop
/// OpenGL version for an explicitly chosen platform capability (for example virtual-display CI).
/// `--headless` keeps the editor's non-visual lifecycle usable in CI and defaults to a single frame.
/// `--bridge-stdio` always implies headless mode and runs a framed JSON-RPC bridge server instead
/// of constructing native ImGui/GLFW presentation for this process.
///
/// The editor drives EngineCoreUVE's lifecycle by hand (Init()/Load()/TickFrameUVE()) rather than
/// through RunUVE() — the one desktop entry point that does not automatically get RunUVE()'s
/// built-in exception boundary (see EngineCoreUVE::RunUVE()'s doc comment) — so this function
/// wraps that entire hand-driven lifecycle in its own boundary below, following the same shape:
/// log via UVE_FATAL, still run Shutdown() if (and only if) the engine had reached
/// EngineStateUVE::Running by the time something threw, and return
/// EngineCoreUVE::kUnhandledExceptionExitCodeUVE instead of letting the exception unwind out of
/// main() into std::terminate().
int main(const int argc, char** argv) {
    const EditorLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);

    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = "uve_editor.log";
    config.enableConsoleLogging = !options.bridgeStdio;
    config.headlessUVE = options.headless;
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);
    if (options.glMajor.has_value() && options.glMinor.has_value()) {
        config.windowGlVersionMajor = *options.glMajor;
        config.windowGlVersionMinor = *options.glMinor;
    }

    UVE::Core::EngineCoreUVE engine(config);
    try {
        engine.Init();
        if (!engine.Load()) {
            engine.Shutdown();
            return 1;
        }

        UVE::Editor::EditorUVE editor(engine.GetServicesUVE(), options.scenePath, 100U, &engine);
        editor.InitUVE();

        if (std::filesystem::exists(options.scenePath)) {
            static_cast<void>(editor.LoadSceneUVE());
        }

        if (options.bridgeStdio) {
            UVE::Asset::DataTableRegistryUVE dataTableRegistry;
            UVE::Editor::EditorBridgeUVE bridge(editor, &dataTableRegistry);
            UVE::Editor::EditorBridgeStdioServerUVE server(bridge);
            const int result = server.ServeUVE(std::cin, std::cout, std::cerr);
            editor.ShutdownUVE();
            engine.Shutdown();
            return result;
        }

        // EngineCoreUVE's own scene render stays a documented no-op (the editor doesn't own a
        // gameplay camera), so the Viewport panel's real content comes entirely from
        // ViewportPanelBackendUVE below - grid, orbit camera, gizmos, and one proxy cube per live
        // scene entity, composited via ImGui::Image() rather than EngineCoreUVE's own render
        // target. Only constructed in real windowed mode: it needs an actual current GL context,
        // which headless mode's NullRenderDeviceUVE never creates.
        std::optional<ViewportPanelBackendUVE> viewportBackend;
        if (!options.headless) {
            viewportBackend.emplace(editor, engine.GetServicesUVE().GetEntityManagerUVE());
            editor.SetViewportPanelRendererUVE(
                [&backend = *viewportBackend](
                    const UVE::Math::Vector2UVE& availableSize, UVE::Math::Vector2UVE& outUsedSize,
                    const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
                    return backend.RenderUVE(availableSize, outUsedSize, overlayState);
                });
        }
        engine.SetPostRenderCallbackUVE([&editor] { editor.RenderOverlayUVE(); });

        int framesRun = 0;
        while (!engine.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!options.frameLimit.has_value() || framesRun < *options.frameLimit)) {
            editor.TickUVE();
            engine.TickFrameUVE();
            ++framesRun;
        }

        engine.SetPostRenderCallbackUVE({});
        editor.SetViewportPanelRendererUVE({});
        // Destroyed here, before engine.Shutdown() tears down the window/GL context below - its
        // destructor deletes real GL objects (framebuffers/textures) that must still be valid.
        viewportBackend.reset();
        editor.ShutdownUVE();
        engine.Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("uve_editor_app: unhandled exception escaped the editor lifecycle - shutting down: {}",
                   exception.what());
    } catch (...) {
        UVE_FATAL("uve_editor_app: unhandled non-std::exception escaped the editor lifecycle - shutting down");
    }

    // Reached only via one of the catches above. Only safe to call Shutdown() if the engine had
    // actually reached Running - see EngineCoreUVE::RunUVE()'s doc comment for why an exception
    // during Init() itself must not force a Shutdown() call. `editor` (and any object declared
    // inside the try block above) is already out of scope here, having been destroyed normally
    // during stack unwinding.
    if (engine.GetStateUVE() == UVE::Core::EngineStateUVE::Running) {
        try {
            engine.Shutdown();
        } catch (const std::exception& exception) {
            UVE_FATAL("uve_editor_app: engine.Shutdown() itself threw while recovering from the exception "
                       "above: {}",
                       exception.what());
        } catch (...) {
            UVE_FATAL("uve_editor_app: engine.Shutdown() itself threw a non-std::exception while recovering "
                       "from the exception above");
        }
    }
    return UVE::Core::EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
}
