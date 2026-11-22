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
/// and calls through kEditorAndroidEntryPointUVE.
inline constexpr std::string_view kEditorExecutableStemUVE = "uve_editor";
/// The C function an Android editor library exports:
/// int UveEditorRunUVE(android_app* app, int argc, char** argv), which returns the editor's exit code.
inline constexpr std::string_view kEditorAndroidEntryPointUVE = "UveEditorRunUVE";
/// Editor options a launcher passes; they match the editor's own command line.
inline constexpr std::string_view kEditorProjectOptionUVE = "--project";
inline constexpr std::string_view kEditorProjectRegistryOptionUVE = "--project-registry";

/// One installed engine: where its editor and shipped templates are, and which version it is.
/// Today a launcher uses the engine installed beside it; a version manager can supply others.
struct EngineInstallUVE final {
    Core::VersionUVE version;
    std::filesystem::path root;
    std::filesystem::path editorExecutable;
    std::filesystem::path templatesDirectory;
};

struct EngineInstallResultUVE final {
    std::string message;
    std::optional<EngineInstallUVE> install;

    [[nodiscard]] bool IsFoundUVE() const noexcept { return install.has_value(); }
};

/// Describes the engine installed in `installRoot`, whose version is `version`. Fails when the
/// directory holds no editor executable.
[[nodiscard]] EngineInstallResultUVE ResolveEngineInstallUVE(const std::filesystem::path& installRoot,
                                                             const Core::VersionUVE& version);

/// The editor command line that opens `projectFile`, recording it in `registryFile` when one is
/// given instead of the user's own registry.
[[nodiscard]] std::vector<std::string> MakeEditorLaunchArgumentsUVE(const std::filesystem::path& projectFile,
                                                                    const std::filesystem::path& registryFile = {});

} // namespace UVE::Pack
