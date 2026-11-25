// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor_project_package.h"
#include "engine_version.h"
#include "vector3.h"

namespace UVE::Pack {

/// Name of the directory, beside the engine executables, that holds the shipped project templates.
inline constexpr std::string_view kProjectTemplatesDirectoryName = "project_templates";
/// Content root of a project whose template ships no content of its own.
inline constexpr std::string_view kDefaultProjectContentDirectoryName = "Content";
/// The descriptor every template directory carries.
inline constexpr std::string_view kProjectTemplateManifestFileName = "template.json";
inline constexpr std::string_view kProjectTemplateFormat = "uvtemplate";
inline constexpr std::uint32_t kCurrentProjectTemplateSchemaVersion = 1U;
/// Upper bound on a template descriptor's size; a descriptor is a short JSON document.
inline constexpr std::uintmax_t kMaximumProjectTemplateManifestBytes = 1024U * 1024U;
/// Upper bound on the objects a starter scene may declare.
inline constexpr std::size_t kMaximumStarterObjects = 256U;

/// File, in the root of a newly created project, listing the objects its startup scene begins
/// with. The editor builds that scene from it the first time the project opens, then removes it.
inline constexpr std::string_view kProjectStarterSceneFileName = "starter_scene.uvstarter";
inline constexpr std::string_view kProjectStarterSceneFormat = "uvstarter";
inline constexpr std::uint32_t kCurrentProjectStarterSceneSchemaVersion = 1U;

/// One object a starter scene begins with. `kind` uses the editor's object-kind words
/// (for example "camera", "directionalLight", "plane", "cube"); the editor rejects kinds it does
/// not know. An empty `name` keeps the editor's default name for the kind.
struct StarterObject final {
    std::string kind;
    std::string name;
    Math::Vector3 position{};
    Math::Vector3 rotationDegrees{};
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
};

struct StarterScene final {
    std::vector<StarterObject> objects;
};

/// A project template on disk: a descriptor plus the content copied into each new project.
struct ProjectTemplate final {
    std::string id;
    std::string displayName;
    std::string description;
    /// The template directory itself (absolute).
    std::filesystem::path directory;
    /// Absolute preview image, or empty when the template has none.
    std::filesystem::path previewImage;
    /// Directory, relative to `directory`, copied as the new project's content root. Empty means
    /// the project starts with an empty content root.
    std::filesystem::path contentDirectory;
    /// The project's startup scene, relative to its content root. Empty leaves it unset.
    std::filesystem::path startupScenePath;
    StarterScene starterScene;
};

enum class ProjectTemplateCode : std::uint8_t {
    Loaded = 0,
    MissingDescriptor,
    InvalidDescriptor,
    UnsupportedSchema,
    InvalidPath,
};

struct ProjectTemplateLoadResult final {
    ProjectTemplateCode code = ProjectTemplateCode::MissingDescriptor;
    std::string message;
    std::optional<ProjectTemplate> projectTemplate;

    [[nodiscard]] bool IsLoaded() const noexcept {
        return code == ProjectTemplateCode::Loaded && projectTemplate.has_value();
    }
};

/// Reads and validates the template in `templateDirectory`. Every path the descriptor names must
/// stay inside the template directory.
[[nodiscard]] ProjectTemplateLoadResult LoadProjectTemplate(const std::filesystem::path& templateDirectory);

struct ProjectTemplateDiscovery final {
    /// Valid templates, ordered by display name.
    std::vector<ProjectTemplate> templates;
    /// One message per template directory that could not be loaded.
    std::vector<std::string> problems;
};

/// Loads every template directory directly under `templatesRoot`.
[[nodiscard]] ProjectTemplateDiscovery DiscoverProjectTemplates(const std::filesystem::path& templatesRoot);

enum class ProjectCreationCode : std::uint8_t {
    Created = 0,
    InvalidName,
    InvalidLocation,
    DestinationNotEmpty,
    CopyFailed,
    WriteFailed,
};

struct ProjectCreationResult final {
    ProjectCreationCode code = ProjectCreationCode::InvalidName;
    std::string message;
    /// The new project's manifest, when created.
    std::filesystem::path projectFile;

    [[nodiscard]] bool IsCreated() const noexcept { return code == ProjectCreationCode::Created; }
};

/// The directory and manifest stem a project called `displayName` gets: characters that are not
/// portable in file names are replaced, surrounding whitespace is trimmed. Empty when nothing usable
/// remains.
[[nodiscard]] std::string MakeProjectFileStem(std::string_view displayName);

/// Creates a project called `displayName` in a new directory under `parentDirectory`, from
/// `projectTemplate`. The project is assembled in a hidden sibling directory and moved into place
/// only once complete, so a failure leaves nothing behind. An existing, non-empty destination is
/// never touched. The manifest records `engineVersion` and a freshly generated project ID.
[[nodiscard]] ProjectCreationResult CreateProjectFromTemplate(
    const ProjectTemplate& projectTemplate,
    const std::filesystem::path& parentDirectory,
    std::string_view displayName,
    const Core::Version& engineVersion,
    Platform::RendererProfile rendererProfile = Platform::DefaultRendererProfileForHost());

enum class StarterSceneCode : std::uint8_t {
    Loaded = 0,
    Absent,
    Invalid,
};

struct StarterSceneLoadResult final {
    StarterSceneCode code = StarterSceneCode::Absent;
    std::string message;
    StarterScene scene;
};

/// Reads the starter scene a newly created project in `projectRoot` carries, if any.
[[nodiscard]] StarterSceneLoadResult LoadProjectStarterScene(const std::filesystem::path& projectRoot);

/// Removes the starter scene file once its scene exists. False only when the file remains.
[[nodiscard]] bool RemoveProjectStarterScene(const std::filesystem::path& projectRoot);

} // namespace UVE::Pack
