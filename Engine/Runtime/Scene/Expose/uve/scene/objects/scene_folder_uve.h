// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Marks a Folder: a pure Object that only groups the objects under it in the Scene panel, like a
/// folder in a level outliner. It has no transform, so moving objects into or out of one never moves
/// them in the world, and it changes nothing about how the scene runs.
struct FolderComponentUVE final {
    [[nodiscard]] bool operator==(const FolderComponentUVE&) const = default;
};

[[nodiscard]] constexpr bool IsFolderComponentValidUVE(const FolderComponentUVE&) noexcept {
    return true; // a pure marker
}

/// Marks the Outliner's 3D Viewport: the level itself, at the top of the Outliner beside its
/// DirectionalLight3D and WorldEnvironment. Every folder of the level lives under it. A pure Object,
/// not a folder; the editor keeps it from being deleted or moved.
struct OutlinerViewport3DComponentUVE final {
    [[nodiscard]] bool operator==(const OutlinerViewport3DComponentUVE&) const = default;
};

[[nodiscard]] constexpr bool IsOutlinerViewport3DComponentValidUVE(const OutlinerViewport3DComponentUVE&) noexcept {
    return true; // a pure marker
}

struct FolderObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Folder";
};

/// Makes `entity` a pure Object (no transform) carrying the folder marker.
void ApplyFolderObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity, const FolderObjectDefinitionUVE& value);

struct Viewport3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Viewport3D";
};

/// Makes `entity` a pure Object (no transform) carrying the 3D Viewport marker.
void ApplyViewport3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Viewport3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
