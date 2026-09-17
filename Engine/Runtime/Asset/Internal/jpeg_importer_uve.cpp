// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/jpeg_importer_uve.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/debug/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kJpegImporterNameUVE = "JpegImporterUVE";
constexpr std::uint64_t kMaximumJpegImporterSourceBytesUVE = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kJpegTemporarySuffixUVE = ".uve_jpeg_tmp";

[[nodiscard]] bool ImportJpegSourceUVE(const std::filesystem::path& sourcePath,
                                       const std::filesystem::path& destinationPath,
                                       const AssetImportSettingsUVE& /*settings*/) {
    try {
        if (destinationPath.extension() != ".uvetex") {
            UVE_ERROR("JpegImporterUVE: destination \"{}\" must use the .uvetex extension",
                      destinationPath.string());
            return false;
        }
        std::vector<std::byte> sourceBytes;
        if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kJpegImporterNameUVE, kMaximumJpegImporterSourceBytesUVE,
                                               sourceBytes)) {
            return false;
        }
        JpegRgba8ImageUVE decoded;
        if (!DecodeJpegRgba8ImageUVE(sourceBytes, decoded)) {
            UVE_ERROR("JpegImporterUVE: source \"{}\" failed bounded JPEG RGBA8 decoding", sourcePath.string());
            return false;
        }
        const std::uint64_t expectedPixelBytes = static_cast<std::uint64_t>(decoded.width) *
                                                  static_cast<std::uint64_t>(decoded.height) * 4ULL;
        if (expectedPixelBytes != decoded.pixels.size()) {
            UVE_ERROR("JpegImporterUVE: decoder published inconsistent RGBA8 pixel bytes for \"{}\"",
                      sourcePath.string());
            return false;
        }
        TextureAssetUVE texture;
        texture.width = decoded.width;
        texture.height = decoded.height;
        texture.format = TextureFormatUVE::RGBA8Unorm;
        texture.pixels = std::move(decoded.pixels);
        return Detail::PublishAssetAtomicallyUVE(destinationPath, kJpegImporterNameUVE, kJpegTemporarySuffixUVE,
                                                 [&texture](const std::filesystem::path& temporaryPath) {
                                                     return SaveTextureAssetUVE(texture, temporaryPath);
                                                 });
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterJpegImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("jpg", &ImportJpegSourceUVE);
    importer.RegisterImporterUVE("jpeg", &ImportJpegSourceUVE);
}

} // namespace UVE::Asset
