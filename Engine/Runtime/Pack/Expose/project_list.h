// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine_version.h"
#include "project_compatibility.h"
#include "project_registry.h"

namespace UVE::Pack {

enum class ProjectListSort : std::uint8_t {
    RecentlyOpened = 0,
    Name,
};

/// What opening a listed project with the current engine would do.
enum class ProjectListStatus : std::uint8_t {
    Ready = 0,
    NeedsUpgrade,
    OpensWithWarning,
    Incompatible,
    Missing,
    /// No installed engine can open it yet; one has to be downloaded first.
    EngineNotInstalled,
};

struct ProjectListRow final {
    ProjectRegistryEntry entry;
    ProjectListStatus status = ProjectListStatus::Ready;
    /// Short text for the row's badge, e.g. "Engine 0.1.0+build.1" or "Missing".
    std::string badge;
};

/// The rows a project launcher shows: entries whose name or path contains `query` (ignoring case),
/// favorites first, then in `sort` order, each with what opening it with `engineVersion` would do.
/// `isMissing` decides whether a manifest is gone; it defaults to checking the disk.
[[nodiscard]] std::vector<ProjectListRow> BuildProjectList(
    const std::vector<ProjectRegistryEntry>& entries, std::string_view query, ProjectListSort sort,
    const Core::Version& engineVersion,
    const std::function<bool(const ProjectRegistryEntry&)>& isMissing = &ProjectRegistry::IsMissing);

/// What opening a project saved with `projectVersion` in engine `engineVersion` would do.
[[nodiscard]] ProjectListStatus ProjectListStatusFor(const Platform::EditorProjectVersion& projectVersion,
                                                           const Core::Version& engineVersion);

/// Settings of the project launcher itself, kept beside the project registry.
inline constexpr std::string_view kLauncherSettingsFileName = "launcher_settings.json";
inline constexpr std::string_view kLauncherSettingsFormat = "uvlaunchersettings";
inline constexpr std::uint32_t kCurrentLauncherSettingsSchemaVersion = 1U;
/// Folder under the user's home directory new projects go to until the user picks another.
inline constexpr std::string_view kDefaultProjectsDirectoryName = "UniVex Projects";

struct LauncherSettings final {
    std::filesystem::path defaultProjectLocation;
};

/// Reads `file`; a missing or unreadable file yields the defaults, with `defaultProjectLocation`
/// under the user's home directory.
[[nodiscard]] LauncherSettings LoadLauncherSettings(const std::filesystem::path& file);
[[nodiscard]] bool SaveLauncherSettings(const std::filesystem::path& file, const LauncherSettings& settings);

} // namespace UVE::Pack
