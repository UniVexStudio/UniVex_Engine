// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "import_helpers_uve.h"

#include <fstream>
#include <limits>

#include "uve/asset/texture_compression_uve.h"
#include "uve/asset/texture_mipmap_uve.h"

namespace UVE::Asset::Detail {

AssetGuidUVE ResolveImportedSiblingTextureGuidUVE(const std::filesystem::path& destinationPath,
                                                  const std::string_view authoredTextureFile,
                                                  const IAssetDatabaseUVE& assetDatabase) {
    if (authoredTextureFile.empty()) {
        return kInvalidAssetGuidUVE;
    }
    const std::filesystem::path authored{std::string(authoredTextureFile)};
    const std::string stem = authored.stem().string();
    if (stem.empty()) {
        return kInvalidAssetGuidUVE;
    }
    const auto toLower = [](std::string_view s) {
        std::string res;
        res.reserve(s.size());
        for (const char c : s) {
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return res;
    };
    const std::string stemLower = toLower(stem);
    const std::filesystem::path siblingDirectory = destinationPath.parent_path().lexically_normal();
    const std::filesystem::path texturesDir1 = (siblingDirectory / "textures").lexically_normal();
    const std::filesystem::path texturesDir2 = (siblingDirectory / "Textures").lexically_normal();
    const std::filesystem::path fbmDir =
        (siblingDirectory / (destinationPath.stem().string() + ".fbm")).lexically_normal();

    AssetGuidUVE fallbackGuid = kInvalidAssetGuidUVE;

    for (const AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
        if (record.path.extension() != ".uvtex") {
            continue;
        }
        const std::string recStem = record.path.stem().string();
        const std::string recStemLower = toLower(recStem);
        if (recStemLower != stemLower && recStemLower.find(stemLower) == std::string::npos &&
            stemLower.find(recStemLower) == std::string::npos) {
            continue;
        }
        const std::filesystem::path recParent = record.path.parent_path().lexically_normal();
        if (recParent == siblingDirectory || recParent == texturesDir1 || recParent == texturesDir2 ||
            recParent == fbmDir) {
            return record.guid;
        }
        if (fallbackGuid == kInvalidAssetGuidUVE && recStemLower == stemLower) {
            fallbackGuid = record.guid;
        }
    }
    return fallbackGuid;
}

[[nodiscard]] bool PrepareTextureForImportUVE(TextureAssetUVE& texture,
                                               const TextureImportSettingsUVE& settings) {
    if (settings.generateMipmaps &&
        !GenerateTextureMipmapsUVE(texture, settings.mipFilter, settings.maxMipLevels)) {
        return false;
    }
    if (settings.compressionMode != TextureCompressionModeUVE::None &&
        !CompressTextureAssetWithBasisUVE(texture, settings.compressionMode, settings.compressionQuality,
                                          settings.compressionEffort)) {
        return false;
    }
    return IsTextureAssetValidUVE(texture);
}

[[nodiscard]] bool ReadBoundedSourceBytesUVE(const std::filesystem::path& sourcePath,
                                             const char* importerName,
                                             const std::uint64_t maximumBytes,
                                             std::vector<std::byte>& outBytes) {
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        UVE_ERROR("{}: failed to open source \"{}\"", importerName, sourcePath.string());
        return false;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > maximumBytes ||
        static_cast<std::uint64_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("{}: source \"{}\" exceeds the bounded source-size limit", importerName, sourcePath.string());
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            UVE_ERROR("{}: source \"{}\" could not be read completely", importerName, sourcePath.string());
            return false;
        }
    }
    outBytes = std::move(bytes);
    return true;
}

} // namespace UVE::Asset::Detail
