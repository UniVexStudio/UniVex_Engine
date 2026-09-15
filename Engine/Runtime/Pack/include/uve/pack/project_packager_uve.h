// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace UVE::Pack {

struct ProjectPackOptionsUVE final {
    /// Path to the project's `.uveditor` package (see Platform::EditorProjectPackageCodecUVE).
    std::filesystem::path projectFile;
    /// Path to the already-built `uve_runtime` executable to copy into the distributable folder.
    std::filesystem::path runtimeExecutablePath;
    /// Destination folder for the packaged, distributable project. Created if it does not exist;
    /// must be empty or not yet exist (this is a "produce a fresh distributable" tool, not an
    /// incremental updater - see ProjectPackagerUVE::PackUVE's own doc comment).
    std::filesystem::path outputDirectory;
};

enum class ProjectPackCodeUVE : std::uint8_t {
    Success = 0,
    InvalidProjectFile,
    NoStartupSceneConfigured,
    RuntimeExecutableNotFound,
    ContentRootNotFound,
    OutputDirectoryNotEmpty,
    CopyFailed,
};

struct ProjectPackResultUVE final {
    ProjectPackCodeUVE code = ProjectPackCodeUVE::InvalidProjectFile;
    std::string message;

    [[nodiscard]] bool IsSuccessUVE() const noexcept { return code == ProjectPackCodeUVE::Success; }
};

/// Roadmap item #7's minimal packaging/export pipeline: produces one self-contained, runnable
/// distributable folder from an authored project - exactly the roadmap's own stated bar ("copy the
/// runtime binary + content folder + a manifest naming the startup scene into one distributable
/// folder"). No new manifest format is invented: the project's own `.uveditor` package (already
/// real, tested, and now carrying `startupScenePath` - see EditorProjectPackageUVE) is copied
/// verbatim alongside the content it references, so every relative path inside it still resolves
/// unchanged in the copy. The result is directly runnable in place:
/// `<outputDirectory>/uve_runtime --project <outputDirectory>/<projectFile's own filename>`.
///
/// Deliberately out of scope for this minimal pass (stated honestly, not silently dropped): no
/// asset stripping/optimization, no content-database pruning to only-referenced assets, no
/// platform cross-compilation (the copied binary is whatever was already built for the host
/// platform), no installer/archive creation - a plain folder is the deliverable.
class ProjectPackagerUVE final {
public:
    [[nodiscard]] static ProjectPackResultUVE PackUVE(const ProjectPackOptionsUVE& options);
};

} // namespace UVE::Pack
