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

using JsonUVE = nlohmann::json;

/// Prefix and marker of the hidden directory a project is assembled in before it is moved into
/// place; a leftover one is recognizable as an interrupted creation.
inline constexpr std::string_view kStagingDirectoryPrefixUVE = ".";
inline constexpr std::string_view kStagingDirectoryMarkerUVE = ".creating-";
/// Separates the readable part of a generated project ID from its unique suffix.
inline constexpr char kProjectIdSeparatorUVE = '-';
/// Hex digits of the unique suffix: the full width of a generated GUID.
inline constexpr std::size_t kUniqueSuffixDigitsUVE = 16U;
/// Replaces a character that is not portable in a file name.
inline constexpr char kFileNameReplacementUVE = '_';
/// Suffix of the temporary sibling a starter scene is written through.
inline constexpr std::string_view kTemporaryFileSuffixUVE = ".tmp";

[[nodiscard]] ProjectTemplateLoadResultUVE TemplateFailureUVE(const ProjectTemplateCodeUVE code,
                                                              std::string message) {
    return {code, std::move(message), std::nullopt};
}

[[nodiscard]] ProjectCreationResultUVE CreationFailureUVE(const ProjectCreationCodeUVE code, std::string message) {
    return {code, std::move(message), {}};
}

/// A relative path that stays inside the directory it is resolved against.
[[nodiscard]] bool IsContainedRelativePathUVE(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) {
        return false;
    }
    const std::filesystem::path normal = path.lexically_normal();
    return !normal.empty() && *normal.begin() != "..";
}

[[nodiscard]] bool ReadBoundedTextFileUVE(const std::filesystem::path& path, const std::uintmax_t maximumBytes,
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

[[nodiscard]] bool ParseVector3UVE(const JsonUVE& json, const char* key, Math::Vector3UVE& out) {
    if (!json.contains(key)) {
        return true;
    }
    const JsonUVE& value = json.at(key);
    constexpr std::size_t kComponentsUVE = 3U;
    if (!value.is_array() || value.size() != kComponentsUVE) {
        return false;
    }
    std::array<float, kComponentsUVE> components{};
    for (std::size_t index = 0U; index < kComponentsUVE; ++index) {
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

[[nodiscard]] JsonUVE Vector3ToJsonUVE(const Math::Vector3UVE& value) {
    return JsonUVE::array({value.x, value.y, value.z});
}

/// Parses the object list shared by template descriptors and starter scene files.
[[nodiscard]] bool ParseStarterObjectsUVE(const JsonUVE& json, StarterSceneUVE& scene, std::string& message) {
    if (!json.contains("starterObjects")) {
        return true;
    }
    const JsonUVE& objects = json.at("starterObjects");
    if (!objects.is_array() || objects.size() > kMaximumStarterObjectsUVE) {
        message = "starterObjects must be an array of at most " + std::to_string(kMaximumStarterObjectsUVE) +
                  " objects.";
        return false;
    }
    for (const JsonUVE& entry : objects) {
        StarterObjectUVE object;
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
        if (!ParseVector3UVE(entry, "position", object.position) ||
            !ParseVector3UVE(entry, "rotationDegrees", object.rotationDegrees) ||
            !ParseVector3UVE(entry, "scale", object.scale)) {
            message = "A starter object's position, rotationDegrees and scale must each be three finite numbers.";
            return false;
        }
        scene.objects.push_back(std::move(object));
    }
    return true;
}

[[nodiscard]] JsonUVE StarterObjectsToJsonUVE(const StarterSceneUVE& scene) {
    JsonUVE objects = JsonUVE::array();
    for (const StarterObjectUVE& object : scene.objects) {
        objects.push_back({{"kind", object.kind},
                           {"name", object.name},
                           {"position", Vector3ToJsonUVE(object.position)},
                           {"rotationDegrees", Vector3ToJsonUVE(object.rotationDegrees)},
                           {"scale", Vector3ToJsonUVE(object.scale)}});
    }
    return objects;
}

[[nodiscard]] std::string UniqueSuffixUVE() {
    const Asset::AssetGuidUVE guid = Asset::GenerateAssetGuidUVE();
    std::array<char, kUniqueSuffixDigitsUVE> digits{};
    constexpr int kHexBaseUVE = 16;
    const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), guid.value, kHexBaseUVE);
    std::string text(digits.data(), end);
    // Fixed width keeps generated IDs uniform and sortable.
    text.insert(text.begin(), kUniqueSuffixDigitsUVE - text.size(), '0');
    return text;
}

/// A project ID from the file stem: lower-case, restricted to the characters the manifest allows,
/// followed by a unique suffix, and within the manifest's length bound.
[[nodiscard]] std::string MakeProjectIdUVE(const std::string_view stem) {
    std::string readable;
    for (const char raw : stem) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            readable.push_back(static_cast<char>(std::tolower(character)));
        } else if (!readable.empty() && readable.back() != kProjectIdSeparatorUVE) {
            readable.push_back(kProjectIdSeparatorUVE);
        }
    }
    while (!readable.empty() && readable.back() == kProjectIdSeparatorUVE) {
        readable.pop_back();
    }
    const std::string suffix = UniqueSuffixUVE();
    const std::size_t readableLimit = Platform::kMaximumEditorProjectIdBytesUVE - suffix.size() - 1U;
    if (readable.size() > readableLimit) {
        readable.resize(readableLimit);
    }
    return readable.empty() ? suffix : readable + kProjectIdSeparatorUVE + suffix;
}

[[nodiscard]] bool IsDirectoryEmptyOrMissingUVE(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return !error;
    }
    return std::filesystem::is_directory(directory, error) && std::filesystem::is_empty(directory, error) && !error;
}

[[nodiscard]] bool WriteTextAtomicallyUVE(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::path temporary = path;
    temporary += kTemporaryFileSuffixUVE;
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
class StagingDirectoryUVE final {
public:
    explicit StagingDirectoryUVE(std::filesystem::path path) : m_path(std::move(path)) {}
    StagingDirectoryUVE(const StagingDirectoryUVE&) = delete;
    StagingDirectoryUVE& operator=(const StagingDirectoryUVE&) = delete;
    ~StagingDirectoryUVE() {
        if (!m_kept) {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
    }
    [[nodiscard]] const std::filesystem::path& PathUVE() const noexcept { return m_path; }
    void KeepUVE() noexcept { m_kept = true; }

private:
    std::filesystem::path m_path;
    bool m_kept = false;
};

} // namespace

ProjectTemplateLoadResultUVE LoadProjectTemplateUVE(const std::filesystem::path& templateDirectory) {
    std::error_code error;
    const std::filesystem::path directory = std::filesystem::absolute(templateDirectory, error).lexically_normal();
    if (error) {
        return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidPath,
                                  "Unable to resolve the template directory: " + error.message());
    }
    const std::filesystem::path descriptorPath = directory / kProjectTemplateManifestFileNameUVE;
    std::string text;
    if (!std::filesystem::is_regular_file(descriptorPath, error) ||
        !ReadBoundedTextFileUVE(descriptorPath, kMaximumProjectTemplateManifestBytesUVE, text)) {
        return TemplateFailureUVE(ProjectTemplateCodeUVE::MissingDescriptor,
                                  "The template has no readable " + std::string(kProjectTemplateManifestFileNameUVE) +
                                      ": " + directory.string());
    }

    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kProjectTemplateFormatUVE) {
        return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidDescriptor,
                                  "The template descriptor is not a " + std::string(kProjectTemplateFormatUVE) +
                                      " document: " + descriptorPath.string());
    }
    try {
        if (json.at("schemaVersion").get<std::uint32_t>() != kCurrentProjectTemplateSchemaVersionUVE) {
            return TemplateFailureUVE(ProjectTemplateCodeUVE::UnsupportedSchema,
                                      "The template descriptor uses an unsupported schema: " + descriptorPath.string());
        }
        ProjectTemplateUVE projectTemplate;
        projectTemplate.directory = directory;
        projectTemplate.id = json.at("id").get<std::string>();
        projectTemplate.displayName = json.at("displayName").get<std::string>();
        projectTemplate.description = json.value("description", std::string{});
        if (projectTemplate.id.empty() || projectTemplate.displayName.empty()) {
            return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidDescriptor,
                                      "A template needs an id and a display name: " + descriptorPath.string());
        }

        const std::filesystem::path previewImage = json.value("previewImage", std::string{});
        const std::filesystem::path contentDirectory = json.value("contentDirectory", std::string{});
        const std::filesystem::path startupScenePath = json.value("startupScenePath", std::string{});
        for (const std::filesystem::path* const path : {&previewImage, &contentDirectory, &startupScenePath}) {
            if (!path->empty() && !IsContainedRelativePathUVE(*path)) {
                return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidPath,
                                          "A template path must stay inside its directory: " + path->string());
            }
        }
        if (!previewImage.empty()) {
            projectTemplate.previewImage = directory / previewImage;
            if (!std::filesystem::is_regular_file(projectTemplate.previewImage, error)) {
                return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidPath,
                                          "The template preview image is missing: " +
                                              projectTemplate.previewImage.string());
            }
        }
        if (!contentDirectory.empty() && !std::filesystem::is_directory(directory / contentDirectory, error)) {
            return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidPath,
                                      "The template content directory is missing: " +
                                          (directory / contentDirectory).string());
        }
        projectTemplate.contentDirectory = contentDirectory.lexically_normal();
        projectTemplate.startupScenePath = startupScenePath.lexically_normal();

        std::string message;
        if (!ParseStarterObjectsUVE(json, projectTemplate.starterScene, message)) {
            return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidDescriptor,
                                      message + " (" + descriptorPath.string() + ")");
        }
        if (!projectTemplate.starterScene.objects.empty() && projectTemplate.startupScenePath.empty()) {
            return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidDescriptor,
                                      "A template with starter objects needs a startup scene: " +
                                          descriptorPath.string());
        }
        return {ProjectTemplateCodeUVE::Loaded, "Loaded template " + projectTemplate.id + ".",
                std::move(projectTemplate)};
    } catch (const JsonUVE::exception& exception) {
        return TemplateFailureUVE(ProjectTemplateCodeUVE::InvalidDescriptor,
                                  "The template descriptor is malformed (" + std::string(exception.what()) +
                                      "): " + descriptorPath.string());
    }
}

ProjectTemplateDiscoveryUVE DiscoverProjectTemplatesUVE(const std::filesystem::path& templatesRoot) {
    ProjectTemplateDiscoveryUVE discovery;
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
        ProjectTemplateLoadResultUVE loaded = LoadProjectTemplateUVE(entry.path());
        if (loaded.IsLoadedUVE()) {
            discovery.templates.push_back(std::move(*loaded.projectTemplate));
        } else {
            discovery.problems.push_back(std::move(loaded.message));
        }
    }
    std::sort(discovery.templates.begin(), discovery.templates.end(),
              [](const ProjectTemplateUVE& lhs, const ProjectTemplateUVE& rhs) {
                  return lhs.displayName < rhs.displayName;
              });
    return discovery;
}

std::string MakeProjectFileStemUVE(const std::string_view displayName) {
    // Characters reserved on at least one desktop file system, plus control characters.
    constexpr std::string_view kReservedCharactersUVE = "<>:\"/\\|?*";
    std::string stem;
    stem.reserve(displayName.size());
    for (const char raw : displayName) {
        const auto character = static_cast<unsigned char>(raw);
        const bool reserved = std::iscntrl(character) != 0 || kReservedCharactersUVE.find(raw) != std::string_view::npos;
        stem.push_back(reserved ? kFileNameReplacementUVE : raw);
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
        return c == kFileNameReplacementUVE;
    });
    return onlyReplacements ? std::string{} : stem;
}

ProjectCreationResultUVE CreateProjectFromTemplateUVE(const ProjectTemplateUVE& projectTemplate,
                                                      const std::filesystem::path& parentDirectory,
                                                      const std::string_view displayName,
                                                      const Core::VersionUVE& engineVersion) {
    const std::string stem = MakeProjectFileStemUVE(displayName);
    if (stem.empty() || displayName.size() > Platform::kMaximumEditorProjectNameBytesUVE) {
        return CreationFailureUVE(ProjectCreationCodeUVE::InvalidName,
                                  "The project name is empty, too long, or has no usable characters.");
    }
    std::error_code error;
    const std::filesystem::path parent = std::filesystem::absolute(parentDirectory, error).lexically_normal();
    if (error || !std::filesystem::is_directory(parent, error)) {
        return CreationFailureUVE(ProjectCreationCodeUVE::InvalidLocation,
                                  "The project location is not an existing directory: " + parentDirectory.string());
    }
    const std::filesystem::path destination = parent / stem;
    if (!IsDirectoryEmptyOrMissingUVE(destination)) {
        return CreationFailureUVE(ProjectCreationCodeUVE::DestinationNotEmpty,
                                  "A project location must be new or empty: " + destination.string());
    }

    StagingDirectoryUVE staging(parent / (std::string(kStagingDirectoryPrefixUVE) + stem +
                                          std::string(kStagingDirectoryMarkerUVE) + UniqueSuffixUVE()));
    const std::filesystem::path contentRoot = projectTemplate.contentDirectory.empty()
                                                  ? std::filesystem::path(kDefaultProjectContentDirectoryNameUVE)
                                                  : projectTemplate.contentDirectory.filename();
    std::filesystem::create_directories(staging.PathUVE() / contentRoot, error);
    if (error) {
        return CreationFailureUVE(ProjectCreationCodeUVE::WriteFailed,
                                  "Unable to create the project directory: " + error.message());
    }
    if (!projectTemplate.contentDirectory.empty()) {
        std::filesystem::copy(projectTemplate.directory / projectTemplate.contentDirectory,
                              staging.PathUVE() / contentRoot, std::filesystem::copy_options::recursive, error);
        if (error) {
            return CreationFailureUVE(ProjectCreationCodeUVE::CopyFailed,
                                      "Unable to copy the template content: " + error.message());
        }
    }

    if (!projectTemplate.starterScene.objects.empty()) {
        const JsonUVE starter{{"format", kProjectStarterSceneFormatUVE},
                              {"schemaVersion", kCurrentProjectStarterSceneSchemaVersionUVE},
                              {"starterObjects", StarterObjectsToJsonUVE(projectTemplate.starterScene)}};
        if (!WriteTextAtomicallyUVE(staging.PathUVE() / kProjectStarterSceneFileNameUVE, starter.dump(kPackJsonIndentUVE))) {
            return CreationFailureUVE(ProjectCreationCodeUVE::WriteFailed, "Unable to write the starter scene.");
        }
    }

    Platform::EditorProjectPackageUVE package;
    package.projectId = MakeProjectIdUVE(stem);
    package.displayName = std::string(displayName);
    package.engineVersion = {engineVersion.major, engineVersion.minor, engineVersion.patch, engineVersion.build};
    package.contentRoot = contentRoot;
    // The project keeps the file names the engine uses by default, in its own root.
    static const Core::EngineConfigUVE kDefaultConfigUVE{};
    package.assetDatabasePath = kDefaultConfigUVE.assetDatabaseFilePath.filename();
    package.settingsPath = kDefaultConfigUVE.projectSettingsFilePath.filename();
    package.startupScenePath = projectTemplate.startupScenePath;
    const std::filesystem::path stagedManifest =
        staging.PathUVE() / (stem + std::string(Platform::kEditorProjectFileExtensionUVE));
    const Platform::EditorProjectPackageResultUVE saved =
        Platform::EditorProjectPackageCodecUVE::SaveUVE(stagedManifest, package);
    if (!saved.IsAcceptedUVE()) {
        return CreationFailureUVE(ProjectCreationCodeUVE::WriteFailed,
                                  "Unable to write the project manifest: " + saved.message);
    }

    // An empty destination directory is replaced by the assembled project.
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(staging.PathUVE(), destination, error);
    if (error) {
        return CreationFailureUVE(ProjectCreationCodeUVE::WriteFailed,
                                  "Unable to move the new project into place: " + error.message());
    }
    staging.KeepUVE();
    return {ProjectCreationCodeUVE::Created, "Created project " + package.displayName + ".",
            destination / stagedManifest.filename()};
}

StarterSceneLoadResultUVE LoadProjectStarterSceneUVE(const std::filesystem::path& projectRoot) {
    StarterSceneLoadResultUVE result;
    const std::filesystem::path path = projectRoot / kProjectStarterSceneFileNameUVE;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        result.code = StarterSceneCodeUVE::Absent;
        return result;
    }
    result.code = StarterSceneCodeUVE::Invalid;
    std::string text;
    if (!ReadBoundedTextFileUVE(path, kMaximumProjectTemplateManifestBytesUVE, text)) {
        result.message = "Unable to read the starter scene: " + path.string();
        return result;
    }
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() ||
        json.value("format", std::string{}) != kProjectStarterSceneFormatUVE ||
        json.value("schemaVersion", 0U) != kCurrentProjectStarterSceneSchemaVersionUVE) {
        result.message = "The starter scene is not a supported " + std::string(kProjectStarterSceneFormatUVE) +
                         " document: " + path.string();
        return result;
    }
    if (!ParseStarterObjectsUVE(json, result.scene, result.message)) {
        return result;
    }
    result.code = StarterSceneCodeUVE::Loaded;
    return result;
}

bool RemoveProjectStarterSceneUVE(const std::filesystem::path& projectRoot) {
    std::error_code error;
    std::filesystem::remove(projectRoot / kProjectStarterSceneFileNameUVE, error);
    return !error;
}

} // namespace UVE::Pack
