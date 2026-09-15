// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_packager_uve.h"

#include <system_error>
#include <utility>

#include "uve/platform/editor_project_package_uve.h"

namespace UVE::Pack {
namespace {

[[nodiscard]] ProjectPackResultUVE MakeResultUVE(const ProjectPackCodeUVE code, std::string message) {
    return {code, std::move(message)};
}

} // namespace

ProjectPackResultUVE ProjectPackagerUVE::PackUVE(const ProjectPackOptionsUVE& options) {
    const Platform::EditorProjectPackageLoadResultUVE loaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(options.projectFile);
    if (!loaded.IsAcceptedUVE()) {
        return MakeResultUVE(ProjectPackCodeUVE::InvalidProjectFile,
                             "Unable to load the .uveditor project file: " + loaded.result.message);
    }
    const Platform::EditorProjectPackageUVE& package = *loaded.package;
    if (package.startupScenePath.empty()) {
        return MakeResultUVE(ProjectPackCodeUVE::NoStartupSceneConfigured,
                             "Cannot package a project with no startup scene configured "
                             "(EditorProjectPackageUVE::startupScenePath is empty).");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(options.runtimeExecutablePath, error) || error) {
        return MakeResultUVE(ProjectPackCodeUVE::RuntimeExecutableNotFound,
                             "The runtime executable was not found: " + options.runtimeExecutablePath.string());
    }

    const std::filesystem::path projectRoot = options.projectFile.parent_path();
    const std::filesystem::path contentRoot = projectRoot / package.contentRoot;
    if (!std::filesystem::is_directory(contentRoot, error) || error) {
        return MakeResultUVE(ProjectPackCodeUVE::ContentRootNotFound,
                             "The project's content root was not found: " + contentRoot.string());
    }

    error.clear();
    if (std::filesystem::exists(options.outputDirectory, error) &&
        !std::filesystem::is_empty(options.outputDirectory, error)) {
        return MakeResultUVE(ProjectPackCodeUVE::OutputDirectoryNotEmpty,
                             "The output directory already exists and is not empty: " +
                                 options.outputDirectory.string());
    }
    if (error) {
        return MakeResultUVE(ProjectPackCodeUVE::CopyFailed,
                             "Unable to inspect the output directory: " + options.outputDirectory.string());
    }

    error.clear();
    std::filesystem::create_directories(options.outputDirectory, error);
    if (error) {
        return MakeResultUVE(ProjectPackCodeUVE::CopyFailed,
                             "Unable to create the output directory: " + options.outputDirectory.string());
    }

    // Copy the runtime binary in, preserving its executable permission bits - copy_file alone
    // copies file content but not necessarily the source's mode bits on every platform/filesystem.
    const std::filesystem::path copiedRuntimePath =
        options.outputDirectory / options.runtimeExecutablePath.filename();
    error.clear();
    std::filesystem::copy_file(options.runtimeExecutablePath, copiedRuntimePath,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return MakeResultUVE(ProjectPackCodeUVE::CopyFailed,
                             "Unable to copy the runtime executable into the output directory.");
    }
    const std::filesystem::perms sourcePermissions = std::filesystem::status(options.runtimeExecutablePath, error).permissions();
    if (!error) {
        std::filesystem::permissions(copiedRuntimePath, sourcePermissions, error);
    }

    // Copy the .uveditor manifest verbatim (unchanged) - its relative contentRoot/
    // startupScenePath still resolve correctly once the content tree below is copied alongside
    // it at the same relative path.
    error.clear();
    std::filesystem::copy_file(options.projectFile, options.outputDirectory / options.projectFile.filename(),
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return MakeResultUVE(ProjectPackCodeUVE::CopyFailed,
                             "Unable to copy the .uveditor project file into the output directory.");
    }

    // Copy the whole content tree, preserving package.contentRoot's own relative path/name so the
    // copied manifest's relative paths keep resolving without any rewriting.
    error.clear();
    std::filesystem::copy(contentRoot, options.outputDirectory / package.contentRoot,
                          std::filesystem::copy_options::recursive, error);
    if (error) {
        return MakeResultUVE(ProjectPackCodeUVE::CopyFailed,
                             "Unable to copy the project's content root into the output directory: " +
                                 error.message());
    }

    return MakeResultUVE(ProjectPackCodeUVE::Success,
                         "Packaged '" + package.displayName + "' into " + options.outputDirectory.string() +
                             " - run with: " + copiedRuntimePath.filename().string() + " --project " +
                             options.projectFile.filename().string());
}

} // namespace UVE::Pack
