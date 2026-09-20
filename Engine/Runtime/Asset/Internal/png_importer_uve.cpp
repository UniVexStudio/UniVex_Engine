// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/png_importer_uve.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kPngImporterNameUVE = "PngImporterUVE";
constexpr std::uint64_t kMaximumPngImporterSourceBytesUVE = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kPngTemporarySuffixUVE = ".uve_png_tmp";

[[nodiscard]] bool ImportPngSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvetex") {
        UVE_ERROR("PngImporterUVE: destination \"{}\" must use the .uvetex extension", destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kPngImporterNameUVE, kMaximumPngImporterSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    PngRgba8ImageUVE decoded;
    if (!DecodePngRgba8ImageUVE(sourceBytes, decoded)) {
        UVE_ERROR("PngImporterUVE: source \"{}\" failed bounded PNG RGBA8 decoding", sourcePath.string());
        return false;
    }

    const std::uint64_t expectedPixelBytes = static_cast<std::uint64_t>(decoded.width) *
                                               static_cast<std::uint64_t>(decoded.height) * 4ULL;
    if (expectedPixelBytes != decoded.pixels.size()) {
        UVE_ERROR("PngImporterUVE: decoder published inconsistent RGBA8 pixel bytes for \"{}\"",
                  sourcePath.string());
        return false;
    }

    TextureAssetUVE texture;
    texture.width = decoded.width;
    texture.height = decoded.height;
    texture.format = TextureFormatUVE::RGBA8Unorm;
    texture.pixels = std::move(decoded.pixels);
    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kPngImporterNameUVE, kPngTemporarySuffixUVE,
        [&texture](const std::filesystem::path& temporaryPath) { return SaveTextureAssetUVE(texture, temporaryPath); });
}

} // namespace

void RegisterPngImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("png", &ImportPngSourceUVE);
}

} // namespace UVE::Asset
