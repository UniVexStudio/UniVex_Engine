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

struct FolderNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "Folder";
};

/// Makes `entity` a pure Node (no transform) carrying the folder marker.
void ApplyFolderNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity, const FolderNodeDefinitionUVE& value);

} // namespace UVE::Scene
