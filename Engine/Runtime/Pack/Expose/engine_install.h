// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine_version.h"

namespace UVE::Pack {

/// File stem of the editor executable inside an engine installation. On Android, where an app runs
/// no second program, the file of that name is the editor as a shared library, which the Hub loads
/// and calls through kEditorAndroidEntryPoint.
inline constexpr std::string_view kEditorExecutableStem = "uve_editor";
/// The C function an Android editor library exports:
/// int UveEditorRun(android_app* app, int argc, char** argv), which returns the editor's exit code.
inline constexpr std::string_view kEditorAndroidEntryPoint = "UveEditorRun";
/// Editor options a launcher passes; they match the editor's own command line.
inline constexpr std::string_view kEditorProjectOption = "--project";
inline constexpr std::string_view kEditorProjectRegistryOption = "--project-registry";

/// One installed engine: where its editor and shipped templates are, and which version it is.
/// Today a launcher uses the engine installed beside it; a version manager can supply others.
struct EngineInstall final {
    Core::Version version;
    std::filesystem::path root;
    std::filesystem::path editorExecutable;
    std::filesystem::path templatesDirectory;
};

struct EngineInstallResult final {
    std::string message;
    std::optional<EngineInstall> install;

    [[nodiscard]] bool IsFound() const noexcept { return install.has_value(); }
};

/// Describes the engine installed in `installRoot`, whose version is `version`. Fails when the
/// directory holds no editor executable.
[[nodiscard]] EngineInstallResult ResolveEngineInstall(const std::filesystem::path& installRoot,
                                                             const Core::Version& version);

/// The editor command line that opens `projectFile`, recording it in `registryFile` when one is
/// given instead of the user's own registry.
[[nodiscard]] std::vector<std::string> MakeEditorLaunchArguments(const std::filesystem::path& projectFile,
                                                                    const std::filesystem::path& registryFile = {});

} // namespace UVE::Pack
