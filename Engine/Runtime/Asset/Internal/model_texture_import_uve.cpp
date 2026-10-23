// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "model_texture_import_uve.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/texture_import_settings_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset::Detail {
namespace {

constexpr std::string_view kPngSignatureUVE{"\x89PNG\r\n\x1a\n", 8U};

[[nodiscard]] bool StartsWithUVE(const std::vector<std::byte>& bytes, const std::string_view prefix) noexcept {
    if (bytes.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < prefix.size(); ++index) {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(prefix[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool LooksLikePngUVE(const std::vector<std::byte>& bytes) noexcept {
    return StartsWithUVE(bytes, kPngSignatureUVE);
}

/// The three-byte JPEG SOI+marker prefix: every JPEG the codec boundary accepts starts with it.
[[nodiscard]] bool LooksLikeJpegUVE(const std::vector<std::byte>& bytes) noexcept {
    return bytes.size() >= 3U && std::to_integer<unsigned char>(bytes[0]) == 0xFFU &&
           std::to_integer<unsigned char>(bytes[1]) == 0xD8U && std::to_integer<unsigned char>(bytes[2]) == 0xFFU;
}

/// True when `relative` resolves to a path inside the directory it is resolved against. Model files
/// are untrusted input, so a texture reference that climbs out of the source directory is refused
/// before any file is opened.
[[nodiscard]] bool IsTraversalSafeUVE(const std::filesystem::path& relative) {
    if (relative.empty() || relative.has_root_path() || relative.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string ToLowerUVE(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

/// Decodes one blob into the asset's level-0 pixels. The format is decided by the bytes, never by a
/// file extension or an author-supplied MIME string: those are hints at best, and a wrong hint must
/// not turn a decode failure into a texture of the wrong shape.
[[nodiscard]] bool DecodeTextureSourceUVE(const std::vector<std::byte>& imageBytes, const char* importerName,
                                          TextureAssetUVE& outTexture) {
    if (LooksLikePngUVE(imageBytes)) {
        PngRgba8ImageUVE decoded;
        if (!DecodePngRgba8ImageUVE(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte PNG image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    if (LooksLikeJpegUVE(imageBytes)) {
        JpegRgba8ImageUVE decoded;
        if (!DecodeJpegRgba8ImageUVE(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte JPEG image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    UVE_WARNING("{}: a {} byte image is neither PNG nor JPEG, the two raster forms a glTF or MTL model "
                "may embed; the texture is skipped",
                importerName, imageBytes.size());
    return false;
}

} // namespace

std::string MakeTextureFileStemUVE(const std::string_view authoredName, const std::string_view fallbackPrefix,
                                   const std::size_t index, std::vector<std::string>& usedStems) {
    std::string sanitised;
    sanitised.reserve(authoredName.size());
    for (const char character : authoredName) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || character == '_' || character == '-') {
            sanitised.push_back(character);
        } else if (character == ' ' || character == '.' || character == ':' || character == '/' || character == '\\') {
            sanitised.push_back('_');
        }
    }
    if (sanitised.empty()) {
        sanitised = std::string(fallbackPrefix) + "_" + std::to_string(index);
    }
    std::string candidate = sanitised;
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
        candidate = sanitised + "_" + std::to_string(suffix);
    }
    usedStems.push_back(candidate);
    return candidate;
}

AssetGuidUVE ImportModelTextureBytesUVE(const std::vector<std::byte>& imageBytes, const std::string_view textureStem,
                                        const std::filesystem::path& destinationPath,
                                        const TextureAssetColorSpaceUVE colorSpace, const TextureUsageUVE usage,
                                        const ModelTextureImportOptionsUVE& options,
                                        IAssetDatabaseUVE& assetDatabase) {
    if (imageBytes.empty() || imageBytes.size() > options.maximumImageBytes) {
        UVE_WARNING("{}: a model texture is {} bytes, outside the bounded [1, {}] byte range; the texture is "
                    "skipped",
                    options.importerName, imageBytes.size(), options.maximumImageBytes);
        return kInvalidAssetGuidUVE;
    }
    try {
        TextureAssetUVE texture;
        if (!DecodeTextureSourceUVE(imageBytes, options.importerName, texture)) {
            return kInvalidAssetGuidUVE;
        }
        texture.format = TextureAssetFormatUVE::RGBA8Unorm;
        texture.colorSpace = colorSpace;
        texture.usage = usage;

        TextureImportSettingsUVE settings;
        settings.colorSpace = colorSpace;
        settings.usage = usage;
        if (!PrepareTextureForImportUVE(texture, settings)) {
            UVE_WARNING("{}: a model texture failed mip generation, compression or validation; the texture is "
                        "skipped",
                        options.importerName);
            return kInvalidAssetGuidUVE;
        }

        const std::filesystem::path texturePath =
            destinationPath.parent_path() / (std::string(textureStem) + ".uvtex");
        if (!PublishAssetAtomicallyUVE(
                texturePath, options.importerName, options.temporarySuffix,
                [&texture](const std::filesystem::path& temporaryPath) {
                    return SaveTextureAssetUVE(texture, temporaryPath);
                })) {
            return kInvalidAssetGuidUVE;
        }
        const AssetGuidUVE guid = assetDatabase.RegisterUVE(texturePath);
        if (guid == kInvalidAssetGuidUVE) {
            UVE_ERROR("{}: asset database rejected imported texture \"{}\"", options.importerName,
                      texturePath.string());
            return kInvalidAssetGuidUVE;
        }
        return guid;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing a model texture", options.importerName);
        return kInvalidAssetGuidUVE;
    }
}

AssetGuidUVE ImportModelTextureFileUVE(const std::filesystem::path& sourceDirectory,
                                       const std::filesystem::path& destinationPath,
                                       const std::string_view authoredTextureFile,
                                       const TextureAssetColorSpaceUVE colorSpace, const TextureUsageUVE usage,
                                       const ModelTextureImportOptionsUVE& options,
                                       IAssetDatabaseUVE& assetDatabase) {
    if (authoredTextureFile.empty()) {
        return kInvalidAssetGuidUVE;
    }
    // An already imported sibling keeps its GUID: the model's material must keep pointing at the
    // texture the project already knows, not at a second copy beside it.
    const AssetGuidUVE existing =
        ResolveImportedSiblingTextureGuidUVE(destinationPath, authoredTextureFile, assetDatabase);
    if (existing != kInvalidAssetGuidUVE) {
        return existing;
    }

    std::string normalised(authoredTextureFile);
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    const std::filesystem::path relative = std::filesystem::path(normalised).lexically_normal();
    if (!IsTraversalSafeUVE(relative)) {
        UVE_WARNING("{}: model references texture \"{}\" outside its own directory; the texture is skipped",
                    options.importerName, authoredTextureFile);
        return kInvalidAssetGuidUVE;
    }
    const std::string extension = ToLowerUVE(relative.extension().string());
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
        UVE_WARNING("{}: model references texture \"{}\" with a format the model importers cannot decode "
                    "(only PNG and JPEG); the texture is skipped",
                    options.importerName, authoredTextureFile);
        return kInvalidAssetGuidUVE;
    }

    const std::filesystem::path sourcePath = sourceDirectory / relative;
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(sourcePath, errorCode)) {
        UVE_WARNING("{}: model references texture \"{}\" but \"{}\" is not a file next to the source; the "
                    "texture is skipped",
                    options.importerName, authoredTextureFile, sourcePath.string());
        return kInvalidAssetGuidUVE;
    }

    std::vector<std::byte> imageBytes;
    if (!ReadBoundedSourceBytesUVE(sourcePath, options.importerName, options.maximumImageBytes, imageBytes)) {
        return kInvalidAssetGuidUVE;
    }
    const std::string stem = relative.stem().string();
    return ImportModelTextureBytesUVE(imageBytes, stem.empty() ? "texture" : stem, destinationPath, colorSpace,
                                      usage, options, assetDatabase);
}

} // namespace UVE::Asset::Detail
