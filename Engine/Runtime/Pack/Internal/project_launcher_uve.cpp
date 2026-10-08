// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_launcher_uve.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "uve/asset/png_metadata_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/platform/application_runtime_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Pack {
namespace {

[[nodiscard]] ProjectLaunchResultUVE MakeLaunchResultUVE(const ProjectLaunchCodeUVE code, std::string message) {
    return {code, std::move(message)};
}

[[nodiscard]] ProjectRuntimeSetupResultUVE MakeSetupResultUVE(const ProjectRuntimeSetupCodeUVE code,
                                                              std::string message) {
    return {code, std::move(message), std::nullopt};
}

[[nodiscard]] std::optional<Scene::EntityUVE> FindPlayCameraEntityUVE(Scene::IEntityManagerUVE& entityManager) {
    return Scene::FindCurrentCameraEntityUVE(entityManager);
}

[[nodiscard]] bool ReadBoundedFileUVE(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                                      std::string& errorMessage) {
    constexpr std::uintmax_t kMaximumIconFileBytesUVE = 64ULL * 1024ULL * 1024ULL;
    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error || fileSize == 0U || fileSize > kMaximumIconFileBytesUVE ||
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

[[nodiscard]] bool ResolveContainedPathUVE(const std::filesystem::path& root,
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

[[nodiscard]] bool ResolveContainedRegularFileUVE(const std::filesystem::path& root,
                                                  const std::filesystem::path& candidate,
                                                  std::filesystem::path& resolvedPath) {
    std::error_code error;
    return ResolveContainedPathUVE(root, candidate, resolvedPath) &&
           std::filesystem::is_regular_file(resolvedPath, error) && !error;
}

[[nodiscard]] bool ResolveContainedDirectoryUVE(const std::filesystem::path& root,
                                                const std::filesystem::path& candidate,
                                                std::filesystem::path& resolvedPath) {
    std::error_code error;
    return ResolveContainedPathUVE(root, candidate, resolvedPath) &&
           std::filesystem::is_directory(resolvedPath, error) && !error;
}

[[nodiscard]] std::string LowercaseExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool LoadCurrentTargetWindowIconsUVE(const Platform::EditorProjectPackageUVE& package,
                                                   const std::filesystem::path& contentRoot,
                                                   std::vector<Core::EngineApplicationIconImageUVE>& outIcons,
                                                   std::string& errorMessage) {
    const auto targetIt = package.applicationSettings.iconPathsByTarget.find(
        std::string{Platform::GetApplicationTargetUVE()});
    if (targetIt == package.applicationSettings.iconPathsByTarget.end()) {
        return true;
    }

    constexpr std::uint64_t kMaximumDecodedIconBytesUVE = 64ULL * 1024ULL * 1024ULL;
    for (const auto& [pixelSize, relativePath] : targetIt->second) {
        // ICO/ICNS are retained for native packaging; the cross-platform GLFW window backend
        // accepts decoded RGBA pixels, so only PNGs are applied directly to the live window.
        if (LowercaseExtensionUVE(relativePath) != ".png") {
            continue;
        }
        const std::filesystem::path requestedIconPath = contentRoot / relativePath;
        std::filesystem::path iconPath;
        if (!ResolveContainedRegularFileUVE(contentRoot, requestedIconPath, iconPath)) {
            errorMessage = "The configured PNG icon must be a regular file under the project content root: " +
                           requestedIconPath.string();
            return false;
        }
        std::vector<std::byte> fileBytes;
        if (!ReadBoundedFileUVE(iconPath, fileBytes, errorMessage)) {
            return false;
        }
        const std::optional<Asset::PngMetadataUVE> metadata = Asset::ParsePngMetadataUVE(fileBytes);
        if (!metadata.has_value() || metadata->width != pixelSize || metadata->height != pixelSize ||
            !Asset::ValidatePngRgba8PixelBudgetUVE(*metadata, kMaximumDecodedIconBytesUVE)) {
            errorMessage = "A configured PNG icon must be a valid square image matching its declared pixel size: " +
                           iconPath.string();
            return false;
        }
        Asset::PngRgba8ImageUVE decoded;
        if (!Asset::DecodePngRgba8ImageUVE(fileBytes, decoded) || decoded.width != pixelSize ||
            decoded.height != pixelSize) {
            errorMessage = "Unable to decode a configured PNG icon: " + iconPath.string();
            return false;
        }
        Core::EngineApplicationIconImageUVE icon;
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

[[nodiscard]] ProjectLaunchResultUVE LoadAndActivateSceneUVE(Core::EngineCoreUVE& engine,
                                                              const Platform::EditorProjectPackageUVE& package,
                                                              const std::filesystem::path& projectRoot) {
    if (package.startupScenePath.empty()) {
        return MakeLaunchResultUVE(ProjectLaunchCodeUVE::NoStartupSceneConfigured,
                                   "The project has no startup scene configured (EditorProjectPackageUVE::"
                                   "startupScenePath is empty) - nothing to load.");
    }

    std::filesystem::path contentRoot;
    if (!ResolveContainedDirectoryUVE(projectRoot, projectRoot / package.contentRoot, contentRoot)) {
        return MakeLaunchResultUVE(ProjectLaunchCodeUVE::SceneLoadFailed,
                                   "The project content root must be a directory under the project root: " +
                                       (projectRoot / package.contentRoot).string());
    }
    const std::filesystem::path requestedScenePath = contentRoot / package.startupScenePath;
    std::filesystem::path scenePath;
    if (!ResolveContainedRegularFileUVE(contentRoot, requestedScenePath, scenePath)) {
        return MakeLaunchResultUVE(ProjectLaunchCodeUVE::SceneLoadFailed,
                                   "The startup scene must be a regular file under the project content root: " +
                                       requestedScenePath.string());
    }

    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::SceneSerializerUVE serializer;
    const std::vector<Scene::EntityUVE> loadedEntities = serializer.LoadUVE(entityManager, scenePath);
    if (loadedEntities.empty()) {
        return MakeLaunchResultUVE(ProjectLaunchCodeUVE::SceneLoadFailed,
                                   "Failed to load the startup scene: " + scenePath.string());
    }

    // Newly-loaded entities only have their authored local transforms until the scene graph
    // computes world transforms from them - the camera lookup below (and the very first render)
    // both need a real WorldTransformComponentUVE to already be present.
    services.GetSceneGraphUVE().UpdateUVE(entityManager);

    const std::optional<Scene::EntityUVE> cameraEntity = FindPlayCameraEntityUVE(entityManager);
    if (cameraEntity.has_value()) {
        engine.SetActiveCameraUVE(*cameraEntity);
    }

    return MakeLaunchResultUVE(ProjectLaunchCodeUVE::Loaded, "Loaded startup scene: " + scenePath.string());
}

} // namespace

ProjectRuntimeSetupResultUVE PrepareProjectRuntimeUVE(const std::filesystem::path& projectFile,
                                                       Core::EngineConfigUVE& config) {
    const Platform::EditorProjectPackageLoadResultUVE loaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
    if (!loaded.IsAcceptedUVE()) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::InvalidProjectFile,
                                  "Unable to load the .uvproject project file: " + loaded.result.message);
    }
    const Platform::EditorProjectPackageUVE& package = *loaded.package;
    if (package.startupScenePath.empty()) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::NoStartupSceneConfigured,
                                  "The project has no startup scene configured.");
    }

    std::error_code error;
    std::filesystem::path absoluteProjectFile = std::filesystem::absolute(projectFile, error);
    if (error) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::InvalidProjectFile,
                                  "Unable to resolve the project file path: " + error.message());
    }
    absoluteProjectFile = absoluteProjectFile.lexically_normal();
    const std::filesystem::path projectRoot = absoluteProjectFile.parent_path();
    std::filesystem::path contentRoot;
    if (!ResolveContainedDirectoryUVE(projectRoot, projectRoot / package.contentRoot, contentRoot)) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::ApplicationResourceInvalid,
                                  "The project content root must be a directory under the project root: " +
                                      (projectRoot / package.contentRoot).string());
    }

    Core::EngineConfigUVE candidateConfig = config;
    candidateConfig.windowIconsUVE.clear();
    std::string resourceError;
    if (!LoadCurrentTargetWindowIconsUVE(package, contentRoot, candidateConfig.windowIconsUVE, resourceError)) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::ApplicationResourceInvalid,
                                  std::move(resourceError));
    }
    if (!package.applicationSettings.splashImagePath.empty()) {
        const std::filesystem::path requestedSplashImage = contentRoot / package.applicationSettings.splashImagePath;
        std::filesystem::path splashImage;
        if (!ResolveContainedRegularFileUVE(contentRoot, requestedSplashImage, splashImage)) {
            return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::ApplicationResourceInvalid,
                                      "The configured splash image must be a regular file under the project content root: " +
                                          requestedSplashImage.string());
        }
        candidateConfig.bootSplashImagePathUVE = splashImage;
    } else {
        candidateConfig.bootSplashImagePathUVE.clear();
    }

    const Platform::ApplicationUserDataDirectoryResultUVE dataDirectory =
        Platform::ResolveApplicationUserDataDirectoryUVE(package, projectRoot);
    if (!dataDirectory.success) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::UserDataDirectoryUnavailable,
                                  dataDirectory.message);
    }

    const std::string target{Platform::GetApplicationTargetUVE()};
    const auto identifierIt = package.applicationSettings.applicationIdentifiersByTarget.find(target);
    const std::string applicationIdentifier =
        identifierIt != package.applicationSettings.applicationIdentifiersByTarget.end()
            ? identifierIt->second
            : package.projectId;
    const std::string& productName =
        package.productMetadata.name.empty() ? package.displayName : package.productMetadata.name;

    candidateConfig.applicationIdentifierUVE = applicationIdentifier;
    candidateConfig.userDataDirectoryPathUVE = dataDirectory.directory;
    candidateConfig.enforceSingleInstanceUVE = package.applicationSettings.enforceSingleInstance;
    candidateConfig.quitOnLastWindowClosedUVE = package.applicationSettings.quitOnLastWindowClosed;
    candidateConfig.crashHandlerEnabledUVE = package.applicationSettings.crashHandlerEnabled;
    candidateConfig.crashDumpDirectoryUVE = dataDirectory.directory / package.applicationSettings.crashDumpDirectory;
    candidateConfig.symbolUploadEndpointUVE = package.applicationSettings.symbolUploadEndpoint;
    candidateConfig.backgroundColorUVE = package.applicationSettings.splashBackgroundColor;
    candidateConfig.splashFadeSecondsUVE = package.applicationSettings.splashFadeSeconds;
    candidateConfig.splashMinimumDisplaySecondsUVE = package.applicationSettings.splashMinimumDisplaySeconds;
    candidateConfig.splashSkippableUVE = package.applicationSettings.splashSkippable;
    candidateConfig.skipSplashInEditorPlayModeUVE = package.applicationSettings.skipSplashInEditorPlayMode;
    candidateConfig.windowTitle = productName;
    candidateConfig.projectDisplayNameUVE = package.displayName;
    const Platform::EditorProjectApplicationSettingsUVE& windowSettings = package.applicationSettings;
    candidateConfig.windowWidth = windowSettings.windowWidth;
    candidateConfig.windowHeight = windowSettings.windowHeight;
    candidateConfig.windowModeUVE = windowSettings.windowMode;
    candidateConfig.windowResizableUVE = windowSettings.windowResizable;
    candidateConfig.windowBorderlessUVE = windowSettings.windowBorderless;
    candidateConfig.windowAlwaysOnTopUVE = windowSettings.windowAlwaysOnTop;
    candidateConfig.windowTransparentUVE = windowSettings.windowTransparent;
    candidateConfig.windowMinimumWidthUVE = windowSettings.minimumWindowWidth;
    candidateConfig.windowMinimumHeightUVE = windowSettings.minimumWindowHeight;
    candidateConfig.windowMaximumWidthUVE = windowSettings.maximumWindowWidth;
    candidateConfig.windowMaximumHeightUVE = windowSettings.maximumWindowHeight;
    candidateConfig.windowPositionSpecifiedUVE = windowSettings.initialWindowPositionSpecified;
    candidateConfig.windowPositionXUVE = windowSettings.initialWindowPositionX;
    candidateConfig.windowPositionYUVE = windowSettings.initialWindowPositionY;
    candidateConfig.windowMonitorNameUVE = windowSettings.initialMonitorName;
    candidateConfig.highDpiAwareUVE = windowSettings.highDpiAware;
    candidateConfig.perMonitorScalingUVE = windowSettings.perMonitorScaling;
    candidateConfig.contentScaleOverrideUVE = windowSettings.contentScaleOverride;
    candidateConfig.stretchModeUVE = windowSettings.stretchMode;
    candidateConfig.aspectPolicyUVE = windowSettings.aspectPolicy;
    candidateConfig.integerOnlyScalingUVE = windowSettings.integerOnlyScaling;
    candidateConfig.orientationUVE = windowSettings.orientation;
    candidateConfig.allowedOrientationsUVE = windowSettings.allowedOrientations;
    candidateConfig.vsyncModeUVE = windowSettings.vsyncMode;
    candidateConfig.vsyncModeExplicitUVE = true;
    candidateConfig.vsyncEnabledUVE = windowSettings.vsyncMode != Platform::VSyncModeUVE::Off;
    candidateConfig.focusedFrameRateCapUVE = windowSettings.focusedFrameRateCap;
    candidateConfig.unfocusedFrameRateCapUVE = windowSettings.unfocusedFrameRateCap;
    candidateConfig.allowDisplaySleepUVE = windowSettings.allowDisplaySleep;
    candidateConfig.cursorImagePathUVE.clear();
    candidateConfig.cursorRgba8UVE.clear();
    candidateConfig.cursorImageWidthUVE = 0U;
    candidateConfig.cursorImageHeightUVE = 0U;
    candidateConfig.cursorHotspotXUVE = windowSettings.cursorHotspotX;
    candidateConfig.cursorHotspotYUVE = windowSettings.cursorHotspotY;
    candidateConfig.cursorVisibleUVE = windowSettings.cursorVisible;
    candidateConfig.cursorConfinedToWindowUVE = windowSettings.cursorConfinedToWindow;
    if (!windowSettings.cursorImagePath.empty()) {
        const std::filesystem::path requestedCursor = contentRoot / windowSettings.cursorImagePath;
        std::filesystem::path resolvedCursor;
        if (!ResolveContainedRegularFileUVE(contentRoot, requestedCursor, resolvedCursor)) {
            return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::ApplicationResourceInvalid,
                                      "The configured cursor image must be a regular PNG file under the project "
                                      "content root: " + requestedCursor.string());
        }
        candidateConfig.cursorImagePathUVE = std::move(resolvedCursor);
    }
    candidateConfig.windowTitleFormatUVE = windowSettings.windowTitleFormat;
    candidateConfig.appendSceneNameInEditorPlayModeUVE = windowSettings.appendSceneNameInEditorPlayMode;
    candidateConfig.settingsFilePath = dataDirectory.directory / ".uvsettings";
    candidateConfig.platformSettingsFilePath.clear();
    candidateConfig.projectSettingsFilePath = projectRoot / package.settingsPath;
    candidateConfig.inputMapFilePath = projectRoot / "project.uvinput";
    candidateConfig.assetDatabaseFilePath = projectRoot / package.assetDatabasePath;
    candidateConfig.projectContentRootUVE = contentRoot;
    candidateConfig.projectRootDirectoryUVE = projectRoot;
    candidateConfig.derivedArtifactCacheRootUVE = dataDirectory.directory / "DerivedData" / "Import";
    candidateConfig.saveDirectoryPath = dataDirectory.directory / "saves";
    candidateConfig.shaderCachePath = dataDirectory.directory / "shader_cache";
    candidateConfig.logFilePath = dataDirectory.directory / "logs" / "uve_engine.log";

    error.clear();
    std::filesystem::create_directories(candidateConfig.logFilePath.parent_path(), error);
    if (error) {
        return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::UserDataDirectoryUnavailable,
                                  "Unable to create the application log directory: " + error.message());
    }
    if (candidateConfig.crashHandlerEnabledUVE) {
        error.clear();
        std::filesystem::create_directories(candidateConfig.crashDumpDirectoryUVE, error);
        if (error) {
            return MakeSetupResultUVE(ProjectRuntimeSetupCodeUVE::UserDataDirectoryUnavailable,
                                      "Unable to create the crash dump directory: " + error.message());
        }
    }

    config = std::move(candidateConfig);
    ProjectRuntimeSetupUVE setup;
    setup.package = package;
    setup.projectFile = std::move(absoluteProjectFile);
    setup.projectRoot = projectRoot;
    setup.contentRoot = contentRoot;
    setup.userDataDirectory = dataDirectory.directory;
    setup.applicationIdentifier = applicationIdentifier;
    return {ProjectRuntimeSetupCodeUVE::Ready, "Project runtime configuration is ready.", std::move(setup)};
}

ProjectLaunchResultUVE LoadAndActivateProjectSceneUVE(Core::EngineCoreUVE& engine,
                                                       const std::filesystem::path& projectFile) {
    const Platform::EditorProjectPackageLoadResultUVE loaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
    if (!loaded.IsAcceptedUVE()) {
        return MakeLaunchResultUVE(ProjectLaunchCodeUVE::InvalidProjectFile,
                                   "Unable to load the .uvproject project file: " + loaded.result.message);
    }
    const std::filesystem::path projectRoot = projectFile.parent_path().empty()
                                                  ? std::filesystem::current_path()
                                                  : projectFile.parent_path();
    return LoadAndActivateSceneUVE(engine, *loaded.package, projectRoot);
}

ProjectLaunchResultUVE LoadAndActivateProjectSceneUVE(Core::EngineCoreUVE& engine,
                                                       const ProjectRuntimeSetupUVE& setup) {
    return LoadAndActivateSceneUVE(engine, setup.package, setup.projectRoot);
}

} // namespace UVE::Pack
