// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"

namespace UVE::Asset {

/// A Widget Blueprint's file extension.
inline constexpr std::string_view kWidgetBlueprintExtensionUVE = ".uvwbp";

/// Whether `path` names a Widget Blueprint (`.uvwbp`).
[[nodiscard]] bool IsWidgetBlueprintPathUVE(std::string_view path) noexcept;

/// A Widget Blueprint (`.uvwbp`) is a prefab envelope - its widgets as an object tree, a Canvas at
/// the root, loaded by the prefab system like any entity asset - whose payload also carries the
/// widget's graph under "widgetGraph". The root Canvas's Script slot names the file itself, so
/// the engine runs the graph (as UVScript) on the widget it was made for.

/// The graph held in a Widget Blueprint's bytes. Nothing when the file is not a prefab envelope;
/// a widget saved without a graph answers an empty one made for a Canvas.
[[nodiscard]] std::optional<BlueprintAssetUVE> ReadWidgetBlueprintGraphUVE(const std::vector<std::byte>& file);
[[nodiscard]] std::optional<BlueprintAssetUVE> LoadWidgetBlueprintGraphUVE(const std::filesystem::path& path);

/// Puts `graph` into the Widget Blueprint at `path` (its widgets are kept as they are). False when
/// the file is not a prefab envelope or cannot be written.
[[nodiscard]] bool SaveWidgetBlueprintGraphUVE(const std::filesystem::path& path, const BlueprintAssetUVE& graph);

/// The same, in memory: the bytes of `file` with `graph` put in. Nothing for bytes that are not a
/// prefab envelope.
[[nodiscard]] std::optional<std::vector<std::byte>> WithWidgetBlueprintGraphUVE(const std::vector<std::byte>& file,
                                                                             const BlueprintAssetUVE& graph);

} // namespace UVE::Asset
