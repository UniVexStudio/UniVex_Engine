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

} // namespace UVE::Editor
