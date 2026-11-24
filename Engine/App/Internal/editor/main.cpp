// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/gl_api.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "crash_reporter.h"
#include "engine_core.h"
#include "project_launcher.h"
#include "project_template.h"
#include "project_registry.h"
#include "engine_install.h"
#include "quaternion.h"
#include "scalar.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Views.h"
#include "logging_macros.h"
#include "editor_bridge_stdio.h"
#include "editor.h"
#include "vector3.h"
#include "light_component.h"
#include "transform_component.h"
#include "name_component.h"
#include "i_entity_manager.h"
#include "editor_viewport_host.h"
#include "application_runtime.h"
#include "editor_main.h"
#include "process_launcher.h"

namespace {

/// File name of the editor log. Without a project it is written to the working directory; with one
/// it moves under the project's log directory (see PrepareProjectEditorUVE).
inline constexpr std::string_view kEditorLogFileNameUVE = "uve_editor.log";
/// Built-in shader sources packaged beside the editor executable.
inline constexpr std::string_view kShadersDirectoryNameUVE = "shaders";
/// The level the editor opens when neither --scene nor the project's startup scene names one,
/// relative to the content root.
inline constexpr std::string_view kDefaultViewportFolderUVE = "Viewport";
inline constexpr std::string_view kDefaultViewportSceneFileUVE = "Viewport.uvscene";

[[nodiscard]] std::filesystem::path EditorExecutableDirectoryUVE(const char* const argv0) {
#if !defined(__ANDROID__)
    const std::filesystem::path self = UVE::Platform::CurrentExecutablePathUVE();
    if (!self.empty()) {
        return self.parent_path();
    }
#endif
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(std::filesystem::path(argv0), error);
    return error ? std::filesystem::current_path(error) : executable.parent_path();
}
/// Where levels were saved before the Viewport asset existed, relative to the working directory.
inline constexpr std::string_view kLegacyEditorSceneFileUVE = "editor_scene.uvscene";
inline constexpr std::string_view kProjectOptionUVE = UVE::Pack::kEditorProjectOptionUVE;
/// Accepts upgrading an older project to this engine without asking, for runs with no one to ask.
inline constexpr std::string_view kUpgradeProjectOptionUVE = "--upgrade-project";
/// Prints the engine version and exits; release tooling checks a build against its tag with it.
inline constexpr std::string_view kVersionOptionUVE = "--version";
/// Records opened projects in this registry file instead of the user's own, for isolated runs.
inline constexpr std::string_view kProjectRegistryOptionUVE = UVE::Pack::kEditorProjectRegistryOptionUVE;
inline constexpr float kUpgradeDialogWidthUVE = 520.0F;
inline constexpr float kUpgradeDialogLineSpacingUVE = 6.0F;
/// Process exit code for a command line the editor cannot act on.
inline constexpr int kInvalidCommandLineExitCodeUVE = 2;
/// Process exit code for a project the editor cannot open.
inline constexpr int kProjectUnavailableExitCodeUVE = 1;

struct EditorLaunchOptionsUVE final {
    /// The `.uvproject` to author. Empty keeps the working-directory layout.
    std::filesystem::path projectFile;
    /// Registry that records the opened project; empty uses the current user's registry.
    std::filesystem::path projectRegistry;
    /// Upgrade an older project to this engine without the confirmation dialog.
    bool upgradeProject = false;
    bool printVersion = false;
    /// Set when the command line cannot be acted on; the editor reports it and exits.
    std::optional<std::string> error;
    /// Empty means the project's default Viewport: Content/Viewport/Viewport.uvscene.
    std::filesystem::path scenePath;
    std::optional<int> frameLimit;
    std::optional<std::uint32_t> glMajor;
    std::optional<std::uint32_t> glMinor;
    bool headless = false;
    bool bridgeStdio = false;
    bool decorated = false;
    /// The PRISM editor shell instead of the previous UI backend.
    // The editor's UI is PRISM; "--prism-ui" is still accepted (it was the opt-in while both existed).
    bool prismUi = true;
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
        if (argument == kProjectOptionUVE) {
            if (!options.projectFile.empty()) {
                options.error = std::string(kProjectOptionUVE) + " may only be supplied once";
                return options;
            }
            if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
                options.error = std::string(kProjectOptionUVE) + " requires a path to a .uvproject file";
                return options;
            }
            options.projectFile = argv[++index];
            continue;
        }
        if (argument == kProjectRegistryOptionUVE) {
            if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
                options.error = std::string(kProjectRegistryOptionUVE) + " requires a registry file path";
                return options;
            }
            options.projectRegistry = argv[++index];
            continue;
        }
        if (argument == kVersionOptionUVE) {
            options.printVersion = true;
            continue;
        }
        if (argument == kUpgradeProjectOptionUVE) {
            options.upgradeProject = true;
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
    if (options.upgradeProject && options.projectFile.empty() && !options.error.has_value()) {
        options.error = std::string(kUpgradeProjectOptionUVE) + " requires " + std::string(kProjectOptionUVE);
    }
    if (options.headless && !options.frameLimit.has_value()) {
        options.frameLimit = 1;
    }
    return options;
}

/// Asks whether to upgrade `project` to this engine. Upgrade records the acceptance; Close ends the
/// session without touching the project. Dismissing the dialog keeps the project open un-upgraded.
void ShowProjectUpgradeDialogUVE(const UVE::Pack::ProjectEditorSetupUVE& project, bool& upgradeAccepted,
                                 bool& closeRequested) {
    const std::string projectVersion = UVE::Pack::FormatProjectVersionUVE(project.package.engineVersion);
    const std::string engineVersion = UVE::Core::EngineCoreUVE::GetEngineVersionUVE().ToStringUVE();
    auto body = prism::vbox(kUpgradeDialogLineSpacingUVE);
    body->add(prism::make<prism::Label>("'" + project.package.displayName + "' was saved with engine " +
                                        projectVersion + "."));
    body->add(prism::make<prism::Label>("Upgrade it to engine " + engineVersion + "?"));
    body->add(prism::make<prism::Label>("Once upgraded, older engines may no longer open it."));
    prism::showDialog("Upgrade Project", body,
                      {{"Close", false, [&closeRequested] { closeRequested = true; }},
                       {"Upgrade", true, [&upgradeAccepted] { upgradeAccepted = true; }}},
                      kUpgradeDialogWidthUVE);
}

/// Builds a new project's startup scene from the starter objects its template declared. Objects
/// of a kind the editor does not know are reported and skipped. True when every object was placed.
[[nodiscard]] bool PopulateStarterSceneUVE(UVE::Editor::EditorUVE& editor, UVE::Scene::IEntityManagerUVE& entities,
                                           const UVE::Pack::StarterSceneUVE& scene) {
    bool placedAll = true;
    for (const UVE::Pack::StarterObjectUVE& object : scene.objects) {
        // New objects join the selected object; starter objects all sit at the level's top.
        editor.ClearSelectionUVE();
        const std::optional<UVE::Editor::EditorEntityKindUVE> kind = UVE::Editor::ParseEditorEntityKindUVE(object.kind);
        const UVE::Scene::EntityUVE entity =
            kind.has_value() ? editor.CreateDocumentEntityUVE(*kind) : UVE::Scene::kInvalidEntityUVE;
        if (entity == UVE::Scene::kInvalidEntityUVE) {
            UVE_WARNING("uve_editor: the starter object kind '{}' cannot be created", object.kind);
            placedAll = false;
            continue;
        }
        // A new entity is the selection, so the rename applies to it. A name that is already the
        // kind's default needs no rename.
        const bool keepsDefaultName = !entities.HasComponentUVE<UVE::Scene::NameComponentUVE>(entity) ||
                                      entities.GetComponentUVE<UVE::Scene::NameComponentUVE>(entity).name == object.name;
        if (!object.name.empty() && !keepsDefaultName && !editor.SetSelectedEntityNameUVE(object.name)) {
            UVE_WARNING("uve_editor: the starter object name '{}' is not usable", object.name);
        }
        if (entities.HasComponentUVE<UVE::Scene::TransformComponentUVE>(entity)) {
            auto& transform = entities.GetComponentUVE<UVE::Scene::TransformComponentUVE>(entity);
            transform.localPosition = object.position;
            transform.localScale = object.scale;
            const UVE::Math::Vector3UVE radians{UVE::Math::DegToRadUVE(object.rotationDegrees.x),
                                                UVE::Math::DegToRadUVE(object.rotationDegrees.y),
                                                UVE::Math::DegToRadUVE(object.rotationDegrees.z)};
            UVE::Math::QuaternionUVE rotation{};
            if (UVE::Math::TryMakeEulerOrderedUVE(radians, transform.eulerOrder, rotation)) {
                transform.localEulerRadians = radians;
                transform.localRotation = rotation;
            }
        }
    }
    editor.ClearSelectionUVE();
    return placedAll;
}

} // namespace

/// Starts the standalone UniVex Editor Foundation v1. `--project <file.uvproject>` opens that
/// project: its content root, settings, input map and asset database replace the working-directory
/// defaults, and its startup scene is the level opened when `--scene` is not given. A project saved
/// with a newer engine beyond the compatibility policy is refused; an older one asks before it is
/// upgraded, unless `--upgrade-project` accepts in advance (headless runs never ask). `--scene <path>` selects the `.uvscene`
/// document. `--frames <n>` bounds a run for automation, while the normal windowed invocation runs
/// until the user closes the editor. `--gl-version <major.minor>` overrides the requested desktop
/// OpenGL version for an explicitly chosen platform capability (for example virtual-display CI).
/// `--headless` keeps the editor's non-visual lifecycle usable in CI and defaults to a single frame.
/// `--bridge-stdio` always implies headless mode and runs a framed JSON-RPC bridge server instead
/// of constructing native GLFW presentation for this process.
///
/// The editor drives EngineCoreUVE's lifecycle by hand (Init()/Load()/TickFrameUVE()) rather than
/// through RunUVE() — the one desktop entry point that does not automatically get RunUVE()'s
/// built-in exception boundary (see EngineCoreUVE::RunUVE()'s doc comment) — so this function
/// wraps that entire hand-driven lifecycle in its own boundary below, following the same shape:
/// log via UVE_FATAL, still run Shutdown() if (and only if) the engine had reached
/// EngineStateUVE::Running by the time something threw, and return
/// EngineCoreUVE::kUnhandledExceptionExitCodeUVE instead of letting the exception unwind out of
/// main() into std::terminate().
int UVE::App::RunEditorUVE(const int argc, char** argv, const EditorPlatformUVE& platform) {
    EditorLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);
    if (options.error.has_value() || options.printVersion || options.headless || options.bridgeStdio) {
        UVE::Platform::AttachParentConsoleForCliUVE();
    }
    if (options.error.has_value()) {
        std::cerr << "uve_editor: " << *options.error << '\n';
        return kInvalidCommandLineExitCodeUVE;
    }
    if (options.printVersion) {
        std::cout << UVE::Core::EngineCoreUVE::GetEngineVersionUVE().ToStringUVE() << '\n';
        return 0;
    }

    const std::filesystem::path executableDirectory = EditorExecutableDirectoryUVE(argv[0]);
    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = platform.logDirectory.empty() ? std::filesystem::path(kEditorLogFileNameUVE)
                                                       : platform.logDirectory / kEditorLogFileNameUVE;
    config.nativeWindowHandleUVE = platform.nativeSurface;
    if (!platform.shaderDirectory.empty()) {
        config.shaderSourceRealDirectoryUVE = platform.shaderDirectory;
    } else {
        std::error_code shaderError;
        const std::filesystem::path bundledShaders = executableDirectory / kShadersDirectoryNameUVE;
        if (std::filesystem::is_directory(bundledShaders, shaderError)) {
            config.shaderSourceRealDirectoryUVE = bundledShaders;
        }
    }
    if (!platform.shaderCacheDirectory.empty()) {
        config.shaderCachePath = platform.shaderCacheDirectory;
    }
#if defined(__ANDROID__)
    config.renderBackendPreferenceUVE = UVE::Core::RenderBackendPreferenceUVE::OpenGLUVE;
#endif
    config.enableConsoleLogging = !options.bridgeStdio;
    config.headlessUVE = options.headless;
    config.windowBorderlessUVE = !options.decorated;
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);
    // The editor never needs more than the display shows: as many frames as the monitor refreshes
    // while one of its windows has focus (set once the window exists, below), 15 behind another
    // program's window (a minimised one draws nothing at all), so a laptop's GPU and fan are not
    // spent on a scene nobody is looking at. On Android, EGL VSync paces natively at 60/90/120 Hz.
#if defined(__ANDROID__)
    config.focusedFrameRateCapUVE = 0U;
#else
    config.focusedFrameRateCapUVE = 60U;
#endif
    config.unfocusedFrameRateCapUVE = 15U;
    if (options.glMajor.has_value() && options.glMinor.has_value()) {
        config.windowGlVersionMajor = *options.glMajor;
        config.windowGlVersionMinor = *options.glMinor;
    }

    // A level named on the command line is opened as given; otherwise the editor chooses one and
    // makes sure it exists.
    const bool sceneNamedOnCommandLine = !options.scenePath.empty();
    std::optional<UVE::Pack::ProjectEditorSetupUVE> project;
    if (!options.projectFile.empty()) {
        UVE::Pack::ProjectEditorSetupResultUVE setupResult =
            UVE::Pack::PrepareProjectEditorUVE(options.projectFile, UVE::Core::EngineCoreUVE::GetEngineVersionUVE(),
                                               config);
        if (!setupResult.IsReadyUVE()) {
            std::cerr << "uve_editor: " << setupResult.message << '\n';
            return kProjectUnavailableExitCodeUVE;
        }
        project = std::move(setupResult.setup);
        if (options.scenePath.empty()) {
            options.scenePath = project->startupScene;
        }
    }

    if (options.prismUi && !options.headless) {
        // Interactive PRISM sessions detect external Content/ additions asynchronously in
        // EditorUVE::TickExternalContentWatcherUVE (focus regain + 2s off-thread metadata scan
        // with 300ms write-stability debounce), preventing synchronous SHA-256 hashing of large
        // externally pasted folders on the UI thread inside EngineCoreUVE::TickFrameUVE().
        config.projectChangeWatchPollIntervalSecondsUVE = 86400.0;
    }

    UVE::Core::EngineCoreUVE engine(config);
    UVE::Platform::CrashReporterUVE crashReporter;
    try {
        engine.Init();
        if (!options.headless && !options.bridgeStdio) {
            const UVE::Core::EngineConfigUVE& effectiveConfig = engine.GetConfigUVE();
            if (effectiveConfig.crashHandlerEnabledUVE && !effectiveConfig.userDataDirectoryPathUVE.empty()) {
                const std::filesystem::path dumpDir =
                    effectiveConfig.crashDumpDirectoryUVE.is_absolute()
                        ? effectiveConfig.crashDumpDirectoryUVE
                        : (effectiveConfig.userDataDirectoryPathUVE / effectiveConfig.crashDumpDirectoryUVE);
                static_cast<void>(crashReporter.InstallUVE(dumpDir, "uve_editor"));
            }
        }
        if (!engine.Load()) {
            engine.Shutdown();
            return 1;
        }
        // Vsync paces the frames to the monitor (60, 120, 144 Hz ...); the cap sits a tenth above
        // its refresh, only as a guard for when vsync cannot block (a covered window), so it never
        // fights the display and adds no stutter of its own.
        if (GLFWmonitor* const monitor = glfwGetPrimaryMonitor()) {
            if (const GLFWvidmode* const mode = glfwGetVideoMode(monitor); mode != nullptr && mode->refreshRate >= 30) {
                const auto refresh = static_cast<std::uint32_t>(std::min(mode->refreshRate, 360));
                engine.SetFrameRateCapsUVE(refresh + refresh / 10U, 15U);
            }
        }

        // The level is a Viewport asset in the Asset Table: Content/Viewport/Viewport.uvscene
        // unless --scene or the project's startup scene names another. Outside a project, a level
        // saved at the old working-directory default moves there once.
        bool createDefaultViewport = false;
        if (options.scenePath.empty()) {
            options.scenePath = engine.GetServicesUVE().GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot /
                                kDefaultViewportFolderUVE / kDefaultViewportSceneFileUVE;
        }
        if (!sceneNamedOnCommandLine) {
            std::error_code error;
            std::filesystem::create_directories(options.scenePath.parent_path(), error);
            if (!std::filesystem::exists(options.scenePath, error)) {
                const std::filesystem::path legacyScene{kLegacyEditorSceneFileUVE};
                if (!project.has_value() && std::filesystem::exists(legacyScene, error)) {
                    std::filesystem::copy_file(legacyScene, options.scenePath, error);
                } else {
                    createDefaultViewport = true;
                }
            }
        }

        UVE::Editor::EditorUVE editor(engine.GetServicesUVE(), options.scenePath, 100U, &engine);
        if (project.has_value()) {
            editor.SetProjectFilePathUVE(project->projectFile);
        }
        if (options.prismUi && !options.headless) {
            // PRISM's fonts / themes / locales ship next to the executable (UVBT copies them).
            std::error_code assetError;
            std::filesystem::path assetDir = executableDirectory / "prism_assets";
            if (!platform.prismAssetsDirectory.empty()) {
                assetDir = platform.prismAssetsDirectory;
            } else if (!std::filesystem::is_directory(assetDir, assetError)) {
                assetDir = "prism_assets";
            }
            editor.SetPrismUiEnabledUVE(true, assetDir);
            if (platform.safeArea) {
                editor.SetSafeAreaCallbackUVE(platform.safeArea);
            }
        }
        editor.InitUVE();
        if (project.has_value() &&
            project->compatibility.verdict == UVE::Pack::ProjectCompatibilityVerdictUVE::NewerCompatible) {
            UVE_WARNING("uve_editor: {}", project->compatibility.message);
        }

        if (project.has_value()) {
            std::string registryError;
            const std::optional<std::filesystem::path> registryFile =
                options.projectRegistry.empty() ? UVE::Pack::DefaultProjectRegistryFileUVE(&registryError)
                                                : std::optional<std::filesystem::path>(options.projectRegistry);
            if (!registryFile.has_value()) {
                UVE_WARNING("uve_editor: the recent-projects list is unavailable: {}", registryError);
            } else if (const UVE::Pack::ProjectRegistryResultUVE recorded =
                           UVE::Pack::RecordProjectOpenedUVE(*registryFile, project->projectFile, project->package);
                       !recorded.IsAppliedUVE()) {
                UVE_WARNING("uve_editor: could not record the project in the recent-projects list: {}",
                            recorded.message);
            }
        }

        bool upgradeAccepted = options.upgradeProject;
        bool closeRequested = false;
        if (project.has_value() && project->compatibility.NeedsUpgradeConfirmationUVE() && !upgradeAccepted &&
            editor.IsPrismUiActiveUVE()) {
            ShowProjectUpgradeDialogUVE(*project, upgradeAccepted, closeRequested);
        }

        if (std::filesystem::exists(options.scenePath)) {
            static_cast<void>(editor.LoadSceneUVE());
        } else if (createDefaultViewport) {
            // A project created from a template begins with the objects the template listed.
            bool starterSceneBuilt = false;
            if (project.has_value()) {
                const UVE::Pack::StarterSceneLoadResultUVE starter =
                    UVE::Pack::LoadProjectStarterSceneUVE(project->projectRoot);
                if (starter.code == UVE::Pack::StarterSceneCodeUVE::Invalid) {
                    UVE_WARNING("uve_editor: {}", starter.message);
                } else if (starter.code == UVE::Pack::StarterSceneCodeUVE::Loaded) {
                    static_cast<void>(PopulateStarterSceneUVE(
                        editor, engine.GetServicesUVE().GetEntityManagerUVE(), starter.scene));
                    starterSceneBuilt = true;
                }
            }
            // The default Viewport asset exists from the start.
            if (editor.SaveSceneUVE() && starterSceneBuilt &&
                !UVE::Pack::RemoveProjectStarterSceneUVE(project->projectRoot)) {
                UVE_WARNING("uve_editor: could not remove the consumed starter scene from {}",
                            project->projectRoot.string());
            }
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
        // gameplay camera), so each editor window's 3D view comes from its dedicated viewport
        // owned by EditorViewportHostUVE: level_viewport, camera_preview_viewport,
        // entity_viewport, widget_preview_viewport, retarget_viewport, montage_viewport, and
        // vfx_viewport. Only constructed in real windowed mode: it needs an actual current GL
        // context, which headless mode's NullRenderDeviceUVE never creates.
        std::unique_ptr<UVE::App::Viewports::EditorViewportHostUVE> viewportHost;
        editor.SetDocumentSceneFactoryUVE([&engine] { return engine.CreateDocumentSceneUVE(); });
        editor.SetDocumentSceneClosingUVE([&viewportHost](const UVE::Core::EngineServicesUVE& services) {
            if (viewportHost != nullptr) {
                viewportHost->ForgetServicesUVE(services);
            }
        });
        if (!options.headless) {
            viewportHost = std::make_unique<UVE::App::Viewports::EditorViewportHostUVE>(editor, engine);
            viewportHost->BindToEditorUVE(editor);
        }
        engine.SetPostRenderCallbackUVE([&editor] { editor.RenderOverlayUVE(); });

        int framesRun = 0;
        while (!closeRequested && !engine.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!options.frameLimit.has_value() || framesRun < *options.frameLimit)) {
            if (platform.beforeFrame && !platform.beforeFrame(engine)) {
                break;
            }
            editor.TickUVE();
            engine.TickFrameUVE();
            ++framesRun;
        }

        if (!options.prismScreenshot.empty() && !editor.SavePrismScreenshotUVE(options.prismScreenshot)) {
            std::fprintf(stderr, "uve_editor: could not save the PRISM screenshot\n");
        }
        engine.SetPostRenderCallbackUVE({});
        if (viewportHost != nullptr) {
            viewportHost->UnbindFromEditorUVE(editor);
        }
        // Destroyed here, before engine.Shutdown() tears down the window/GL context below - its
        // destructor deletes real GL objects (framebuffers/textures) that must still be valid.
        viewportHost.reset();
        editor.ShutdownUVE();
        if (project.has_value() &&
            UVE::Pack::ShouldRecordEngineVersionUVE(project->compatibility.verdict, upgradeAccepted)) {
            const UVE::Platform::EditorProjectPackageResultUVE recorded = UVE::Pack::RecordProjectEngineVersionUVE(
                project->projectFile, UVE::Core::EngineCoreUVE::GetEngineVersionUVE());
            if (!recorded.IsAcceptedUVE()) {
                UVE_WARNING("uve_editor: could not record the engine version in {}: {}",
                            project->projectFile.string(), recorded.message);
            }
        }
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

#if !defined(__ANDROID__)
int main(const int argc, char** argv) {
    return UVE::App::RunEditorUVE(argc, argv, {});
}
#endif
