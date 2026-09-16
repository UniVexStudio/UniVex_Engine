// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "uve/core/engine_core_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/pack/project_launcher_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/components/ui_button_component_uve.h"
#include "uve/scene/components/ui_image_component_uve.h"
#include "uve/scene/components/ui_text_component_uve.h"

namespace {

/// Phase U3a diagnostic-only fixture: authors a camera plus one UIImage/UIButton/UIText entity and
/// makes the camera active, purely so `--ui-overlay-demo` has real on-screen content to screenshot/
/// click-test against a live window - this is not a general scene-loading feature (that is
/// roadmap item #7's own, separate, future scope), just the smallest real content this diagnostic
/// flag needs.
void AuthorUIOverlayDemoFixtureUVE(UVE::Core::EngineCoreUVE& engine) {
    UVE::Core::EngineServicesUVE& services = engine.GetServicesUVE();
    UVE::Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    UVE::Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();

    const UVE::Scene::EntityUVE camera = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, camera, UVE::Scene::TransformComponentUVE{});
    entityManager.AddComponentUVE<UVE::Scene::CameraComponentUVE>(camera);
    engine.SetActiveCameraUVE(camera);

    const UVE::Scene::EntityUVE imageEntity = entityManager.CreateEntityUVE();
    UVE::Scene::UIImageComponentUVE image{};
    image.positionPixels = UVE::Math::Vector2UVE{40.0F, 40.0F};
    image.sizePixels = UVE::Math::Vector2UVE{120.0F, 120.0F};
    image.tintColor = UVE::Math::Vector3UVE{0.85F, 0.20F, 0.20F};
    entityManager.AddComponentUVE<UVE::Scene::UIImageComponentUVE>(imageEntity, image);

    const UVE::Scene::EntityUVE buttonEntity = entityManager.CreateEntityUVE();
    UVE::Scene::UIButtonComponentUVE button{};
    button.positionPixels = UVE::Math::Vector2UVE{220.0F, 60.0F};
    button.sizePixels = UVE::Math::Vector2UVE{160.0F, 48.0F};
    button.normalColor = UVE::Math::Vector3UVE{0.15F, 0.55F, 0.20F};
    button.hoverColor = UVE::Math::Vector3UVE{0.20F, 0.70F, 0.28F};
    button.pressedColor = UVE::Math::Vector3UVE{0.85F, 0.75F, 0.15F};
    entityManager.AddComponentUVE<UVE::Scene::UIButtonComponentUVE>(buttonEntity, button);

    const UVE::Scene::EntityUVE textEntity = entityManager.CreateEntityUVE();
    UVE::Scene::UITextComponentUVE text{};
    text.text = "Phase U3a UI Overlay Demo - click the button";
    text.positionPixels = UVE::Math::Vector2UVE{40.0F, 200.0F};
    text.fontSize = 22.0F;
    text.color = UVE::Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    entityManager.AddComponentUVE<UVE::Scene::UITextComponentUVE>(textEntity, text);
}

} // namespace

/// Proof-of-life entry point for the UniVex Engine. By default opens a real GLFW3 window with an
/// OpenGL 4.6 Core render device (Increment 20) and drives a full Init -> Load -> N x
/// (BeginFrame/Update/LateUpdate/Render/EndFrame) -> Shutdown cycle through EngineCoreUVE,
/// exiting 0 — the demo triangle Render() draws each frame is the "initializes the engine and
/// renders a frame" proof-of-life. Pass `--headless` to fall back to NullWindowManagerUVE/
/// NullRenderDeviceUVE instead (no display required — the mode CI and this project's own test
/// suite use). `argv[1..argc)` (the program path at argv[0] excluded) is forwarded to
/// EngineConfigUVE::commandLineArgs, so real invocations like
/// `uve_runtime --project <path>`, `uve_runtime --server`, or `uve_runtime --headless` (the
/// shapes the Hub launches this executable with) are parsed by CommandLineUVE.
///
/// `--ui-overlay-demo [frameCount]` is a Phase U3a diagnostic-only path (not part of the above
/// contract): it drives EngineCoreUVE's lifecycle by hand instead of RunUVE(), authors the small
/// fixture above, and runs for `frameCount` frames (default 3000, ~ a minute at 60 Hz) so a real
/// windowed run under Xvfb stays open long enough for an external xdotool screenshot/click pass.
int main(int argc, char** argv) {
    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = "uve_engine.log";
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);

    // Roadmap item #7: plays an authored/packaged project standalone - loads its .uveditor
    // manifest's configured startup scene and activates its camera (see
    // Pack::LoadAndActivateProjectSceneUVE), then runs until the window is closed (or,
    // headlessly, until the safety frame cap below - there is no window-close signal to wait on
    // without a window). This is the real "played game" entry point ProjectPackagerUVE's own
    // packaged distributables are meant to be launched with.
    const auto projectFlagIt =
        std::find(config.commandLineArgs.begin(), config.commandLineArgs.end(), "--project");
    if (projectFlagIt != config.commandLineArgs.end()) {
        const auto projectPathIt = std::next(projectFlagIt);
        if (projectPathIt == config.commandLineArgs.end()) {
            std::cerr << "uve_runtime: --project requires a path to a .uveditor file\n";
            return 1;
        }
        const std::filesystem::path projectPath = *projectPathIt;
        config.commandLineArgs.erase(projectFlagIt, std::next(projectPathIt));

        UVE::Core::EngineCoreUVE engine(config);
        engine.Init();
        if (!engine.Load()) {
            return 1;
        }
        const UVE::Pack::ProjectLaunchResultUVE launchResult =
            UVE::Pack::LoadAndActivateProjectSceneUVE(engine, projectPath);
        if (!launchResult.IsAcceptedUVE()) {
            std::cerr << "uve_runtime: " << launchResult.message << '\n';
            engine.Shutdown();
            return 1;
        }
        // Windowed runs exit naturally via TickFrameUVE()'s own window-close check
        // (IsQuitRequestedUVE() then flips true); this cap only bounds a headless run, which has
        // no window to close and would otherwise spin forever - one hour at a nominal 60 Hz.
        constexpr int kMaxFramesUVE = 216000;
        for (int frame = 0; frame < kMaxFramesUVE && !engine.IsQuitRequestedUVE(); ++frame) {
            engine.TickFrameUVE();
        }
        engine.Shutdown();
        return 0;
    }

    const auto demoFlagIt = std::find(config.commandLineArgs.begin(), config.commandLineArgs.end(),
                                       "--ui-overlay-demo");
    if (demoFlagIt != config.commandLineArgs.end()) {
        int frameCount = 3000;
        const auto frameCountIt = std::next(demoFlagIt);
        if (frameCountIt != config.commandLineArgs.end()) {
            frameCount = std::atoi(frameCountIt->c_str());
        }
        config.commandLineArgs.erase(demoFlagIt, config.commandLineArgs.end());
        config.windowGlVersionMajor = 4U;
        config.windowGlVersionMinor = 5U;

        UVE::Core::EngineCoreUVE engine(config);
        engine.Init();
        if (!engine.Load()) {
            return 1;
        }
        AuthorUIOverlayDemoFixtureUVE(engine);
        for (int frame = 0; frame < frameCount; ++frame) {
            engine.TickFrameUVE();
        }
        engine.Shutdown();
        return 0;
    }

    UVE::Core::EngineCoreUVE engine(config);

    constexpr int kFrameCount = 60; // ~1 second of frames at nominal 60 Hz
    return engine.RunUVE(kFrameCount);
}
