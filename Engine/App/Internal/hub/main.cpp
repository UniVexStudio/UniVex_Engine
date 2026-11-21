// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "application_runtime.h"
#include "engine_core.h"
#include "engine_install.h"
#include "engine_library.h"
#include "engine_release_feed.h"
#include "hub_project_opener.h"
#include "hub_setup.h"
#include "hub_window.h"
#include "project_template.h"
#include "launcher_window.h"
#include <iterator>
#include "project_list.h"
#include "project_registry.h"

namespace {

inline constexpr std::uint32_t kHubWindowWidthUVE = 1200U;
inline constexpr std::uint32_t kHubWindowHeightUVE = 760U;
inline constexpr int kInvalidCommandLineExitCodeUVE = 2;
inline constexpr int kStartupFailedExitCodeUVE = UVE::LauncherUI::kLauncherStartupFailedExitCodeUVE;

inline constexpr std::string_view kFramesOptionUVE = "--frames";
inline constexpr std::string_view kScreenshotOptionUVE = "--screenshot";
inline constexpr std::string_view kEngineRootOptionUVE = "--engine-root";
inline constexpr std::string_view kSettingsOptionUVE = "--settings";
inline constexpr std::string_view kPageOptionUVE = "--page";
/// Installs an engine package (with its checksum file beside it) without opening the window.
inline constexpr std::string_view kInstallPackageOptionUVE = "--install-package";
/// Prints the Hub's version and exits; release tooling checks it against the hub-v tag.
inline constexpr std::string_view kVersionOptionUVE = "--version";

struct PageChoiceUVE final {
    std::string_view word;
    UVE::Hub::HubPageUVE page;
};
inline constexpr std::array<PageChoiceUVE, 4U> kPageChoicesUVE{{
    {"projects", UVE::Hub::HubPageUVE::Projects},
    {"new-project", UVE::Hub::HubPageUVE::NewProject},
    {"engines", UVE::Hub::HubPageUVE::Engines},
    {"settings", UVE::Hub::HubPageUVE::Settings},
}};

struct HubLaunchOptionsUVE final {
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
    std::filesystem::path engineRoot;
    std::filesystem::path registryFile;
    std::filesystem::path settingsFile;
    UVE::Hub::HubPageUVE page = UVE::Hub::HubPageUVE::Projects;
    std::filesystem::path installPackage;
    /// A project file named on the command line (opening a .uvproject from the file manager).
    std::filesystem::path projectFile;
    bool printVersion = false;
    std::optional<std::string> error;
};

[[nodiscard]] HubLaunchOptionsUVE ParseOptionsUVE(const int argc, char** argv) {
    HubLaunchOptionsUVE options;
    const auto takeValue = [&](int& index, const std::string_view option, std::filesystem::path& out) {
        if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
            options.error = std::string(option) + " requires a value";
            return false;
        }
        out = argv[++index];
        return true;
    };
    for (int index = 1; index < argc && !options.error.has_value(); ++index) {
        const std::string_view argument{argv[index]};
        if (argument == kFramesOptionUVE) {
            std::filesystem::path value;
            if (!takeValue(index, kFramesOptionUVE, value)) {
                break;
            }
            const std::string text = value.string();
            int frames = 0;
            const auto [end, parseError] = std::from_chars(text.data(), text.data() + text.size(), frames);
            if (parseError != std::errc{} || end != text.data() + text.size() || frames < 1) {
                options.error = std::string(kFramesOptionUVE) + " requires a positive whole number";
                break;
            }
            options.frameLimit = frames;
        } else if (argument == kScreenshotOptionUVE) {
            static_cast<void>(takeValue(index, kScreenshotOptionUVE, options.screenshot));
        } else if (argument == kEngineRootOptionUVE) {
            static_cast<void>(takeValue(index, kEngineRootOptionUVE, options.engineRoot));
        } else if (argument == UVE::Pack::kEditorProjectRegistryOptionUVE) {
            static_cast<void>(takeValue(index, UVE::Pack::kEditorProjectRegistryOptionUVE, options.registryFile));
        } else if (argument == kPageOptionUVE) {
            std::filesystem::path value;
            if (!takeValue(index, kPageOptionUVE, value)) {
                break;
            }
            const auto choice = std::find_if(kPageChoicesUVE.begin(), kPageChoicesUVE.end(),
                                             [&value](const PageChoiceUVE& item) { return item.word == value.string(); });
            if (choice == kPageChoicesUVE.end()) {
                options.error = std::string(kPageOptionUVE) + " must be projects, new-project, engines or settings";
                break;
            }
            options.page = choice->page;
        } else if (argument == kInstallPackageOptionUVE) {
            static_cast<void>(takeValue(index, kInstallPackageOptionUVE, options.installPackage));
        } else if (argument == kVersionOptionUVE) {
            options.printVersion = true;
        } else if (!argument.starts_with("--") &&
                   std::filesystem::path(argument).extension() == UVE::Platform::kEditorProjectFileExtensionUVE) {
            options.projectFile = std::filesystem::path(argument);
        } else if (argument == kSettingsOptionUVE) {
            static_cast<void>(takeValue(index, kSettingsOptionUVE, options.settingsFile));
        } else {
            options.error = "unknown option " + std::string(argument);
        }
    }
    return options;
}

/// Installs a package file for offline machines and release checks; returns the process exit code.
[[nodiscard]] int InstallPackageUVE(const std::filesystem::path& package, const std::filesystem::path& enginesRoot) {
    const UVE::Online::EngineInstallOutcomeUVE outcome = UVE::Online::InstallLocalEnginePackageUVE(package, enginesRoot);
    (outcome.installed ? std::cout : std::cerr) << "uve_hub: " << outcome.message << '\n';
    return outcome.installed ? 0 : kStartupFailedExitCodeUVE;
}

[[nodiscard]] std::filesystem::path ExecutableDirectoryUVE(const char* const argv0) {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(std::filesystem::path(argv0), error);
    return error ? std::filesystem::current_path(error) : executable.parent_path();
}

} // namespace

/// Starts the UniVex Hub: lists the user's projects, creates new ones from the engine's templates
/// and opens them in the engine's editor. `--engine-root <dir>` names the engine installation
/// (default: the Hub's own directory). `--project-registry <file>` and `--settings <file>` keep an
/// isolated run away from the user's own lists. `--page <projects|new-project|engines|settings>` chooses the
/// first page. `--frames <n>` and `--screenshot <png>` bound a run for automation.
/// `--install-package <file>` installs an engine package file (checksum beside it) and exits.
int main(const int argc, char** argv) {
    const HubLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);
    if (options.error.has_value()) {
        std::cerr << "uve_hub: " << *options.error << '\n';
        return kInvalidCommandLineExitCodeUVE;
    }
    if (options.printVersion) {
        std::cout << UVE::Online::kHubVersionUVE.ToStringUVE() << '\n';
        return 0;
    }
    const std::filesystem::path executableDirectory = ExecutableDirectoryUVE(argv[0]);

    UVE::Hub::HubContextUVE context;
    // An installed Hub carries no engine; a development build sits beside the editor built from the
    // same source, which then counts as a local engine of this source's version.
    const UVE::Pack::EngineInstallResultUVE local = UVE::Pack::ResolveEngineInstallUVE(
        options.engineRoot.empty() ? executableDirectory : options.engineRoot,
        UVE::Core::EngineCoreUVE::GetEngineVersionUVE());
    if (local.IsFoundUVE()) {
        context.localEngine = *local.install;
    } else if (!options.engineRoot.empty()) {
        std::cerr << "uve_hub: " << local.message << '\n';
        return kStartupFailedExitCodeUVE;
    }
    context.templatesDirectory = executableDirectory / UVE::Pack::kProjectTemplatesDirectoryNameUVE;
    context.registryOverridden = !options.registryFile.empty();
    if (context.registryOverridden) {
        context.registryFile = options.registryFile;
    } else {
        std::string registryError;
        const std::optional<std::filesystem::path> registry = UVE::Pack::DefaultProjectRegistryFileUVE(&registryError);
        if (!registry.has_value()) {
            std::cerr << "uve_hub: " << registryError << '\n';
            return kStartupFailedExitCodeUVE;
        }
        context.registryFile = *registry;
    }
    context.settingsFile = options.settingsFile.empty()
                               ? context.registryFile.parent_path() / UVE::Pack::kLauncherSettingsFileNameUVE
                               : options.settingsFile;
    // Downloaded engines sit beside the lists, so an isolated run stays isolated.
    context.enginesRoot = context.registryFile.parent_path() / UVE::Online::kEnginesDirectoryNameUVE;
    context.feed = UVE::Online::LoadEngineFeedSourceUVE(executableDirectory / UVE::Online::kEngineFeedFileNameUVE);

    if (!options.installPackage.empty()) {
        return InstallPackageUVE(options.installPackage, context.enginesRoot);
    }

    // Opening a project file goes straight to its engine; the window only appears when it cannot.
    std::string openMessage;
    if (!options.projectFile.empty()) {
        std::optional<UVE::Online::InstalledEngineUVE> localEngine;
        if (context.localEngine.has_value()) {
            localEngine = UVE::Online::InstalledEngineUVE{*context.localEngine, UVE::Online::EngineChannelUVE::Stable, true};
        }
        const UVE::Hub::ProjectOpenResultUVE opened = UVE::Hub::OpenProjectInEngineUVE(
            std::filesystem::absolute(options.projectFile),
            UVE::Online::ListInstalledEnginesUVE(context.enginesRoot, localEngine), context.registryFile,
            context.registryOverridden ? context.registryFile : std::filesystem::path{});
        if (opened.launched) {
            return 0;
        }
        openMessage = opened.message;
    }

    UVE::LauncherUI::LauncherWindowDescUVE window;
    window.title = std::string(UVE::Hub::kHubWindowTitleUVE);
    window.width = kHubWindowWidthUVE;
    window.height = kHubWindowHeightUVE;
    window.prismAssetsDirectory = executableDirectory / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    window.logFile = context.registryFile.parent_path() / UVE::Hub::kHubLogFileNameUVE;
    window.commandLine = std::vector<std::string>(argv + 1, argv + argc);
    window.frameLimit = options.frameLimit;
    window.screenshot = options.screenshot;

    UVE::Hub::HubWindowUVE hub(std::move(context));
    return UVE::LauncherUI::RunLauncherWindowUVE(
        window,
        [&hub, &options, &openMessage] {
            prism::Ref<prism::Widget> root = hub.BuildUVE();
            hub.ShowPageUVE(options.page);
            if (!openMessage.empty()) {
                hub.ShowMessageUVE(openMessage, true);
            }
            return root;
        },
        [&hub] { hub.TickUVE(); });
}
