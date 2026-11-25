// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "bmp_importer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers.h"

#include "bmp_metadata.h"
#include "texture_asset.h"
#include "texture_import_settings.h"
#include "texture_mipmap.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kBmpImporterName = "BmpImporter";
constexpr std::uint64_t kMaximumBmpImporterSourceBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kBmpTemporarySuffix = ".uve_bmp_tmp";

[[nodiscard]] bool ImportBmpSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettings& settings) {
    if (destinationPath.extension() != ".uvtex") {
        UVE_ERROR("BmpImporter: destination \"{}\" must use the .uvtex extension", destinationPath.string());
        return false;
    }
    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytes(sourcePath, kBmpImporterName, kMaximumBmpImporterSourceBytes,
                                           sourceBytes)) {
        return false;
    }
    BmpRgba8Image decoded;
    if (!DecodeBmpRgba8Image(sourceBytes, decoded)) {
        UVE_ERROR("BmpImporter: source \"{}\" failed bounded BMP RGBA8 decoding", sourcePath.string());
        return false;
    }
    TextureAsset texture;
    texture.width = decoded.width;
    texture.height = decoded.height;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    const TextureImportSettings importSettings = ResolveTextureImportSettings(settings);
    texture.colorSpace = importSettings.colorSpace;
    texture.usage = importSettings.usage;
    texture.pixels = std::move(decoded.pixels);
    if (!Detail::PrepareTextureForImport(texture, importSettings)) {
        UVE_ERROR("BmpImporter: failed to prepare mip chain or compress texture for \"{}\"",
                  sourcePath.string());
        return false;
    }
    return Detail::PublishAssetAtomically(
        destinationPath, kBmpImporterName, kBmpTemporarySuffix,
        [&texture](const std::filesystem::path& temporaryPath) { return SaveTextureAsset(texture, temporaryPath); });
}

} // namespace

void RegisterBmpImporter(IAssetImporter& importer) {
    importer.RegisterImporter("bmp", &ImportBmpSource);
}

} // namespace UVE::Asset
