// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "application_runtime.h"
#include "editor_host.h"
#include "engine_core.h"
#include "engine_install.h"
#include "hub_window.h"
#include "logging_macros.h"
#include "project_list.h"
#include "project_registry.h"

namespace {

inline constexpr std::string_view kHubWindowTitleUVE = "UniVex Hub";
inline constexpr std::uint32_t kHubWindowWidthUVE = 1200U;
inline constexpr std::uint32_t kHubWindowHeightUVE = 760U;
inline constexpr std::string_view kHubLogFileNameUVE = "uve_hub.log";
/// PRISM's fonts, themes and locales ship beside the executable under this name.
inline constexpr std::string_view kPrismAssetsDirectoryNameUVE = "prism_assets";
/// Frame-rate caps: the Hub is a quiet window and needs no more.
inline constexpr std::uint32_t kFocusedFrameRateCapUVE = 60U;
inline constexpr std::uint32_t kUnfocusedFrameRateCapUVE = 10U;
inline constexpr int kInvalidCommandLineExitCodeUVE = 2;
inline constexpr int kStartupFailedExitCodeUVE = 1;

inline constexpr std::string_view kFramesOptionUVE = "--frames";
inline constexpr std::string_view kScreenshotOptionUVE = "--screenshot";
inline constexpr std::string_view kEngineRootOptionUVE = "--engine-root";
inline constexpr std::string_view kSettingsOptionUVE = "--settings";
inline constexpr std::string_view kPageOptionUVE = "--page";

struct PageChoiceUVE final {
    std::string_view word;
    UVE::Hub::HubPageUVE page;
};
inline constexpr std::array<PageChoiceUVE, 3U> kPageChoicesUVE{{
    {"projects", UVE::Hub::HubPageUVE::Projects},
    {"new-project", UVE::Hub::HubPageUVE::NewProject},
    {"settings", UVE::Hub::HubPageUVE::Settings},
}};

struct HubLaunchOptionsUVE final {
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
    std::filesystem::path engineRoot;
    std::filesystem::path registryFile;
    std::filesystem::path settingsFile;
    UVE::Hub::HubPageUVE page = UVE::Hub::HubPageUVE::Projects;
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
                options.error = std::string(kPageOptionUVE) + " must be projects, new-project or settings";
                break;
            }
            options.page = choice->page;
        } else if (argument == kSettingsOptionUVE) {
            static_cast<void>(takeValue(index, kSettingsOptionUVE, options.settingsFile));
        } else {
            options.error = "unknown option " + std::string(argument);
        }
    }
    return options;
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
/// isolated run away from the user's own lists. `--page <projects|new-project|settings>` chooses the
/// first page. `--frames <n>` and `--screenshot <png>` bound a run for automation.
int main(const int argc, char** argv) {
    const HubLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);
    if (options.error.has_value()) {
        std::cerr << "uve_hub: " << *options.error << '\n';
        return kInvalidCommandLineExitCodeUVE;
    }
    const std::filesystem::path executableDirectory = ExecutableDirectoryUVE(argv[0]);

    const UVE::Pack::EngineInstallResultUVE engine = UVE::Pack::ResolveEngineInstallUVE(
        options.engineRoot.empty() ? executableDirectory : options.engineRoot,
        UVE::Core::EngineCoreUVE::GetEngineVersionUVE());
    if (!engine.IsFoundUVE()) {
        std::cerr << "uve_hub: " << engine.message << '\n';
        return kStartupFailedExitCodeUVE;
    }

    UVE::Hub::HubContextUVE context;
    context.engine = *engine.install;
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

    UVE::Core::EngineConfigUVE config{};
    config.windowTitle = std::string(kHubWindowTitleUVE);
    config.windowWidth = kHubWindowWidthUVE;
    config.windowHeight = kHubWindowHeightUVE;
    config.logFilePath = context.registryFile.parent_path() / kHubLogFileNameUVE;
    config.focusedFrameRateCapUVE = kFocusedFrameRateCapUVE;
    config.unfocusedFrameRateCapUVE = kUnfocusedFrameRateCapUVE;
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);

    UVE::Core::EngineCoreUVE core(config);
    try {
        core.Init();
        if (!core.Load()) {
            core.Shutdown();
            return kStartupFailedExitCodeUVE;
        }
        void* const nativeWindow = core.GetServicesUVE().GetWindowManagerUVE().GetNativeWindowHandleUVE();
        if (nativeWindow == nullptr) {
            std::cerr << "uve_hub: the Hub needs a window and none could be created\n";
            core.Shutdown();
            return kStartupFailedExitCodeUVE;
        }
        UVE::Editor::EditorHostUVE host;
        if (!host.InitializeUVE(static_cast<GLFWwindow*>(nativeWindow),
                                executableDirectory / kPrismAssetsDirectoryNameUVE)) {
            std::cerr << "uve_hub: the Hub's interface could not start\n";
            core.Shutdown();
            return kStartupFailedExitCodeUVE;
        }
        UVE::Hub::HubWindowUVE hub(std::move(context));
        host.SetRootUVE(hub.BuildUVE());
        hub.ShowPageUVE(options.page);
        core.SetPostRenderCallbackUVE([&host] {
            host.BeginFrameUVE();
            host.RenderUVE(std::span<const UVE::Editor::EditorHostUVE::ViewImageUVE>{});
        });

        int framesRun = 0;
        while (!host.QuitRequestedUVE() && !core.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!options.frameLimit.has_value() || framesRun < *options.frameLimit)) {
            core.TickFrameUVE();
            ++framesRun;
        }
        if (!options.screenshot.empty() && !host.SaveScreenshotUVE(options.screenshot)) {
            std::fprintf(stderr, "uve_hub: could not save the screenshot\n");
        }
        core.SetPostRenderCallbackUVE({});
        host.ShutdownUVE();
        core.Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("uve_hub: unhandled exception - shutting down: {}", exception.what());
    } catch (...) {
        UVE_FATAL("uve_hub: unhandled non-std::exception - shutting down");
    }
    if (core.GetStateUVE() == UVE::Core::EngineStateUVE::Running) {
        try {
            core.Shutdown();
        } catch (...) {
            UVE_FATAL("uve_hub: shutting down after an exception failed as well");
        }
    }
    return UVE::Core::EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
}
