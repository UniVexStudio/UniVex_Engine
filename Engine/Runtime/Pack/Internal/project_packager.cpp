// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_packager.h"

#include <system_error>
#include <utility>

#include "editor_project_package.h"

namespace UVE::Pack {
namespace {

[[nodiscard]] ProjectPackResult MakeResult(const ProjectPackCode code, std::string message) {
    return {code, std::move(message)};
}

} // namespace

ProjectPackResult ProjectPackager::Pack(const ProjectPackOptions& options) {
    const Platform::EditorProjectPackageLoadResult loaded =
        Platform::EditorProjectPackageCodec::Load(options.projectFile);
    if (!loaded.IsAccepted()) {
        return MakeResult(ProjectPackCode::InvalidProjectFile,
                             "Unable to load the .uvproject project file: " + loaded.result.message);
    }
    const Platform::EditorProjectPackage& package = *loaded.package;
    if (package.startupScenePath.empty()) {
        return MakeResult(ProjectPackCode::NoStartupSceneConfigured,
                             "Cannot package a project with no startup scene configured "
                             "(EditorProjectPackage::startupScenePath is empty).");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(options.runtimeExecutablePath, error) || error) {
        return MakeResult(ProjectPackCode::RuntimeExecutableNotFound,
                             "The runtime executable was not found: " + options.runtimeExecutablePath.string());
    }

    const std::filesystem::path projectRoot = options.projectFile.parent_path();
    const std::filesystem::path contentRoot = projectRoot / package.contentRoot;
    if (!std::filesystem::is_directory(contentRoot, error) || error) {
        return MakeResult(ProjectPackCode::ContentRootNotFound,
                             "The project's content root was not found: " + contentRoot.string());
    }

    const auto referencedResourceExists = [&contentRoot](const std::filesystem::path& relativePath) {
        if (relativePath.empty()) {
            return true;
        }
        std::error_code resourceError;
        return std::filesystem::is_regular_file(contentRoot / relativePath, resourceError) && !resourceError;
    };
    if (!referencedResourceExists(package.applicationSettings.splashImagePath)) {
        return MakeResult(ProjectPackCode::ApplicationResourceNotFound,
                             "The configured splash image was not found under the content root.");
    }
    for (const auto& [target, icons] : package.applicationSettings.iconPathsByTarget) {
        static_cast<void>(target);
        for (const auto& [pixelSize, iconPath] : icons) {
            static_cast<void>(pixelSize);
            if (!referencedResourceExists(iconPath)) {
                return MakeResult(ProjectPackCode::ApplicationResourceNotFound,
                                     "A configured application icon was not found under the content root: " +
                                         iconPath.string());
            }
        }
    }

    error.clear();
    if (std::filesystem::exists(options.outputDirectory, error) &&
        !std::filesystem::is_empty(options.outputDirectory, error)) {
        return MakeResult(ProjectPackCode::OutputDirectoryNotEmpty,
                             "The output directory already exists and is not empty: " +
                                 options.outputDirectory.string());
    }
    if (error) {
        return MakeResult(ProjectPackCode::CopyFailed,
                             "Unable to inspect the output directory: " + options.outputDirectory.string());
    }

    error.clear();
    std::filesystem::create_directories(options.outputDirectory, error);
    if (error) {
        return MakeResult(ProjectPackCode::CopyFailed,
                             "Unable to create the output directory: " + options.outputDirectory.string());
    }

    if (options.cookTarget.has_value()) {
        // The cooker publishes a fresh directory atomically; remove only our newly-created,
        // still-empty destination before handing publication to it.
        std::filesystem::remove(options.outputDirectory, error);
        const auto cooked = CookProjectAssets(options.projectFile, options.outputDirectory, *options.cookTarget);
        if (!cooked.success) return MakeResult(ProjectPackCode::CopyFailed, cooked.message);
        std::filesystem::copy_file(options.runtimeExecutablePath,
            options.outputDirectory/options.runtimeExecutablePath.filename(), error);
        if (error) return MakeResult(ProjectPackCode::CopyFailed, error.message());
#if defined(_WIN32)
        // Portable tool distributions keep the runtime's dependent DLLs beside its executable.
        // Carry that runtime set into the game export; otherwise it only runs on the build PC.
        const auto runtimeDirectory = options.runtimeExecutablePath.parent_path().empty()
            ? std::filesystem::path(".") : options.runtimeExecutablePath.parent_path();
        for (std::filesystem::directory_iterator it(runtimeDirectory,error), end; !error && it != end; it.increment(error)) {
            if (it->path().extension() == ".dll" && it->is_regular_file(error))
                std::filesystem::copy_file(it->path(),options.outputDirectory/it->path().filename(),error);
            if (error) break;
        }
        if (error) return MakeResult(ProjectPackCode::CopyFailed,"Cannot copy runtime DLLs: " + error.message());
#endif
        return MakeResult(ProjectPackCode::Success, cooked.message);
    }

    // Copy the runtime binary in, preserving its executable permission bits - copy_file alone
    // copies file content but not necessarily the source's mode bits on every platform/filesystem.
    const std::filesystem::path copiedRuntimePath =
        options.outputDirectory / options.runtimeExecutablePath.filename();
    error.clear();
    std::filesystem::copy_file(options.runtimeExecutablePath, copiedRuntimePath,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return MakeResult(ProjectPackCode::CopyFailed,
                             "Unable to copy the runtime executable into the output directory.");
    }
    const std::filesystem::perms sourcePermissions = std::filesystem::status(options.runtimeExecutablePath, error).permissions();
    if (!error) {
        std::filesystem::permissions(copiedRuntimePath, sourcePermissions, error);
    }

    // Copy the .uvproject manifest verbatim (unchanged) - its relative contentRoot/
    // startupScenePath still resolve correctly once the content tree below is copied alongside
    // it at the same relative path.
    error.clear();
    std::filesystem::copy_file(options.projectFile, options.outputDirectory / options.projectFile.filename(),
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return MakeResult(ProjectPackCode::CopyFailed,
                             "Unable to copy the .uvproject project file into the output directory.");
    }

    // Copy the whole content tree, preserving package.contentRoot's own relative path/name so the
    // copied manifest's relative paths keep resolving without any rewriting.
    error.clear();
    std::filesystem::copy(contentRoot, options.outputDirectory / package.contentRoot,
                          std::filesystem::copy_options::recursive, error);
    if (error) {
        return MakeResult(ProjectPackCode::CopyFailed,
                             "Unable to copy the project's content root into the output directory: " +
                                 error.message());
    }

    const std::string& productName =
        package.productMetadata.name.empty() ? package.displayName : package.productMetadata.name;
    std::string message = "Packaged '" + productName + "' v" + package.productMetadata.version + " (build " +
                          std::to_string(package.productMetadata.buildNumber) + ") into " +
                          options.outputDirectory.string() + " - run with: " +
                          copiedRuntimePath.filename().string() + " --project " +
                          options.projectFile.filename().string();
    if (!package.applicationSettings.publisherName.empty()) {
        message += " - publisher: " + package.applicationSettings.publisherName;
    }
    if (!package.applicationSettings.copyrightLine.empty()) {
        message += " - " + package.applicationSettings.copyrightLine;
    }
    return MakeResult(ProjectPackCode::Success, std::move(message));
}

} // namespace UVE::Pack
