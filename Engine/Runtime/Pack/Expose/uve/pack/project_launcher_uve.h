// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "uve/core/engine_config_uve.h"
#include "uve/platform/editor_project_package_uve.h"

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

/// Loads a validated project manifest and applies its application/user-data policy to the
/// EngineConfig before EngineCoreUVE::Init(). This is the runtime bootstrap boundary: project paths
/// become absolute, per-user/portable paths are resolved, current-target identity/icons are selected,
/// and no partially configured EngineConfig escapes on failure.
[[nodiscard]] ProjectRuntimeSetupResultUVE PrepareProjectRuntimeUVE(
    const std::filesystem::path& projectFile, Core::EngineConfigUVE& config);

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
