// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// Marks a Folder: a pure Object that only groups the objects under it in the Scene panel, like a
/// folder in a level outliner. It has no transform, so moving objects into or out of one never moves
/// them in the world, and it changes nothing about how the scene runs.
struct FolderComponent final {
    [[nodiscard]] bool operator==(const FolderComponent&) const = default;
};

[[nodiscard]] constexpr bool IsFolderComponentValid(const FolderComponent&) noexcept {
    return true; // a pure marker
}

/// Marks the Outliner's Viewport: the level itself, at the top of the Outliner beside its
/// DirectionalLight3D and WorldEnvironment. Every folder of the level lives under it. A pure Object,
/// not a folder; the editor keeps it from being deleted or moved.
struct OutlinerViewport3DComponent final {
    [[nodiscard]] bool operator==(const OutlinerViewport3DComponent&) const = default;
};

[[nodiscard]] constexpr bool IsOutlinerViewport3DComponentValid(const OutlinerViewport3DComponent&) noexcept {
    return true; // a pure marker
}

struct FolderObjectDefinition final {
    static constexpr std::string_view defaultName = "Folder";
};

/// Makes `entity` a pure Object (no transform) carrying the folder marker.
void ApplyFolderObjectDefinition(IEntityManager& entityManager, Entity entity, const FolderObjectDefinition& value);

struct Viewport3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Viewport3D";
};

/// Makes `entity` a pure Object (no transform) carrying the 3D Viewport marker.
void ApplyViewport3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                    const Viewport3DObjectDefinition& value);

} // namespace UVE::Scene
