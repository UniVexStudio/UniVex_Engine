// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_registry.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "application_runtime.h"
#include "pack_json.h"

namespace UVE::Pack {
namespace {

using JsonUVE = nlohmann::json;

/// Suffix of the temporary sibling the registry is written through.
inline constexpr std::string_view kTemporaryFileSuffixUVE = ".tmp";

[[nodiscard]] ProjectRegistryResultUVE MakeResultUVE(const ProjectRegistryCodeUVE code, std::string message) {
    return {code, std::move(message)};
}

[[nodiscard]] JsonUVE VersionToJsonUVE(const Platform::EditorProjectVersionUVE& version) {
    return {{"major", version.major}, {"minor", version.minor}, {"patch", version.patch}, {"build", version.build}};
}

[[nodiscard]] Platform::EditorProjectVersionUVE VersionFromJsonUVE(const JsonUVE& json) {
    return {json.at("major").get<std::uint32_t>(), json.at("minor").get<std::uint32_t>(),
            json.at("patch").get<std::uint32_t>(), json.at("build").get<std::uint32_t>()};
}

[[nodiscard]] std::int64_t NowUnixSecondsUVE() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::filesystem::path ProjectRegistryUVE::NormalizeProjectFileUVE(const std::filesystem::path& projectFile) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(projectFile, error);
    return (error ? projectFile : absolute).lexically_normal();
}

ProjectRegistryResultUVE ProjectRegistryUVE::LoadUVE(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        m_entries.clear();
        m_recentLimit = kDefaultRecentProjectLimitUVE;
        return MakeResultUVE(ProjectRegistryCodeUVE::Applied, "No project registry yet.");
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size > kMaximumProjectRegistryBytesUVE) {
        return MakeResultUVE(ProjectRegistryCodeUVE::ReadFailed,
                             "The project registry is unreadable or too large: " + file.string());
    }
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return MakeResultUVE(ProjectRegistryCodeUVE::ReadFailed, "Unable to open the project registry: " + file.string());
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        return MakeResultUVE(ProjectRegistryCodeUVE::ReadFailed, "Unable to read the project registry: " + file.string());
    }
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kProjectRegistryFormatUVE) {
        return MakeResultUVE(ProjectRegistryCodeUVE::ParseFailed,
                             "The project registry is not a " + std::string(kProjectRegistryFormatUVE) +
                                 " document: " + file.string());
    }
    try {
        if (json.at("schemaVersion").get<std::uint32_t>() != kCurrentProjectRegistrySchemaVersionUVE) {
            return MakeResultUVE(ProjectRegistryCodeUVE::UnsupportedSchema,
                                 "The project registry uses an unsupported schema: " + file.string());
        }
        std::vector<ProjectRegistryEntryUVE> entries;
        for (const JsonUVE& item : json.at("projects")) {
            ProjectRegistryEntryUVE entry;
            entry.projectFile = NormalizeProjectFileUVE(item.at("projectFile").get<std::string>());
            entry.displayName = item.value("displayName", std::string{});
            entry.lastOpenedUnixSeconds = item.value("lastOpenedUnixSeconds", std::int64_t{0});
            entry.engineVersion = VersionFromJsonUVE(item.at("engineVersion"));
            entry.favorite = item.value("favorite", false);
            entry.engineChoice = item.value("engineChoice", std::string{});
            if (std::none_of(entries.begin(), entries.end(), [&entry](const ProjectRegistryEntryUVE& existing) {
                    return existing.projectFile == entry.projectFile;
                })) {
                entries.push_back(std::move(entry));
            }
        }
        m_entries = std::move(entries);
        SetRecentLimitUVE(json.value("recentLimit", kDefaultRecentProjectLimitUVE));
    } catch (const JsonUVE::exception& exception) {
        return MakeResultUVE(ProjectRegistryCodeUVE::ParseFailed,
                             "The project registry is malformed (" + std::string(exception.what()) + "): " +
                                 file.string());
    }
    return MakeResultUVE(ProjectRegistryCodeUVE::Applied, "Loaded the project registry.");
}

ProjectRegistryResultUVE ProjectRegistryUVE::SaveUVE(const std::filesystem::path& file) const {
    JsonUVE projects = JsonUVE::array();
    for (const ProjectRegistryEntryUVE& entry : m_entries) {
        projects.push_back({{"projectFile", entry.projectFile.generic_string()},
                            {"displayName", entry.displayName},
                            {"lastOpenedUnixSeconds", entry.lastOpenedUnixSeconds},
                            {"engineVersion", VersionToJsonUVE(entry.engineVersion)},
                            {"favorite", entry.favorite},
                            {"engineChoice", entry.engineChoice}});
    }
    const JsonUVE json{{"format", kProjectRegistryFormatUVE},
                       {"schemaVersion", kCurrentProjectRegistrySchemaVersionUVE},
                       {"recentLimit", m_recentLimit},
                       {"projects", std::move(projects)}};

    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error) {
            return MakeResultUVE(ProjectRegistryCodeUVE::WriteFailed,
                                 "Unable to create the project registry directory: " + error.message());
        }
    }
    std::filesystem::path temporary = file;
    temporary += kTemporaryFileSuffixUVE;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json.dump(kPackJsonIndentUVE);
        if (!output.good()) {
            return MakeResultUVE(ProjectRegistryCodeUVE::WriteFailed,
                                 "Unable to write the project registry: " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return MakeResultUVE(ProjectRegistryCodeUVE::WriteFailed,
                             "Unable to publish the project registry: " + error.message());
    }
    return MakeResultUVE(ProjectRegistryCodeUVE::Applied, "Saved the project registry.");
}

void ProjectRegistryUVE::RecordOpenedUVE(const std::filesystem::path& projectFile, std::string displayName,
                                         const Platform::EditorProjectVersionUVE& engineVersion,
                                         const std::int64_t openedUnixSeconds) {
    const std::filesystem::path key = NormalizeProjectFileUVE(projectFile);
    ProjectRegistryEntryUVE entry;
    const auto existing = std::find_if(m_entries.begin(), m_entries.end(),
                                       [&key](const ProjectRegistryEntryUVE& item) { return item.projectFile == key; });
    if (existing != m_entries.end()) {
        entry = std::move(*existing);
        m_entries.erase(existing);
    }
    entry.projectFile = key;
    entry.displayName = std::move(displayName);
    entry.engineVersion = engineVersion;
    entry.lastOpenedUnixSeconds = openedUnixSeconds;
    m_entries.insert(m_entries.begin(), std::move(entry));
    TrimUVE();
}

bool ProjectRegistryUVE::SetFavoriteUVE(const std::filesystem::path& projectFile, const bool favorite) {
    const std::filesystem::path key = NormalizeProjectFileUVE(projectFile);
    for (ProjectRegistryEntryUVE& entry : m_entries) {
        if (entry.projectFile == key) {
            entry.favorite = favorite;
            TrimUVE();
            return true;
        }
    }
    return false;
}

bool ProjectRegistryUVE::SetEngineChoiceUVE(const std::filesystem::path& projectFile, std::string engineVersion) {
    const std::filesystem::path key = NormalizeProjectFileUVE(projectFile);
    for (ProjectRegistryEntryUVE& entry : m_entries) {
        if (entry.projectFile == key) {
            entry.engineChoice = std::move(engineVersion);
            return true;
        }
    }
    return false;
}

bool ProjectRegistryUVE::RemoveUVE(const std::filesystem::path& projectFile) {
    const std::filesystem::path key = NormalizeProjectFileUVE(projectFile);
    const auto removed = std::remove_if(m_entries.begin(), m_entries.end(),
                                        [&key](const ProjectRegistryEntryUVE& entry) { return entry.projectFile == key; });
    const bool found = removed != m_entries.end();
    m_entries.erase(removed, m_entries.end());
    return found;
}

const ProjectRegistryEntryUVE* ProjectRegistryUVE::FindUVE(const std::filesystem::path& projectFile) const {
    const std::filesystem::path key = NormalizeProjectFileUVE(projectFile);
    const auto found = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&key](const ProjectRegistryEntryUVE& entry) { return entry.projectFile == key; });
    return found == m_entries.end() ? nullptr : &*found;
}

void ProjectRegistryUVE::SetRecentLimitUVE(const std::size_t limit) {
    m_recentLimit = std::clamp(limit, kMinimumRecentProjectLimitUVE, kMaximumRecentProjectLimitUVE);
    TrimUVE();
}

bool ProjectRegistryUVE::IsMissingUVE(const ProjectRegistryEntryUVE& entry) {
    std::error_code error;
    return !std::filesystem::is_regular_file(entry.projectFile, error);
}

void ProjectRegistryUVE::TrimUVE() {
    // Entries are newest first, so the non-favorites past the limit are the oldest ones.
    std::size_t recentKept = 0U;
    std::erase_if(m_entries, [this, &recentKept](const ProjectRegistryEntryUVE& entry) {
        if (entry.favorite) {
            return false;
        }
        return ++recentKept > m_recentLimit;
    });
}

std::optional<std::filesystem::path> DefaultProjectRegistryFileUVE(std::string* const errorMessage) {
    const Platform::ApplicationUserDataDirectoryResultUVE directory =
        Platform::ResolveToolUserDataDirectoryUVE(kToolsUserDataDirectoryNameUVE);
    if (!directory.success) {
        if (errorMessage != nullptr) {
            *errorMessage = directory.message;
        }
        return std::nullopt;
    }
    return directory.directory / kProjectRegistryFileNameUVE;
}

ProjectRegistryResultUVE RecordProjectOpenedUVE(const std::filesystem::path& file,
                                                const std::filesystem::path& projectFile,
                                                const Platform::EditorProjectPackageUVE& package) {
    ProjectRegistryUVE registry;
    if (ProjectRegistryResultUVE loaded = registry.LoadUVE(file); !loaded.IsAppliedUVE()) {
        // A damaged registry is left for the user to inspect rather than overwritten.
        return loaded;
    }
    registry.RecordOpenedUVE(projectFile, package.displayName, package.engineVersion, NowUnixSecondsUVE());
    return registry.SaveUVE(file);
}

} // namespace UVE::Pack
