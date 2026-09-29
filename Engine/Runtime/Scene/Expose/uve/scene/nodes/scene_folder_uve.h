// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Marks a Folder: a pure Node that only groups the nodes under it in the Scene panel, like a
/// folder in a level outliner. It has no transform, so moving nodes into or out of one never moves
/// them in the world, and it changes nothing about how the scene runs.
struct FolderComponentUVE final {
    [[nodiscard]] bool operator==(const FolderComponentUVE&) const = default;
};

[[nodiscard]] constexpr bool IsFolderComponentValidUVE(const FolderComponentUVE&) noexcept {
    return true; // a pure marker
}

/// Marks the Outliner's Viewport: the one folder at the top of a level that every other folder
/// lives under. It is a Folder in every other respect; the marker is what lets the editor find it
/// and keep it from being deleted or moved.
struct OutlinerViewportComponentUVE final {
    [[nodiscard]] bool operator==(const OutlinerViewportComponentUVE&) const = default;
};

[[nodiscard]] constexpr bool IsOutlinerViewportComponentValidUVE(const OutlinerViewportComponentUVE&) noexcept {
    return true; // a pure marker
}

struct FolderNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "Folder";
};

/// Makes `entity` a pure Node (no transform) carrying the folder marker.
void ApplyFolderNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity, const FolderNodeDefinitionUVE& value);

} // namespace UVE::Scene
