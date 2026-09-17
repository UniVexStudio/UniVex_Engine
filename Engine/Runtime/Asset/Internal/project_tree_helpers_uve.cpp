// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_tree_helpers_uve.h"

#include <system_error>

namespace UVE::Asset::Detail {

[[nodiscard]] std::optional<std::filesystem::path>
MakeRootRelativePathUVE(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::error_code errorCode;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode).lexically_normal();
    if (errorCode) {
        return std::nullopt;
    }

    auto rootIt = root.begin();
    auto pathIt = absolutePath.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
        if (pathIt == absolutePath.end() || *rootIt != *pathIt) {
            return std::nullopt;
        }
    }

    std::filesystem::path relativePath;
    for (; pathIt != absolutePath.end(); ++pathIt) {
        relativePath /= *pathIt;
    }
    if (relativePath.empty() || relativePath == ".") {
        return std::nullopt;
    }
    return relativePath.lexically_normal();
}

[[nodiscard]] std::filesystem::path NormalizeProjectContentRootUVE(std::filesystem::path path) {
    path = std::move(path).lexically_normal();
    // `lexically_normal()` deliberately retains a trailing separator on a relative directory such
    // as `assets/`. Remove only that empty final component so path iteration cannot mistake it for
    // a root-boundary mismatch, while preserving absolute filesystem roots such as `/`.
    if (path.has_relative_path() && path.filename().empty()) {
        path = path.parent_path();
    }
    return path;
}

[[nodiscard]] std::map<std::string, AssetGuidUVE>
BuildRegisteredAssetsByRelativePathUVE(const std::filesystem::path& absoluteContentRoot,
                                       const IAssetDatabaseUVE& assetDatabase) {
    std::map<std::string, AssetGuidUVE> registeredAssetsByRelativePath;
    for (const AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
        const std::optional<std::filesystem::path> relativePath =
            MakeRootRelativePathUVE(record.path, absoluteContentRoot);
        if (!relativePath.has_value()) {
            continue;
        }
        registeredAssetsByRelativePath.emplace(relativePath->generic_string(), record.guid);
    }
    return registeredAssetsByRelativePath;
}

[[nodiscard]] bool WalkProjectContentTreeUVE(
    const std::filesystem::path& absoluteContentRoot,
    std::string& outDiagnostic,
    const std::function<bool(const std::filesystem::path& absoluteEntryPath,
                             const std::filesystem::path& rootRelativePath,
                             ProjectFileEntryKindUVE kind)>& perEntry) {
    outDiagnostic.clear();
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(
        absoluteContentRoot, std::filesystem::directory_options::skip_permission_denied, errorCode);
    if (errorCode) {
        outDiagnostic = "Unable to enumerate the project content root: " + errorCode.message();
        return false;
    }

    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const std::filesystem::directory_entry entry = *iterator;
        const std::filesystem::file_status entryStatus = entry.symlink_status(errorCode);
        if (errorCode) {
            outDiagnostic = "Unable to inspect a project content entry: " + errorCode.message();
            return false;
        }

        if (std::filesystem::is_symlink(entryStatus)) {
            iterator.disable_recursion_pending();
        } else if (std::filesystem::is_directory(entryStatus) || std::filesystem::is_regular_file(entryStatus)) {
            const std::optional<std::filesystem::path> relativePath =
                MakeRootRelativePathUVE(entry.path(), absoluteContentRoot);
            if (!relativePath.has_value()) {
                outDiagnostic = "A project content entry escaped the configured root boundary.";
                return false;
            }

            const ProjectFileEntryKindUVE kind = std::filesystem::is_directory(entryStatus)
                                                     ? ProjectFileEntryKindUVE::Directory
                                                     : ProjectFileEntryKindUVE::File;
            // When perEntry reports failure it owns the diagnostic content (or deliberately
            // leaves it empty, like the historically-silent index).
            if (!perEntry(entry.path(), *relativePath, kind)) {
                return false;
            }
        } else {
            iterator.disable_recursion_pending();
        }

        iterator.increment(errorCode);
        if (errorCode) {
            outDiagnostic = "Unable to continue project content enumeration: " + errorCode.message();
            return false;
        }
    }
    return true;
}

} // namespace UVE::Asset::Detail
