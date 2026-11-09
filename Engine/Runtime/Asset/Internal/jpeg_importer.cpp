// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "jpeg_importer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers.h"

#include "jpeg_metadata.h"
#include "texture_asset.h"
#include "texture_import_settings.h"
#include "texture_mipmap.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kJpegImporterNameUVE = "JpegImporterUVE";
constexpr std::uint64_t kMaximumJpegImporterSourceBytesUVE = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kJpegTemporarySuffixUVE = ".uve_jpeg_tmp";

[[nodiscard]] bool ImportJpegSourceUVE(const std::filesystem::path& sourcePath,
                                       const std::filesystem::path& destinationPath,
                                       const AssetImportSettingsUVE& settings) {
    try {
        if (destinationPath.extension() != ".uvtex") {
            UVE_ERROR("JpegImporterUVE: destination \"{}\" must use the .uvtex extension",
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
        texture.format = TextureAssetFormatUVE::RGBA8Unorm;
        const TextureImportSettingsUVE importSettings = ResolveTextureImportSettingsUVE(settings);
        texture.colorSpace = importSettings.colorSpace;
        texture.usage = importSettings.usage;
        texture.pixels = std::move(decoded.pixels);
        if (!Detail::PrepareTextureForImportUVE(texture, importSettings)) {
            UVE_ERROR("JpegImporterUVE: failed to prepare mip chain or compress texture for \"{}\"",
                      sourcePath.string());
            return false;
        }
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
