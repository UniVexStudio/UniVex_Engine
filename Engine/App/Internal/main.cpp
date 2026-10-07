// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "uve/core/engine_core_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/config/settings_registry_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/platform/application_runtime_uve.h"
#include "uve/platform/crash_reporter_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/pack/project_launcher_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"

namespace {

[[nodiscard]] std::string BuildRuntimeCommandLineHelpUVE() {
    std::ostringstream output;
    output << "UniVex Runtime\n\n"
              "Usage:\n"
              "  uve_runtime --project <file.uvproject> [options]\n"
              "  uve_runtime --help\n\n"
              "Application options:\n"
              "  --project <path>  Load the named .uvproject and its configured startup scene.\n"
              "  --help, -h        Show this command-line reference.\n\n"
              "Engine settings (bool values may be omitted to mean true):\n";

    UVE::Config::SettingsRegistryUVE registry;
    if (!UVE::Core::RegisterEngineProjectSettingsUVE(registry)) {
        output << "  (engine command-line settings could not be registered)\n";
        return output.str();
    }
    for (const UVE::Config::SettingDescriptorUVE* const descriptor : registry.GetAllUVE()) {
        if (descriptor == nullptr || !UVE::Core::IsEngineConfigSettingIdUVE(descriptor->id)) {
            continue;
        }
        output << "  --" << descriptor->id;
        if (descriptor->type == UVE::Config::SettingTypeUVE::Bool) {
            output << " [true|false]";
        } else {
            output << " <value>";
        }
        output << "\n      " << descriptor->tooltip << '\n';
    }
    output << "\nValues: colors/vectors use comma-separated numbers; enums accept a label or integer.\n"
              "Unknown flags are ignored. In a headless project run, the runtime exits after its safety frame cap.\n"
              "Diagnostic option: --ui-overlay-demo [frame-count].\n";
    return output.str();
}

[[nodiscard]] bool IsContainedExistingResourceUVE(const std::filesystem::path& contentRoot,
                                                    const std::filesystem::path& resourcePath,
                                                    std::filesystem::path& canonicalResource) {
    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::canonical(contentRoot, error);
    if (error) {
        return false;
    }
    canonicalResource = std::filesystem::canonical(resourcePath, error);
    if (error) {
        return false;
    }
    if (!std::filesystem::is_regular_file(canonicalResource, error) || error) {
        return false;
    }
    const std::filesystem::path relative = canonicalResource.lexically_relative(canonicalRoot);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] bool HasRawArgumentUVE(const int argc, char** argv, const std::string_view argument) {
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && argument == argv[index]) {
            return true;
        }
    }
    return false;
}

struct ProjectBootSplashOverlayUVE final {
    UVE::Core::EngineCoreUVE* engine = nullptr;
    UVE::Scene::EntityUVE previousCamera = UVE::Scene::kInvalidEntityUVE;
    UVE::Scene::EntityUVE temporaryCamera = UVE::Scene::kInvalidEntityUVE;
    UVE::Scene::EntityUVE backgroundEntity = UVE::Scene::kInvalidEntityUVE;
    UVE::Scene::EntityUVE imageEntity = UVE::Scene::kInvalidEntityUVE;
    std::optional<UVE::Asset::AssetHandleUVE<UVE::Asset::TextureAssetUVE>> textureHandle;
    std::uint32_t surfaceWidth = 0U;
    std::uint32_t surfaceHeight = 0U;
    std::array<float, 4U> backgroundColor{0.0F, 0.0F, 0.0F, 1.0F};

    ProjectBootSplashOverlayUVE() = default;
    ProjectBootSplashOverlayUVE(const ProjectBootSplashOverlayUVE&) = delete;
    ProjectBootSplashOverlayUVE& operator=(const ProjectBootSplashOverlayUVE&) = delete;

    ~ProjectBootSplashOverlayUVE() { CleanupUVE(); }

    void CleanupUVE() noexcept {
        if (engine == nullptr || engine->GetStateUVE() != UVE::Core::EngineStateUVE::Running) {
            return;
        }
        try {
            UVE::Core::EngineServicesUVE& services = engine->GetServicesUVE();
            UVE::Scene::IEntityManagerUVE& entities = services.GetEntityManagerUVE();
            if (temporaryCamera != UVE::Scene::kInvalidEntityUVE) {
                engine->SetActiveCameraUVE(previousCamera);
            }
            if (imageEntity != UVE::Scene::kInvalidEntityUVE) {
                static_cast<void>(entities.DestroyEntityUVE(imageEntity));
                imageEntity = UVE::Scene::kInvalidEntityUVE;
            }
            if (backgroundEntity != UVE::Scene::kInvalidEntityUVE) {
                static_cast<void>(entities.DestroyEntityUVE(backgroundEntity));
                backgroundEntity = UVE::Scene::kInvalidEntityUVE;
            }
            if (temporaryCamera != UVE::Scene::kInvalidEntityUVE) {
                static_cast<void>(entities.DestroyEntityUVE(temporaryCamera));
                temporaryCamera = UVE::Scene::kInvalidEntityUVE;
            }
            static_cast<void>(engine->SetApplicationBackgroundColorUVE(backgroundColor));
        } catch (...) {
            // Splash cleanup is best-effort during exception unwinding; EngineCore still owns the
            // authoritative scene and renderer teardown immediately afterward.
        }
        textureHandle.reset();
        engine = nullptr;
    }

    void SetVisibilityUVE(const float visibility) {
        if (engine == nullptr || backgroundEntity == UVE::Scene::kInvalidEntityUVE) {
            return;
        }
        UVE::Scene::IEntityManagerUVE& entities = engine->GetServicesUVE().GetEntityManagerUVE();
        UVE::Scene::UIImageComponentUVE& background =
            entities.GetComponentUVE<UVE::Scene::UIImageComponentUVE>(backgroundEntity);
        background.tintColor = UVE::Math::Vector3UVE{backgroundColor[0] * visibility,
                                                      backgroundColor[1] * visibility,
                                                      backgroundColor[2] * visibility};
        background.alpha = backgroundColor[3] * visibility;
        if (imageEntity != UVE::Scene::kInvalidEntityUVE) {
            UVE::Scene::UIImageComponentUVE& image =
                entities.GetComponentUVE<UVE::Scene::UIImageComponentUVE>(imageEntity);
            image.alpha = visibility;
            float aspectRatio = 1.6F;
            if (textureHandle.has_value() && textureHandle->IsReadyUVE()) {
                const UVE::Asset::TextureAssetUVE* const texture = textureHandle->TryGetUVE();
                if (texture != nullptr && texture->width > 0U && texture->height > 0U) {
                    aspectRatio = static_cast<float>(texture->width) / static_cast<float>(texture->height);
                }
            }
            const float maxWidth = static_cast<float>(surfaceWidth) * 0.62F;
            const float maxHeight = static_cast<float>(surfaceHeight) * 0.62F;
            float imageWidth = maxWidth;
            float imageHeight = maxWidth / aspectRatio;
            if (imageHeight > maxHeight) {
                imageHeight = maxHeight;
                imageWidth = maxHeight * aspectRatio;
            }
            image.sizePixels = UVE::Math::Vector2UVE{imageWidth, imageHeight};
            image.positionPixels = UVE::Math::Vector2UVE{
                (static_cast<float>(surfaceWidth) - imageWidth) * 0.5F,
                (static_cast<float>(surfaceHeight) - imageHeight) * 0.5F};
        }
    }
};

[[nodiscard]] bool InitializeProjectBootSplashOverlayUVE(UVE::Core::EngineCoreUVE& engine,
                                                           const UVE::Pack::ProjectRuntimeSetupUVE& setup,
                                                           ProjectBootSplashOverlayUVE& overlay) {
    UVE::Core::EngineServicesUVE& services = engine.GetServicesUVE();
    UVE::Window::IWindowManagerUVE& window = services.GetWindowManagerUVE();
    if (!window.IsValidUVE() || window.GetWidthUVE() == 0U || window.GetHeightUVE() == 0U) {
        return false;
    }
    overlay.engine = &engine;
    overlay.previousCamera = engine.GetActiveCameraUVE();
    overlay.surfaceWidth = window.GetWidthUVE();
    overlay.surfaceHeight = window.GetHeightUVE();
    overlay.backgroundColor = engine.GetConfigUVE().backgroundColorUVE;

    UVE::Asset::AssetGuidUVE splashTextureGuid = UVE::Asset::kInvalidAssetGuidUVE;
    std::filesystem::path splashImagePath = engine.GetConfigUVE().bootSplashImagePathUVE;
    if (splashImagePath.is_relative()) {
        // Project setting overrides use paths relative to the content directory, just like the
        // package's splashImagePath. Resolve before importing so process CWD never affects startup.
        splashImagePath = setup.contentRoot / splashImagePath;
    }
    if (!splashImagePath.empty()) {
        std::filesystem::path canonicalSplashImagePath;
        if (!IsContainedExistingResourceUVE(setup.contentRoot, splashImagePath, canonicalSplashImagePath)) {
            std::cerr << "uve_runtime: warning: the configured boot image must be a regular file under the project content root; "
                         "showing the background color only\n";
            splashImagePath.clear();
        } else {
            splashImagePath = std::move(canonicalSplashImagePath);
        }
    }
    if (!splashImagePath.empty()) {
        std::string extension = splashImagePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (extension == ".uvtex") {
            splashTextureGuid = services.GetAssetDatabaseUVE().RegisterUVE(splashImagePath);
        } else {
            std::error_code error;
            const std::filesystem::path derivedDirectory =
                setup.userDataDirectory / "DerivedData" / "BootSplash";
            std::filesystem::create_directories(derivedDirectory, error);
            if (!error) {
#if defined(_WIN32)
                const auto processId = static_cast<unsigned long long>(_getpid());
#else
                const auto processId = static_cast<unsigned long long>(getpid());
#endif
                const std::filesystem::path derivedTexturePath =
                    derivedDirectory / ("boot_splash_" + std::to_string(processId) + ".uvtex");
                splashTextureGuid = services.GetAssetImporterUVE().ImportUVE(
                    splashImagePath, derivedTexturePath, services.GetAssetDatabaseUVE());
            }
        }
        if (splashTextureGuid != UVE::Asset::kInvalidAssetGuidUVE) {
            overlay.textureHandle.emplace(services.GetAssetManagerUVE().LoadUVE<UVE::Asset::TextureAssetUVE>(
                splashTextureGuid, services.GetAssetDatabaseUVE()));
        } else {
            std::cerr << "uve_runtime: warning: the configured boot image could not be imported; "
                         "showing the background color only\n";
        }
    }

    UVE::Scene::IEntityManagerUVE& entities = services.GetEntityManagerUVE();
    UVE::Scene::ISceneGraphUVE& sceneGraph = services.GetSceneGraphUVE();
    if (overlay.previousCamera == UVE::Scene::kInvalidEntityUVE) {
        overlay.temporaryCamera = entities.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entities, overlay.temporaryCamera, UVE::Scene::TransformComponentUVE{});
        entities.AddComponentUVE<UVE::Scene::CameraComponentUVE>(overlay.temporaryCamera);
        sceneGraph.UpdateUVE(entities);
        engine.SetActiveCameraUVE(overlay.temporaryCamera);
    }

    overlay.backgroundEntity = entities.CreateEntityUVE();
    UVE::Scene::UIImageComponentUVE background;
    background.positionPixels = UVE::Math::Vector2UVE{0.0F, 0.0F};
    background.sizePixels = UVE::Math::Vector2UVE{static_cast<float>(overlay.surfaceWidth),
                                                  static_cast<float>(overlay.surfaceHeight)};
    background.tintColor = UVE::Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    background.alpha = 1.0F;
    entities.AddComponentUVE<UVE::Scene::UIImageComponentUVE>(overlay.backgroundEntity, background);

    if (splashTextureGuid != UVE::Asset::kInvalidAssetGuidUVE) {
        overlay.imageEntity = entities.CreateEntityUVE();
        UVE::Scene::UIImageComponentUVE image;
        image.textureAssetGuid = splashTextureGuid;
        image.sizePixels = UVE::Math::Vector2UVE{1.0F, 1.0F};
        image.tintColor = UVE::Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        image.alpha = 0.0F;
        entities.AddComponentUVE<UVE::Scene::UIImageComponentUVE>(overlay.imageEntity, image);
    }
    return true;
}

void RunProjectBootSplashUVE(UVE::Core::EngineCoreUVE& engine,
                             const UVE::Pack::ProjectRuntimeSetupUVE& setup) {
    const UVE::Core::EngineConfigUVE& config = engine.GetConfigUVE();
    if (config.headlessUVE || !engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
        return; // A headless run has no surface to display or wait for.
    }
    const auto fadeDuration = std::chrono::duration<double>(config.splashFadeSecondsUVE);
    const auto minimumDuration = std::chrono::duration<double>(config.splashMinimumDisplaySecondsUVE);
    const auto holdEnd = std::max(fadeDuration, minimumDuration);
    const auto bootStart = std::chrono::steady_clock::now();
    const UVE::Core::SimulationExecutionModeUVE previousSimulationMode = engine.GetSimulationExecutionModeUVE();
    static_cast<void>(engine.SetSimulationExecutionModeUVE(UVE::Core::SimulationExecutionModeUVE::Paused));

    ProjectBootSplashOverlayUVE overlay;
    if (!InitializeProjectBootSplashOverlayUVE(engine, setup, overlay)) {
        static_cast<void>(engine.SetSimulationExecutionModeUVE(previousSimulationMode));
        return;
    }
    const auto tickAndPace = [&engine] {
        engine.TickFrameUVE();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    };
    bool skipRequested = false;
    while (!engine.IsQuitRequestedUVE() && std::chrono::steady_clock::now() - bootStart < holdEnd) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - bootStart).count();
        const float visibility = fadeDuration.count() > 0.0
                                     ? static_cast<float>(std::clamp(elapsed / fadeDuration.count(), 0.0, 1.0))
                                     : 1.0F;
        overlay.SetVisibilityUVE(visibility);
        tickAndPace();
        skipRequested = skipRequested ||
                        (config.splashSkippableUVE &&
                         engine.GetServicesUVE().GetInputSystemUVE().WasKeyPressedThisFrameUVE(
                             UVE::Input::KeyCodeUVE::Escape));
    }

    const auto fadeOutStart = std::chrono::steady_clock::now();
    while (!engine.IsQuitRequestedUVE() && !skipRequested && fadeDuration.count() > 0.0 &&
           std::chrono::steady_clock::now() - fadeOutStart < fadeDuration) {
        const double progress =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - fadeOutStart).count() /
            fadeDuration.count();
        overlay.SetVisibilityUVE(static_cast<float>(1.0 - std::clamp(progress, 0.0, 1.0)));
        tickAndPace();
        skipRequested = config.splashSkippableUVE &&
                        engine.GetServicesUVE().GetInputSystemUVE().IsKeyDownUVE(UVE::Input::KeyCodeUVE::Escape);
    }
    static_cast<void>(engine.SetSimulationExecutionModeUVE(previousSimulationMode));
}

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
/// EngineConfigUVE::commandLineArgs. `--help` prints the runtime's supported options and registered
/// engine-setting overrides; `--project <path>` loads a packaged project before engine startup.
///
/// `--ui-overlay-demo [frameCount]` is a Phase U3a diagnostic-only path (not part of the above
/// contract): it drives EngineCoreUVE's lifecycle by hand instead of RunUVE(), authors the small
/// fixture above, and runs for `frameCount` frames (default 3000, ~ a minute at 60 Hz) so a real
/// windowed run under Xvfb stays open long enough for an external xdotool screenshot/click pass.
int main(int argc, char** argv) {
    if (HasRawArgumentUVE(argc, argv, "--help") || HasRawArgumentUVE(argc, argv, "-h")) {
        std::cout << BuildRuntimeCommandLineHelpUVE();
        return 0;
    }

    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = "uve_engine.log";
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);

    // Roadmap item #7: configure all project/application paths before EngineCore startup, then
    // load the manifest's launch scene and run the game. The setup snapshot is reused afterward so
    // the runtime configuration and startup scene cannot come from different manifest revisions.
    const auto projectFlagIt = std::find(config.commandLineArgs.begin(), config.commandLineArgs.end(), "--project");
    if (projectFlagIt != config.commandLineArgs.end()) {
        if (std::count(config.commandLineArgs.begin(), config.commandLineArgs.end(), "--project") != 1) {
            std::cerr << "uve_runtime: --project may only be supplied once\n";
            return 2;
        }
        const auto projectPathIt = std::next(projectFlagIt);
        if (projectPathIt == config.commandLineArgs.end() || projectPathIt->starts_with("--")) {
            std::cerr << "uve_runtime: --project requires a path to a .uvproject file\n";
            return 2;
        }
        const std::filesystem::path projectPath = *projectPathIt;
        config.commandLineArgs.erase(projectFlagIt, std::next(projectPathIt));

        const UVE::Pack::ProjectRuntimeSetupResultUVE setupResult =
            UVE::Pack::PrepareProjectRuntimeUVE(projectPath, config);
        if (!setupResult.IsReadyUVE()) {
            std::cerr << "uve_runtime: " << setupResult.message << '\n';
            return 1;
        }

        UVE::Platform::SingleInstanceLockUVE instanceLock;
        if (config.enforceSingleInstanceUVE) {
            const UVE::Platform::SingleInstanceLockResultUVE lockResult =
                instanceLock.AcquireUVE(config.applicationIdentifierUVE, config.userDataDirectoryPathUVE);
            if (lockResult.code == UVE::Platform::SingleInstanceLockCodeUVE::AlreadyRunning) {
                std::cerr << "uve_runtime: " << lockResult.message << '\n';
                return 2;
            }
            if (lockResult.code == UVE::Platform::SingleInstanceLockCodeUVE::Failed) {
                std::cerr << "uve_runtime: " << lockResult.message << '\n';
                return 1;
            }
            if (lockResult.code == UVE::Platform::SingleInstanceLockCodeUVE::Unsupported) {
                std::cerr << "uve_runtime: warning: " << lockResult.message << '\n';
            }
        }

        UVE::Core::EngineCoreUVE engine(config);
        UVE::Platform::CrashReporterUVE crashReporter;
        try {
            engine.Init();
            const UVE::Core::EngineConfigUVE& effectiveConfig = engine.GetConfigUVE();
            if (effectiveConfig.crashHandlerEnabledUVE) {
                std::filesystem::path crashDirectory = effectiveConfig.crashDumpDirectoryUVE;
                if (crashDirectory.is_relative()) {
                    const bool escapesUserData = std::any_of(
                        crashDirectory.begin(), crashDirectory.end(),
                        [](const std::filesystem::path& component) { return component == ".."; });
                    if (escapesUserData) {
                        std::cerr << "uve_runtime: warning: crash dump path escapes user data; handler not installed\n";
                    } else {
                        crashDirectory = effectiveConfig.userDataDirectoryPathUVE / crashDirectory;
                    }
                }
                if (crashDirectory.is_absolute()) {
                    const UVE::Platform::CrashReporterResultUVE crashResult =
                        crashReporter.InstallUVE(crashDirectory, effectiveConfig.applicationIdentifierUVE);
                    if (!crashResult.IsInstalledUVE()) {
                        std::cerr << "uve_runtime: warning: " << crashResult.message << '\n';
                    }
                }
            }
            if (!engine.Load()) {
                engine.Shutdown();
                return 1;
            }

            const UVE::Pack::ProjectLaunchResultUVE launchResult =
                UVE::Pack::LoadAndActivateProjectSceneUVE(engine, *setupResult.setup);
            if (!launchResult.IsAcceptedUVE()) {
                std::cerr << "uve_runtime: " << launchResult.message << '\n';
                engine.Shutdown();
                return 1;
            }

            RunProjectBootSplashUVE(engine, *setupResult.setup);
            if (engine.IsQuitRequestedUVE()) {
                engine.Shutdown();
                return 0;
            }

            // A CPU-only build can have a Null window manager even when --headless was not passed;
            // treat either case as headless so a packaged run never waits forever for a close event
            // that cannot exist. Bound and pace those runs at a nominal 60 Hz.
            const bool headlessRun = engine.GetConfigUVE().headlessUVE ||
                                     !engine.GetServicesUVE().GetWindowManagerUVE().IsValidUVE();
            constexpr int kMaximumHeadlessFramesUVE = 216000; // one hour at 60 Hz
            int frame = 0;
            while (!engine.IsQuitRequestedUVE() && (!headlessRun || frame < kMaximumHeadlessFramesUVE)) {
                engine.TickFrameUVE();
                ++frame;
                if (headlessRun) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            }
            engine.Shutdown();
            return 0;
        } catch (const std::exception& exception) {
            std::cerr << "uve_runtime: unhandled exception: " << exception.what() << '\n';
        } catch (...) {
            std::cerr << "uve_runtime: unhandled non-standard exception\n";
        }
        if (engine.GetStateUVE() == UVE::Core::EngineStateUVE::Running) {
            try {
                engine.Shutdown();
            } catch (...) {
                std::cerr << "uve_runtime: shutdown also failed while recovering from an exception\n";
            }
        }
        return UVE::Core::EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
    }

    const auto demoFlagIt = std::find(config.commandLineArgs.begin(), config.commandLineArgs.end(),
                                       "--ui-overlay-demo");
    if (demoFlagIt != config.commandLineArgs.end()) {
        int frameCount = 3000;
        const auto frameCountIt = std::next(demoFlagIt);
        if (frameCountIt != config.commandLineArgs.end()) {
            frameCount = std::max(0, std::atoi(frameCountIt->c_str()));
        }
        config.commandLineArgs.erase(demoFlagIt, config.commandLineArgs.end());
        config.windowGlVersionMajor = 4U;
        config.windowGlVersionMinor = 5U;

        UVE::Core::EngineCoreUVE engine(config);
        engine.Init();
        if (!engine.Load()) {
            engine.Shutdown();
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
