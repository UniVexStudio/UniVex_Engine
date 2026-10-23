// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// glTF/GLB material and image import: the part of the importer that turns `materials`,
// `textures` and `images` into engine assets.
//
// The single-primitive converter is deliberately geometry-only, so before this module a GLB with
// baked-in materials imported as a mesh with no material at all and drew grey - the textures were
// inside the file the whole time. Here the material the imported primitive names is published as a
// `.uvmat` beside the model, every texture it samples is published as a `.uvtex` beside the model
// too (GLB images are read out of the binary chunk, external URIs from the source directory), and
// the mesh references the material by GUID. Everything is bounded and failure-atomic: a malformed
// material or an undecodable image leaves that part out with a diagnostic instead of failing the
// model, because a mesh with a missing texture is still a usable asset.

#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <nlohmann/json.hpp>

#include "model_texture_import_uve.h"

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"

namespace UVE::Asset::Detail {

struct GltfMaterialImportResultUVE final {
    /// In the order the material the primitive uses was published; empty when the primitive names
    /// no material or that material could not be published.
    std::vector<AssetGuidUVE> materialGuids;
    /// The published material of the single imported primitive, or `kInvalidAssetGuidUVE`.
    AssetGuidUVE primitiveMaterialGuid = kInvalidAssetGuidUVE;
};

/// Publishes the material of the document's single imported primitive, plus the textures it
/// samples. Returns false only when the asset database refuses a published file; a material or
/// image the document merely describes badly is skipped with a warning and reported through
/// `outResult` staying empty, so the caller can still publish the geometry.
[[nodiscard]] bool ImportGltfMaterialsUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                                          const std::filesystem::path& sourcePath,
                                          const std::filesystem::path& destinationPath,
                                          IAssetDatabaseUVE& assetDatabase,
                                          GltfMaterialImportResultUVE& outResult);

} // namespace UVE::Asset::Detail
