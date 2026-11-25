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

using Json = nlohmann::json;

/// Suffix of the temporary sibling the registry is written through.
inline constexpr std::string_view kTemporaryFileSuffix = ".tmp";

[[nodiscard]] ProjectRegistryResult MakeResult(const ProjectRegistryCode code, std::string message) {
    return {code, std::move(message)};
}

[[nodiscard]] Json VersionToJson(const Platform::EditorProjectVersion& version) {
    return {{"major", version.major}, {"minor", version.minor}, {"patch", version.patch}, {"build", version.build}};
}

[[nodiscard]] Platform::EditorProjectVersion VersionFromJson(const Json& json) {
    return {json.at("major").get<std::uint32_t>(), json.at("minor").get<std::uint32_t>(),
            json.at("patch").get<std::uint32_t>(), json.at("build").get<std::uint32_t>()};
}

[[nodiscard]] std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::filesystem::path ProjectRegistry::NormalizeProjectFile(const std::filesystem::path& projectFile) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(projectFile, error);
    return (error ? projectFile : absolute).lexically_normal();
}

ProjectRegistryResult ProjectRegistry::Load(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        m_entries.clear();
        m_recentLimit = kDefaultRecentProjectLimit;
        return MakeResult(ProjectRegistryCode::Applied, "No project registry yet.");
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size > kMaximumProjectRegistryBytes) {
        return MakeResult(ProjectRegistryCode::ReadFailed,
                             "The project registry is unreadable or too large: " + file.string());
    }
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return MakeResult(ProjectRegistryCode::ReadFailed, "Unable to open the project registry: " + file.string());
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        return MakeResult(ProjectRegistryCode::ReadFailed, "Unable to read the project registry: " + file.string());
    }
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kProjectRegistryFormat) {
        return MakeResult(ProjectRegistryCode::ParseFailed,
                             "The project registry is not a " + std::string(kProjectRegistryFormat) +
                                 " document: " + file.string());
    }
    try {
        if (json.at("schemaVersion").get<std::uint32_t>() != kCurrentProjectRegistrySchemaVersion) {
            return MakeResult(ProjectRegistryCode::UnsupportedSchema,
                                 "The project registry uses an unsupported schema: " + file.string());
        }
        std::vector<ProjectRegistryEntry> entries;
        for (const Json& item : json.at("projects")) {
            ProjectRegistryEntry entry;
            entry.projectFile = NormalizeProjectFile(item.at("projectFile").get<std::string>());
            entry.displayName = item.value("displayName", std::string{});
            entry.lastOpenedUnixSeconds = item.value("lastOpenedUnixSeconds", std::int64_t{0});
            entry.engineVersion = VersionFromJson(item.at("engineVersion"));
            entry.favorite = item.value("favorite", false);
            entry.engineChoice = item.value("engineChoice", std::string{});
            if (std::none_of(entries.begin(), entries.end(), [&entry](const ProjectRegistryEntry& existing) {
                    return existing.projectFile == entry.projectFile;
                })) {
                entries.push_back(std::move(entry));
            }
        }
        m_entries = std::move(entries);
        SetRecentLimit(json.value("recentLimit", kDefaultRecentProjectLimit));
    } catch (const Json::exception& exception) {
        return MakeResult(ProjectRegistryCode::ParseFailed,
                             "The project registry is malformed (" + std::string(exception.what()) + "): " +
                                 file.string());
    }
    return MakeResult(ProjectRegistryCode::Applied, "Loaded the project registry.");
}

ProjectRegistryResult ProjectRegistry::Save(const std::filesystem::path& file) const {
    Json projects = Json::array();
    for (const ProjectRegistryEntry& entry : m_entries) {
        projects.push_back({{"projectFile", entry.projectFile.generic_string()},
                            {"displayName", entry.displayName},
                            {"lastOpenedUnixSeconds", entry.lastOpenedUnixSeconds},
                            {"engineVersion", VersionToJson(entry.engineVersion)},
                            {"favorite", entry.favorite},
                            {"engineChoice", entry.engineChoice}});
    }
    const Json json{{"format", kProjectRegistryFormat},
                       {"schemaVersion", kCurrentProjectRegistrySchemaVersion},
                       {"recentLimit", m_recentLimit},
                       {"projects", std::move(projects)}};

    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error) {
            return MakeResult(ProjectRegistryCode::WriteFailed,
                                 "Unable to create the project registry directory: " + error.message());
        }
    }
    std::filesystem::path temporary = file;
    temporary += kTemporaryFileSuffix;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << json.dump(kPackJsonIndent);
        if (!output.good()) {
            return MakeResult(ProjectRegistryCode::WriteFailed,
                                 "Unable to write the project registry: " + temporary.string());
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return MakeResult(ProjectRegistryCode::WriteFailed,
                             "Unable to publish the project registry: " + error.message());
    }
    return MakeResult(ProjectRegistryCode::Applied, "Saved the project registry.");
}

void ProjectRegistry::RecordOpened(const std::filesystem::path& projectFile, std::string displayName,
                                         const Platform::EditorProjectVersion& engineVersion,
                                         const std::int64_t openedUnixSeconds) {
    const std::filesystem::path key = NormalizeProjectFile(projectFile);
    ProjectRegistryEntry entry;
    const auto existing = std::find_if(m_entries.begin(), m_entries.end(),
                                       [&key](const ProjectRegistryEntry& item) { return item.projectFile == key; });
    if (existing != m_entries.end()) {
        entry = std::move(*existing);
        m_entries.erase(existing);
    }
    entry.projectFile = key;
    entry.displayName = std::move(displayName);
    entry.engineVersion = engineVersion;
    entry.lastOpenedUnixSeconds = openedUnixSeconds;
    m_entries.insert(m_entries.begin(), std::move(entry));
    Trim();
}

bool ProjectRegistry::SetFavorite(const std::filesystem::path& projectFile, const bool favorite) {
    const std::filesystem::path key = NormalizeProjectFile(projectFile);
    for (ProjectRegistryEntry& entry : m_entries) {
        if (entry.projectFile == key) {
            entry.favorite = favorite;
            Trim();
            return true;
        }
    }
    return false;
}

bool ProjectRegistry::SetEngineChoice(const std::filesystem::path& projectFile, std::string engineVersion) {
    const std::filesystem::path key = NormalizeProjectFile(projectFile);
    for (ProjectRegistryEntry& entry : m_entries) {
        if (entry.projectFile == key) {
            entry.engineChoice = std::move(engineVersion);
            return true;
        }
    }
    return false;
}

bool ProjectRegistry::Remove(const std::filesystem::path& projectFile) {
    const std::filesystem::path key = NormalizeProjectFile(projectFile);
    const auto removed = std::remove_if(m_entries.begin(), m_entries.end(),
                                        [&key](const ProjectRegistryEntry& entry) { return entry.projectFile == key; });
    const bool found = removed != m_entries.end();
    m_entries.erase(removed, m_entries.end());
    return found;
}

const ProjectRegistryEntry* ProjectRegistry::Find(const std::filesystem::path& projectFile) const {
    const std::filesystem::path key = NormalizeProjectFile(projectFile);
    const auto found = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&key](const ProjectRegistryEntry& entry) { return entry.projectFile == key; });
    return found == m_entries.end() ? nullptr : &*found;
}

void ProjectRegistry::SetRecentLimit(const std::size_t limit) {
    m_recentLimit = std::clamp(limit, kMinimumRecentProjectLimit, kMaximumRecentProjectLimit);
    Trim();
}

bool ProjectRegistry::IsMissing(const ProjectRegistryEntry& entry) {
    std::error_code error;
    return !std::filesystem::is_regular_file(entry.projectFile, error);
}

void ProjectRegistry::Trim() {
    // Entries are newest first, so the non-favorites past the limit are the oldest ones.
    std::size_t recentKept = 0U;
    std::erase_if(m_entries, [this, &recentKept](const ProjectRegistryEntry& entry) {
        if (entry.favorite) {
            return false;
        }
        return ++recentKept > m_recentLimit;
    });
}

std::optional<std::filesystem::path> DefaultProjectRegistryFile(std::string* const errorMessage) {
    const Platform::ApplicationUserDataDirectoryResult directory =
        Platform::ResolveToolUserDataDirectory(kToolsUserDataDirectoryName);
    if (!directory.success) {
        if (errorMessage != nullptr) {
            *errorMessage = directory.message;
        }
        return std::nullopt;
    }
    return directory.directory / kProjectRegistryFileName;
}

ProjectRegistryResult RecordProjectOpened(const std::filesystem::path& file,
                                                const std::filesystem::path& projectFile,
                                                const Platform::EditorProjectPackage& package) {
    ProjectRegistry registry;
    if (ProjectRegistryResult loaded = registry.Load(file); !loaded.IsApplied()) {
        // A damaged registry is left for the user to inspect rather than overwritten.
        return loaded;
    }
    registry.RecordOpened(projectFile, package.displayName, package.engineVersion, NowUnixSeconds());
    return registry.Save(file);
}

} // namespace UVE::Pack
