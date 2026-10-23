// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Texture acquisition shared by the model importers that publish materials beside their model
// (glTF/GLB today, OBJ/MTL next to it). The FBX importer could only resolve textures somebody had
// already imported; a GLB carries its images inside the file and an OBJ only names a PNG next to
// it, so both need to turn source bytes into a registered `.uvtex` themselves, or the model
// arrives with material slots that point at nothing and renders grey.
//
// This header owns exactly that step: decode (PNG/JPEG), build the mip chain through the shared
// texture pipeline, publish `<destination-dir>/<stem>.uvtex` atomically, and register it. It does
// not parse model files, decide which slot a texture belongs to, or touch the GPU.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/texture_asset_uve.h"

namespace UVE::Asset::Detail {

/// Diagnostic identity and hard bounds for one model import's texture work. `maximumImageBytes`
/// caps a single decoded source image; the format importers already cap their own sources at
/// 64 MB, and a model's embedded image has no reason to exceed that.
struct ModelTextureImportOptionsUVE final {
    const char* importerName = "";
    std::string_view temporarySuffix;
    std::uint64_t maximumImageBytes = 64ULL * 1024ULL * 1024ULL;
};

/// Decodes one image blob (PNG or JPEG, chosen by its own signature) and publishes it as a
/// registered `.uvtex` in `destinationPath`'s directory, named from `textureStem`. Returns the
/// registered GUID, or `kInvalidAssetGuidUVE` after logging why the blob could not become a
/// texture. Nothing is published when the image is rejected, so a failed texture never leaves a
/// half-written asset behind for the material to reference.
[[nodiscard]] AssetGuidUVE ImportModelTextureBytesUVE(const std::vector<std::byte>& imageBytes,
                                                      std::string_view textureStem,
                                                      const std::filesystem::path& destinationPath,
                                                      TextureAssetColorSpaceUVE colorSpace,
                                                      TextureUsageUVE usage,
                                                      const ModelTextureImportOptionsUVE& options,
                                                      IAssetDatabaseUVE& assetDatabase);

/// Resolves a texture file named by a model source into a texture GUID. An already imported
/// sibling `.uvtex` with that stem wins (re-imports therefore keep their GUIDs); otherwise the file
/// is read from `sourceDirectory`, decoded and published beside the model exactly as
/// ImportModelTextureBytesUVE does. Paths that leave `sourceDirectory` - absolute, drive-qualified,
/// or containing `..` - are refused rather than followed, because a model file is untrusted input.
[[nodiscard]] AssetGuidUVE ImportModelTextureFileUVE(const std::filesystem::path& sourceDirectory,
                                                     const std::filesystem::path& destinationPath,
                                                     std::string_view authoredTextureFile,
                                                     TextureAssetColorSpaceUVE colorSpace,
                                                     TextureUsageUVE usage,
                                                     const ModelTextureImportOptionsUVE& options,
                                                     IAssetDatabaseUVE& assetDatabase);

/// Filesystem-safe stem for a published texture: `fallbackPrefix_<index>` when the authored name
/// contains nothing usable, with a numeric suffix when two images of one model share a name.
[[nodiscard]] std::string MakeTextureFileStemUVE(std::string_view authoredName, std::string_view fallbackPrefix,
                                                 std::size_t index, std::vector<std::string>& usedStems);

} // namespace UVE::Asset::Detail
