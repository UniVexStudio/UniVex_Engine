// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "application_runtime.h"
#include "tar_gz_archive.h"
#include "hub_setup.h"
#include "launcher_files.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "setup_window.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

inline constexpr std::string_view kSetupTitleUVE = "UniVex Hub Setup";
inline constexpr std::string_view kUninstallTitleUVE = "Remove UniVex Hub";
inline constexpr std::uint32_t kSetupWidthUVE = 820U;
inline constexpr std::uint32_t kSetupHeightUVE = 520U;
inline constexpr std::string_view kSetupLogFileNameUVE = "univex-hub-setup.log";
inline constexpr int kInvalidCommandLineExitCodeUVE = 2;
inline constexpr int kSetupFailedExitCodeUVE = 1;
/// How often a silent run checks on the installation.
inline constexpr std::chrono::milliseconds kSilentPollIntervalUVE{50};
/// Temporary folder holding the interface assets unpacked from the payload.
inline constexpr std::string_view kInterfaceScratchPrefixUVE = "univex-hub-setup-ui-";
/// Inside that scratch folder: the payload's copy and the folder its interface assets unpack into.
inline constexpr std::string_view kPayloadArchiveStemUVE = "payload";
inline constexpr std::string_view kInterfaceUnpackDirectoryNameUVE = "ui";

inline constexpr std::string_view kInstallDirOptionUVE = "--install-dir";
inline constexpr std::string_view kSilentOptionUVE = "--silent";
inline constexpr std::string_view kUninstallOptionUVE = "--uninstall";
inline constexpr std::string_view kNoApplicationsEntryOptionUVE = "--no-applications-entry";
inline constexpr std::string_view kEntriesRootOptionUVE = "--entries-root";
inline constexpr std::string_view kStepOptionUVE = "--step";
inline constexpr std::string_view kFramesOptionUVE = "--frames";
inline constexpr std::string_view kScreenshotOptionUVE = "--screenshot";
#if defined(_WIN32)
/// Marks the copy of the uninstaller that runs from the temporary folder.
inline constexpr std::string_view kRelocatedOptionUVE = "--relocated";
#endif

struct StepChoiceUVE final {
    std::string_view word;
    UVE::Setup::SetupStepUVE step;
};
inline constexpr std::array<StepChoiceUVE, 3U> kStepChoicesUVE{{
    {"welcome", UVE::Setup::SetupStepUVE::Welcome},
    {"location", UVE::Setup::SetupStepUVE::Location},
    {"done", UVE::Setup::SetupStepUVE::Done},
}};

struct SetupOptionsUVE final {
    std::filesystem::path installDirectory;
    std::filesystem::path entriesRoot;
    bool silent = false;
    bool uninstall = false;
    bool addToApplications = true;
    bool relocated = false;
    UVE::Setup::SetupStepUVE step = UVE::Setup::SetupStepUVE::Welcome;
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
    std::optional<std::string> error;
};

[[nodiscard]] SetupOptionsUVE ParseOptionsUVE(const int argc, char** argv) {
    SetupOptionsUVE options;
    const auto value = [&](int& index, const std::string_view option) -> std::optional<std::string> {
        if (index + 1 >= argc || std::string_view{argv[index + 1]}.starts_with("--")) {
            options.error = std::string(option) + " requires a value";
            return std::nullopt;
        }
        return std::string(argv[++index]);
    };
    for (int index = 1; index < argc && !options.error.has_value(); ++index) {
        const std::string_view argument{argv[index]};
        if (argument == kInstallDirOptionUVE) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.installDirectory = *text;
            }
        } else if (argument == kEntriesRootOptionUVE) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.entriesRoot = *text;
            }
        } else if (argument == kSilentOptionUVE) {
            options.silent = true;
        } else if (argument == kUninstallOptionUVE) {
            options.uninstall = true;
        } else if (argument == kNoApplicationsEntryOptionUVE) {
            options.addToApplications = false;
#if defined(_WIN32)
        } else if (argument == kRelocatedOptionUVE) {
            options.relocated = true;
#endif
        } else if (argument == kStepOptionUVE) {
            if (const std::optional<std::string> text = value(index, argument)) {
                const auto choice = std::find_if(kStepChoicesUVE.begin(), kStepChoicesUVE.end(),
                                                 [&text](const StepChoiceUVE& item) { return item.word == *text; });
                if (choice == kStepChoicesUVE.end()) {
                    options.error = std::string(kStepOptionUVE) + " must be welcome, location or done";
                } else {
                    options.step = choice->step;
                }
            }
        } else if (argument == kFramesOptionUVE) {
            if (const std::optional<std::string> text = value(index, argument)) {
                int frames = 0;
                const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), frames);
                if (error != std::errc{} || end != text->data() + text->size() || frames < 1) {
                    options.error = std::string(kFramesOptionUVE) + " requires a positive whole number";
                } else {
                    options.frameLimit = frames;
                }
            }
        } else if (argument == kScreenshotOptionUVE) {
            if (const std::optional<std::string> text = value(index, argument)) {
                options.screenshot = *text;
            }
        } else {
            options.error = "unknown option " + std::string(argument);
        }
    }
    return options;
}

#if defined(_WIN32)
/// A running program cannot delete its own file on Windows, so the uninstaller re-runs itself from
/// the temporary folder and lets that copy remove the installation. True when the copy took over.
[[nodiscard]] bool RelaunchOutsideInstallationUVE(const std::filesystem::path& self, int argc, char** argv) {
    std::error_code error;
    const std::filesystem::path copy = std::filesystem::temp_directory_path(error) / self.filename();
    if (error || !std::filesystem::copy_file(self, copy, std::filesystem::copy_options::overwrite_existing, error)) {
        return false;
    }
    std::vector<std::string> arguments(argv + 1, argv + argc);
    arguments.emplace_back(kRelocatedOptionUVE);
    if (!UVE::Platform::LaunchDetachedProcessUVE({copy, arguments, {}}).launched) {
        return false;
    }
    // The temporary copy cleans itself up at the next restart.
    MoveFileExW(copy.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return true;
}
#endif

/// A single-file setup has no PRISM assets beside it; they travel in the payload. This unpacks just
/// those into `scratch` and returns their folder, or empty when the payload has none.
[[nodiscard]] std::filesystem::path UnpackInterfaceAssetsUVE(const UVE::Setup::SetupContextUVE& context,
                                                             const std::filesystem::path& scratch) {
    if (scratch.empty()) {
        return {};
    }
    const std::filesystem::path unpacked = scratch / kInterfaceUnpackDirectoryNameUVE;
    const std::filesystem::path archive =
        scratch / (std::string(kPayloadArchiveStemUVE) + std::string(UVE::Online::kTarGzFileExtensionUVE));
    std::error_code error;
    std::filesystem::create_directories(unpacked, error);
    if (error || !context.payload.has_value() ||
        !UVE::Online::ExtractSetupPayloadUVE(context.setupExecutable, *context.payload, archive)) {
        return {};
    }
    UVE::Online::ArchiveLimitsUVE limits;
    limits.include = [](const std::filesystem::path& path) {
        return !path.empty() && *path.begin() == UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    };
    const bool extracted = UVE::Online::ExtractTarGzUVE(archive, unpacked, limits).extracted;
    std::filesystem::remove(archive, error);
    return extracted ? unpacked / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE : std::filesystem::path{};
}

} // namespace

/// UniVex Hub's installer. With a payload appended (see Engine/Tools/make_hub_installer.py) it
/// installs or updates the Hub and its engine for the current user; `--uninstall` removes it again.
/// `--silent` works without a window; `--install-dir <dir>` chooses the folder;
/// `--no-applications-entry` skips the applications menu; `--entries-root <dir>` writes that entry
/// elsewhere (isolated runs). `--step`, `--frames` and `--screenshot` drive the window for automation.
int main(const int argc, char** argv) {
    const SetupOptionsUVE options = ParseOptionsUVE(argc, argv);
    if (options.error.has_value() || options.silent) {
        UVE::Platform::AttachParentConsoleForCliUVE();
    }
    if (options.error.has_value()) {
        std::cerr << "uve_hub_setup: " << *options.error << '\n';
        return kInvalidCommandLineExitCodeUVE;
    }
    const std::filesystem::path self = UVE::Platform::CurrentExecutablePathUVE();
    std::error_code error;
    const std::filesystem::path selfDirectory =
        self.empty() ? std::filesystem::absolute(std::filesystem::path(argv[0]), error).parent_path() : self.parent_path();

    UVE::Setup::SetupContextUVE context;
    context.mode = options.uninstall ? UVE::Setup::SetupModeUVE::Uninstall : UVE::Setup::SetupModeUVE::Install;
    context.setupExecutable = self;
    context.payload = self.empty() ? std::nullopt : UVE::Online::LocateSetupPayloadUVE(self);
    context.entriesRoot = options.entriesRoot;
    context.version = UVE::Online::FormatDisplayVersionUVE(UVE::Online::kHubVersionUVE);
    if (!options.installDirectory.empty()) {
        context.installDirectory = std::filesystem::absolute(options.installDirectory, error);
    } else if (options.uninstall) {
        // The uninstaller lives inside the installation it removes.
        context.installDirectory = selfDirectory;
    } else if (const std::optional<std::filesystem::path> fallback = UVE::Online::DefaultHubInstallDirectoryUVE()) {
        context.installDirectory = *fallback;
    }

#if defined(_WIN32)
    if (options.uninstall && !options.relocated &&
        selfDirectory.lexically_normal() == context.installDirectory.lexically_normal() &&
        RelaunchOutsideInstallationUVE(self, argc, argv)) {
        return 0;
    }
#endif

    if (options.silent) {
        UVE::Setup::SetupJobUVE job(context, options.addToApplications);
        while (!job.IsFinishedUVE()) {
            std::this_thread::sleep_for(kSilentPollIntervalUVE);
        }
        const UVE::Online::HubSetupResultUVE result = job.ResultUVE();
        (result.succeeded ? std::cout : std::cerr) << "uve_hub_setup: " << result.message << '\n';
        return result.succeeded ? 0 : kSetupFailedExitCodeUVE;
    }

    UVE::LauncherUI::LauncherWindowDescUVE window;
    window.title = std::string(options.uninstall ? kUninstallTitleUVE : kSetupTitleUVE);
    window.width = kSetupWidthUVE;
    window.height = kSetupHeightUVE;
    // Assets beside the program (a development build, or the uninstaller inside an installation)
    // are used as they are; a downloaded setup unpacks its own.
    window.prismAssetsDirectory = selfDirectory / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    const std::filesystem::path scratch = UVE::LauncherUI::UniqueTemporaryPathUVE(kInterfaceScratchPrefixUVE, {});
    if (!std::filesystem::is_directory(window.prismAssetsDirectory, error)) {
        window.prismAssetsDirectory = UnpackInterfaceAssetsUVE(context, scratch);
    }
    window.logFile = std::filesystem::temp_directory_path(error) / kSetupLogFileNameUVE;
    window.commandLine = std::vector<std::string>(argv + 1, argv + argc);
    window.frameLimit = options.frameLimit;
    window.screenshot = options.screenshot;

    UVE::Setup::SetupWindowUVE setup(std::move(context));
    const int exitCode = UVE::LauncherUI::RunLauncherWindowUVE(
        window,
        [&setup, &options] {
            prism::Ref<prism::Widget> root = setup.BuildUVE();
            setup.ShowStepUVE(options.step);
            return root;
        },
        [&setup] { setup.TickUVE(); });
    std::filesystem::remove_all(scratch, error);
    return exitCode;
}
