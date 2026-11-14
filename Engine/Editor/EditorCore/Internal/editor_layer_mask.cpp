// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_layer_mask.h"

#include <algorithm>
#include <bit>
#include <string>

namespace UVE::Editor {
namespace {

constexpr std::size_t kLayerCountUVE = std::tuple_size_v<LayerNamesUVE>;

[[nodiscard]] bool IsLayerSetUVE(const std::uint32_t mask, const std::size_t index) noexcept {
    return ((mask >> index) & 1U) != 0U;
}

} // namespace

std::string GetLayerLabelUVE(const LayerNamesUVE& names, const std::size_t index) {
    if (index < kLayerCountUVE && !names[index].empty()) {
        return names[index];
    }
    return "Layer " + std::to_string(index + 1U);
}

std::string FormatLayerMaskSummaryUVE(const std::uint32_t mask, const LayerNamesUVE& names,
                                      const std::size_t maxShown) {
    if (mask == 0U) {
        return "None";
    }
    if (mask == 0xFFFFFFFFU) {
        return "All";
    }
    std::string summary;
    std::size_t shown = 0U;
    for (std::size_t index = 0U; index < kLayerCountUVE && shown < maxShown; ++index) {
        if (IsLayerSetUVE(mask, index)) {
            summary += (shown == 0U ? "" : ", ") + GetLayerLabelUVE(names, index);
            ++shown;
        }
    }
    const auto total = static_cast<std::size_t>(std::popcount(mask));
    if (total > shown) {
        summary += " +" + std::to_string(total - shown);
    }
    return summary;
}

bool HasUnnamedLayerInMaskUVE(const std::uint32_t mask, const LayerNamesUVE& names) noexcept {
    for (std::size_t index = 0U; index < kLayerCountUVE; ++index) {
        if (IsLayerSetUVE(mask, index) && names[index].empty()) {
            return true;
        }
    }
    return false;
}

bool LayerCollisionMatrixUVE::CanCollideUVE(const std::size_t layerA, const std::size_t layerB) const noexcept {
    if (layerA >= kLayerCountUVE || layerB >= kLayerCountUVE) {
        return false;
    }
    return ((rowMasks[layerA] >> layerB) & 1U) != 0U;
}

void LayerCollisionMatrixUVE::SetCollisionUVE(const std::size_t layerA, const std::size_t layerB,
                                              const bool enabled) noexcept {
    if (layerA >= kLayerCountUVE || layerB >= kLayerCountUVE) {
        return;
    }
    const std::uint32_t bitA = (1U << static_cast<std::uint32_t>(layerA));
    const std::uint32_t bitB = (1U << static_cast<std::uint32_t>(layerB));
    if (enabled) {
        rowMasks[layerA] |= bitB;
        rowMasks[layerB] |= bitA;
    } else {
        rowMasks[layerA] &= ~bitB;
        rowMasks[layerB] &= ~bitA;
    }
}

#if UVE_HAS_IMGUI_HEADER_UVE

#endif // UVE_HAS_IMGUI_HEADER_UVE

} // namespace UVE::Editor
