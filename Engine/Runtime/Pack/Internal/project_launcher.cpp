// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_launcher.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "png_metadata.h"
#include "engine_core.h"
#include "application_runtime.h"
#include "editor_project_package.h"
#include "camera_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "camera_3d.h"
#include "i_scene_graph.h"
#include "scene_serializer.h"

namespace UVE::Pack {
namespace {

[[nodiscard]] ProjectLaunchResult MakeLaunchResult(const ProjectLaunchCode code, std::string message) {
    return {code, std::move(message)};
}

[[nodiscard]] ProjectRuntimeSetupResult MakeSetupResult(const ProjectRuntimeSetupCode code,
                                                              std::string message) {
    return {code, std::move(message), std::nullopt};
}

[[nodiscard]] std::optional<Scene::Entity> FindPlayCameraEntity(Scene::IEntityManager& entityManager) {
    return Scene::FindCurrentCameraEntity(entityManager);
}

[[nodiscard]] bool ReadBoundedFile(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                                      std::string& errorMessage) {
    constexpr std::uintmax_t kMaximumIconFileBytes = 64ULL * 1024ULL * 1024ULL;
    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error || fileSize == 0U || fileSize > kMaximumIconFileBytes ||
        fileSize > static_cast<std::uintmax_t>(bytes.max_size())) {
        errorMessage = "The configured PNG icon is missing, empty, or exceeds its file-size bound: " + path.string();
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "Unable to open the configured PNG icon: " + path.string();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(fileSize));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || input.bad()) {
        errorMessage = "Unable to read the complete configured PNG icon: " + path.string();
        bytes.clear();
        return false;
    }
    return true;
}

[[nodiscard]] bool ResolveContainedPath(const std::filesystem::path& root,
                                           const std::filesystem::path& candidate,
                                           std::filesystem::path& resolvedPath) {
    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::canonical(root, error);
    if (error) {
        return false;
    }
    resolvedPath = std::filesystem::canonical(candidate, error);
    if (error) {
        return false;
    }
    if (resolvedPath == canonicalRoot) {
        return true;
    }
    const std::filesystem::path relative = resolvedPath.lexically_relative(canonicalRoot);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] bool ResolveContainedRegularFile(const std::filesystem::path& root,
                                                  const std::filesystem::path& candidate,
                                                  std::filesystem::path& resolvedPath) {
    std::error_code error;
    return ResolveContainedPath(root, candidate, resolvedPath) &&
           std::filesystem::is_regular_file(resolvedPath, error) && !error;
}

[[nodiscard]] bool ResolveContainedDirectory(const std::filesystem::path& root,
                                                const std::filesystem::path& candidate,
                                                std::filesystem::path& resolvedPath) {
    std::error_code error;
    return ResolveContainedPath(root, candidate, resolvedPath) &&
           std::filesystem::is_directory(resolvedPath, error) && !error;
}

[[nodiscard]] std::string LowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool LoadCurrentTargetWindowIcons(const Platform::EditorProjectPackage& package,
                                                   const std::filesystem::path& contentRoot,
                                                   std::vector<Core::EngineApplicationIconImage>& outIcons,
                                                   std::string& errorMessage) {
    const auto targetIt = package.applicationSettings.iconPathsByTarget.find(
        std::string{Platform::GetApplicationTarget()});
    if (targetIt == package.applicationSettings.iconPathsByTarget.end()) {
        return true;
    }

    constexpr std::uint64_t kMaximumDecodedIconBytes = 64ULL * 1024ULL * 1024ULL;
    for (const auto& [pixelSize, relativePath] : targetIt->second) {
        // ICO/ICNS are retained for native packaging; the cross-platform GLFW window backend
        // accepts decoded RGBA pixels, so only PNGs are applied directly to the live window.
        if (LowercaseExtension(relativePath) != ".png") {
            continue;
        }
        const std::filesystem::path requestedIconPath = contentRoot / relativePath;
        std::filesystem::path iconPath;
        if (!ResolveContainedRegularFile(contentRoot, requestedIconPath, iconPath)) {
            errorMessage = "The configured PNG icon must be a regular file under the project content root: " +
                           requestedIconPath.string();
            return false;
        }
        std::vector<std::byte> fileBytes;
        if (!ReadBoundedFile(iconPath, fileBytes, errorMessage)) {
            return false;
        }
        const std::optional<Asset::PngMetadata> metadata = Asset::ParsePngMetadata(fileBytes);
        if (!metadata.has_value() || metadata->width != pixelSize || metadata->height != pixelSize ||
            !Asset::ValidatePngRgba8PixelBudget(*metadata, kMaximumDecodedIconBytes)) {
            errorMessage = "A configured PNG icon must be a valid square image matching its declared pixel size: " +
                           iconPath.string();
            return false;
        }
        Asset::PngRgba8Image decoded;
        if (!Asset::DecodePngRgba8Image(fileBytes, decoded) || decoded.width != pixelSize ||
            decoded.height != pixelSize) {
            errorMessage = "Unable to decode a configured PNG icon: " + iconPath.string();
            return false;
        }
        Core::EngineApplicationIconImage icon;
        icon.width = decoded.width;
        icon.height = decoded.height;
        icon.rgba8.reserve(decoded.pixels.size());
        for (const std::byte pixel : decoded.pixels) {
            icon.rgba8.push_back(std::to_integer<std::uint8_t>(pixel));
        }
        outIcons.push_back(std::move(icon));
    }
    return true;
}

/// What a `.uvproject` resolves to on disk, shared by the editor and runtime bootstraps.
struct ProjectLayout final {
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::filesystem::path contentRoot;
};

enum class ProjectLayoutError : std::uint8_t {
    None = 0,
    ManifestInvalid,
    PathUnresolvable,
    ContentRootInvalid,
};

struct ProjectLayoutResult final {
    ProjectLayoutError error = ProjectLayoutError::ManifestInvalid;
    std::string message;
    /// Present whenever the manifest itself loaded, so callers can apply their own manifest checks
    /// before reporting a layout failure.
    std::optional<Platform::EditorProjectPackage> package;
    std::optional<ProjectLayout> layout;
};

[[nodiscard]] ProjectLayoutResult ResolveProjectLayout(const std::filesystem::path& projectFile) {
    ProjectLayoutResult result;
    Platform::EditorProjectPackageLoadResult loaded = Platform::EditorProjectPackageCodec::Load(projectFile);
    if (!loaded.IsAccepted()) {
        result.message = "Unable to load the .uvproject project file: " + loaded.result.message;
        return result;
    }
    result.package = std::move(loaded.package);

    std::error_code error;
    std::filesystem::path absoluteProjectFile = std::filesystem::absolute(projectFile, error);
    if (error) {
        result.error = ProjectLayoutError::PathUnresolvable;
        result.message = "Unable to resolve the project file path: " + error.message();
        return result;
    }
    absoluteProjectFile = absoluteProjectFile.lexically_normal();
    std::filesystem::path projectRoot = absoluteProjectFile.parent_path();
    std::filesystem::path contentRoot;
    if (!ResolveContainedDirectory(projectRoot, projectRoot / result.package->contentRoot, contentRoot)) {
        result.error = ProjectLayoutError::ContentRootInvalid;
        result.message = "The project content root must be a directory under the project root: " +
                         (projectRoot / result.package->contentRoot).string();
        return result;
    }
    result.error = ProjectLayoutError::None;
    result.layout = ProjectLayout{std::move(absoluteProjectFile), std::move(projectRoot), std::move(contentRoot)};
    return result;
}

/// Points the configuration at the project's own authored data. The input map keeps the file name
/// the engine uses by default, placed in the project root.
void ApplyProjectAuthoringPaths(Core::EngineConfig& config, const Platform::EditorProjectPackage& package,
                                   const ProjectLayout& layout) {
    static const Core::EngineConfig kDefaultConfig{};
    config.projectContentRoot = layout.contentRoot;
    config.projectRootDirectory = layout.projectRoot;
    config.projectSettingsFilePath = layout.projectRoot / package.settingsPath;
    config.inputMapFilePath = layout.projectRoot / kDefaultConfig.inputMapFilePath.filename();
    config.assetDatabaseFilePath = layout.projectRoot / package.assetDatabasePath;
    config.projectDisplayName = package.displayName;
    config.requestedRendererProfile = package.rendererProfile;
}

[[nodiscard]] Platform::EditorProjectVersion ToProjectVersion(const Core::Version& version) noexcept {
    return {version.major, version.minor, version.patch, version.build};
}

[[nodiscard]] ProjectLaunchResult LoadAndActivateScene(Core::EngineCore& engine,
                                                              const Platform::EditorProjectPackage& package,
                                                              const std::filesystem::path& projectRoot) {
    if (package.startupScenePath.empty()) {
        return MakeLaunchResult(ProjectLaunchCode::NoStartupSceneConfigured,
                                   "The project has no startup scene configured (EditorProjectPackage::"
                                   "startupScenePath is empty) - nothing to load.");
    }

    std::filesystem::path contentRoot;
    if (!ResolveContainedDirectory(projectRoot, projectRoot / package.contentRoot, contentRoot)) {
        return MakeLaunchResult(ProjectLaunchCode::SceneLoadFailed,
                                   "The project content root must be a directory under the project root: " +
                                       (projectRoot / package.contentRoot).string());
    }
    const std::filesystem::path requestedScenePath = contentRoot / package.startupScenePath;
    std::filesystem::path scenePath;
    if (!ResolveContainedRegularFile(contentRoot, requestedScenePath, scenePath)) {
        return MakeLaunchResult(ProjectLaunchCode::SceneLoadFailed,
                                   "The startup scene must be a regular file under the project content root: " +
                                       requestedScenePath.string());
    }

    Core::EngineServices& services = engine.GetServices();
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    Scene::SceneSerializer serializer;
    const std::vector<Scene::Entity> loadedEntities = serializer.Load(entityManager, scenePath);
    if (loadedEntities.empty()) {
        return MakeLaunchResult(ProjectLaunchCode::SceneLoadFailed,
                                   "Failed to load the startup scene: " + scenePath.string());
    }

    // Newly-loaded entities only have their authored local transforms until the scene graph
    // computes world transforms from them - the camera lookup below (and the very first render)
    // both need a real WorldTransformComponent to already be present.
    services.GetSceneGraph().Update(entityManager);

    const std::optional<Scene::Entity> cameraEntity = FindPlayCameraEntity(entityManager);
    if (cameraEntity.has_value()) {
        engine.SetActiveCamera(*cameraEntity);
    }

    return MakeLaunchResult(ProjectLaunchCode::Loaded, "Loaded startup scene: " + scenePath.string());
}

} // namespace

ProjectRuntimeSetupResult PrepareProjectRuntime(const std::filesystem::path& projectFile,
                                                       Core::EngineConfig& config,
                                                       const std::filesystem::path& nativeUserDataRoot) {
    ProjectLayoutResult resolved = ResolveProjectLayout(projectFile);
    if (!resolved.package.has_value()) {
        return MakeSetupResult(ProjectRuntimeSetupCode::InvalidProjectFile, std::move(resolved.message));
    }
    const Platform::EditorProjectPackage& package = *resolved.package;
    if (package.startupScenePath.empty()) {
        return MakeSetupResult(ProjectRuntimeSetupCode::NoStartupSceneConfigured,
                                  "The project has no startup scene configured.");
    }
    if (!resolved.layout.has_value()) {
        const ProjectRuntimeSetupCode code = resolved.error == ProjectLayoutError::ContentRootInvalid
                                                    ? ProjectRuntimeSetupCode::ApplicationResourceInvalid
                                                    : ProjectRuntimeSetupCode::InvalidProjectFile;
        return MakeSetupResult(code, std::move(resolved.message));
    }
    ProjectLayout& layout = *resolved.layout;
    const std::filesystem::path& projectRoot = layout.projectRoot;
    const std::filesystem::path& contentRoot = layout.contentRoot;
    std::error_code error;

    Core::EngineConfig candidateConfig = config;
    candidateConfig.windowIcons.clear();
    std::string resourceError;
    if (!LoadCurrentTargetWindowIcons(package, contentRoot, candidateConfig.windowIcons, resourceError)) {
        return MakeSetupResult(ProjectRuntimeSetupCode::ApplicationResourceInvalid,
                                  std::move(resourceError));
    }
    if (!package.applicationSettings.splashImagePath.empty()) {
        const std::filesystem::path requestedSplashImage = contentRoot / package.applicationSettings.splashImagePath;
        std::filesystem::path splashImage;
        if (!ResolveContainedRegularFile(contentRoot, requestedSplashImage, splashImage)) {
            return MakeSetupResult(ProjectRuntimeSetupCode::ApplicationResourceInvalid,
                                      "The configured splash image must be a regular file under the project content root: " +
                                          requestedSplashImage.string());
        }
        candidateConfig.bootSplashImagePath = splashImage;
    } else {
        candidateConfig.bootSplashImagePath.clear();
    }

    Platform::ApplicationUserDataDirectoryResult dataDirectory;
    if (!nativeUserDataRoot.empty()) {
        // Native mobile hosts own their sandbox path; it must survive cooked-content replacement.
        if (!nativeUserDataRoot.is_absolute())
            return MakeSetupResult(ProjectRuntimeSetupCode::UserDataDirectoryUnavailable,"Native user-data root must be absolute");
        std::filesystem::create_directories(nativeUserDataRoot,error);
        dataDirectory = {!error,nativeUserDataRoot,error.message()};
    } else dataDirectory = Platform::ResolveApplicationUserDataDirectory(package, projectRoot);
    if (!dataDirectory.success) {
        return MakeSetupResult(ProjectRuntimeSetupCode::UserDataDirectoryUnavailable,
                                  dataDirectory.message);
    }

    const std::string target{Platform::GetApplicationTarget()};
    const auto identifierIt = package.applicationSettings.applicationIdentifiersByTarget.find(target);
    const std::string applicationIdentifier =
        identifierIt != package.applicationSettings.applicationIdentifiersByTarget.end()
            ? identifierIt->second
            : package.projectId;
    const std::string& productName =
        package.productMetadata.name.empty() ? package.displayName : package.productMetadata.name;

    candidateConfig.applicationIdentifier = applicationIdentifier;
    candidateConfig.userDataDirectoryPath = dataDirectory.directory;
    candidateConfig.enforceSingleInstance = package.applicationSettings.enforceSingleInstance;
    candidateConfig.quitOnLastWindowClosed = package.applicationSettings.quitOnLastWindowClosed;
    candidateConfig.crashHandlerEnabled = package.applicationSettings.crashHandlerEnabled;
    candidateConfig.crashDumpDirectory = dataDirectory.directory / package.applicationSettings.crashDumpDirectory;
    candidateConfig.symbolUploadEndpoint = package.applicationSettings.symbolUploadEndpoint;
    candidateConfig.backgroundColor = package.applicationSettings.splashBackgroundColor;
    candidateConfig.splashFadeSeconds = package.applicationSettings.splashFadeSeconds;
    candidateConfig.splashMinimumDisplaySeconds = package.applicationSettings.splashMinimumDisplaySeconds;
    candidateConfig.splashSkippable = package.applicationSettings.splashSkippable;
    candidateConfig.skipSplashInEditorPlayMode = package.applicationSettings.skipSplashInEditorPlayMode;
    candidateConfig.windowTitle = productName;
    const Platform::EditorProjectApplicationSettings& windowSettings = package.applicationSettings;
    candidateConfig.windowWidth = windowSettings.windowWidth;
    candidateConfig.windowHeight = windowSettings.windowHeight;
    candidateConfig.windowMode = windowSettings.windowMode;
    candidateConfig.windowResizable = windowSettings.windowResizable;
    candidateConfig.windowBorderless = windowSettings.windowBorderless;
    candidateConfig.windowAlwaysOnTop = windowSettings.windowAlwaysOnTop;
    candidateConfig.windowTransparent = windowSettings.windowTransparent;
    candidateConfig.windowMinimumWidth = windowSettings.minimumWindowWidth;
    candidateConfig.windowMinimumHeight = windowSettings.minimumWindowHeight;
    candidateConfig.windowMaximumWidth = windowSettings.maximumWindowWidth;
    candidateConfig.windowMaximumHeight = windowSettings.maximumWindowHeight;
    candidateConfig.windowPositionSpecified = windowSettings.initialWindowPositionSpecified;
    candidateConfig.windowPositionX = windowSettings.initialWindowPositionX;
    candidateConfig.windowPositionY = windowSettings.initialWindowPositionY;
    candidateConfig.windowMonitorName = windowSettings.initialMonitorName;
    candidateConfig.highDpiAware = windowSettings.highDpiAware;
    candidateConfig.perMonitorScaling = windowSettings.perMonitorScaling;
    candidateConfig.contentScaleOverride = windowSettings.contentScaleOverride;
    candidateConfig.stretchMode = windowSettings.stretchMode;
    candidateConfig.aspectPolicy = windowSettings.aspectPolicy;
    candidateConfig.integerOnlyScaling = windowSettings.integerOnlyScaling;
    candidateConfig.orientation = windowSettings.orientation;
    candidateConfig.allowedOrientations = windowSettings.allowedOrientations;
    candidateConfig.vsyncMode = windowSettings.vsyncMode;
    candidateConfig.vsyncModeExplicit = true;
    candidateConfig.vsyncEnabled = windowSettings.vsyncMode != Platform::VSyncMode::Off;
    candidateConfig.focusedFrameRateCap = windowSettings.focusedFrameRateCap;
    candidateConfig.unfocusedFrameRateCap = windowSettings.unfocusedFrameRateCap;
    candidateConfig.allowDisplaySleep = windowSettings.allowDisplaySleep;
    candidateConfig.cursorImagePath.clear();
    candidateConfig.cursorRgba8.clear();
    candidateConfig.cursorImageWidth = 0U;
    candidateConfig.cursorImageHeight = 0U;
    candidateConfig.cursorHotspotX = windowSettings.cursorHotspotX;
    candidateConfig.cursorHotspotY = windowSettings.cursorHotspotY;
    candidateConfig.cursorVisible = windowSettings.cursorVisible;
    candidateConfig.cursorConfinedToWindow = windowSettings.cursorConfinedToWindow;
    if (!windowSettings.cursorImagePath.empty()) {
        const std::filesystem::path requestedCursor = contentRoot / windowSettings.cursorImagePath;
        std::filesystem::path resolvedCursor;
        if (!ResolveContainedRegularFile(contentRoot, requestedCursor, resolvedCursor)) {
            return MakeSetupResult(ProjectRuntimeSetupCode::ApplicationResourceInvalid,
                                      "The configured cursor image must be a regular PNG file under the project "
                                      "content root: " + requestedCursor.string());
        }
        candidateConfig.cursorImagePath = std::move(resolvedCursor);
    }
    candidateConfig.windowTitleFormat = windowSettings.windowTitleFormat;
    candidateConfig.appendSceneNameInEditorPlayMode = windowSettings.appendSceneNameInEditorPlayMode;
    candidateConfig.settingsFilePath = dataDirectory.directory / ".uvsettings";
    candidateConfig.platformSettingsFilePath.clear();
    ApplyProjectAuthoringPaths(candidateConfig, package, layout);
    if (std::filesystem::exists(projectRoot/"cooked.json")) {
        try {
            std::ifstream input(projectRoot/"cooked.json");
            nlohmann::json metadata; input >> metadata;
            if (metadata.at("schema") != 1) throw std::runtime_error("Unsupported cooked schema");
            candidateConfig.cookedAssetPaths = true;
            candidateConfig.assetResidentBudgetBytes = metadata.at("assetBudgetBytes").get<std::size_t>();
            if (candidateConfig.assetResidentBudgetBytes == 0) throw std::runtime_error("Cooked budget must be finite");
        } catch (const std::exception& exception) {
            return MakeSetupResult(ProjectRuntimeSetupCode::InvalidProjectFile, exception.what());
        }
    }
    candidateConfig.derivedArtifactCacheRoot = dataDirectory.directory / "DerivedData" / "Import";
    candidateConfig.saveDirectoryPath = dataDirectory.directory / "saves";
    candidateConfig.shaderCachePath = dataDirectory.directory / "shader_cache";
    candidateConfig.logFilePath = dataDirectory.directory / "logs" / "uve_engine.log";

    error.clear();
    std::filesystem::create_directories(candidateConfig.logFilePath.parent_path(), error);
    if (error) {
        return MakeSetupResult(ProjectRuntimeSetupCode::UserDataDirectoryUnavailable,
                                  "Unable to create the application log directory: " + error.message());
    }
    if (candidateConfig.crashHandlerEnabled) {
        error.clear();
        std::filesystem::create_directories(candidateConfig.crashDumpDirectory, error);
        if (error) {
            return MakeSetupResult(ProjectRuntimeSetupCode::UserDataDirectoryUnavailable,
                                      "Unable to create the crash dump directory: " + error.message());
        }
    }

    config = std::move(candidateConfig);
    ProjectRuntimeSetup setup;
    setup.package = package;
    setup.projectFile = std::move(layout.projectFile);
    setup.projectRoot = projectRoot;
    setup.contentRoot = contentRoot;
    setup.userDataDirectory = dataDirectory.directory;
    setup.applicationIdentifier = applicationIdentifier;
    return {ProjectRuntimeSetupCode::Ready, "Project runtime configuration is ready.", std::move(setup)};
}

ProjectEditorSetupResult PrepareProjectEditor(const std::filesystem::path& projectFile,
                                                     const Core::Version& engineVersion,
                                                     Core::EngineConfig& config,
                                                     const ProjectCompatibilityPolicy& policy) {
    const auto fail = [](const ProjectEditorSetupCode code, std::string message) {
        return ProjectEditorSetupResult{code, std::move(message), std::nullopt};
    };
    ProjectLayoutResult resolved = ResolveProjectLayout(projectFile);
    if (!resolved.layout.has_value()) {
        const ProjectEditorSetupCode code = resolved.error == ProjectLayoutError::ContentRootInvalid
                                                   ? ProjectEditorSetupCode::ContentRootInvalid
                                                   : ProjectEditorSetupCode::InvalidProjectFile;
        return fail(code, std::move(resolved.message));
    }
    Platform::EditorProjectPackage& package = *resolved.package;
    ProjectLayout& layout = *resolved.layout;

    ProjectCompatibility compatibility =
        EvaluateProjectCompatibility(package.engineVersion, engineVersion, policy);
    if (!compatibility.CanOpen()) {
        return fail(ProjectEditorSetupCode::EngineTooOld, std::move(compatibility.message));
    }

    // The scene may not have been saved yet, so containment is checked on the normalized path
    // rather than through canonical(), which requires the file to exist.
    std::filesystem::path startupScene;
    if (!package.startupScenePath.empty()) {
        startupScene = (layout.contentRoot / package.startupScenePath).lexically_normal();
        const std::filesystem::path relative = startupScene.lexically_relative(layout.contentRoot);
        if (package.startupScenePath.is_absolute() || relative.empty() || *relative.begin() == "..") {
            return fail(ProjectEditorSetupCode::StartupSceneInvalid,
                        "The startup scene must be under the project content root: " +
                            package.startupScenePath.string());
        }
    }

    static const Core::EngineConfig kDefaultConfig{};
    Core::EngineConfig candidateConfig = config;
    ApplyProjectAuthoringPaths(candidateConfig, package, layout);
    candidateConfig.derivedArtifactCacheRoot = layout.projectRoot / kDefaultConfig.derivedArtifactCacheRoot;
    candidateConfig.logFilePath =
        layout.projectRoot / kProjectEditorLogDirectoryName / config.logFilePath.filename();

    std::error_code error;
    std::filesystem::create_directories(candidateConfig.logFilePath.parent_path(), error);
    if (error) {
        return fail(ProjectEditorSetupCode::DirectoryUnavailable,
                    "Unable to create the project log directory: " + error.message());
    }

    config = std::move(candidateConfig);
    ProjectEditorSetup setup;
    setup.package = std::move(package);
    setup.projectFile = std::move(layout.projectFile);
    setup.projectRoot = std::move(layout.projectRoot);
    setup.contentRoot = std::move(layout.contentRoot);
    setup.startupScene = std::move(startupScene);
    setup.compatibility = std::move(compatibility);
    return {ProjectEditorSetupCode::Ready, "Project editor configuration is ready.", std::move(setup)};
}

Platform::EditorProjectPackageResult RecordProjectEngineVersion(const std::filesystem::path& projectFile,
                                                                     const Core::Version& engineVersion) {
    Platform::EditorProjectPackageLoadResult loaded = Platform::EditorProjectPackageCodec::Load(projectFile);
    if (!loaded.IsAccepted()) {
        return loaded.result;
    }
    const Platform::EditorProjectVersion recorded = ToProjectVersion(engineVersion);
    if (loaded.package->engineVersion == recorded) {
        return {Platform::EditorProjectPackageCode::Applied, "The project already records this engine version."};
    }
    const std::uint64_t expectedRevision = loaded.package->revision;
    Platform::EditorProjectPackage replacement = std::move(*loaded.package);
    replacement.engineVersion = recorded;
    replacement.revision = expectedRevision + 1U;
    return Platform::EditorProjectPackageCodec::ApplyUpdate(projectFile, expectedRevision, replacement);
}

ProjectLaunchResult LoadAndActivateProjectScene(Core::EngineCore& engine,
                                                       const std::filesystem::path& projectFile) {
    const Platform::EditorProjectPackageLoadResult loaded =
        Platform::EditorProjectPackageCodec::Load(projectFile);
    if (!loaded.IsAccepted()) {
        return MakeLaunchResult(ProjectLaunchCode::InvalidProjectFile,
                                   "Unable to load the .uvproject project file: " + loaded.result.message);
    }
    const std::filesystem::path projectRoot = projectFile.parent_path().empty()
                                                  ? std::filesystem::current_path()
                                                  : projectFile.parent_path();
    return LoadAndActivateScene(engine, *loaded.package, projectRoot);
}

ProjectLaunchResult LoadAndActivateProjectScene(Core::EngineCore& engine,
                                                       const ProjectRuntimeSetup& setup) {
    return LoadAndActivateScene(engine, setup.package, setup.projectRoot);
}

} // namespace UVE::Pack
