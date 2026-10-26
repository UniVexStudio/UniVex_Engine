// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "import_helpers_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/texture_compression_uve.h"
#include "uve/asset/texture_mipmap_uve.h"

namespace UVE::Asset::Detail {

namespace {

/// The signature bytes each supported embedded image format starts with. Sniffing beats trusting a
/// filename because the name inside an FBX is routinely wrong or missing entirely.
[[nodiscard]] bool StartsWithUVE(const std::vector<std::byte>& bytes, const std::span<const std::byte> signature) {
    return bytes.size() >= signature.size() &&
           std::equal(signature.begin(), signature.end(), bytes.begin());
}

constexpr std::array<std::byte, 8U> kPngSignatureUVE{std::byte{0x89}, std::byte{0x50}, std::byte{0x4E},
                                                      std::byte{0x47}, std::byte{0x0D}, std::byte{0x0A},
                                                      std::byte{0x1A}, std::byte{0x0A}};
constexpr std::array<std::byte, 3U> kJpegSignatureUVE{std::byte{0xFF}, std::byte{0xD8}, std::byte{0xFF}};

/// The decoded pixels of whichever format the bytes turned out to be.
struct DecodedImageUVE final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::byte> pixels;
};

/// Matches a filename ignoring case, which is what a Windows-authored model's reference needs on a
/// case-sensitive filesystem.
[[nodiscard]] bool EqualsIgnoringCaseUVE(const std::string_view lhs, const std::string_view rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const char a, const char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

} // namespace

bool DecodeImageBytesUVE(const std::vector<std::byte>& bytes, const TextureImportSettingsUVE& settings,
                         TextureAssetUVE& outTexture) {
    if (bytes.empty()) {
        return false;
    }
    DecodedImageUVE decoded;
    if (StartsWithUVE(bytes, std::span<const std::byte>(kPngSignatureUVE))) {
        PngRgba8ImageUVE png;
        if (!DecodePngRgba8ImageUVE(bytes, png)) {
            return false;
        }
        decoded = DecodedImageUVE{png.width, png.height, std::move(png.pixels)};
    } else if (StartsWithUVE(bytes, std::span<const std::byte>(kJpegSignatureUVE))) {
        JpegRgba8ImageUVE jpeg;
        if (!DecodeJpegRgba8ImageUVE(bytes, jpeg)) {
            return false;
        }
        decoded = DecodedImageUVE{jpeg.width, jpeg.height, std::move(jpeg.pixels)};
    } else {
        return false; // neither format this engine can decode from bytes
    }

    const std::uint64_t expectedPixelBytes =
        static_cast<std::uint64_t>(decoded.width) * static_cast<std::uint64_t>(decoded.height) * 4ULL;
    if (decoded.width == 0U || decoded.height == 0U || expectedPixelBytes != decoded.pixels.size()) {
        return false;
    }

    TextureAssetUVE texture;
    texture.width = decoded.width;
    texture.height = decoded.height;
    texture.format = TextureAssetFormatUVE::RGBA8Unorm;
    texture.colorSpace = settings.colorSpace;
    texture.usage = settings.usage;
    texture.pixels = std::move(decoded.pixels);
    if (!PrepareTextureForImportUVE(texture, settings)) {
        return false;
    }
    outTexture = std::move(texture);
    return true;
}

bool PublishEmbeddedTextureUVE(const std::filesystem::path& destinationPath, const std::vector<std::byte>& bytes,
                               const TextureImportSettingsUVE& settings, const char* importerName,
                               const std::string_view describeSource) {
    TextureAssetUVE texture;
    if (!DecodeImageBytesUVE(bytes, settings, texture)) {
        UVE_WARNING("{}: {} is not a PNG or JPEG this engine can decode; the slot stays empty", importerName,
                    describeSource);
        return false;
    }
    return PublishAssetAtomicallyUVE(
        destinationPath, importerName, std::string_view{".uve_embedded_tex_tmp"},
        [&texture](const std::filesystem::path& temporaryPath) { return SaveTextureAssetUVE(texture, temporaryPath); });
}

std::optional<std::filesystem::path> ResolveAuthoredTextureFileUVE(const std::filesystem::path& modelDirectory,
                                                                   const std::string_view modelFileStem,
                                                                   const std::string_view authoredTextureFile) {
    if (authoredTextureFile.empty() || modelDirectory.empty()) {
        return std::nullopt;
    }
    // A model authored on Windows names its textures with backslashes and often with the exporting
    // machine's whole directory in front of them; only the tail can mean anything here.
    std::string authored{authoredTextureFile};
    std::replace(authored.begin(), authored.end(), '\\', '/');
    const std::size_t lastSeparator = authored.find_last_of('/');
    const std::string_view baseName{authored.data() + (lastSeparator == std::string::npos ? 0U : lastSeparator + 1U),
                                    authored.size() - (lastSeparator == std::string::npos ? 0U : lastSeparator + 1U)};
    if (baseName.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    // Exact first: the whole authored tail relative to the model, then the name in the places a
    // model's maps actually live. Both are cheap and neither touches a directory listing.
    const std::vector<std::filesystem::path> candidates{
        (modelDirectory / authored).lexically_normal(),
        modelDirectory / baseName,
        modelDirectory / "textures" / baseName,
        modelDirectory / "Textures" / baseName,
        modelDirectory / (std::string{modelFileStem} + ".fbm") / baseName,
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
    }

    // Then the same places, matched by name ignoring case: `Wood.PNG` and `wood.png` are one file
    // to the tool that wrote the model, and must be one file here too.
    for (const std::filesystem::path& directory :
         {modelDirectory, modelDirectory / "textures", modelDirectory / "Textures",
          modelDirectory / (std::string{modelFileStem} + ".fbm")}) {
        std::filesystem::directory_iterator entries(directory, error);
        if (error) {
            error.clear();
            continue;
        }
        for (const std::filesystem::directory_entry& entry : entries) {
            std::error_code entryError;
            if (!entry.is_regular_file(entryError)) {
                continue;
            }
            if (EqualsIgnoringCaseUVE(entry.path().filename().string(), baseName)) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

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

std::string MakeMaterialFileStemUVE(const std::string_view modelStem, const std::string_view materialName,
                                       const std::size_t materialIndex, const std::string_view fallbackPrefix,
                                       std::vector<std::string>& usedStems) {
    std::string sanitised;
    sanitised.reserve(materialName.size());
    for (const char character : materialName) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || character == '_' || character == '-') {
            sanitised.push_back(character);
        } else if (character == ' ' || character == '.' || character == ':' || character == '/' || character == '\\') {
            sanitised.push_back('_');
        }
    }
    if (sanitised.empty()) {
        sanitised = std::string(fallbackPrefix) + "_" + std::to_string(materialIndex);
    }
    // A long authored name must not decide whether the model imports: every mainstream filesystem
    // caps a single component at 255 bytes, and this stem gains a model prefix and an extension on
    // top. Cutting here keeps the published path legal, and the cut is deterministic.
    constexpr std::size_t kMaximumStemCharactersUVE = 64U;
    if (sanitised.size() > kMaximumStemCharactersUVE) {
        sanitised.resize(kMaximumStemCharactersUVE);
    }
    std::string candidate = std::string(modelStem) + "." + sanitised;
    for (std::size_t suffix = 2U;; ++suffix) {
        bool clash = false;
        for (const std::string& used : usedStems) {
            if (used == candidate) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            break;
        }
        candidate = std::string(modelStem) + "." + sanitised + "_" + std::to_string(suffix);
    }
    usedStems.push_back(candidate);
    return candidate;
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
