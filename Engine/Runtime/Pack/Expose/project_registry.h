// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor_project_package.h"

namespace UVE::Pack {

/// Directory under the per-user data root where UniVex tools keep the user's own data.
inline constexpr std::string_view kToolsUserDataDirectoryName = "UniVex";
inline constexpr std::string_view kProjectRegistryFileName = "projects.uvregistry";
inline constexpr std::string_view kProjectRegistryFormat = "uvregistry";
inline constexpr std::uint32_t kCurrentProjectRegistrySchemaVersion = 1U;
/// How many projects that are not favorites the registry remembers unless the user chose otherwise.
inline constexpr std::size_t kDefaultRecentProjectLimit = 32U;
/// Bounds on the user's choice; favorites are never trimmed by the limit.
inline constexpr std::size_t kMinimumRecentProjectLimit = 1U;
inline constexpr std::size_t kMaximumRecentProjectLimit = 512U;
inline constexpr std::uintmax_t kMaximumProjectRegistryBytes = 8U * 1024U * 1024U;

/// One project the user has opened.
struct ProjectRegistryEntry final {
    /// Absolute, normalized path of the project manifest; identifies the entry.
    std::filesystem::path projectFile;
    std::string displayName;
    /// Seconds since the Unix epoch when the project was last opened.
    std::int64_t lastOpenedUnixSeconds = 0;
    Platform::EditorProjectVersion engineVersion{};
    bool favorite = false;
    /// The engine version the user chose to open this project with; empty lets the launcher choose.
    std::string engineChoice;

    [[nodiscard]] bool operator==(const ProjectRegistryEntry&) const = default;
};

enum class ProjectRegistryCode : std::uint8_t {
    Applied = 0,
    ReadFailed,
    ParseFailed,
    UnsupportedSchema,
    WriteFailed,
};

struct ProjectRegistryResult final {
    ProjectRegistryCode code = ProjectRegistryCode::Applied;
    std::string message;

    [[nodiscard]] bool IsApplied() const noexcept { return code == ProjectRegistryCode::Applied; }
};

/// The projects a user has opened, shared by every UniVex tool on the machine. Entries are kept
/// most recently opened first. A project that disappears from disk is kept and reported missing,
/// never dropped silently; the user removes it. Not thread-safe; each tool owns its own instance
/// and reloads before changing the file, so concurrent tools only race on the final rename.
class ProjectRegistry final {
public:
    /// Loads `file`. A missing file is an empty registry, not an error.
    [[nodiscard]] ProjectRegistryResult Load(const std::filesystem::path& file);
    /// Writes the registry atomically to `file`, creating its directory.
    [[nodiscard]] ProjectRegistryResult Save(const std::filesystem::path& file) const;

    /// Moves `projectFile` to the front with the given details, adding it when new, then trims the
    /// oldest entries that are not favorites beyond the recent limit.
    void RecordOpened(const std::filesystem::path& projectFile, std::string displayName,
                         const Platform::EditorProjectVersion& engineVersion, std::int64_t openedUnixSeconds);
    /// False when the project is not in the registry.
    bool SetFavorite(const std::filesystem::path& projectFile, bool favorite);
    /// False when the project is not in the registry.
    bool Remove(const std::filesystem::path& projectFile);
    /// Records the engine version to open the project with ("" to let the launcher choose). False when
    /// the project is not in the registry.
    bool SetEngineChoice(const std::filesystem::path& projectFile, std::string engineVersion);

    [[nodiscard]] const std::vector<ProjectRegistryEntry>& Entries() const noexcept { return m_entries; }
    [[nodiscard]] const ProjectRegistryEntry* Find(const std::filesystem::path& projectFile) const;

    [[nodiscard]] std::size_t RecentLimit() const noexcept { return m_recentLimit; }
    /// Clamped to [kMinimumRecentProjectLimit, kMaximumRecentProjectLimit]; trims at once.
    void SetRecentLimit(std::size_t limit);

    /// Whether the entry's manifest is no longer on disk.
    [[nodiscard]] static bool IsMissing(const ProjectRegistryEntry& entry);
    /// The key an entry is stored under: absolute and normalized.
    [[nodiscard]] static std::filesystem::path NormalizeProjectFile(const std::filesystem::path& projectFile);

private:
    void Trim();

    std::vector<ProjectRegistryEntry> m_entries;
    std::size_t m_recentLimit = kDefaultRecentProjectLimit;
};

/// The current user's registry file, under kToolsUserDataDirectoryName in the per-user data root.
[[nodiscard]] std::optional<std::filesystem::path> DefaultProjectRegistryFile(std::string* errorMessage = nullptr);

/// Reloads the registry at `file`, records that `projectFile` was opened now and saves it again.
[[nodiscard]] ProjectRegistryResult RecordProjectOpened(const std::filesystem::path& file,
                                                              const std::filesystem::path& projectFile,
                                                              const Platform::EditorProjectPackage& package);

} // namespace UVE::Pack
