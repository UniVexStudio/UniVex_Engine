// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_list.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "application_runtime.h"
#include "pack_json.h"

namespace UVE::Pack {
namespace {

using JsonUVE = nlohmann::json;

/// Suffix of the temporary sibling the settings are written through.
inline constexpr std::string_view kTemporaryFileSuffixUVE = ".tmp";
inline constexpr std::string_view kMissingBadgeUVE = "Missing";
inline constexpr std::string_view kEngineBadgePrefixUVE = "Engine ";

[[nodiscard]] std::string LowerUVE(const std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

[[nodiscard]] ProjectListStatusUVE StatusForUVE(const ProjectCompatibilityVerdictUVE verdict) noexcept {
    switch (verdict) {
    case ProjectCompatibilityVerdictUVE::Same:
    case ProjectCompatibilityVerdictUVE::OlderUpgradesSilently:
        return ProjectListStatusUVE::Ready;
    case ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade:
        return ProjectListStatusUVE::NeedsUpgrade;
    case ProjectCompatibilityVerdictUVE::NewerCompatible:
        return ProjectListStatusUVE::OpensWithWarning;
    case ProjectCompatibilityVerdictUVE::NewerIncompatible:
        return ProjectListStatusUVE::Incompatible;
    }
    return ProjectListStatusUVE::Incompatible;
}

} // namespace

std::vector<ProjectListRowUVE> BuildProjectListUVE(const std::vector<ProjectRegistryEntryUVE>& entries,
                                                   const std::string_view query, const ProjectListSortUVE sort,
                                                   const Core::VersionUVE& engineVersion,
                                                   const std::function<bool(const ProjectRegistryEntryUVE&)>& isMissing) {
    const std::string needle = LowerUVE(query);
    std::vector<ProjectListRowUVE> rows;
    rows.reserve(entries.size());
    for (const ProjectRegistryEntryUVE& entry : entries) {
        if (!needle.empty() && LowerUVE(entry.displayName).find(needle) == std::string::npos &&
            LowerUVE(entry.projectFile.string()).find(needle) == std::string::npos) {
            continue;
        }
        ProjectListRowUVE row;
        row.entry = entry;
        if (isMissing && isMissing(entry)) {
            row.status = ProjectListStatusUVE::Missing;
            row.badge = kMissingBadgeUVE;
        } else {
            row.status = StatusForUVE(EvaluateProjectCompatibilityUVE(entry.engineVersion, engineVersion).verdict);
            row.badge = std::string(kEngineBadgePrefixUVE) + FormatProjectVersionUVE(entry.engineVersion);
        }
        rows.push_back(std::move(row));
    }
    // Registry order is most recent first; a stable sort keeps it among equals.
    std::stable_sort(rows.begin(), rows.end(), [sort](const ProjectListRowUVE& lhs, const ProjectListRowUVE& rhs) {
        if (lhs.entry.favorite != rhs.entry.favorite) {
            return lhs.entry.favorite;
        }
        if (sort == ProjectListSortUVE::Name) {
            return LowerUVE(lhs.entry.displayName) < LowerUVE(rhs.entry.displayName);
        }
        return lhs.entry.lastOpenedUnixSeconds > rhs.entry.lastOpenedUnixSeconds;
    });
    return rows;
}

ProjectListStatusUVE ProjectListStatusForUVE(const Platform::EditorProjectVersionUVE& projectVersion,
                                             const Core::VersionUVE& engineVersion) {
    return StatusForUVE(EvaluateProjectCompatibilityUVE(projectVersion, engineVersion).verdict);
}

LauncherSettingsUVE LoadLauncherSettingsUVE(const std::filesystem::path& file) {
    LauncherSettingsUVE settings;
    const Platform::ApplicationUserDataDirectoryResultUVE documents = Platform::ResolveUserDocumentsDirectoryUVE();
    if (documents.success) {
        settings.defaultProjectLocation = documents.directory / kDefaultProjectsDirectoryNameUVE;
    } else if (const Platform::ApplicationUserDataDirectoryResultUVE home = Platform::ResolveUserHomeDirectoryUVE();
               home.success) {
        settings.defaultProjectLocation = home.directory / kDefaultProjectsDirectoryNameUVE;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return settings;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kLauncherSettingsFormatUVE ||
        json.value("schemaVersion", 0U) != kCurrentLauncherSettingsSchemaVersionUVE) {
        return settings;
    }
    const std::string location = json.value("defaultProjectLocation", std::string{});
    if (!location.empty() && std::filesystem::path(location).is_absolute()) {
#if defined(__ANDROID__)
        // Auto-migrate legacy private internal /data/user/... default to user-visible external storage.
        const Platform::ApplicationUserDataDirectoryResultUVE internalHome = Platform::ResolveUserHomeDirectoryUVE();
        const bool isLegacyPrivateDefault =
            internalHome.success &&
            std::filesystem::path(location).lexically_normal() ==
                (internalHome.directory / kDefaultProjectsDirectoryNameUVE).lexically_normal();
        if (!isLegacyPrivateDefault) {
            settings.defaultProjectLocation = location;
        }
#else
        settings.defaultProjectLocation = location;
#endif
    }
    return settings;
}

bool SaveLauncherSettingsUVE(const std::filesystem::path& file, const LauncherSettingsUVE& settings) {
    const JsonUVE json{{"format", kLauncherSettingsFormatUVE},
                       {"schemaVersion", kCurrentLauncherSettingsSchemaVersionUVE},
                       {"defaultProjectLocation", settings.defaultProjectLocation.generic_string()}};
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error) {
            return false;
        }
    }
    std::filesystem::path temporary = file;
    temporary += kTemporaryFileSuffixUVE;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json.dump(kPackJsonIndentUVE);
        if (!output.good()) {
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace UVE::Pack
