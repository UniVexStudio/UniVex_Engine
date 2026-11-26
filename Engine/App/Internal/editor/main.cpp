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
/// it moves under the project's log directory (see PrepareProjectEditor).
inline constexpr std::string_view kEditorLogFileName = "uve_editor.log";
/// Built-in shader sources packaged beside the editor executable.
inline constexpr std::string_view kShadersDirectoryName = "shaders";
/// The level the editor opens when neither --scene nor the project's startup scene names one,
/// relative to the content root.
inline constexpr std::string_view kDefaultViewportFolder = "Viewport";
inline constexpr std::string_view kDefaultViewportSceneFile = "Viewport.uvscene";

[[nodiscard]] std::filesystem::path EditorExecutableDirectory(const char* const argv0) {
#if !defined(__ANDROID__)
    const std::filesystem::path self = UVE::Platform::CurrentExecutablePath();
    if (!self.empty()) {
        return self.parent_path();
    }
#endif
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(std::filesystem::path(argv0), error);
    return error ? std::filesystem::current_path(error) : executable.parent_path();
}
/// Where levels were saved before the Viewport asset existed, relative to the working directory.
inline constexpr std::string_view kLegacyEditorSceneFile = "editor_scene.uvscene";
inline constexpr std::string_view kProjectOption = UVE::Pack::kEditorProjectOption;
/// Accepts upgrading an older project to this engine without asking, for runs with no one to ask.
inline constexpr std::string_view kUpgradeProjectOption = "--upgrade-project";
/// Prints the engine version and exits; release tooling checks a build against its tag with it.
inline constexpr std::string_view kVersionOption = "--version";
/// Records opened projects in this registry file instead of the user's own, for isolated runs.
inline constexpr std::string_view kProjectRegistryOption = UVE::Pack::kEditorProjectRegistryOption;
inline constexpr float kUpgradeDialogWidth = 520.0F;
inline constexpr float kUpgradeDialogLineSpacing = 6.0F;
/// Process exit code for a command line the editor cannot act on.
inline constexpr int kInvalidCommandLineExitCode = 2;
/// Process exit code for a project the editor cannot open.
inline constexpr int kProjectUnavailableExitCode = 1;

struct EditorLaunchOptions final {
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
    int prismDemoGrid = 0;
    bool prismDemoGizmos = false;
};

[[nodiscard]] bool ParseGlVersion(const std::string_view value, std::uint32_t& major,
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

[[nodiscard]] EditorLaunchOptions ParseOptions(const int argc, char** argv) {
    EditorLaunchOptions options{};
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
        if (argument == "--prism-demo-grid" && index + 1 < argc) {
            int side = 0;
            const std::string_view text{argv[index + 1]};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), side);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || side < 1 || side > 64) {
                options.error = "--prism-demo-grid requires a layer count from 1 to 64";
                return options;
            }
            options.prismDemoGrid = side;
            options.prismDemoObject = true;
            ++index;
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
        if (argument == kProjectOption) {
            if (!options.projectFile.empty()) {
                options.error = std::string(kProjectOption) + " may only be supplied once";
                return options;
            }
            if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
                options.error = std::string(kProjectOption) + " requires a path to a .uvproject file";
                return options;
            }
            options.projectFile = argv[++index];
            continue;
        }
        if (argument == kProjectRegistryOption) {
            if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
                options.error = std::string(kProjectRegistryOption) + " requires a registry file path";
                return options;
            }
            options.projectRegistry = argv[++index];
            continue;
        }
        if (argument == kVersionOption) {
            options.printVersion = true;
            continue;
        }
        if (argument == kUpgradeProjectOption) {
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
            if (ParseGlVersion(argv[++index], major, minor)) {
                options.glMajor = major;
                options.glMinor = minor;
            }
        }
    }
    if (options.upgradeProject && options.projectFile.empty() && !options.error.has_value()) {
        options.error = std::string(kUpgradeProjectOption) + " requires " + std::string(kProjectOption);
    }
    if (options.headless && !options.frameLimit.has_value()) {
        options.frameLimit = 1;
    }
    return options;
}

/// Asks whether to upgrade `project` to this engine. Upgrade records the acceptance; Close ends the
/// session without touching the project. Dismissing the dialog keeps the project open un-upgraded.
void ShowProjectUpgradeDialog(const UVE::Pack::ProjectEditorSetup& project, bool& upgradeAccepted,
                                 bool& closeRequested) {
    const std::string projectVersion = UVE::Pack::FormatProjectVersion(project.package.engineVersion);
    const std::string engineVersion = UVE::Core::EngineCore::GetEngineVersion().ToString();
    auto body = prism::vbox(kUpgradeDialogLineSpacing);
    body->add(prism::make<prism::Label>("'" + project.package.displayName + "' was saved with engine " +
                                        projectVersion + "."));
    body->add(prism::make<prism::Label>("Upgrade it to engine " + engineVersion + "?"));
    body->add(prism::make<prism::Label>("Once upgraded, older engines may no longer open it."));
    prism::showDialog("Upgrade Project", body,
                      {{"Close", false, [&closeRequested] { closeRequested = true; }},
                       {"Upgrade", true, [&upgradeAccepted] { upgradeAccepted = true; }}},
                      kUpgradeDialogWidth);
}

/// Builds a new project's startup scene from the starter objects its template declared. Objects
/// of a kind the editor does not know are reported and skipped. True when every object was placed.
[[nodiscard]] bool PopulateStarterScene(UVE::Editor::Editor& editor, UVE::Scene::IEntityManager& entities,
                                           const UVE::Pack::StarterScene& scene) {
    bool placedAll = true;
    for (const UVE::Pack::StarterObject& object : scene.objects) {
        // New objects join the selected object; starter objects all sit at the level's top.
        editor.ClearSelection();
        const std::optional<UVE::Editor::EditorEntityKind> kind = UVE::Editor::ParseEditorEntityKind(object.kind);
        const UVE::Scene::Entity entity =
            kind.has_value() ? editor.CreateDocumentEntity(*kind) : UVE::Scene::kInvalidEntity;
        if (entity == UVE::Scene::kInvalidEntity) {
            UVE_WARNING("uve_editor: the starter object kind '{}' cannot be created", object.kind);
            placedAll = false;
            continue;
        }
        // A new entity is the selection, so the rename applies to it. A name that is already the
        // kind's default needs no rename.
        const bool keepsDefaultName = !entities.HasComponent<UVE::Scene::NameComponent>(entity) ||
                                      entities.GetComponent<UVE::Scene::NameComponent>(entity).name == object.name;
        if (!object.name.empty() && !keepsDefaultName && !editor.SetSelectedEntityName(object.name)) {
            UVE_WARNING("uve_editor: the starter object name '{}' is not usable", object.name);
        }
        if (entities.HasComponent<UVE::Scene::TransformComponent>(entity)) {
            auto& transform = entities.GetComponent<UVE::Scene::TransformComponent>(entity);
            transform.localPosition = object.position;
            transform.localScale = object.scale;
            const UVE::Math::Vector3 radians{UVE::Math::DegToRad(object.rotationDegrees.x),
                                                UVE::Math::DegToRad(object.rotationDegrees.y),
                                                UVE::Math::DegToRad(object.rotationDegrees.z)};
            UVE::Math::Quaternion rotation{};
            if (UVE::Math::TryMakeEulerOrdered(radians, transform.eulerOrder, rotation)) {
                transform.localEulerRadians = radians;
                transform.localRotation = rotation;
            }
        }
    }
    editor.ClearSelection();
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
/// The editor drives EngineCore's lifecycle by hand (Init()/Load()/TickFrame()) rather than
/// through Run() — the one desktop entry point that does not automatically get Run()'s
/// built-in exception boundary (see EngineCore::Run()'s doc comment) — so this function
/// wraps that entire hand-driven lifecycle in its own boundary below, following the same shape:
/// log via UVE_FATAL, still run Shutdown() if (and only if) the engine had reached
/// EngineState::Running by the time something threw, and return
/// EngineCore::kUnhandledExceptionExitCode instead of letting the exception unwind out of
/// main() into std::terminate().
int UVE::App::RunEditor(const int argc, char** argv, const EditorPlatform& platform) {
    EditorLaunchOptions options = ParseOptions(argc, argv);
    if (options.error.has_value() || options.printVersion || options.headless || options.bridgeStdio) {
        UVE::Platform::AttachParentConsoleForCli();
    }
    if (options.error.has_value()) {
        std::cerr << "uve_editor: " << *options.error << '\n';
        return kInvalidCommandLineExitCode;
    }
    if (options.printVersion) {
        std::cout << UVE::Core::EngineCore::GetEngineVersion().ToString() << '\n';
        return 0;
    }

    const std::filesystem::path executableDirectory = EditorExecutableDirectory(argv[0]);
    UVE::Core::EngineConfig config{};
    config.logFilePath = platform.logDirectory.empty() ? std::filesystem::path(kEditorLogFileName)
                                                       : platform.logDirectory / kEditorLogFileName;
    config.nativeWindowHandle = platform.nativeSurface;
    if (!platform.shaderDirectory.empty()) {
        config.shaderSourceRealDirectory = platform.shaderDirectory;
    } else {
        std::error_code shaderError;
        const std::filesystem::path bundledShaders = executableDirectory / kShadersDirectoryName;
        if (std::filesystem::is_directory(bundledShaders, shaderError)) {
            config.shaderSourceRealDirectory = bundledShaders;
        }
    }
    if (!platform.shaderCacheDirectory.empty()) {
        config.shaderCachePath = platform.shaderCacheDirectory;
    }
#if defined(__ANDROID__)
    config.renderBackendPreference = UVE::Core::RenderBackendPreference::OpenGL;
#endif
    config.enableConsoleLogging = !options.bridgeStdio;
    config.headless = options.headless;
    config.windowBorderless = !options.decorated;
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);
    // The editor never needs more than the display shows: as many frames as the monitor refreshes
    // while one of its windows has focus (set once the window exists, below), 15 behind another
    // program's window (a minimised one draws nothing at all), so a laptop's GPU and fan are not
    // spent on a scene nobody is looking at. On Android, EGL VSync paces natively at 60/90/120 Hz.
#if defined(__ANDROID__)
    config.focusedFrameRateCap = 0U;
#else
    config.focusedFrameRateCap = 60U;
#endif
    config.unfocusedFrameRateCap = 15U;
    if (options.glMajor.has_value() && options.glMinor.has_value()) {
        config.windowGlVersionMajor = *options.glMajor;
        config.windowGlVersionMinor = *options.glMinor;
    }

    // A level named on the command line is opened as given; otherwise the editor chooses one and
    // makes sure it exists.
    const bool sceneNamedOnCommandLine = !options.scenePath.empty();
    std::optional<UVE::Pack::ProjectEditorSetup> project;
    if (!options.projectFile.empty()) {
        UVE::Pack::ProjectEditorSetupResult setupResult =
            UVE::Pack::PrepareProjectEditor(options.projectFile, UVE::Core::EngineCore::GetEngineVersion(),
                                               config);
        if (!setupResult.IsReady()) {
            std::cerr << "uve_editor: " << setupResult.message << '\n';
            return kProjectUnavailableExitCode;
        }
        project = std::move(setupResult.setup);
        if (options.scenePath.empty()) {
            options.scenePath = project->startupScene;
        }
    }

    if (options.prismUi && !options.headless) {
        // Interactive PRISM sessions detect external Content/ additions asynchronously in
        // Editor::TickExternalContentWatcher (focus regain + 2s off-thread metadata scan
        // with 300ms write-stability debounce), preventing synchronous SHA-256 hashing of large
        // externally pasted folders on the UI thread inside EngineCore::TickFrame().
        config.projectChangeWatchPollIntervalSeconds = 86400.0;
    }

    UVE::Core::EngineCore engine(config);
    UVE::Platform::CrashReporter crashReporter;
    try {
        engine.Init();
        if (!options.headless && !options.bridgeStdio) {
            const UVE::Core::EngineConfig& effectiveConfig = engine.GetConfig();
            if (effectiveConfig.crashHandlerEnabled && !effectiveConfig.userDataDirectoryPath.empty()) {
                const std::filesystem::path dumpDir =
                    effectiveConfig.crashDumpDirectory.is_absolute()
                        ? effectiveConfig.crashDumpDirectory
                        : (effectiveConfig.userDataDirectoryPath / effectiveConfig.crashDumpDirectory);
                static_cast<void>(crashReporter.Install(dumpDir, "uve_editor"));
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
                engine.SetFrameRateCaps(refresh + refresh / 10U, 15U);
            }
        }

        // The level is a Viewport asset in the Asset Table: Content/Viewport/Viewport.uvscene
        // unless --scene or the project's startup scene names another. Outside a project, a level
        // saved at the old working-directory default moves there once.
        bool createDefaultViewport = false;
        if (options.scenePath.empty()) {
            options.scenePath = engine.GetServices().GetProjectFileIndex().GetSnapshot().contentRoot /
                                kDefaultViewportFolder / kDefaultViewportSceneFile;
        }
        if (!sceneNamedOnCommandLine) {
            std::error_code error;
            std::filesystem::create_directories(options.scenePath.parent_path(), error);
            if (!std::filesystem::exists(options.scenePath, error)) {
                const std::filesystem::path legacyScene{kLegacyEditorSceneFile};
                if (!project.has_value() && std::filesystem::exists(legacyScene, error)) {
                    std::filesystem::copy_file(legacyScene, options.scenePath, error);
                } else {
                    createDefaultViewport = true;
                }
            }
        }

        UVE::Editor::Editor editor(engine.GetServices(), options.scenePath, 100U, &engine);
        if (project.has_value()) {
            editor.SetProjectFilePath(project->projectFile);
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
            editor.SetPrismUiEnabled(true, assetDir);
            if (platform.safeArea) {
                editor.SetSafeAreaCallback(platform.safeArea);
            }
        }
        editor.Init();
        if (project.has_value() &&
            project->compatibility.verdict == UVE::Pack::ProjectCompatibilityVerdict::NewerCompatible) {
            UVE_WARNING("uve_editor: {}", project->compatibility.message);
        }

        if (project.has_value()) {
            std::string registryError;
            const std::optional<std::filesystem::path> registryFile =
                options.projectRegistry.empty() ? UVE::Pack::DefaultProjectRegistryFile(&registryError)
                                                : std::optional<std::filesystem::path>(options.projectRegistry);
            if (!registryFile.has_value()) {
                UVE_WARNING("uve_editor: the recent-projects list is unavailable: {}", registryError);
            } else if (const UVE::Pack::ProjectRegistryResult recorded =
                           UVE::Pack::RecordProjectOpened(*registryFile, project->projectFile, project->package);
                       !recorded.IsApplied()) {
                UVE_WARNING("uve_editor: could not record the project in the recent-projects list: {}",
                            recorded.message);
            }
        }

        bool upgradeAccepted = options.upgradeProject;
        bool closeRequested = false;
        if (project.has_value() && project->compatibility.NeedsUpgradeConfirmation() && !upgradeAccepted &&
            editor.IsPrismUiActive()) {
            ShowProjectUpgradeDialog(*project, upgradeAccepted, closeRequested);
        }

        if (std::filesystem::exists(options.scenePath)) {
            static_cast<void>(editor.LoadScene());
        } else if (createDefaultViewport) {
            // A project created from a template begins with the objects the template listed.
            bool starterSceneBuilt = false;
            if (project.has_value()) {
                const UVE::Pack::StarterSceneLoadResult starter =
                    UVE::Pack::LoadProjectStarterScene(project->projectRoot);
                if (starter.code == UVE::Pack::StarterSceneCode::Invalid) {
                    UVE_WARNING("uve_editor: {}", starter.message);
                } else if (starter.code == UVE::Pack::StarterSceneCode::Loaded) {
                    static_cast<void>(PopulateStarterScene(
                        editor, engine.GetServices().GetEntityManager(), starter.scene));
                    starterSceneBuilt = true;
                }
            }
            // The default Viewport asset exists from the start.
            if (editor.SaveScene() && starterSceneBuilt &&
                !UVE::Pack::RemoveProjectStarterScene(project->projectRoot)) {
                UVE_WARNING("uve_editor: could not remove the consumed starter scene from {}",
                            project->projectRoot.string());
            }
        }
        if (options.prismDemoObject) {
            static_cast<void>(editor.CreateDocumentSceneObject(UVE::Scene::Objects::SceneObjectKind::Folder));
            const UVE::Scene::Entity sun =
                editor.CreateDocumentSceneObject(UVE::Scene::Objects::SceneObjectKind::DirectionalLight3D);
            const UVE::Scene::Entity camera =
                editor.CreateDocumentSceneObject(UVE::Scene::Objects::SceneObjectKind::Camera3D);
            if (options.prismDemoGizmos) {
                // The sun and the camera beside the box rather than inside it.
                UVE::Scene::IEntityManager& placed = engine.GetServices().GetEntityManager();
                const auto place = [&placed](const UVE::Scene::Entity entity, const UVE::Math::Vector3 at) {
                    if (entity != UVE::Scene::kInvalidEntity &&
                        placed.HasComponent<UVE::Scene::TransformComponent>(entity)) {
                        placed.GetComponent<UVE::Scene::TransformComponent>(entity).localPosition = at;
                    }
                };
                place(sun, UVE::Math::Vector3{2.5F, 2.0F, 2.5F});
                place(camera, UVE::Math::Vector3{-2.5F, 1.0F, 2.0F});
                // One of each object with a gizmo of its own, spread out along X.
                using Kind = UVE::Scene::Objects::SceneObjectKind;
                const std::array<Kind, 7> kinds{Kind::Light3D, Kind::Decal3D, Kind::Marker3D,
                                                   Kind::SpawnPoint3D, Kind::RayCast3D, Kind::SpringArm3D,
                                                   Kind::NavMeshVolume3D};
                UVE::Scene::IEntityManager& entities = engine.GetServices().GetEntityManager();
                float x = -6.0F;
                for (const Kind kind : kinds) {
                    const UVE::Scene::Entity entity =
                        editor.CreateDocumentSceneObject(kind, editor.GetDocumentViewport());
                    if (entity != UVE::Scene::kInvalidEntity &&
                        entities.HasComponent<UVE::Scene::TransformComponent>(entity)) {
                        entities.GetComponent<UVE::Scene::TransformComponent>(entity).localPosition =
                            UVE::Math::Vector3{x, 1.0F, -3.0F};
                    }
                    // The light as a spot, so its cone shows.
                    if (kind == Kind::Light3D && entities.HasComponent<UVE::Scene::LightComponent>(entity)) {
                        auto& light = entities.GetComponent<UVE::Scene::LightComponent>(entity);
                        light.type = UVE::Scene::LightType::Spot;
                        light.range = 4.0F;
                        light.spotAngleDegrees = 30.0F;
                    }
                    x += 2.0F;
                }
            }
            static_cast<void>(editor.CreateDocumentSceneObject(UVE::Scene::Objects::SceneObjectKind::BoxMesh3D));
            if (options.prismDemoGrid > 0) {
                // A stack of large overlapping boxes at staggered depths. Many small boxes would
                // measure per-draw setup instead; this measures the thing a fill-rate change
                // changes - how much screen area every pass has to shade.
                UVE::Scene::IEntityManager& entities = engine.GetServices().GetEntityManager();
                const int count = options.prismDemoGrid;
                for (int index = 0; index < count; ++index) {
                    const UVE::Scene::Entity entity = editor.CreateDocumentSceneObject(
                        UVE::Scene::Objects::SceneObjectKind::BoxMesh3D, editor.GetDocumentViewport());
                    if (entity == UVE::Scene::kInvalidEntity ||
                        !entities.HasComponent<UVE::Scene::TransformComponent>(entity)) {
                        continue;
                    }
                    auto& transform = entities.GetComponent<UVE::Scene::TransformComponent>(entity);
                    const float stagger = static_cast<float>(index);
                    transform.localPosition = UVE::Math::Vector3{
                        0.05F * static_cast<float>((index * 37) % 7) - 0.15F,
                        0.05F * static_cast<float>((index * 53) % 7) - 0.15F,
                        -0.35F * stagger};
                    transform.localScale = UVE::Math::Vector3{5.0F, 5.0F, 5.0F};
                }
                // The per-object selection outline is editor overlay work that scales with the
                // grid and would sit on top of whatever the renderer does; leave it out of the
                // measurement.
                editor.ClearSelection();
            }
        }

        if (options.bridgeStdio) {
            UVE::Asset::DataTableRegistry dataTableRegistry;
            UVE::Editor::EditorBridge bridge(editor, &dataTableRegistry);
            UVE::Editor::EditorBridgeStdioServer server(bridge);
            const int result = server.Serve(std::cin, std::cout, std::cerr);
            editor.Shutdown();
            engine.Shutdown();
            return result;
        }

        // EngineCore's own scene render stays a documented no-op (the editor doesn't own a
        // gameplay camera), so each editor window's 3D view comes from its dedicated viewport
        // owned by EditorViewportHost: level_viewport, camera_preview_viewport,
        // entity_viewport, widget_preview_viewport, retarget_viewport, montage_viewport, and
        // vfx_viewport. Only constructed in real windowed mode: it needs an actual current GL
        // context, which headless mode's NullRenderDevice never creates.
        std::unique_ptr<UVE::App::Viewports::EditorViewportHost> viewportHost;
        editor.SetDocumentSceneFactory([&engine] { return engine.CreateDocumentScene(); });
        editor.SetDocumentSceneClosing([&viewportHost](const UVE::Core::EngineServices& services) {
            if (viewportHost != nullptr) {
                viewportHost->ForgetServices(services);
            }
        });
        if (!options.headless) {
            viewportHost = std::make_unique<UVE::App::Viewports::EditorViewportHost>(editor, engine);
            viewportHost->BindToEditor(editor);
        }
        engine.SetPostRenderCallback([&editor] { editor.RenderOverlay(); });

        int framesRun = 0;
        while (!closeRequested && !engine.GetServices().GetWindowManager().IsCloseRequested() &&
               (!options.frameLimit.has_value() || framesRun < *options.frameLimit)) {
            if (platform.beforeFrame && !platform.beforeFrame(engine)) {
                break;
            }
            editor.Tick();
            engine.TickFrame();
            ++framesRun;
        }

        if (!options.prismScreenshot.empty() && !editor.SavePrismScreenshot(options.prismScreenshot)) {
            std::fprintf(stderr, "uve_editor: could not save the PRISM screenshot\n");
        }
        engine.SetPostRenderCallback({});
        if (viewportHost != nullptr) {
            viewportHost->UnbindFromEditor(editor);
        }
        // Destroyed here, before engine.Shutdown() tears down the window/GL context below - its
        // destructor deletes real GL objects (framebuffers/textures) that must still be valid.
        viewportHost.reset();
        editor.Shutdown();
        if (project.has_value() &&
            UVE::Pack::ShouldRecordEngineVersion(project->compatibility.verdict, upgradeAccepted)) {
            const UVE::Platform::EditorProjectPackageResult recorded = UVE::Pack::RecordProjectEngineVersion(
                project->projectFile, UVE::Core::EngineCore::GetEngineVersion());
            if (!recorded.IsAccepted()) {
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
    // actually reached Running - see EngineCore::Run()'s doc comment for why an exception
    // during Init() itself must not force a Shutdown() call. `editor` (and any object declared
    // inside the try block above) is already out of scope here, having been destroyed normally
    // during stack unwinding.
    if (engine.GetState() == UVE::Core::EngineState::Running) {
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
    return UVE::Core::EngineCore::kUnhandledExceptionExitCode;
}

#if !defined(__ANDROID__)
int main(const int argc, char** argv) {
    return UVE::App::RunEditor(argc, argv, {});
}
#endif
