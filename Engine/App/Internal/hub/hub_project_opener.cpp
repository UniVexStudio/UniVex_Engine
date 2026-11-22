// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_project_opener.h"

#include <chrono>
#include <utility>

#include "editor_project_package.h"
#include "engine_install.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "project_compatibility.h"

namespace UVE::Hub {
namespace {

/// The editor launch Android carries out after the Hub's window closes.
std::optional<Platform::ProcessLaunchDescUVE> g_pendingEditorLaunchUVE;

} // namespace

std::optional<Platform::ProcessLaunchDescUVE> TakePendingEditorLaunchUVE() {
    return std::exchange(g_pendingEditorLaunchUVE, std::nullopt);
}

ProjectOpenResultUVE OpenProjectInEngineUVE(const std::filesystem::path& projectFile,
                                            const std::vector<Online::InstalledEngineUVE>& engines,
                                            const std::filesystem::path& registryFile,
                                            const std::filesystem::path& registryOverride) {
    if (projectFile.empty()) {
        return {false, "Enter the path of a project file to open."};
    }
    const Platform::EditorProjectPackageLoadResultUVE manifest = Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
    if (!manifest.IsAcceptedUVE()) {
        return {false, "Cannot open " + projectFile.string() + ": " + manifest.result.message};
    }
    const Platform::EditorProjectPackageUVE& package = *manifest.package;

    Pack::ProjectRegistryUVE registry;
    const bool registryLoaded = registry.LoadUVE(registryFile).IsAppliedUVE();
    const Pack::ProjectRegistryEntryUVE* const entry = registryLoaded ? registry.FindUVE(projectFile) : nullptr;
    const Online::InstalledEngineUVE* const engine = Online::ChooseEngineForProjectUVE(
        engines, package.engineVersion, entry != nullptr ? entry->engineChoice : std::string{});

    // Listed either way, so a project that cannot open yet still shows up with what it needs.
    if (registryLoaded) {
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        registry.RecordOpenedUVE(projectFile, package.displayName, package.engineVersion, now);
        static_cast<void>(registry.SaveUVE(registryFile));
    }
    if (engine == nullptr) {
        return {false, package.displayName + " needs engine " + Pack::FormatProjectVersionUVE(package.engineVersion) +
                           ", which is not downloaded. Download it on the Engines page."};
    }
    const Pack::ProjectCompatibilityUVE compatibility =
        Pack::EvaluateProjectCompatibilityUVE(package.engineVersion, engine->install.version);
    if (!compatibility.CanOpenUVE()) {
        return {false, compatibility.message};
    }
    Platform::ProcessLaunchDescUVE launch{
        engine->install.editorExecutable,
        Pack::MakeEditorLaunchArgumentsUVE(Pack::ProjectRegistryUVE::NormalizeProjectFileUVE(projectFile), registryOverride),
        projectFile.parent_path()};
#if defined(__ANDROID__)
    g_pendingEditorLaunchUVE = std::move(launch);
    LauncherUI::RequestLauncherCloseUVE();
#else
    const Platform::ProcessLaunchResultUVE launched = Platform::LaunchDetachedProcessUVE(launch);
    if (!launched.launched) {
        return {false, launched.message};
    }
#endif
    return {true, "Opening " + package.displayName + " with engine " + engine->install.version.ToStringUVE() + "..."};
}

} // namespace UVE::Hub
