// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Editor {

/// One node of a catalogue template. `parent` indexes an earlier node of the same template; the
/// first node is the root and has no parent (-1).
struct ContentCatalogueNodeUVE final {
    Scene::Nodes::SceneNodeKindUVE kind = Scene::Nodes::SceneNodeKindUVE::Node3D;
    std::int32_t parent = -1;
    /// Name of the node inside the asset. Empty keeps the node's default name; the root is always
    /// named after the asset file instead.
    std::string_view name;
};

/// What an item makes when picked.
enum class ContentCatalogueActionUVE : std::uint8_t {
    /// A real directory in the current Content folder.
    Folder = 0,
    /// A `.uveentity` asset holding `nodes`.
    EntityAsset,
};

/// An entry of the Content "+ Add" / right-click menu.
struct ContentCatalogueItemUVE final {
    std::string_view id;
    std::string_view label;
    std::string_view group;
    std::string_view tooltip;
    ContentCatalogueActionUVE action = ContentCatalogueActionUVE::EntityAsset;
    std::span<const ContentCatalogueNodeUVE> nodes;
};

/// Every item, in menu order: items of one group are adjacent and groups appear in the order of
/// GetContentCatalogueGroupsUVE().
[[nodiscard]] std::span<const ContentCatalogueItemUVE> GetContentCatalogueItemsUVE() noexcept;

/// The group names, in menu order.
[[nodiscard]] std::span<const std::string_view> GetContentCatalogueGroupsUVE() noexcept;

[[nodiscard]] const ContentCatalogueItemUVE* FindContentCatalogueItemUVE(std::string_view id) noexcept;

/// True when `item` matches a search: every space-separated word of `query` appears
/// (case-insensitive) in its label, group or tooltip. An empty query matches everything.
[[nodiscard]] bool DoesContentCatalogueItemMatchUVE(const ContentCatalogueItemUVE& item,
                                                    std::string_view query) noexcept;

/// How well `item` matches a search, for ordering results: 0 no match, 1 matched only through its
/// group or tooltip, 2 its label contains the first word, 3 its label starts with it.
[[nodiscard]] int RankContentCatalogueItemUVE(const ContentCatalogueItemUVE& item, std::string_view query) noexcept;

/// The node kind whose icon stands for `item` (its root; Folder for a folder).
[[nodiscard]] Scene::Nodes::SceneNodeKindUVE GetContentCatalogueIconKindUVE(
    const ContentCatalogueItemUVE& item) noexcept;

} // namespace UVE::Editor
