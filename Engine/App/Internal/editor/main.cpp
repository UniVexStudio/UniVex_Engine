// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/gl_api.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "viewport_render_pass.h"
#include "integration/editor_mesh_layer.h"
#include "integration/selection_outline_geometry.h"
#include "integration/entity_picker.h"
#include "univex/camera/viewport_metrics.h"
#include "univex/gizmo/gizmo_drag.h"
#include "univex/gizmo/gizmo_picking.h"
#include "integration/math_conversions.h"
#include "univex/camera/orbit_camera.h"
#include "univex/render/shader_program.h"
#include "univex/render/studio_backdrop_renderer.h"

#include "engine_core.h"
#include "logging_macros.h"
#include "editor_bridge_stdio.h"
#include "ui_draw_batch.h"
#include "ui_font_atlas.h"
#include "editor.h"
#include "vector2.h"
#include "camera_component.h"
#include "light_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "camera_3d.h"
#include "directional_light_3d.h"
#include "fog_volume_3d.h"
#include "occluder_3d.h"
#include "reflection_probe_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"
#include "camera_system.h"
#include "i_scene_graph.h"
#include "univex/gizmo/gizmo_geometry.h"
#include "editor_viewport_backends.h"

namespace {

struct EditorLaunchOptionsUVE final {
    /// Empty means the project's default Viewport: Content/Viewport/Viewport.uvscene.
    std::filesystem::path scenePath;
    std::optional<int> frameLimit;
    std::optional<std::uint32_t> glMajor;
    std::optional<std::uint32_t> glMinor;
    bool headless = false;
    bool bridgeStdio = false;
    bool decorated = false;
    /// The PRISM editor shell instead of the previous UI backend.
    bool prismUi = false;
    /// After the last frame (--frames), save the PRISM editor frame as PNG here.
    std::filesystem::path prismScreenshot;
    /// Screenshot aid: put a folder, a sun, a camera and a box in the level and select the box, so the
    /// panels and the viewport gizmos have content.
    bool prismDemoObject = false;
    bool prismDemoGizmos = false;
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
        if (argument == "--prism-ui") {
            options.prismUi = true;
            continue;
        }
        if (argument == "--prism-demo-object") {
            options.prismDemoObject = true;
            continue;
        }
        if (argument == "--prism-demo-gizmos") {
            options.prismDemoObject = true;
            options.prismDemoGizmos = true;
            continue;
        }
        if (argument == "--prism-screenshot" && index + 1 < argc) {
            options.prismScreenshot = argv[++index];
            continue;
        }
        if (argument == "--decorated") {
            options.decorated = true;
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

/// Starts the standalone UniVex Editor Foundation v1. `--scene <path>` selects the `.uvscene`
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
    EditorLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);

    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = "uve_editor.log";
    config.enableConsoleLogging = !options.bridgeStdio;
    config.headlessUVE = options.headless;
    config.windowBorderlessUVE = !options.decorated;
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

        // The level is a Viewport asset in the Content Browser: Content/Viewport/Viewport.uvscene
        // unless --scene names another. A level saved at the old default moves there once.
        bool createDefaultViewport = false;
        if (options.scenePath.empty()) {
            const std::filesystem::path folder =
                engine.GetServicesUVE().GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / "Viewport";
            options.scenePath = folder / "Viewport.uvscene";
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (!std::filesystem::exists(options.scenePath, error)) {
                if (std::filesystem::exists("editor_scene.uvscene", error)) {
                    std::filesystem::copy_file("editor_scene.uvscene", options.scenePath, error);
                } else {
                    createDefaultViewport = true;
                }
            }
        }

        UVE::Editor::EditorUVE editor(engine.GetServicesUVE(), options.scenePath, 100U, &engine);
        if (options.prismUi && !options.headless) {
            // PRISM's fonts / themes / locales ship next to the executable (UVBT copies them).
            std::error_code assetError;
            std::filesystem::path assetDir =
                std::filesystem::absolute(std::filesystem::path(argv[0]), assetError).parent_path() / "prism_assets";
            if (!std::filesystem::is_directory(assetDir, assetError)) {
                assetDir = "prism_assets";
            }
            editor.SetPrismUiEnabledUVE(true, assetDir);
        }
        editor.InitUVE();

        if (std::filesystem::exists(options.scenePath)) {
            static_cast<void>(editor.LoadSceneUVE());
        } else if (createDefaultViewport) {
            static_cast<void>(editor.SaveSceneUVE()); // the default Viewport asset exists from the start
        }
        if (options.prismDemoObject) {
            static_cast<void>(editor.CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::Folder));
            const UVE::Scene::EntityUVE sun =
                editor.CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
            const UVE::Scene::EntityUVE camera =
                editor.CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::Camera3D);
            if (options.prismDemoGizmos) {
                // The sun and the camera beside the box rather than inside it.
                UVE::Scene::IEntityManagerUVE& placed = engine.GetServicesUVE().GetEntityManagerUVE();
                const auto place = [&placed](const UVE::Scene::EntityUVE entity, const UVE::Math::Vector3UVE at) {
                    if (entity != UVE::Scene::kInvalidEntityUVE &&
                        placed.HasComponentUVE<UVE::Scene::TransformComponentUVE>(entity)) {
                        placed.GetComponentUVE<UVE::Scene::TransformComponentUVE>(entity).localPosition = at;
                    }
                };
                place(sun, UVE::Math::Vector3UVE{2.5F, 2.0F, 2.5F});
                place(camera, UVE::Math::Vector3UVE{-2.5F, 1.0F, 2.0F});
                // One of each object with a gizmo of its own, spread out along X.
                using KindUVE = UVE::Scene::Objects::SceneObjectKindUVE;
                const std::array<KindUVE, 7> kinds{KindUVE::Light3D, KindUVE::Decal3D, KindUVE::Marker3D,
                                                   KindUVE::SpawnPoint3D, KindUVE::RayCast3D, KindUVE::SpringArm3D,
                                                   KindUVE::NavMeshVolume3D};
                UVE::Scene::IEntityManagerUVE& entities = engine.GetServicesUVE().GetEntityManagerUVE();
                float x = -6.0F;
                for (const KindUVE kind : kinds) {
                    const UVE::Scene::EntityUVE entity =
                        editor.CreateDocumentSceneObjectUVE(kind, editor.GetDocumentViewportUVE());
                    if (entity != UVE::Scene::kInvalidEntityUVE &&
                        entities.HasComponentUVE<UVE::Scene::TransformComponentUVE>(entity)) {
                        entities.GetComponentUVE<UVE::Scene::TransformComponentUVE>(entity).localPosition =
                            UVE::Math::Vector3UVE{x, 1.0F, -3.0F};
                    }
                    // The light as a spot, so its cone shows.
                    if (kind == KindUVE::Light3D && entities.HasComponentUVE<UVE::Scene::LightComponentUVE>(entity)) {
                        auto& light = entities.GetComponentUVE<UVE::Scene::LightComponentUVE>(entity);
                        light.type = UVE::Scene::LightTypeUVE::Spot;
                        light.range = 4.0F;
                        light.spotAngleDegrees = 30.0F;
                    }
                    x += 2.0F;
                }
            }
            static_cast<void>(editor.CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::BoxMesh3D));
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
        // EditorViewportBackendsUVE - grid, orbit camera, gizmos, and one proxy cube per live
        // scene entity, composited via ImGui::Image() rather than EngineCoreUVE's own render
        // target. Only constructed in real windowed mode: it needs an actual current GL context,
        // which headless mode's NullRenderDeviceUVE never creates.
        // Three real backends, not three names routed through one mutable renderer.  In particular,
        // resizing/orbiting the level view can no longer recreate the Entity Editor's target or
        // overwrite its camera; Retarget also keeps its fixed studio framing in its own backend.
        using ViewportContextUVE = UVE::Editor::EditorUVE::ViewportContextUVE;
        std::unique_ptr<UVE::App::EditorViewportBackendsUVE> viewportBackends;
        if (!options.headless) {
            viewportBackends = std::make_unique<UVE::App::EditorViewportBackendsUVE>(editor, engine);
            editor.SetViewportPanelRendererUVE(
                [&viewportBackends](const ViewportContextUVE context,
                                    const UVE::Math::Vector2UVE& availableSize,
                                    UVE::Math::Vector2UVE& outUsedSize,
                                    const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
                    return viewportBackends->RenderUVE(context, availableSize, outUsedSize, overlayState);
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

        if (!options.prismScreenshot.empty() && !editor.SavePrismScreenshotUVE(options.prismScreenshot)) {
            std::fprintf(stderr, "uve_editor: could not save the PRISM screenshot\n");
        }
        engine.SetPostRenderCallbackUVE({});
        editor.SetViewportPanelRendererUVE({});
        // Destroyed here, before engine.Shutdown() tears down the window/GL context below - its
        // destructor deletes real GL objects (framebuffers/textures) that must still be valid.
        viewportBackends.reset();
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
