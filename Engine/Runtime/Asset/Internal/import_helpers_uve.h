// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Shared building blocks for the raw-file format importers (BMP/TGA/PNG/JPEG/OBJ/MTL/glTF and the
// shader-source importer). Before this header existed, every importer owned a private copy of the
// same bounded source-read (~45 lines) and atomic-publish (~30 lines) helpers, distinguished only
// by the importer-name used in log messages and the temporary-file suffix — the 2026-09-17 engine
// audit flagged that copy-paste family as the engine's largest duplication cluster. The helpers
// here preserve each importer's exact prior behavior: identical log strings (parameterized by
// importerName), identical "<destination><temporarySuffix>" publication paths, and identical
// error-code-driven control flow.
//
// Format importers remain responsible for everything genuinely format-specific: source caps,
// destination-extension policy, decoding/conversion, and asset assembly.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_import_settings_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset::Detail {

/// Reads a source file into memory under a hard byte cap, logging with the caller's importer name
/// so diagnostics stay format-specific. Returns false (and logs) when the file cannot be opened,
/// exceeds maximumBytes, or cannot be read completely.
[[nodiscard]] bool ReadBoundedSourceBytesUVE(const std::filesystem::path& sourcePath,
                                             const char* importerName,
                                             std::uint64_t maximumBytes,
                                             std::vector<std::byte>& outBytes);

/// Foundation WP7: turns a texture filename authored inside a model file (FBX today, MTL when it
/// adopts this) into the GUID of an ALREADY IMPORTED texture, or `kInvalidAssetGuidUVE`.
/// The only evidence accepted is a registered `.uvtex` record whose stem equals the authored
/// file's stem and whose directory is the destination's directory (the import's siblings):
/// first match in the database's deterministic order wins. Nothing is registered, created or
/// guessed - an unresolved reference stays empty and the caller reports it, because inventing a
/// GUID for a texture nobody imported would be a dangling reference with a confident face.
[[nodiscard]] AssetGuidUVE ResolveImportedSiblingTextureGuidUVE(const std::filesystem::path& destinationPath,
                                                                std::string_view authoredTextureFile,
                                                                const IAssetDatabaseUVE& assetDatabase);

/// `<model-stem>.<sanitised-name>`: the file stem a model importer publishes beside the model for
/// one of the model's own materials. The name is reduced to [A-Za-z0-9_-], falls back to
/// `fallbackPrefix_<index>` when nothing survives, and gains `_<n>` on a clash so two authored
/// materials of one model can never publish over each other. Deterministic for a given file, which
/// is what makes a re-import land on the same paths - and therefore, through RegisterUVE, on the
/// same GUIDs. Lives here rather than in one importer because FBX, glTF and OBJ all publish
/// per-material assets beside the model and must name them the same way.
[[nodiscard]] std::string MakeMaterialFileStemUVE(std::string_view modelStem, std::string_view materialName,
                                                  std::size_t materialIndex, std::string_view fallbackPrefix,
                                                  std::vector<std::string>& usedStems);

/// Applies the shared texture post-decode pipeline: generate configured mip levels, encode the
/// optional portable Basis/KTX2 payload, then validate the complete asset before publication.
[[nodiscard]] bool PrepareTextureForImportUVE(TextureAssetUVE& texture,
                                               const TextureImportSettingsUVE& settings);

/// Publishes a converted asset atomically: creates the destination's parent directories, asks
/// saveAsset to persist to "<destination><temporarySuffix>", then renames onto the destination.
/// A leftover stale temporary file is removed first, and on any failure the temporary file is
/// cleaned up. importerName prefixes every logged diagnostic, exactly as the per-importer private
/// copies did.
template <typename SaveAssetFn>
[[nodiscard]] bool PublishAssetAtomicallyUVE(const std::filesystem::path& destinationPath,
                                             const char* importerName,
                                             const std::string_view temporarySuffix,
                                             SaveAssetFn&& saveAsset) {
    std::error_code errorCode;
    if (const std::filesystem::path parent = destinationPath.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode) {
            UVE_ERROR("{}: failed to create destination directory \"{}\": {}", importerName, parent.string(),
                      errorCode.message());
            return false;
        }
    }
    const std::filesystem::path temporaryPath = destinationPath.string() + std::string(temporarySuffix);
    std::filesystem::remove(temporaryPath, errorCode);
    if (!saveAsset(temporaryPath)) {
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, destinationPath, errorCode);
    if (errorCode) {
        UVE_ERROR("{}: failed to publish destination \"{}\": {}", importerName, destinationPath.string(),
                  errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

} // namespace UVE::Asset::Detail
