// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_icon_set_uve.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/asset/png_metadata_uve.h"

namespace UVE::Editor {

namespace {

#include "uve_editor_icon_set.inc"

[[nodiscard]] constexpr char ToLowerAsciiUVE(const char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

[[nodiscard]] bool EqualsIgnoringCaseUVE(const std::string_view left, const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ToLowerAsciiUVE(left[index]) != ToLowerAsciiUVE(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

std::span<const EditorIconSourceUVE> GetEditorIconSourcesUVE() noexcept {
    return kEditorIconSourcesUVE;
}

const EditorIconSourceUVE* FindEditorIconSourceUVE(const EditorIconGroupUVE group,
                                                   const std::string_view name) noexcept {
    for (const EditorIconSourceUVE& source : kEditorIconSourcesUVE) {
        if (source.group == group && EqualsIgnoringCaseUVE(source.name, name)) {
            return &source;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> DownsampleEditorIconUVE(const std::span<const std::uint8_t> rgba, const int size) {
    const auto source = static_cast<std::size_t>(size);
    const std::size_t half = source / 2U;
    if (size < 2 || rgba.size() != source * source * 4U) {
        return {};
    }
    std::vector<std::uint8_t> result(half * half * 4U);
    for (std::size_t y = 0U; y < half; ++y) {
        for (std::size_t x = 0U; x < half; ++x) {
            std::array<std::uint32_t, 3> colour{};
            std::uint32_t alpha = 0U;
            for (std::size_t dy = 0U; dy < 2U; ++dy) {
                for (std::size_t dx = 0U; dx < 2U; ++dx) {
                    const std::size_t at = (((y * 2U) + dy) * source + ((x * 2U) + dx)) * 4U;
                    const std::uint32_t a = rgba[at + 3U];
                    for (std::size_t channel = 0U; channel < 3U; ++channel) {
                        colour[channel] += rgba[at + channel] * a;
                    }
                    alpha += a;
                }
            }
            const std::size_t out = (y * half + x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                result[out + channel] =
                    alpha == 0U ? std::uint8_t{0U} : static_cast<std::uint8_t>((colour[channel] + (alpha / 2U)) / alpha);
            }
            result[out + 3U] = static_cast<std::uint8_t>((alpha + 2U) / 4U);
        }
    }
    return result;
}

std::vector<EditorIconLevelUVE> DecodeEditorIconUVE(const std::span<const std::uint8_t> png) {
    const auto* const first = reinterpret_cast<const std::byte*>(png.data());
    Asset::PngRgba8ImageUVE image;
    if (!Asset::DecodePngRgba8ImageUVE(std::vector<std::byte>(first, first + png.size()), image)) {
        return {};
    }
    const std::uint32_t size = image.width;
    if (size == 0U || size != image.height || (size & (size - 1U)) != 0U || size > 1024U) {
        return {};
    }
    std::vector<EditorIconLevelUVE> levels;
    const auto* const pixels = reinterpret_cast<const std::uint8_t*>(image.pixels.data());
    levels.push_back({static_cast<int>(size), std::vector<std::uint8_t>(pixels, pixels + image.pixels.size())});
    while (levels.back().size > 1) {
        const EditorIconLevelUVE& previous = levels.back();
        std::vector<std::uint8_t> next = DownsampleEditorIconUVE(previous.rgba, previous.size);
        const int nextSize = previous.size / 2;
        levels.push_back({nextSize, std::move(next)});
    }
    return levels;
}

} // namespace UVE::Editor
