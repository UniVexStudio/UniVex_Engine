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

constexpr const char* kJpegImporterName = "JpegImporter";
constexpr std::uint64_t kMaximumJpegImporterSourceBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::string_view kJpegTemporarySuffix = ".uve_jpeg_tmp";

[[nodiscard]] bool ImportJpegSource(const std::filesystem::path& sourcePath,
                                       const std::filesystem::path& destinationPath,
                                       const AssetImportSettings& settings) {
    try {
        if (destinationPath.extension() != ".uvtex") {
            UVE_ERROR("JpegImporter: destination \"{}\" must use the .uvtex extension",
                      destinationPath.string());
            return false;
        }
        std::vector<std::byte> sourceBytes;
        if (!Detail::ReadBoundedSourceBytes(sourcePath, kJpegImporterName, kMaximumJpegImporterSourceBytes,
                                               sourceBytes)) {
            return false;
        }
        JpegRgba8Image decoded;
        if (!DecodeJpegRgba8Image(sourceBytes, decoded)) {
            UVE_ERROR("JpegImporter: source \"{}\" failed bounded JPEG RGBA8 decoding", sourcePath.string());
            return false;
        }
        const std::uint64_t expectedPixelBytes = static_cast<std::uint64_t>(decoded.width) *
                                                  static_cast<std::uint64_t>(decoded.height) * 4ULL;
        if (expectedPixelBytes != decoded.pixels.size()) {
            UVE_ERROR("JpegImporter: decoder published inconsistent RGBA8 pixel bytes for \"{}\"",
                      sourcePath.string());
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
            UVE_ERROR("JpegImporter: failed to prepare mip chain or compress texture for \"{}\"",
                      sourcePath.string());
            return false;
        }
        return Detail::PublishAssetAtomically(destinationPath, kJpegImporterName, kJpegTemporarySuffix,
                                                 [&texture](const std::filesystem::path& temporaryPath) {
                                                     return SaveTextureAsset(texture, temporaryPath);
                                                 });
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterJpegImporter(IAssetImporter& importer) {
    importer.RegisterImporter("jpg", &ImportJpegSource);
    importer.RegisterImporter("jpeg", &ImportJpegSource);
}

} // namespace UVE::Asset
