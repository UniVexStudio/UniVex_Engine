// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "engine_config.h"
#include "engine_version.h"
#include "editor_project_package.h"
#include "project_compatibility.h"

namespace UVE::Core {
class EngineCoreUVE;
}

namespace UVE::Pack {

enum class ProjectLaunchCodeUVE : std::uint8_t {
    Loaded = 0,
    InvalidProjectFile,
    NoStartupSceneConfigured,
    SceneLoadFailed,
};

struct ProjectLaunchResultUVE final {
    ProjectLaunchCodeUVE code = ProjectLaunchCodeUVE::InvalidProjectFile;
    std::string message;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept { return code == ProjectLaunchCodeUVE::Loaded; }
};

enum class ProjectRuntimeSetupCodeUVE : std::uint8_t {
    Ready = 0,
    InvalidProjectFile,
    NoStartupSceneConfigured,
    ApplicationResourceInvalid,
    UserDataDirectoryUnavailable,
};

struct ProjectRuntimeSetupUVE final {
    Platform::EditorProjectPackageUVE package;
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::filesystem::path contentRoot;
    std::filesystem::path userDataDirectory;
    std::string applicationIdentifier;
};

struct ProjectRuntimeSetupResultUVE final {
    ProjectRuntimeSetupCodeUVE code = ProjectRuntimeSetupCodeUVE::InvalidProjectFile;
    std::string message;
    std::optional<ProjectRuntimeSetupUVE> setup;

    [[nodiscard]] bool IsReadyUVE() const noexcept {
        return code == ProjectRuntimeSetupCodeUVE::Ready && setup.has_value();
    }
};

/// Directory, relative to the project root, that receives the editor's log for that project.
inline constexpr std::string_view kProjectEditorLogDirectoryNameUVE = "Logs";

enum class ProjectEditorSetupCodeUVE : std::uint8_t {
    Ready = 0,
    InvalidProjectFile,
    ContentRootInvalid,
    StartupSceneInvalid,
    /// The project was saved with a newer engine this one must not open.
    EngineTooOld,
    DirectoryUnavailable,
};

/// The project an editor session authors. Every path is absolute and normalized.
struct ProjectEditorSetupUVE final {
    Platform::EditorProjectPackageUVE package;
    std::filesystem::path projectFile;
    std::filesystem::path projectRoot;
    std::filesystem::path contentRoot;
    /// The manifest's startup scene under the content root, or empty when none is configured. The
    /// file itself may not exist yet: a freshly created project names the scene it will save first.
    std::filesystem::path startupScene;
    /// How the project's recorded engine version relates to the engine opening it.
    ProjectCompatibilityUVE compatibility;
};

struct ProjectEditorSetupResultUVE final {
    ProjectEditorSetupCodeUVE code = ProjectEditorSetupCodeUVE::InvalidProjectFile;
    std::string message;
    std::optional<ProjectEditorSetupUVE> setup;

    [[nodiscard]] bool IsReadyUVE() const noexcept {
        return code == ProjectEditorSetupCodeUVE::Ready && setup.has_value();
    }
};

/// Points `config` at the project `projectFile` describes so the editor authors that project's
/// content, settings, input map and asset database, before EngineCoreUVE::Init(). Unlike
/// PrepareProjectRuntimeUVE, the project's shipped-application policy (window, icons, splash,
/// user-data location) is left alone: the editor keeps its own window and per-user preferences, and
/// a project without a startup scene is still a valid project to author. The log keeps the file name
/// the caller chose and moves under kProjectEditorLogDirectoryNameUVE in the project root. A project
/// saved with an engine newer than `engineVersion` beyond the compatibility policy is refused with
/// EngineTooOld. On failure `config` is not modified.
[[nodiscard]] ProjectEditorSetupResultUVE PrepareProjectEditorUVE(
    const std::filesystem::path& projectFile, const Core::VersionUVE& engineVersion, Core::EngineConfigUVE& config,
    const ProjectCompatibilityPolicyUVE& policy = kDefaultProjectCompatibilityPolicyUVE);

/// Records `engineVersion` as the engine that last edited the project, through the manifest's
/// revision-checked update so a concurrent writer is never overwritten. A manifest that already
/// names this version is left untouched and reported as applied.
[[nodiscard]] Platform::EditorProjectPackageResultUVE RecordProjectEngineVersionUVE(
    const std::filesystem::path& projectFile, const Core::VersionUVE& engineVersion);

/// Loads a validated project manifest and applies its application/user-data policy to the
/// EngineConfig before EngineCoreUVE::Init(). This is the runtime bootstrap boundary: project paths
/// become absolute, per-user/portable paths are resolved, current-target identity/icons are selected,
/// and no partially configured EngineConfig escapes on failure.
[[nodiscard]] ProjectRuntimeSetupResultUVE PrepareProjectRuntimeUVE(
    const std::filesystem::path& projectFile, Core::EngineConfigUVE& config,
    const std::filesystem::path& nativeUserDataRoot = {});

/// Roadmap item #7's "played project" bridge: loads `projectFile` (a `.uvproject` package, see
/// Platform::EditorProjectPackageCodecUVE), loads its configured startup scene into `engine`'s own
/// live entity manager, refreshes the scene graph so authored WorldTransformComponentUVE data is
/// correct before the first render, and activates the first entity found with both
/// CameraComponentUVE and WorldTransformComponentUVE (this engine has no "main camera" tag/
/// priority concept yet - same honest, simple first-found convention already used by the editor's
/// own Play-mode game-camera switch). Must be called after `engine.Load()` has already run
/// (needs a live entity manager/scene graph) and before the caller's own per-frame tick loop
/// starts. Does not itself run any frames or call Shutdown() - purely a one-shot scene bootstrap.
[[nodiscard]] ProjectLaunchResultUVE LoadAndActivateProjectSceneUVE(Core::EngineCoreUVE& engine,
                                                                    const std::filesystem::path& projectFile);
/// Same operation using the already-validated bootstrap snapshot, avoiding a second manifest read
/// (and any time-of-check/time-of-use mismatch between EngineConfig and the launch scene).
[[nodiscard]] ProjectLaunchResultUVE LoadAndActivateProjectSceneUVE(Core::EngineCoreUVE& engine,
                                                                    const ProjectRuntimeSetupUVE& setup);

} // namespace UVE::Pack
