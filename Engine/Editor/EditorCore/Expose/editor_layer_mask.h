// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Editor {

/// The project's names for one set of 32 layers, indexed by bit; empty where a layer is unnamed.
using LayerNamesUVE = std::array<std::string, 32>;

/// How layer `index` (bit `index`) is shown: its project name, or "Layer N" counting from one.
[[nodiscard]] std::string GetLayerLabelUVE(const LayerNamesUVE& names, std::size_t index);

/// A mask in a few words: "None", "All", or the labels of its layers - the first `maxShown` and
/// "+N" for the rest - so a row says "Default, Player +2" rather than 0000002B.
[[nodiscard]] std::string FormatLayerMaskSummaryUVE(std::uint32_t mask, const LayerNamesUVE& names,
                                                    std::size_t maxShown = 3U);

/// True when `mask` has at least one bit set whose entry in `names` is empty. Referring to an
/// unnamed layer remains legal at runtime, and the editor uses this flag to warn in the UI.
[[nodiscard]] bool HasUnnamedLayerInMaskUVE(std::uint32_t mask, const LayerNamesUVE& names) noexcept;

/// Symmetric 32x32 layer interaction matrix for 3D/2D physics collision pairs.
struct LayerCollisionMatrixUVE final {
    std::array<std::uint32_t, 32> rowMasks = [] {
        std::array<std::uint32_t, 32> masks{};
        masks.fill(0xFFFFFFFFU);
        return masks;
    }();

    [[nodiscard]] bool CanCollideUVE(std::size_t layerA, std::size_t layerB) const noexcept;
    void SetCollisionUVE(std::size_t layerA, std::size_t layerB, bool enabled) noexcept;
};

} // namespace UVE::Editor
