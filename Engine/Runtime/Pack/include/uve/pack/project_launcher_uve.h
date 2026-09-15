// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>

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

/// Roadmap item #7's "played project" bridge: loads `projectFile` (a `.uveditor` package, see
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

} // namespace UVE::Pack
