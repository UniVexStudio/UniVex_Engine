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
inline constexpr std::string_view kProjectTemplatesDirectoryNameUVE = "project_templates";
/// Content root of a project whose template ships no content of its own.
inline constexpr std::string_view kDefaultProjectContentDirectoryNameUVE = "Content";
/// The descriptor every template directory carries.
inline constexpr std::string_view kProjectTemplateManifestFileNameUVE = "template.json";
inline constexpr std::string_view kProjectTemplateFormatUVE = "uvtemplate";
inline constexpr std::uint32_t kCurrentProjectTemplateSchemaVersionUVE = 1U;
/// Upper bound on a template descriptor's size; a descriptor is a short JSON document.
inline constexpr std::uintmax_t kMaximumProjectTemplateManifestBytesUVE = 1024U * 1024U;
/// Upper bound on the objects a starter scene may declare.
inline constexpr std::size_t kMaximumStarterObjectsUVE = 256U;

/// File, in the root of a newly created project, listing the objects its startup scene begins
/// with. The editor builds that scene from it the first time the project opens, then removes it.
inline constexpr std::string_view kProjectStarterSceneFileNameUVE = "starter_scene.uvstarter";
inline constexpr std::string_view kProjectStarterSceneFormatUVE = "uvstarter";
inline constexpr std::uint32_t kCurrentProjectStarterSceneSchemaVersionUVE = 1U;

/// One object a starter scene begins with. `kind` uses the editor's object-kind words
/// (for example "camera", "directionalLight", "plane", "cube"); the editor rejects kinds it does
/// not know. An empty `name` keeps the editor's default name for the kind.
struct StarterObjectUVE final {
    std::string kind;
    std::string name;
    Math::Vector3UVE position{};
    Math::Vector3UVE rotationDegrees{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

struct StarterSceneUVE final {
    std::vector<StarterObjectUVE> objects;
};

/// A project template on disk: a descriptor plus the content copied into each new project.
struct ProjectTemplateUVE final {
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
    StarterSceneUVE starterScene;
};

enum class ProjectTemplateCodeUVE : std::uint8_t {
    Loaded = 0,
    MissingDescriptor,
    InvalidDescriptor,
    UnsupportedSchema,
    InvalidPath,
};

struct ProjectTemplateLoadResultUVE final {
    ProjectTemplateCodeUVE code = ProjectTemplateCodeUVE::MissingDescriptor;
    std::string message;
    std::optional<ProjectTemplateUVE> projectTemplate;

    [[nodiscard]] bool IsLoadedUVE() const noexcept {
        return code == ProjectTemplateCodeUVE::Loaded && projectTemplate.has_value();
    }
};

/// Reads and validates the template in `templateDirectory`. Every path the descriptor names must
/// stay inside the template directory.
[[nodiscard]] ProjectTemplateLoadResultUVE LoadProjectTemplateUVE(const std::filesystem::path& templateDirectory);

struct ProjectTemplateDiscoveryUVE final {
    /// Valid templates, ordered by display name.
    std::vector<ProjectTemplateUVE> templates;
    /// One message per template directory that could not be loaded.
    std::vector<std::string> problems;
};

/// Loads every template directory directly under `templatesRoot`.
[[nodiscard]] ProjectTemplateDiscoveryUVE DiscoverProjectTemplatesUVE(const std::filesystem::path& templatesRoot);

enum class ProjectCreationCodeUVE : std::uint8_t {
    Created = 0,
    InvalidName,
    InvalidLocation,
    DestinationNotEmpty,
    CopyFailed,
    WriteFailed,
};

struct ProjectCreationResultUVE final {
    ProjectCreationCodeUVE code = ProjectCreationCodeUVE::InvalidName;
    std::string message;
    /// The new project's manifest, when created.
    std::filesystem::path projectFile;

    [[nodiscard]] bool IsCreatedUVE() const noexcept { return code == ProjectCreationCodeUVE::Created; }
};

/// The directory and manifest stem a project called `displayName` gets: characters that are not
/// portable in file names are replaced, surrounding whitespace is trimmed. Empty when nothing usable
/// remains.
[[nodiscard]] std::string MakeProjectFileStemUVE(std::string_view displayName);

/// Creates a project called `displayName` in a new directory under `parentDirectory`, from
/// `projectTemplate`. The project is assembled in a hidden sibling directory and moved into place
/// only once complete, so a failure leaves nothing behind. An existing, non-empty destination is
/// never touched. The manifest records `engineVersion` and a freshly generated project ID.
[[nodiscard]] ProjectCreationResultUVE CreateProjectFromTemplateUVE(
    const ProjectTemplateUVE& projectTemplate,
    const std::filesystem::path& parentDirectory,
    std::string_view displayName,
    const Core::VersionUVE& engineVersion,
    Platform::RendererProfileUVE rendererProfile = Platform::DefaultRendererProfileForHostUVE());

enum class StarterSceneCodeUVE : std::uint8_t {
    Loaded = 0,
    Absent,
    Invalid,
};

struct StarterSceneLoadResultUVE final {
    StarterSceneCodeUVE code = StarterSceneCodeUVE::Absent;
    std::string message;
    StarterSceneUVE scene;
};

/// Reads the starter scene a newly created project in `projectRoot` carries, if any.
[[nodiscard]] StarterSceneLoadResultUVE LoadProjectStarterSceneUVE(const std::filesystem::path& projectRoot);

/// Removes the starter scene file once its scene exists. False only when the file remains.
[[nodiscard]] bool RemoveProjectStarterSceneUVE(const std::filesystem::path& projectRoot);

} // namespace UVE::Pack
