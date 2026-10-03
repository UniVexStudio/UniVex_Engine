// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_mipmap_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace UVE::Asset {
namespace {

[[nodiscard]] bool IsTextureMipmapFilterValidUVE(const TextureMipmapFilterUVE filter) noexcept {
    switch (filter) {
        case TextureMipmapFilterUVE::Box:
        case TextureMipmapFilterUVE::Nearest:
            return true;
    }
    return false;
}

[[nodiscard]] double SrgbToLinearUVE(const double value) noexcept {
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double LinearToSrgbUVE(const double value) noexcept {
    return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

[[nodiscard]] std::uint8_t QuantizeUnorm8UVE(const double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
}

[[nodiscard]] TextureMipLevelUVE GenerateNearestMipLevelUVE(const std::uint32_t sourceWidth,
                                                              const std::uint32_t sourceHeight,
                                                              const std::vector<std::byte>& sourcePixels,
                                                              const std::uint32_t targetWidth,
                                                              const std::uint32_t targetHeight) {
    TextureMipLevelUVE target;
    target.width = targetWidth;
    target.height = targetHeight;
    const std::size_t targetByteCount = static_cast<std::size_t>(targetWidth) * targetHeight * 4U;
    target.pixels.resize(targetByteCount);

    for (std::uint32_t y = 0U; y < targetHeight; ++y) {
        const double sourceY = (static_cast<double>(y) + 0.5) * sourceHeight / targetHeight;
        const std::uint32_t nearestY = std::min(static_cast<std::uint32_t>(sourceY), sourceHeight - 1U);
        for (std::uint32_t x = 0U; x < targetWidth; ++x) {
            const double sourceX = (static_cast<double>(x) + 0.5) * sourceWidth / targetWidth;
            const std::uint32_t nearestX = std::min(static_cast<std::uint32_t>(sourceX), sourceWidth - 1U);
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(nearestY) * sourceWidth + nearestX) * 4U;
            const std::size_t targetOffset = (static_cast<std::size_t>(y) * targetWidth + x) * 4U;
            std::copy_n(sourcePixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset), 4U,
                        target.pixels.begin() + static_cast<std::ptrdiff_t>(targetOffset));
        }
    }
    return target;
}

[[nodiscard]] TextureMipLevelUVE GenerateBoxMipLevelUVE(const std::uint32_t sourceWidth,
                                                         const std::uint32_t sourceHeight,
                                                         const std::vector<std::byte>& sourcePixels,
                                                         const std::uint32_t targetWidth,
                                                         const std::uint32_t targetHeight,
                                                         const bool sourceIsSrgb) {
    TextureMipLevelUVE target;
    target.width = targetWidth;
    target.height = targetHeight;
    const std::size_t targetByteCount = static_cast<std::size_t>(targetWidth) * targetHeight * 4U;
    target.pixels.resize(targetByteCount);

    const double sourcePerTargetX = static_cast<double>(sourceWidth) / targetWidth;
    const double sourcePerTargetY = static_cast<double>(sourceHeight) / targetHeight;
    for (std::uint32_t targetY = 0U; targetY < targetHeight; ++targetY) {
        const double sourceTop = static_cast<double>(targetY) * sourcePerTargetY;
        const double sourceBottom = static_cast<double>(targetY + 1U) * sourcePerTargetY;
        const std::uint32_t firstSourceY = static_cast<std::uint32_t>(std::floor(sourceTop));
        const std::uint32_t endSourceY = std::min(
            static_cast<std::uint32_t>(std::ceil(sourceBottom)), sourceHeight);
        for (std::uint32_t targetX = 0U; targetX < targetWidth; ++targetX) {
            const double sourceLeft = static_cast<double>(targetX) * sourcePerTargetX;
            const double sourceRight = static_cast<double>(targetX + 1U) * sourcePerTargetX;
            const std::uint32_t firstSourceX = static_cast<std::uint32_t>(std::floor(sourceLeft));
            const std::uint32_t endSourceX = std::min(
                static_cast<std::uint32_t>(std::ceil(sourceRight)), sourceWidth);
            double weightedSums[4]{};
            double totalWeight = 0.0;

            for (std::uint32_t sourceY = firstSourceY; sourceY < endSourceY; ++sourceY) {
                const double weightY = std::max(
                    0.0, std::min(sourceBottom, static_cast<double>(sourceY + 1U)) -
                             std::max(sourceTop, static_cast<double>(sourceY)));
                for (std::uint32_t sourceX = firstSourceX; sourceX < endSourceX; ++sourceX) {
                    const double weightX = std::max(
                        0.0, std::min(sourceRight, static_cast<double>(sourceX + 1U)) -
                                 std::max(sourceLeft, static_cast<double>(sourceX)));
                    const double weight = weightX * weightY;
                    if (weight == 0.0) {
                        continue;
                    }
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4U;
                    for (std::size_t channel = 0U; channel < 4U; ++channel) {
                        double value = std::to_integer<std::uint8_t>(sourcePixels[sourceOffset + channel]) /
                                       255.0;
                        if (sourceIsSrgb && channel < 3U) {
                            value = SrgbToLinearUVE(value);
                        }
                        weightedSums[channel] += value * weight;
                    }
                    totalWeight += weight;
                }
            }

            const std::size_t targetOffset = (static_cast<std::size_t>(targetY) * targetWidth + targetX) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                double value = weightedSums[channel] / totalWeight;
                if (sourceIsSrgb && channel < 3U) {
                    value = LinearToSrgbUVE(value);
                }
                target.pixels[targetOffset + channel] =
                    static_cast<std::byte>(QuantizeUnorm8UVE(value));
            }
        }
    }
    return target;
}

} // namespace

bool GenerateTextureMipmapsUVE(TextureAssetUVE& texture, const TextureMipmapFilterUVE filter,
                               const std::uint32_t maxMipLevels) {
    if (!IsTextureAssetMetadataValidUVE(texture) || texture.format != TextureAssetFormatUVE::RGBA8Unorm ||
        texture.width == 0U || texture.height == 0U || !IsTextureMipmapFilterValidUVE(filter)) {
        return false;
    }
    const std::uint64_t basePixelCount = static_cast<std::uint64_t>(texture.width) * texture.height;
    if (basePixelCount > std::numeric_limits<std::uint64_t>::max() / 4U ||
        basePixelCount * 4U != texture.pixels.size()) {
        return false;
    }

    std::uint32_t fullMipLevelCount = 1U;
    std::uint32_t nextWidth = texture.width;
    std::uint32_t nextHeight = texture.height;
    while (nextWidth > 1U || nextHeight > 1U) {
        nextWidth = std::max(1U, nextWidth / 2U);
        nextHeight = std::max(1U, nextHeight / 2U);
        ++fullMipLevelCount;
    }
    const std::uint32_t targetMipLevelCount = maxMipLevels == 0U
                                                  ? fullMipLevelCount
                                                  : std::min(maxMipLevels, fullMipLevelCount);

    std::vector<TextureMipLevelUVE> generatedMipLevels;
    generatedMipLevels.reserve(static_cast<std::size_t>(targetMipLevelCount - 1U));
    const bool sourceIsSrgb = texture.colorSpace == TextureAssetColorSpaceUVE::Srgb;
    std::uint32_t sourceWidth = texture.width;
    std::uint32_t sourceHeight = texture.height;
    const std::vector<std::byte>* sourcePixels = &texture.pixels;

    while (generatedMipLevels.size() + 1U < targetMipLevelCount) {
        const std::uint32_t targetWidth = std::max(1U, sourceWidth / 2U);
        const std::uint32_t targetHeight = std::max(1U, sourceHeight / 2U);
        TextureMipLevelUVE target = filter == TextureMipmapFilterUVE::Nearest
            ? GenerateNearestMipLevelUVE(sourceWidth, sourceHeight, *sourcePixels, targetWidth, targetHeight)
            : GenerateBoxMipLevelUVE(sourceWidth, sourceHeight, *sourcePixels, targetWidth, targetHeight,
                                     sourceIsSrgb);
        generatedMipLevels.push_back(std::move(target));
        const TextureMipLevelUVE& generated = generatedMipLevels.back();
        sourceWidth = generated.width;
        sourceHeight = generated.height;
        sourcePixels = &generated.pixels;
    }

    texture.mipLevels = std::move(generatedMipLevels);
    return true;
}

} // namespace UVE::Asset
