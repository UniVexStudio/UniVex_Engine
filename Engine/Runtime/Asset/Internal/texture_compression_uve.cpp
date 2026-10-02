// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/texture_compression_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

#include "basisu_transcoder.h"
#include "texture_compression_basis_init_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] std::uint32_t MaximumMipLevelCountUVE(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return 0U;
    }
    std::uint32_t levelCount = 1U;
    while (width > 1U || height > 1U) {
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
        ++levelCount;
    }
    return levelCount;
}

[[nodiscard]] bool InitializeKtx2TranscoderUVE(const TextureAssetUVE& texture,
                                                basist::ktx2_transcoder& transcoder,
                                                TextureCompressionInfoUVE& outInfo) noexcept {
    if (texture.payloadEncoding != TexturePayloadEncodingUVE::BasisUniversalKtx2 ||
        texture.format != TextureFormatUVE::RGBA8Unorm || !texture.pixels.empty() ||
        !texture.mipLevels.empty() || !IsTextureAssetMetadataValidUVE(texture) ||
        !IsTextureAssetValidUVE(texture) || !Detail::EnsureBasisUniversalInitializedUVE()) {
        return false;
    }

    const std::vector<std::byte>& payload = texture.basisKtx2Data;
    if (payload.size() > std::numeric_limits<std::uint32_t>::max() ||
        !transcoder.init(payload.data(), static_cast<std::uint32_t>(payload.size()))) {
        return false;
    }
    const basist::ktx2_header& header = transcoder.get_header();
    const bool isKnownLdrBasisFormat = transcoder.is_etc1s() || transcoder.is_uastc();
    if (!isKnownLdrBasisFormat || transcoder.is_hdr() || transcoder.get_width() != texture.width ||
        transcoder.get_height() != texture.height || transcoder.get_levels() == 0U ||
        transcoder.get_faces() != 1U || transcoder.get_layers() != 0U ||
        header.m_pixel_depth.get_uint32() != 0U ||
        transcoder.is_srgb() != (texture.colorSpace == TextureColorSpaceUVE::Srgb)) {
        return false;
    }
    const std::uint32_t maximumMipLevels = MaximumMipLevelCountUVE(texture.width, texture.height);
    if (transcoder.get_levels() > maximumMipLevels) {
        return false;
    }

    TextureCompressionInfoUVE candidate;
    candidate.width = transcoder.get_width();
    candidate.height = transcoder.get_height();
    candidate.mipLevels = transcoder.get_levels();
    candidate.hasAlpha = transcoder.get_has_alpha() != 0U;
    outInfo = candidate;
    return true;
}

[[nodiscard]] bool MapBasisTranscodeFormatUVE(const TextureTranscodeTargetUVE target,
                                               basist::transcoder_texture_format& outFormat) noexcept {
    switch (target) {
        case TextureTranscodeTargetUVE::Rgba8Unorm:
            outFormat = basist::transcoder_texture_format::cTFRGBA32;
            return true;
        case TextureTranscodeTargetUVE::Bc1Rgb:
            outFormat = basist::transcoder_texture_format::cTFBC1_RGB;
            return true;
        case TextureTranscodeTargetUVE::Bc3Rgba:
            outFormat = basist::transcoder_texture_format::cTFBC3_RGBA;
            return true;
        case TextureTranscodeTargetUVE::Bc7Rgba:
            outFormat = basist::transcoder_texture_format::cTFBC7_RGBA;
            return true;
        case TextureTranscodeTargetUVE::Etc2Rgb:
            // ETC1's 4x4 RGB block layout is a valid ETC2_RGB8 representation.
            outFormat = basist::transcoder_texture_format::cTFETC1_RGB;
            return true;
        case TextureTranscodeTargetUVE::Etc2Rgba:
            outFormat = basist::transcoder_texture_format::cTFETC2_RGBA;
            return true;
        case TextureTranscodeTargetUVE::Astc4x4Rgba:
            outFormat = basist::transcoder_texture_format::cTFASTC_LDR_4x4_RGBA;
            return true;
    }
    return false;
}

[[nodiscard]] bool IsAlphaCapableTargetUVE(const TextureTranscodeTargetUVE target) noexcept {
    return target != TextureTranscodeTargetUVE::Bc1Rgb && target != TextureTranscodeTargetUVE::Etc2Rgb;
}

[[nodiscard]] bool CalculateTargetMipByteCountUVE(const TextureTranscodeTargetUVE target,
                                                   const std::uint32_t width,
                                                   const std::uint32_t height,
                                                   std::uint64_t& outBytes,
                                                   std::uint32_t& outBlocksOrPixels) noexcept {
    if (width == 0U || height == 0U) {
        return false;
    }
    if (target == TextureTranscodeTargetUVE::Rgba8Unorm) {
        const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
        if (pixelCount > std::numeric_limits<std::uint32_t>::max() ||
            pixelCount > std::numeric_limits<std::uint64_t>::max() / 4U) {
            return false;
        }
        outBlocksOrPixels = static_cast<std::uint32_t>(pixelCount);
        outBytes = pixelCount * 4U;
        return true;
    }

    std::uint32_t bytesPerBlock = 0U;
    switch (target) {
        case TextureTranscodeTargetUVE::Bc1Rgb:
        case TextureTranscodeTargetUVE::Etc2Rgb:
            bytesPerBlock = 8U;
            break;
        case TextureTranscodeTargetUVE::Bc3Rgba:
        case TextureTranscodeTargetUVE::Bc7Rgba:
        case TextureTranscodeTargetUVE::Etc2Rgba:
        case TextureTranscodeTargetUVE::Astc4x4Rgba:
            bytesPerBlock = 16U;
            break;
        case TextureTranscodeTargetUVE::Rgba8Unorm:
            return false;
    }
    const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + 3U) / 4U;
    const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + 3U) / 4U;
    const std::uint64_t blocks = blocksX * blocksY;
    if (blocks > std::numeric_limits<std::uint32_t>::max() ||
        blocks > std::numeric_limits<std::uint64_t>::max() / bytesPerBlock) {
        return false;
    }
    outBlocksOrPixels = static_cast<std::uint32_t>(blocks);
    outBytes = blocks * bytesPerBlock;
    return outBytes <= std::numeric_limits<std::size_t>::max();
}

} // namespace

bool GetTextureCompressionInfoUVE(const TextureAssetUVE& texture, TextureCompressionInfoUVE& outInfo) {
    basist::ktx2_transcoder transcoder;
    TextureCompressionInfoUVE candidate;
    if (!InitializeKtx2TranscoderUVE(texture, transcoder, candidate)) {
        return false;
    }
    outInfo = candidate;
    return true;
}

bool TranscodeTextureAssetUVE(const TextureAssetUVE& texture, const TextureTranscodeTargetUVE target,
                              TextureTranscodedMipChainUVE& outMipChain) {
    basist::ktx2_transcoder transcoder;
    TextureCompressionInfoUVE compressionInfo;
    if (!InitializeKtx2TranscoderUVE(texture, transcoder, compressionInfo) ||
        (compressionInfo.hasAlpha && !IsAlphaCapableTargetUVE(target))) {
        return false;
    }

    basist::transcoder_texture_format basisTarget{};
    if (!MapBasisTranscodeFormatUVE(target, basisTarget) ||
        !basist::basis_is_format_supported(basisTarget, transcoder.get_basis_tex_format()) ||
        !transcoder.start_transcoding()) {
        return false;
    }

    try {
        TextureTranscodedMipChainUVE candidate;
        candidate.target = target;
        candidate.width = compressionInfo.width;
        candidate.height = compressionInfo.height;
        candidate.mipLevels = compressionInfo.mipLevels;

        std::vector<std::uint64_t> levelOffsets(compressionInfo.mipLevels, 0U);
        std::vector<std::uint32_t> blocksOrPixels(compressionInfo.mipLevels, 0U);
        std::uint64_t totalBytes = 0U;
        std::uint32_t levelWidth = compressionInfo.width;
        std::uint32_t levelHeight = compressionInfo.height;
        for (std::uint32_t level = 0U; level < compressionInfo.mipLevels; ++level) {
            basist::ktx2_image_level_info levelInfo{};
            if (!transcoder.get_image_level_info(levelInfo, level, 0U, 0U) ||
                levelInfo.m_orig_width != levelWidth || levelInfo.m_orig_height != levelHeight) {
                return false;
            }
            std::uint64_t levelBytes = 0U;
            std::uint32_t levelBlocksOrPixels = 0U;
            if (!CalculateTargetMipByteCountUVE(target, levelWidth, levelHeight, levelBytes,
                                                levelBlocksOrPixels) ||
                levelBytes > std::numeric_limits<std::uint64_t>::max() - totalBytes) {
                return false;
            }
            levelOffsets[level] = totalBytes;
            blocksOrPixels[level] = levelBlocksOrPixels;
            totalBytes += levelBytes;
            levelWidth = std::max(1U, levelWidth / 2U);
            levelHeight = std::max(1U, levelHeight / 2U);
        }
        if (totalBytes > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        candidate.pixels.resize(static_cast<std::size_t>(totalBytes));

        for (std::uint32_t level = 0U; level < compressionInfo.mipLevels; ++level) {
            auto* const destination = candidate.pixels.data() + static_cast<std::size_t>(levelOffsets[level]);
            if (!transcoder.transcode_image_level(level, 0U, 0U, destination, blocksOrPixels[level], basisTarget)) {
                return false;
            }
        }
        outMipChain = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
