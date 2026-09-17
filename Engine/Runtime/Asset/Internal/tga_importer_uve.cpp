// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/tga_importer_uve.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/tga_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kTgaImporterNameUVE = "TgaImporterUVE";
constexpr std::uint64_t kMaximumTgaImporterSourceBytesUVE = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kTgaTemporarySuffixUVE = ".uve_tga_tmp";

[[nodiscard]] bool ImportTgaSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvetex") {
        UVE_ERROR("TgaImporterUVE: destination \"{}\" must use the .uvetex extension", destinationPath.string());
        return false;
    }
    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kTgaImporterNameUVE, kMaximumTgaImporterSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }
    TgaRgba8ImageUVE decoded;
    if (!DecodeTgaRgba8ImageUVE(sourceBytes, decoded)) {
        UVE_ERROR("TgaImporterUVE: source \"{}\" failed bounded TGA RGBA8 decoding", sourcePath.string());
        return false;
    }
    TextureAssetUVE texture;
    texture.width = decoded.width;
    texture.height = decoded.height;
    texture.format = TextureFormatUVE::RGBA8Unorm;
    texture.pixels = std::move(decoded.pixels);
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kTgaImporterNameUVE, kTgaTemporarySuffixUVE,
        [&texture](const std::filesystem::path& temporaryPath) { return SaveTextureAssetUVE(texture, temporaryPath); });
}

} // namespace

void RegisterTgaImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("tga", &ImportTgaSourceUVE);
}

} // namespace UVE::Asset
