// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_template.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "asset_guid.h"
#include "engine_config.h"
#include "pack_json.h"

namespace UVE::Pack {
namespace {

using Json = nlohmann::json;

/// Prefix and marker of the hidden directory a project is assembled in before it is moved into
/// place; a leftover one is recognizable as an interrupted creation.
inline constexpr std::string_view kStagingDirectoryPrefix = ".";
inline constexpr std::string_view kStagingDirectoryMarker = ".creating-";
/// Separates the readable part of a generated project ID from its unique suffix.
inline constexpr char kProjectIdSeparator = '-';
/// Hex digits of the unique suffix: the full width of a generated GUID.
inline constexpr std::size_t kUniqueSuffixDigits = 16U;
/// Replaces a character that is not portable in a file name.
inline constexpr char kFileNameReplacement = '_';
/// Suffix of the temporary sibling a starter scene is written through.
inline constexpr std::string_view kTemporaryFileSuffix = ".tmp";

[[nodiscard]] ProjectTemplateLoadResult TemplateFailure(const ProjectTemplateCode code,
                                                              std::string message) {
    return {code, std::move(message), std::nullopt};
}

[[nodiscard]] ProjectCreationResult CreationFailure(const ProjectCreationCode code, std::string message) {
    return {code, std::move(message), {}};
}

/// A relative path that stays inside the directory it is resolved against.
[[nodiscard]] bool IsContainedRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) {
        return false;
    }
    const std::filesystem::path normal = path.lexically_normal();
    return !normal.empty() && *normal.begin() != "..";
}

[[nodiscard]] bool ReadBoundedTextFile(const std::filesystem::path& path, const std::uintmax_t maximumBytes,
                                          std::string& text) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > maximumBytes) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad();
}

[[nodiscard]] bool ParseVector3(const Json& json, const char* key, Math::Vector3& out) {
    if (!json.contains(key)) {
        return true;
    }
    const Json& value = json.at(key);
    constexpr std::size_t kComponents = 3U;
    if (!value.is_array() || value.size() != kComponents) {
        return false;
    }
    std::array<float, kComponents> components{};
    for (std::size_t index = 0U; index < kComponents; ++index) {
        if (!value[index].is_number()) {
            return false;
        }
        components[index] = value[index].get<float>();
        if (!std::isfinite(components[index])) {
            return false;
        }
    }
    out = {components[0], components[1], components[2]};
    return true;
}

[[nodiscard]] Json Vector3ToJson(const Math::Vector3& value) {
    return Json::array({value.x, value.y, value.z});
}

/// Parses the object list shared by template descriptors and starter scene files.
[[nodiscard]] bool ParseStarterObjects(const Json& json, StarterScene& scene, std::string& message) {
    if (!json.contains("starterObjects")) {
        return true;
    }
    const Json& objects = json.at("starterObjects");
    if (!objects.is_array() || objects.size() > kMaximumStarterObjects) {
        message = "starterObjects must be an array of at most " + std::to_string(kMaximumStarterObjects) +
                  " objects.";
        return false;
    }
    for (const Json& entry : objects) {
        StarterObject object;
        if (!entry.is_object() || !entry.contains("kind") || !entry.at("kind").is_string()) {
            message = "Every starter object needs a kind.";
            return false;
        }
        object.kind = entry.at("kind").get<std::string>();
        if (object.kind.empty()) {
            message = "A starter object's kind is empty.";
            return false;
        }
        if (entry.contains("name")) {
            if (!entry.at("name").is_string()) {
                message = "A starter object's name must be text.";
                return false;
            }
            object.name = entry.at("name").get<std::string>();
        }
        if (!ParseVector3(entry, "position", object.position) ||
            !ParseVector3(entry, "rotationDegrees", object.rotationDegrees) ||
            !ParseVector3(entry, "scale", object.scale)) {
            message = "A starter object's position, rotationDegrees and scale must each be three finite numbers.";
            return false;
        }
        scene.objects.push_back(std::move(object));
    }
    return true;
}

[[nodiscard]] Json StarterObjectsToJson(const StarterScene& scene) {
    Json objects = Json::array();
    for (const StarterObject& object : scene.objects) {
        objects.push_back({{"kind", object.kind},
                           {"name", object.name},
                           {"position", Vector3ToJson(object.position)},
                           {"rotationDegrees", Vector3ToJson(object.rotationDegrees)},
                           {"scale", Vector3ToJson(object.scale)}});
    }
    return objects;
}

[[nodiscard]] std::string UniqueSuffix() {
    const Asset::AssetGuid guid = Asset::GenerateAssetGuid();
    std::array<char, kUniqueSuffixDigits> digits{};
    constexpr int kHexBase = 16;
    const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), guid.value, kHexBase);
    std::string text(digits.data(), end);
    // Fixed width keeps generated IDs uniform and sortable.
    text.insert(text.begin(), kUniqueSuffixDigits - text.size(), '0');
    return text;
}

/// A project ID from the file stem: lower-case, restricted to the characters the manifest allows,
/// followed by a unique suffix, and within the manifest's length bound.
[[nodiscard]] std::string MakeProjectId(const std::string_view stem) {
    std::string readable;
    for (const char raw : stem) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            readable.push_back(static_cast<char>(std::tolower(character)));
        } else if (!readable.empty() && readable.back() != kProjectIdSeparator) {
            readable.push_back(kProjectIdSeparator);
        }
    }
    while (!readable.empty() && readable.back() == kProjectIdSeparator) {
        readable.pop_back();
    }
    const std::string suffix = UniqueSuffix();
    const std::size_t readableLimit = Platform::kMaximumEditorProjectIdBytes - suffix.size() - 1U;
    if (readable.size() > readableLimit) {
        readable.resize(readableLimit);
    }
    return readable.empty() ? suffix : readable + kProjectIdSeparator + suffix;
}

[[nodiscard]] bool IsDirectoryEmptyOrMissing(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return !error;
    }
    return std::filesystem::is_directory(directory, error) && std::filesystem::is_empty(directory, error) && !error;
}

[[nodiscard]] bool WriteTextAtomically(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::path temporary = path;
    temporary += kTemporaryFileSuffix;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output << text;
        if (!output.good()) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

/// Removes a staging directory when creation stops early, unless creation succeeded.
class StagingDirectory final {
public:
    explicit StagingDirectory(std::filesystem::path path) : m_path(std::move(path)) {}
    StagingDirectory(const StagingDirectory&) = delete;
    StagingDirectory& operator=(const StagingDirectory&) = delete;
    ~StagingDirectory() {
        if (!m_kept) {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
    }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }
    void Keep() noexcept { m_kept = true; }

private:
    std::filesystem::path m_path;
    bool m_kept = false;
};

} // namespace

ProjectTemplateLoadResult LoadProjectTemplate(const std::filesystem::path& templateDirectory) {
    std::error_code error;
    const std::filesystem::path directory = std::filesystem::absolute(templateDirectory, error).lexically_normal();
    if (error) {
        return TemplateFailure(ProjectTemplateCode::InvalidPath,
                                  "Unable to resolve the template directory: " + error.message());
    }
    const std::filesystem::path descriptorPath = directory / kProjectTemplateManifestFileName;
    std::string text;
    if (!std::filesystem::is_regular_file(descriptorPath, error) ||
        !ReadBoundedTextFile(descriptorPath, kMaximumProjectTemplateManifestBytes, text)) {
        return TemplateFailure(ProjectTemplateCode::MissingDescriptor,
                                  "The template has no readable " + std::string(kProjectTemplateManifestFileName) +
                                      ": " + directory.string());
    }

    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kProjectTemplateFormat) {
        return TemplateFailure(ProjectTemplateCode::InvalidDescriptor,
                                  "The template descriptor is not a " + std::string(kProjectTemplateFormat) +
                                      " document: " + descriptorPath.string());
    }
    try {
        if (json.at("schemaVersion").get<std::uint32_t>() != kCurrentProjectTemplateSchemaVersion) {
            return TemplateFailure(ProjectTemplateCode::UnsupportedSchema,
                                      "The template descriptor uses an unsupported schema: " + descriptorPath.string());
        }
        ProjectTemplate projectTemplate;
        projectTemplate.directory = directory;
        projectTemplate.id = json.at("id").get<std::string>();
        projectTemplate.displayName = json.at("displayName").get<std::string>();
        projectTemplate.description = json.value("description", std::string{});
        if (projectTemplate.id.empty() || projectTemplate.displayName.empty()) {
            return TemplateFailure(ProjectTemplateCode::InvalidDescriptor,
                                      "A template needs an id and a display name: " + descriptorPath.string());
        }

        const std::filesystem::path previewImage = json.value("previewImage", std::string{});
        const std::filesystem::path contentDirectory = json.value("contentDirectory", std::string{});
        const std::filesystem::path startupScenePath = json.value("startupScenePath", std::string{});
        for (const std::filesystem::path* const path : {&previewImage, &contentDirectory, &startupScenePath}) {
            if (!path->empty() && !IsContainedRelativePath(*path)) {
                return TemplateFailure(ProjectTemplateCode::InvalidPath,
                                          "A template path must stay inside its directory: " + path->string());
            }
        }
        if (!previewImage.empty()) {
            projectTemplate.previewImage = directory / previewImage;
            if (!std::filesystem::is_regular_file(projectTemplate.previewImage, error)) {
                return TemplateFailure(ProjectTemplateCode::InvalidPath,
                                          "The template preview image is missing: " +
                                              projectTemplate.previewImage.string());
            }
        }
        if (!contentDirectory.empty() && !std::filesystem::is_directory(directory / contentDirectory, error)) {
            return TemplateFailure(ProjectTemplateCode::InvalidPath,
                                      "The template content directory is missing: " +
                                          (directory / contentDirectory).string());
        }
        projectTemplate.contentDirectory = contentDirectory.lexically_normal();
        projectTemplate.startupScenePath = startupScenePath.lexically_normal();

        std::string message;
        if (!ParseStarterObjects(json, projectTemplate.starterScene, message)) {
            return TemplateFailure(ProjectTemplateCode::InvalidDescriptor,
                                      message + " (" + descriptorPath.string() + ")");
        }
        if (!projectTemplate.starterScene.objects.empty() && projectTemplate.startupScenePath.empty()) {
            return TemplateFailure(ProjectTemplateCode::InvalidDescriptor,
                                      "A template with starter objects needs a startup scene: " +
                                          descriptorPath.string());
        }
        return {ProjectTemplateCode::Loaded, "Loaded template " + projectTemplate.id + ".",
                std::move(projectTemplate)};
    } catch (const Json::exception& exception) {
        return TemplateFailure(ProjectTemplateCode::InvalidDescriptor,
                                  "The template descriptor is malformed (" + std::string(exception.what()) +
                                      "): " + descriptorPath.string());
    }
}

ProjectTemplateDiscovery DiscoverProjectTemplates(const std::filesystem::path& templatesRoot) {
    ProjectTemplateDiscovery discovery;
    std::error_code error;
    std::filesystem::directory_iterator iterator(templatesRoot, error);
    if (error) {
        discovery.problems.push_back("Unable to read the templates directory " + templatesRoot.string() + ": " +
                                     error.message());
        return discovery;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        if (!entry.is_directory(error)) {
            continue;
        }
        ProjectTemplateLoadResult loaded = LoadProjectTemplate(entry.path());
        if (loaded.IsLoaded()) {
            discovery.templates.push_back(std::move(*loaded.projectTemplate));
        } else {
            discovery.problems.push_back(std::move(loaded.message));
        }
    }
    std::sort(discovery.templates.begin(), discovery.templates.end(),
              [](const ProjectTemplate& lhs, const ProjectTemplate& rhs) {
                  return lhs.displayName < rhs.displayName;
              });
    return discovery;
}

std::string MakeProjectFileStem(const std::string_view displayName) {
    // Characters reserved on at least one desktop file system, plus control characters.
    constexpr std::string_view kReservedCharacters = "<>:\"/\\|?*";
    std::string stem;
    stem.reserve(displayName.size());
    for (const char raw : displayName) {
        const auto character = static_cast<unsigned char>(raw);
        const bool reserved = std::iscntrl(character) != 0 || kReservedCharacters.find(raw) != std::string_view::npos;
        stem.push_back(reserved ? kFileNameReplacement : raw);
    }
    const auto isTrimmed = [](const unsigned char character) {
        return std::isspace(character) != 0 || character == '.';
    };
    while (!stem.empty() && isTrimmed(static_cast<unsigned char>(stem.back()))) {
        stem.pop_back();
    }
    std::size_t first = 0U;
    while (first < stem.size() && isTrimmed(static_cast<unsigned char>(stem[first]))) {
        ++first;
    }
    stem.erase(0U, first);
    const bool onlyReplacements = std::all_of(stem.begin(), stem.end(), [](const char c) {
        return c == kFileNameReplacement;
    });
    return onlyReplacements ? std::string{} : stem;
}

ProjectCreationResult CreateProjectFromTemplate(const ProjectTemplate& projectTemplate,
                                                      const std::filesystem::path& parentDirectory,
                                                      const std::string_view displayName,
                                                      const Core::Version& engineVersion,
                                                      const Platform::RendererProfile rendererProfile) {
    const std::string stem = MakeProjectFileStem(displayName);
    if (stem.empty() || displayName.size() > Platform::kMaximumEditorProjectNameBytes) {
        return CreationFailure(ProjectCreationCode::InvalidName,
                                  "The project name is empty, too long, or has no usable characters.");
    }
    std::error_code error;
    const std::filesystem::path parent = std::filesystem::absolute(parentDirectory, error).lexically_normal();
    if (error || !std::filesystem::is_directory(parent, error)) {
        return CreationFailure(ProjectCreationCode::InvalidLocation,
                                  "The project location is not an existing directory: " + parentDirectory.string());
    }
    const std::filesystem::path destination = parent / stem;
    if (!IsDirectoryEmptyOrMissing(destination)) {
        return CreationFailure(ProjectCreationCode::DestinationNotEmpty,
                                  "A project location must be new or empty: " + destination.string());
    }

    StagingDirectory staging(parent / (std::string(kStagingDirectoryPrefix) + stem +
                                          std::string(kStagingDirectoryMarker) + UniqueSuffix()));
    const std::filesystem::path contentRoot = projectTemplate.contentDirectory.empty()
                                                  ? std::filesystem::path(kDefaultProjectContentDirectoryName)
                                                  : projectTemplate.contentDirectory.filename();
    std::filesystem::create_directories(staging.Path() / contentRoot, error);
    if (error) {
        return CreationFailure(ProjectCreationCode::WriteFailed,
                                  "Unable to create the project directory: " + error.message());
    }
    if (!projectTemplate.contentDirectory.empty()) {
        std::filesystem::copy(projectTemplate.directory / projectTemplate.contentDirectory,
                              staging.Path() / contentRoot, std::filesystem::copy_options::recursive, error);
        if (error) {
            return CreationFailure(ProjectCreationCode::CopyFailed,
                                      "Unable to copy the template content: " + error.message());
        }
    }

    if (!projectTemplate.starterScene.objects.empty()) {
        const Json starter{{"format", kProjectStarterSceneFormat},
                              {"schemaVersion", kCurrentProjectStarterSceneSchemaVersion},
                              {"starterObjects", StarterObjectsToJson(projectTemplate.starterScene)}};
        if (!WriteTextAtomically(staging.Path() / kProjectStarterSceneFileName, starter.dump(kPackJsonIndent))) {
            return CreationFailure(ProjectCreationCode::WriteFailed, "Unable to write the starter scene.");
        }
    }

    Platform::EditorProjectPackage package;
    package.projectId = MakeProjectId(stem);
    package.displayName = std::string(displayName);
    package.engineVersion = {engineVersion.major, engineVersion.minor, engineVersion.patch, engineVersion.build};
    package.contentRoot = contentRoot;
    // The project keeps the file names the engine uses by default, in its own root.
    static const Core::EngineConfig kDefaultConfig{};
    package.assetDatabasePath = kDefaultConfig.assetDatabaseFilePath.filename();
    package.settingsPath = kDefaultConfig.projectSettingsFilePath.filename();
    package.startupScenePath = projectTemplate.startupScenePath;
    package.rendererProfile = rendererProfile;
    const std::filesystem::path stagedManifest =
        staging.Path() / (stem + std::string(Platform::kEditorProjectFileExtension));
    const Platform::EditorProjectPackageResult saved =
        Platform::EditorProjectPackageCodec::Save(stagedManifest, package);
    if (!saved.IsAccepted()) {
        return CreationFailure(ProjectCreationCode::WriteFailed,
                                  "Unable to write the project manifest: " + saved.message);
    }

    // An empty destination directory is replaced by the assembled project.
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(staging.Path(), destination, error);
    if (error) {
        return CreationFailure(ProjectCreationCode::WriteFailed,
                                  "Unable to move the new project into place: " + error.message());
    }
    staging.Keep();
    return {ProjectCreationCode::Created, "Created project " + package.displayName + ".",
            destination / stagedManifest.filename()};
}

StarterSceneLoadResult LoadProjectStarterScene(const std::filesystem::path& projectRoot) {
    StarterSceneLoadResult result;
    const std::filesystem::path path = projectRoot / kProjectStarterSceneFileName;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        result.code = StarterSceneCode::Absent;
        return result;
    }
    result.code = StarterSceneCode::Invalid;
    std::string text;
    if (!ReadBoundedTextFile(path, kMaximumProjectTemplateManifestBytes, text)) {
        result.message = "Unable to read the starter scene: " + path.string();
        return result;
    }
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() ||
        json.value("format", std::string{}) != kProjectStarterSceneFormat ||
        json.value("schemaVersion", 0U) != kCurrentProjectStarterSceneSchemaVersion) {
        result.message = "The starter scene is not a supported " + std::string(kProjectStarterSceneFormat) +
                         " document: " + path.string();
        return result;
    }
    if (!ParseStarterObjects(json, result.scene, result.message)) {
        return result;
    }
    result.code = StarterSceneCode::Loaded;
    return result;
}

bool RemoveProjectStarterScene(const std::filesystem::path& projectRoot) {
    std::error_code error;
    std::filesystem::remove(projectRoot / kProjectStarterSceneFileName, error);
    return !error;
}

} // namespace UVE::Pack
