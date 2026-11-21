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
inline constexpr std::string_view kToolsUserDataDirectoryNameUVE = "UniVex";
inline constexpr std::string_view kProjectRegistryFileNameUVE = "projects.uvregistry";
inline constexpr std::string_view kProjectRegistryFormatUVE = "uvregistry";
inline constexpr std::uint32_t kCurrentProjectRegistrySchemaVersionUVE = 1U;
/// How many projects that are not favorites the registry remembers unless the user chose otherwise.
inline constexpr std::size_t kDefaultRecentProjectLimitUVE = 32U;
/// Bounds on the user's choice; favorites are never trimmed by the limit.
inline constexpr std::size_t kMinimumRecentProjectLimitUVE = 1U;
inline constexpr std::size_t kMaximumRecentProjectLimitUVE = 512U;
inline constexpr std::uintmax_t kMaximumProjectRegistryBytesUVE = 8U * 1024U * 1024U;

/// One project the user has opened.
struct ProjectRegistryEntryUVE final {
    /// Absolute, normalized path of the project manifest; identifies the entry.
    std::filesystem::path projectFile;
    std::string displayName;
    /// Seconds since the Unix epoch when the project was last opened.
    std::int64_t lastOpenedUnixSeconds = 0;
    Platform::EditorProjectVersionUVE engineVersion{};
    bool favorite = false;
    /// The engine version the user chose to open this project with; empty lets the launcher choose.
    std::string engineChoice;

    [[nodiscard]] bool operator==(const ProjectRegistryEntryUVE&) const = default;
};

enum class ProjectRegistryCodeUVE : std::uint8_t {
    Applied = 0,
    ReadFailed,
    ParseFailed,
    UnsupportedSchema,
    WriteFailed,
};

struct ProjectRegistryResultUVE final {
    ProjectRegistryCodeUVE code = ProjectRegistryCodeUVE::Applied;
    std::string message;

    [[nodiscard]] bool IsAppliedUVE() const noexcept { return code == ProjectRegistryCodeUVE::Applied; }
};

/// The projects a user has opened, shared by every UniVex tool on the machine. Entries are kept
/// most recently opened first. A project that disappears from disk is kept and reported missing,
/// never dropped silently; the user removes it. Not thread-safe; each tool owns its own instance
/// and reloads before changing the file, so concurrent tools only race on the final rename.
class ProjectRegistryUVE final {
public:
    /// Loads `file`. A missing file is an empty registry, not an error.
    [[nodiscard]] ProjectRegistryResultUVE LoadUVE(const std::filesystem::path& file);
    /// Writes the registry atomically to `file`, creating its directory.
    [[nodiscard]] ProjectRegistryResultUVE SaveUVE(const std::filesystem::path& file) const;

    /// Moves `projectFile` to the front with the given details, adding it when new, then trims the
    /// oldest entries that are not favorites beyond the recent limit.
    void RecordOpenedUVE(const std::filesystem::path& projectFile, std::string displayName,
                         const Platform::EditorProjectVersionUVE& engineVersion, std::int64_t openedUnixSeconds);
    /// False when the project is not in the registry.
    bool SetFavoriteUVE(const std::filesystem::path& projectFile, bool favorite);
    /// False when the project is not in the registry.
    bool RemoveUVE(const std::filesystem::path& projectFile);
    /// Records the engine version to open the project with ("" to let the launcher choose). False when
    /// the project is not in the registry.
    bool SetEngineChoiceUVE(const std::filesystem::path& projectFile, std::string engineVersion);

    [[nodiscard]] const std::vector<ProjectRegistryEntryUVE>& EntriesUVE() const noexcept { return m_entries; }
    [[nodiscard]] const ProjectRegistryEntryUVE* FindUVE(const std::filesystem::path& projectFile) const;

    [[nodiscard]] std::size_t RecentLimitUVE() const noexcept { return m_recentLimit; }
    /// Clamped to [kMinimumRecentProjectLimitUVE, kMaximumRecentProjectLimitUVE]; trims at once.
    void SetRecentLimitUVE(std::size_t limit);

    /// Whether the entry's manifest is no longer on disk.
    [[nodiscard]] static bool IsMissingUVE(const ProjectRegistryEntryUVE& entry);
    /// The key an entry is stored under: absolute and normalized.
    [[nodiscard]] static std::filesystem::path NormalizeProjectFileUVE(const std::filesystem::path& projectFile);

private:
    void TrimUVE();

    std::vector<ProjectRegistryEntryUVE> m_entries;
    std::size_t m_recentLimit = kDefaultRecentProjectLimitUVE;
};

/// The current user's registry file, under kToolsUserDataDirectoryNameUVE in the per-user data root.
[[nodiscard]] std::optional<std::filesystem::path> DefaultProjectRegistryFileUVE(std::string* errorMessage = nullptr);

/// Reloads the registry at `file`, records that `projectFile` was opened now and saves it again.
[[nodiscard]] ProjectRegistryResultUVE RecordProjectOpenedUVE(const std::filesystem::path& file,
                                                              const std::filesystem::path& projectFile,
                                                              const Platform::EditorProjectPackageUVE& package);

} // namespace UVE::Pack
