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

enum class ProjectListSortUVE : std::uint8_t {
    RecentlyOpened = 0,
    Name,
};

/// What opening a listed project with the current engine would do.
enum class ProjectListStatusUVE : std::uint8_t {
    Ready = 0,
    NeedsUpgrade,
    OpensWithWarning,
    Incompatible,
    Missing,
    /// No installed engine can open it yet; one has to be downloaded first.
    EngineNotInstalled,
};

struct ProjectListRowUVE final {
    ProjectRegistryEntryUVE entry;
    ProjectListStatusUVE status = ProjectListStatusUVE::Ready;
    /// Short text for the row's badge, e.g. "Engine 0.1.0+build.1" or "Missing".
    std::string badge;
};

/// The rows a project launcher shows: entries whose name or path contains `query` (ignoring case),
/// favorites first, then in `sort` order, each with what opening it with `engineVersion` would do.
/// `isMissing` decides whether a manifest is gone; it defaults to checking the disk.
[[nodiscard]] std::vector<ProjectListRowUVE> BuildProjectListUVE(
    const std::vector<ProjectRegistryEntryUVE>& entries, std::string_view query, ProjectListSortUVE sort,
    const Core::VersionUVE& engineVersion,
    const std::function<bool(const ProjectRegistryEntryUVE&)>& isMissing = &ProjectRegistryUVE::IsMissingUVE);

/// What opening a project saved with `projectVersion` in engine `engineVersion` would do.
[[nodiscard]] ProjectListStatusUVE ProjectListStatusForUVE(const Platform::EditorProjectVersionUVE& projectVersion,
                                                           const Core::VersionUVE& engineVersion);

/// Settings of the project launcher itself, kept beside the project registry.
inline constexpr std::string_view kLauncherSettingsFileNameUVE = "launcher_settings.json";
inline constexpr std::string_view kLauncherSettingsFormatUVE = "uvlaunchersettings";
inline constexpr std::uint32_t kCurrentLauncherSettingsSchemaVersionUVE = 1U;
/// Folder under the user's home directory new projects go to until the user picks another.
inline constexpr std::string_view kDefaultProjectsDirectoryNameUVE = "UniVex Projects";

struct LauncherSettingsUVE final {
    std::filesystem::path defaultProjectLocation;
};

/// Reads `file`; a missing or unreadable file yields the defaults, with `defaultProjectLocation`
/// under the user's home directory.
[[nodiscard]] LauncherSettingsUVE LoadLauncherSettingsUVE(const std::filesystem::path& file);
[[nodiscard]] bool SaveLauncherSettingsUVE(const std::filesystem::path& file, const LauncherSettingsUVE& settings);

} // namespace UVE::Pack
