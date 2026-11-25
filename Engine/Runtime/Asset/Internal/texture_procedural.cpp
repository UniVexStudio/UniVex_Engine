// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "texture_procedural.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "texture_mipmap.h"

namespace UVE::Asset {
namespace {

constexpr std::uint32_t kMaximumSize = 4096U;

/// A well-mixed 32-bit hash of a lattice point and the seed.
[[nodiscard]] std::uint32_t Hash(std::uint32_t x, std::uint32_t y, const std::uint32_t seed) noexcept {
    std::uint32_t h = seed * 0x9E3779B1U ^ x * 0x85EBCA77U ^ y * 0xC2B2AE3DU;
    h ^= h >> 15U;
    h *= 0x2C1B3C6DU;
    h ^= h >> 12U;
    h *= 0x297A2D39U;
    h ^= h >> 15U;
    return h;
}

[[nodiscard]] float Unit(const std::uint32_t h) noexcept { return static_cast<float>(h & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU); }

[[nodiscard]] float Fade(const float t) noexcept { return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F); }

[[nodiscard]] std::uint32_t Wrap(const std::int64_t value, const std::uint32_t period) noexcept {
    const std::int64_t p = static_cast<std::int64_t>(period);
    return static_cast<std::uint32_t>(((value % p) + p) % p);
}

/// One octave at (x, y) in lattice units, repeating every `period` cells, in about [0, 1].
[[nodiscard]] float Octave(const NoiseTextureType type, const float x, const float y, const std::uint32_t period,
                              const std::uint32_t seed) noexcept {
    const auto fx = static_cast<std::int64_t>(std::floor(x));
    const auto fy = static_cast<std::int64_t>(std::floor(y));
    const float tx = x - static_cast<float>(fx);
    const float ty = y - static_cast<float>(fy);
    if (type == NoiseTextureType::Cellular) {
        // The distance to the nearest point scattered one per cell, over the 3x3 cells around.
        float nearest = 2.0F;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint32_t cx = Wrap(fx + dx, period);
                const std::uint32_t cy = Wrap(fy + dy, period);
                const std::uint32_t h = Hash(cx, cy, seed);
                const float px = static_cast<float>(dx) + Unit(h);
                const float py = static_cast<float>(dy) + Unit(Hash(h, 0x51ED27U, seed));
                nearest = std::min(nearest, std::hypot(px - tx, py - ty));
            }
        }
        return std::clamp(nearest, 0.0F, 1.0F);
    }
    const std::uint32_t x0 = Wrap(fx, period);
    const std::uint32_t y0 = Wrap(fy, period);
    const std::uint32_t x1 = Wrap(fx + 1, period);
    const std::uint32_t y1 = Wrap(fy + 1, period);
    const float u = Fade(tx);
    const float v = Fade(ty);
    const auto lerp = [](const float a, const float b, const float t) { return a + (b - a) * t; };
    if (type == NoiseTextureType::Value) {
        const float a = Unit(Hash(x0, y0, seed));
        const float b = Unit(Hash(x1, y0, seed));
        const float c = Unit(Hash(x0, y1, seed));
        const float d = Unit(Hash(x1, y1, seed));
        return lerp(lerp(a, b, u), lerp(c, d, u), v);
    }
    // Perlin: a gradient at each corner, dotted with the offset to it.
    const auto corner = [seed](const std::uint32_t cx, const std::uint32_t cy, const float ox, const float oy) {
        const float angle = Unit(Hash(cx, cy, seed)) * 6.2831853F;
        return std::cos(angle) * ox + std::sin(angle) * oy;
    };
    const float a = corner(x0, y0, tx, ty);
    const float b = corner(x1, y0, tx - 1.0F, ty);
    const float c = corner(x0, y1, tx, ty - 1.0F);
    const float d = corner(x1, y1, tx - 1.0F, ty - 1.0F);
    return std::clamp(lerp(lerp(a, b, u), lerp(c, d, u), v) * 0.7071F + 0.5F, 0.0F, 1.0F);
}

[[nodiscard]] std::byte Byte(const float value) noexcept {
    return static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)));
}

[[nodiscard]] bool Finish(TextureAsset& texture) {
    return IsTextureAssetValid(texture) && GenerateTextureMipmaps(texture, TextureMipmapFilter::Box, 0U);
}

} // namespace

bool GenerateNoiseTexture(const NoiseTextureSettings& settings, TextureAsset& outTexture) {
    if (settings.width == 0U || settings.height == 0U || settings.width > kMaximumSize || settings.height > kMaximumSize ||
        settings.octaves == 0U || settings.frequency == 0U || !std::isfinite(settings.gain)) {
        return false;
    }
    const std::uint32_t octaves = std::min(settings.octaves, 8U);
    const std::uint32_t lacunarity = std::max(settings.lacunarity, 1U);
    const std::uint32_t w = settings.width;
    const std::uint32_t h = settings.height;
    std::vector<float> height(static_cast<std::size_t>(w) * h);
    for (std::uint32_t y = 0U; y < h; ++y) {
        for (std::uint32_t x = 0U; x < w; ++x) {
            float sum = 0.0F;
            float weight = 0.0F;
            float amplitude = 1.0F;
            std::uint32_t period = settings.frequency;
            for (std::uint32_t octave = 0U; octave < octaves; ++octave) {
                const float px = (static_cast<float>(x) + 0.5F) / static_cast<float>(w) * static_cast<float>(period);
                const float py = (static_cast<float>(y) + 0.5F) / static_cast<float>(h) * static_cast<float>(period);
                sum += Octave(settings.type, px, py, period, settings.seed + octave * 1013U) * amplitude;
                weight += amplitude;
                amplitude *= settings.gain;
                period *= lacunarity;
            }
            height[static_cast<std::size_t>(y) * w + x] = weight > 0.0F ? sum / weight : 0.0F;
        }
    }
    TextureAsset texture;
    texture.width = w;
    texture.height = h;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.pixels.resize(static_cast<std::size_t>(w) * h * 4U);
    if (settings.asNormalMap) {
        // The noise as height: the normal is its slope, wrapping at the edges as the noise does.
        texture.colorSpace = TextureAssetColorSpace::Linear;
        texture.usage = TextureUsage::Normal;
        const auto at = [&](const std::int64_t x, const std::int64_t y) {
            return height[static_cast<std::size_t>(Wrap(y, h)) * w + Wrap(x, w)];
        };
        for (std::uint32_t y = 0U; y < h; ++y) {
            for (std::uint32_t x = 0U; x < w; ++x) {
                const float dx = (at(x + 1, y) - at(static_cast<std::int64_t>(x) - 1, y)) * settings.normalStrength;
                const float dy = (at(x, y + 1) - at(x, static_cast<std::int64_t>(y) - 1)) * settings.normalStrength;
                const float length = std::sqrt(dx * dx + dy * dy + 1.0F);
                const std::size_t p = (static_cast<std::size_t>(y) * w + x) * 4U;
                texture.pixels[p] = Byte(-dx / length * 0.5F + 0.5F);
                texture.pixels[p + 1U] = Byte(dy / length * 0.5F + 0.5F);
                texture.pixels[p + 2U] = Byte(1.0F / length * 0.5F + 0.5F);
                texture.pixels[p + 3U] = std::byte{255};
            }
        }
    } else {
        texture.colorSpace = TextureAssetColorSpace::Srgb;
        texture.usage = TextureUsage::Color;
        for (std::size_t i = 0U; i < height.size(); ++i) {
            const float t = height[i];
            texture.pixels[i * 4U] = Byte(settings.colorA.x + (settings.colorB.x - settings.colorA.x) * t);
            texture.pixels[i * 4U + 1U] = Byte(settings.colorA.y + (settings.colorB.y - settings.colorA.y) * t);
            texture.pixels[i * 4U + 2U] = Byte(settings.colorA.z + (settings.colorB.z - settings.colorA.z) * t);
            texture.pixels[i * 4U + 3U] = std::byte{255};
        }
    }
    if (!Finish(texture)) {
        return false;
    }
    outTexture = std::move(texture);
    return true;
}

bool GenerateGradientTexture(const GradientTextureSettings& settings, TextureAsset& outTexture) {
    if (settings.width == 0U || settings.height == 0U || settings.width > kMaximumSize || settings.height > kMaximumSize) {
        return false;
    }
    TextureAsset texture;
    texture.width = settings.width;
    texture.height = settings.height;
    texture.format = TextureAssetFormat::RGBA8Unorm;
    texture.colorSpace = TextureAssetColorSpace::Srgb;
    texture.usage = TextureUsage::Color;
    texture.pixels.resize(static_cast<std::size_t>(settings.width) * settings.height * 4U);
    for (std::uint32_t y = 0U; y < settings.height; ++y) {
        for (std::uint32_t x = 0U; x < settings.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(settings.width);
            const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(settings.height);
            const float t = settings.radial ? std::clamp(std::hypot(u - 0.5F, v - 0.5F) * 2.0F, 0.0F, 1.0F) : u;
            const std::size_t p = (static_cast<std::size_t>(y) * settings.width + x) * 4U;
            texture.pixels[p] = Byte(settings.colorA.x + (settings.colorB.x - settings.colorA.x) * t);
            texture.pixels[p + 1U] = Byte(settings.colorA.y + (settings.colorB.y - settings.colorA.y) * t);
            texture.pixels[p + 2U] = Byte(settings.colorA.z + (settings.colorB.z - settings.colorA.z) * t);
            texture.pixels[p + 3U] = std::byte{255};
        }
    }
    if (!Finish(texture)) {
        return false;
    }
    outTexture = std::move(texture);
    return true;
}

} // namespace UVE::Asset
