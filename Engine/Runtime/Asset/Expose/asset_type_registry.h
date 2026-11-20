// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "i_asset_importer.h"
#include "uve_file_envelope.h"

namespace UVE::Asset {

/// Strips an optional leading '.' and lowercases ASCII characters ("UVMaterial" / ".UVMaterial" -> "uvmaterial").
[[nodiscard]] std::string NormalizeAssetExtensionUVE(std::string_view extension);

/// Extracts and normalizes `path.extension()` without touching the filesystem.
[[nodiscard]] std::string NormalizeAssetPathExtensionUVE(const std::filesystem::path& path);

/// Single authoritative mapping from a file extension (with or without leading '.', case-insensitive)
/// to its expected `.uve*` envelope `AssetKindUVE`. Returns `std::nullopt` for non-envelope files
/// (such as raw authoring sources or plain text/shader scripts).
[[nodiscard]] std::optional<AssetKindUVE> ExpectedAssetKindForExtensionUVE(std::string_view extension);

/// Convenience overload inspecting `path.extension()`.
[[nodiscard]] std::optional<AssetKindUVE> ExpectedAssetKindForPathUVE(const std::filesystem::path& path);

/// True when `extension` (with or without leading '.', case-insensitive) names a native `.uve*`
/// envelope asset that ships in cooked project content (excludes `.uvsave` and `.uvbundle`).
[[nodiscard]] bool IsCookableNativeEnvelopeExtensionUVE(std::string_view extension);

/// True when `extension` names a text/script/shader source file that ships in cooked project content
/// (`.uvs`, `.glsl`, `.vert`, `.frag`, `.comp`, `.glslinc`).
[[nodiscard]] bool IsCookableRuntimeTextExtensionUVE(std::string_view extension);

/// True when `extension` names any cooked runtime content asset (native envelope or runtime text/shader).
[[nodiscard]] bool IsCookableRuntimeExtensionUVE(std::string_view extension);

/// Returns all normalized native envelope extensions that use generic deterministic envelope copy/re-import.
[[nodiscard]] std::span<const std::string_view> GetBuiltInEnvelopeExtensionsUVE() noexcept;

/// Classifies an extension (with or without leading '.', case-insensitive) into `AssetImportSourceKindUVE`.
[[nodiscard]] AssetImportSourceKindUVE ClassifyImportSourceKindForExtensionUVE(std::string_view extension) noexcept;

/// Canonical camelCase protocol label for an `AssetImportSourceKindUVE` (used by EditorBridge and diagnostics).
[[nodiscard]] std::string_view AssetImportSourceKindLabelUVE(AssetImportSourceKindUVE kind) noexcept;

} // namespace UVE::Asset
