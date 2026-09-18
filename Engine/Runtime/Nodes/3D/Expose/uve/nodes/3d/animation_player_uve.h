// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/animation_player_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the AnimationPlayer scene node: the component set and defaults a
/// freshly created AnimationPlayer entity attaches. This recipe used to be hardcoded inline in
/// EditorUVE's creation switch; it now has the same per-file home every other node kind has.
/// Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*,
/// not a second copy of component storage: player state itself still lives only in
/// AnimationPlayerComponentUVE. (The node remains authored-data-only until the real
/// skeleton/skinning/clip-sampling pipeline exists — see SCENE_NODES_ROADMAP.md.)
struct AnimationPlayerNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created AnimationPlayer as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "AnimationPlayer";

    /// Animation-player authored defaults (no clip bound, default playback settings); the
    /// entity's transform is attached by the creation shell.
    AnimationPlayerComponentUVE player{};
};

[[nodiscard]] bool IsAnimationPlayerNodeDefinitionValidUVE(const AnimationPlayerNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyAnimationPlayerNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const AnimationPlayerNodeDefinitionUVE& value);

} // namespace UVE::Scene
