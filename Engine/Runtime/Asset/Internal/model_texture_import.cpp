// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "model_texture_import.h"

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

#include "import_helpers.h"

#include "bmp_metadata.h"
#include "jpeg_metadata.h"
#include "png_metadata.h"
#include "tga_metadata.h"
#include "texture_import_settings.h"
#include "logging_macros.h"

namespace UVE::Asset::Detail {
namespace {

constexpr std::string_view kPngSignature{"\x89PNG\r\n\x1a\n", 8U};
constexpr std::string_view kBmpSignature{"BM", 2U};

[[nodiscard]] bool StartsWith(const std::vector<std::byte>& bytes, const std::string_view prefix) noexcept {
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

[[nodiscard]] bool LooksLikePng(const std::vector<std::byte>& bytes) noexcept {
    return StartsWith(bytes, kPngSignature);
}

/// The three-byte JPEG SOI+marker prefix: every JPEG the codec boundary accepts starts with it.
[[nodiscard]] bool LooksLikeJpeg(const std::vector<std::byte>& bytes) noexcept {
    return bytes.size() >= 3U && std::to_integer<unsigned char>(bytes[0]) == 0xFFU &&
           std::to_integer<unsigned char>(bytes[1]) == 0xD8U && std::to_integer<unsigned char>(bytes[2]) == 0xFFU;
}

[[nodiscard]] bool LooksLikeBmp(const std::vector<std::byte>& bytes) noexcept {
    return bytes.size() >= 54U && StartsWith(bytes, kBmpSignature);
}

/// True when `relative` resolves to a path inside the directory it is resolved against. Model files
/// are untrusted input, so a texture reference that climbs out of the source directory is refused
/// before any file is opened.
[[nodiscard]] bool IsTraversalSafe(const std::filesystem::path& relative) {
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

[[nodiscard]] std::string ToLower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

/// Decodes one blob into the asset's level-0 pixels. The format is decided by the bytes, never by a
/// file extension or an author-supplied MIME string: those are hints at best, and a wrong hint must
/// not turn a decode failure into a texture of the wrong shape. Because TGA files have no magic
/// prefix at byte 0, `allowTgaFallback` is enabled only when loading a `.tga` file from disk.
[[nodiscard]] bool DecodeTextureSource(const std::vector<std::byte>& imageBytes, const char* importerName,
                                          TextureAsset& outTexture, const bool allowTgaFallback = false) {
    if (LooksLikePng(imageBytes)) {
        PngRgba8Image decoded;
        if (!DecodePngRgba8Image(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte PNG image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    if (LooksLikeJpeg(imageBytes)) {
        JpegRgba8Image decoded;
        if (!DecodeJpegRgba8Image(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte JPEG image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    if (LooksLikeBmp(imageBytes)) {
        BmpRgba8Image decoded;
        if (!DecodeBmpRgba8Image(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte BMP image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    if (allowTgaFallback) {
        TgaRgba8Image decoded;
        if (!DecodeTgaRgba8Image(imageBytes, decoded)) {
            UVE_WARNING("{}: a {} byte TGA image failed bounded RGBA8 decoding; the texture is skipped",
                        importerName, imageBytes.size());
            return false;
        }
        outTexture.width = decoded.width;
        outTexture.height = decoded.height;
        outTexture.pixels = std::move(decoded.pixels);
        return true;
    }
    UVE_WARNING("{}: a {} byte image is not a valid PNG, JPEG, BMP, or TGA model texture; the texture is skipped",
                importerName, imageBytes.size());
    return false;
}

[[nodiscard]] AssetGuid ImportModelTextureBytesInternal(
    const std::vector<std::byte>& imageBytes, const std::string_view textureStem,
    const std::filesystem::path& destinationPath, const TextureAssetColorSpace colorSpace,
    const TextureUsage usage, const ModelTextureImportOptions& options,
    IAssetDatabase& assetDatabase, const bool allowTgaFallback) {
    if (imageBytes.empty() || imageBytes.size() > options.maximumImageBytes) {
        UVE_WARNING("{}: a model texture is {} bytes, outside the bounded [1, {}] byte range; the texture is "
                    "skipped",
                    options.importerName, imageBytes.size(), options.maximumImageBytes);
        return kInvalidAssetGuid;
    }
    try {
        TextureAsset texture;
        if (!DecodeTextureSource(imageBytes, options.importerName, texture, allowTgaFallback)) {
            return kInvalidAssetGuid;
        }
        texture.format = TextureAssetFormat::RGBA8Unorm;
        texture.colorSpace = colorSpace;
        texture.usage = usage;

        TextureImportSettings settings;
        settings.colorSpace = colorSpace;
        settings.usage = usage;
        if (!PrepareTextureForImport(texture, settings)) {
            UVE_WARNING("{}: a model texture failed mip generation, compression or validation; the texture is "
                        "skipped",
                        options.importerName);
            return kInvalidAssetGuid;
        }

        const std::filesystem::path texturePath =
            destinationPath.parent_path() / (std::string(textureStem) + ".uvtex");
        if (!PublishAssetAtomically(
                texturePath, options.importerName, options.temporarySuffix,
                [&texture](const std::filesystem::path& temporaryPath) {
                    return SaveTextureAsset(texture, temporaryPath);
                })) {
            return kInvalidAssetGuid;
        }
        const AssetGuid guid = assetDatabase.Register(texturePath);
        if (guid == kInvalidAssetGuid) {
            UVE_ERROR("{}: asset database rejected imported texture \"{}\"", options.importerName,
                      texturePath.string());
            return kInvalidAssetGuid;
        }
        return guid;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing a model texture", options.importerName);
        return kInvalidAssetGuid;
    }
}

} // namespace

std::string MakeTextureFileStem(const std::string_view authoredName, const std::string_view fallbackPrefix,
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
    // One filesystem component has a 255-byte limit and the caller prefixes the model's own stem,
    // so an image named by a 4 KB `name` in the source cannot be allowed to decide the file path.
    constexpr std::size_t kMaximumStemCharacters = 64U;
    if (sanitised.size() > kMaximumStemCharacters) {
        sanitised.resize(kMaximumStemCharacters);
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

AssetGuid ImportModelTextureBytes(const std::vector<std::byte>& imageBytes, const std::string_view textureStem,
                                        const std::filesystem::path& destinationPath,
                                        const TextureAssetColorSpace colorSpace, const TextureUsage usage,
                                        const ModelTextureImportOptions& options,
                                        IAssetDatabase& assetDatabase) {
    return ImportModelTextureBytesInternal(imageBytes, textureStem, destinationPath, colorSpace, usage, options,
                                              assetDatabase, /*allowTgaFallback=*/false);
}

AssetGuid ImportModelTextureFile(const std::filesystem::path& sourceDirectory,
                                       const std::filesystem::path& destinationPath,
                                       const std::string_view authoredTextureFile,
                                       const TextureAssetColorSpace colorSpace, const TextureUsage usage,
                                       const ModelTextureImportOptions& options,
                                       IAssetDatabase& assetDatabase) {
    if (authoredTextureFile.empty()) {
        return kInvalidAssetGuid;
    }
    // An already imported sibling keeps its GUID: the model's material must keep pointing at the
    // texture the project already knows, not at a second copy beside it.
    const AssetGuid existing =
        ResolveImportedSiblingTextureGuid(destinationPath, authoredTextureFile, assetDatabase);
    if (existing != kInvalidAssetGuid) {
        return existing;
    }

    std::string normalised(authoredTextureFile);
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    const std::filesystem::path relative = std::filesystem::path(normalised).lexically_normal();
    if (!IsTraversalSafe(relative)) {
        UVE_WARNING("{}: model references texture \"{}\" outside its own directory; the texture is skipped",
                    options.importerName, authoredTextureFile);
        return kInvalidAssetGuid;
    }
    const std::string extension = ToLower(relative.extension().string());
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
        extension != ".bmp" && extension != ".tga") {
        UVE_WARNING("{}: model references texture \"{}\" with a format the model importers cannot decode "
                    "(only PNG, JPEG, BMP, and TGA); the texture is skipped",
                    options.importerName, authoredTextureFile);
        return kInvalidAssetGuid;
    }

    const std::filesystem::path sourcePath = sourceDirectory / relative;
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(sourcePath, errorCode)) {
        UVE_WARNING("{}: model references texture \"{}\" but \"{}\" is not a file next to the source; the "
                    "texture is skipped",
                    options.importerName, authoredTextureFile, sourcePath.string());
        return kInvalidAssetGuid;
    }

    std::vector<std::byte> imageBytes;
    if (!ReadBoundedSourceBytes(sourcePath, options.importerName, options.maximumImageBytes, imageBytes)) {
        return kInvalidAssetGuid;
    }
    const std::string stem = relative.stem().string();
    return ImportModelTextureBytesInternal(imageBytes, stem.empty() ? "texture" : stem, destinationPath, colorSpace,
                                              usage, options, assetDatabase, extension == ".tga");
}

} // namespace UVE::Asset::Detail
