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
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset::Detail {

/// Reads a source file into memory under a hard byte cap, logging with the caller's importer name
/// so diagnostics stay format-specific. Returns false (and logs) when the file cannot be opened,
/// exceeds maximumBytes, or cannot be read completely.
[[nodiscard]] bool ReadBoundedSourceBytesUVE(const std::filesystem::path& sourcePath,
                                             const char* importerName,
                                             std::uint64_t maximumBytes,
                                             std::vector<std::byte>& outBytes);

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
