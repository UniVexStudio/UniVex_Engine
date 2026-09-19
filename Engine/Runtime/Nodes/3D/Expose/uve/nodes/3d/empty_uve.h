// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Empty scene node: the transform-only base of every scene
/// hierarchy. Empty deliberately attaches nothing beyond the transform every node receives from
/// its creation shell, so this definition carries no component recipe — it exists so the kind
/// has the same per-file home (and the same name/validate/apply seam) as every other node kind,
/// and so any future Empty-level authored defaults have one obvious place to live.
struct EmptyNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind.
    static constexpr std::string_view defaultName = "Empty";
};

[[nodiscard]] bool IsEmptyNodeDefinitionValidUVE(const EmptyNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity`. Empty is transform-only by design, so this is a
/// documented no-op kept for uniformity: every node kind's creation path reads the same way.
void ApplyEmptyNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 const EmptyNodeDefinitionUVE& value) noexcept;

} // namespace UVE::Scene
