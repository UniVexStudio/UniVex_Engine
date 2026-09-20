// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/audio_source_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the AudioSource3D scene node: the component set and defaults a
/// freshly created AudioSource3D entity attaches. This recipe used to be hardcoded inline in
/// EditorUVE's creation switch; it now has the same per-file home every other node kind has.
/// Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*,
/// not a second copy of component storage: audio state itself still lives only in
/// AudioSourceComponentUVE (and is driven by Audio/AudioSourceSystemUVE).
struct AudioSource3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created AudioSource3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "AudioSource3D";

    /// Audio authored defaults (an unbound, silent source); the entity's transform is attached
    /// by the creation shell.
    AudioSourceComponentUVE source{};
};

[[nodiscard]] bool IsAudioSource3DNodeDefinitionValidUVE(const AudioSource3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyAudioSource3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const AudioSource3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
