// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "uve/scene/objects/scene_folder_uve.h"
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
    /// Nothing on its own: opens the Montage, the one window that offers the bodies an entity can
    /// be built on. Picking the item promises a choice, so it carries no objects and
    /// CreateContentCatalogueItemUVE refuses it - the body the author picks there is what gets made.
    Montage,
};

/// An entry of the Content "+ Add" / right-click menu.
struct ContentCatalogueItemUVE final {
    std::string_view id;
    std::string_view label;
    std::string_view group;
    std::string_view tooltip;
    ContentCatalogueActionUVE action = ContentCatalogueActionUVE::EntityAsset;
    std::span<const ContentCatalogueObjectUVE> objects;
    /// Which kind of scene a SceneAsset writes, and the dimension it is saved with. Ignored by every
    /// other action, whose items leave it at the default.
    Scene::ViewportDimensionUVE sceneDimension = Scene::ViewportDimensionUVE::ThreeD;
};

/// One body the Montage offers: a catalogue item, plus the words the window says about it.
struct ContentCatalogueMontageEntryUVE final {
    /// The catalogue item that makes this body (`player`, `character`, ...). Its label names the
    /// body, its kind picks the icon, and its tooltip is the menu's.
    std::string_view itemId;
    /// The line under the body's name: what it is, in a breath.
    std::string_view summary;
    /// The paragraph beneath: what the body is for, and what to add to it once it is open.
    std::string_view detail;
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

/// The bodies the Montage offers, in window order. Each names a catalogue item, so a body added to
/// the menu is listed there without a second description of what it is.
[[nodiscard]] std::span<const ContentCatalogueMontageEntryUVE> GetContentCatalogueMontageEntriesUVE() noexcept;

/// True when `itemId` is one of the Montage's bodies - the only ids the Montage will make.
[[nodiscard]] bool IsContentCatalogueMontageBodyUVE(std::string_view itemId) noexcept;

/// The object kind whose icon stands for `item` (its root; Folder for a folder, Skeleton3D for the
/// Montage, which stands for the bodies inside it).
[[nodiscard]] Scene::Objects::SceneObjectKindUVE GetContentCatalogueIconKindUVE(
    const ContentCatalogueItemUVE& item) noexcept;

} // namespace UVE::Editor
