// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Editor {

/// One object of a catalogue template. `parent` indexes an earlier object of the same template; the
/// first object is the root and has no parent (-1).
struct ContentCatalogueObjectUVE final {
    Scene::Objects::SceneObjectKindUVE kind = Scene::Objects::SceneObjectKindUVE::Object3D;
    std::int32_t parent = -1;
    /// Name of the object inside the asset. Empty keeps the object's default name; the root is always
    /// named after the asset file instead.
    std::string_view name;
};

/// What an item makes when picked.
enum class ContentCatalogueActionUVE : std::uint8_t {
    /// A real directory in the current Content folder.
    Folder = 0,
    /// A `.uventity` asset holding `objects`.
    EntityAsset,
    /// A `.uvscene` asset with a complete scene root and nodes beneath its Viewport.
    SceneAsset,
    /// A `.uvanimlib` asset: an empty animation library. Like Folder, it makes a file and no
    /// entities, so its item carries no objects.
    LibraryAsset,
};

/// An entry of the Content "+ Add" / right-click menu.
struct ContentCatalogueItemUVE final {
    std::string_view id;
    std::string_view label;
    std::string_view group;
    std::string_view tooltip;
    ContentCatalogueActionUVE action = ContentCatalogueActionUVE::EntityAsset;
    std::span<const ContentCatalogueObjectUVE> objects;
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

/// The object kind whose icon stands for `item` (its root; Folder for a folder).
[[nodiscard]] Scene::Objects::SceneObjectKindUVE GetContentCatalogueIconKindUVE(
    const ContentCatalogueItemUVE& item) noexcept;

} // namespace UVE::Editor
