// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/bmp_importer_uve.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/bmp_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/debug/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kBmpImporterNameUVE = "BmpImporterUVE";
constexpr std::uint64_t kMaximumBmpImporterSourceBytesUVE = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kBmpTemporarySuffixUVE = ".uve_bmp_tmp";

[[nodiscard]] bool ImportBmpSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvetex") {
        UVE_ERROR("BmpImporterUVE: destination \"{}\" must use the .uvetex extension", destinationPath.string());
        return false;
    }
    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kBmpImporterNameUVE, kMaximumBmpImporterSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }
    BmpRgba8ImageUVE decoded;
    if (!DecodeBmpRgba8ImageUVE(sourceBytes, decoded)) {
        UVE_ERROR("BmpImporterUVE: source \"{}\" failed bounded BMP RGBA8 decoding", sourcePath.string());
        return false;
    }
    TextureAssetUVE texture;
    texture.width = decoded.width;
    texture.height = decoded.height;
    texture.format = TextureFormatUVE::RGBA8Unorm;
    texture.pixels = std::move(decoded.pixels);
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kBmpImporterNameUVE, kBmpTemporarySuffixUVE,
        [&texture](const std::filesystem::path& temporaryPath) { return SaveTextureAssetUVE(texture, temporaryPath); });
}

} // namespace

void RegisterBmpImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("bmp", &ImportBmpSourceUVE);
}

} // namespace UVE::Asset
